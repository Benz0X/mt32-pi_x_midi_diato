//
// kernel.cpp
//
// mt32-pi - A bare-metal Roland MT-32 emulator for Raspberry Pi
// Copyright (C) 2020  Dale Whinham <daleyo@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "kernel.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <circle/usb/usbmidi.h>
#include <circle/startup.h>

#include "lcd/hd44780.h"
#include "lcd/ssd1306.h"

#ifndef MT32_PI_VERSION
#define MT32_PI_VERSION "<unknown>"
#endif

#define LCD_UPDATE_PERIOD_MILLIS 16
#define LCD_LOG_TIME_MILLIS 2000
#define LED_TIMEOUT_MILLIS 50
#define ACTIVE_SENSE_TIMEOUT_MILLIS 300

#define I2C_MASTER_DEVICE	1		// 0 on Raspberry Pi 1 Rev. 1 boards, 1 otherwise
#define I2C_MASTER_CONFIG	0		// 0 or 1 on Raspberry Pi 4, 0 otherwise
#define I2C_FAST_MODE		TRUE	// standard mode (100 Kbps) or fast mode (400 Kbps)

CKernel *CKernel::pThis = nullptr;

CKernel::CKernel(void)
	: CStdlibApp("mt32-pi"),
	  CMIDIParser(),

	  mSerial(&mInterrupt, true),
#ifdef HDMI_CONSOLE
	  mScreen(mOptions.GetWidth(), mOptions.GetHeight()),
#endif

	  mTimer(&mInterrupt),
	  mLogger(mOptions.GetLogLevel(), &mTimer),
	  mUSBHCI(&mInterrupt, &mTimer),
	  mEMMC(&mInterrupt, &mTimer, &mActLED),

	  mI2CMaster(1, true),
	  mLCD(nullptr),

	  mLCDLogTime(0),
	  mLCDUpdateTime(0),

	  mSerialMIDIEnabled(false),

	  mActiveSenseFlag(false),
	  mActiveSenseTime(0),

	  mShouldReboot(false),
	  mLEDOn(false),
	  mLEDOnTime(0),

	  mSynth(nullptr)
{
	pThis = this;
}

bool CKernel::Initialize(void)
{
	if (!CStdlibApp::Initialize())
		return false;

#ifdef HDMI_CONSOLE
	if (!mScreen.Initialize())
		return false;
#endif

	CDevice *pLogTarget = mDeviceNameService.GetDevice(mOptions.GetLogDevice(), false);

	if (!pLogTarget)
		pLogTarget = &mNull;

	// Init serial for GPIO MIDI if not being used for logging
	mSerialMIDIEnabled = pLogTarget != &mSerial;
	if (!mSerial.Initialize(mSerialMIDIEnabled ? 31250 : 115200))
		return false;

	if (!mLogger.Initialize(pLogTarget))
		return false;

	if (!mTimer.Initialize())
		return false;

	if (!mEMMC.Initialize())
		return false;

	char const *partitionName = "SD:";

	if (f_mount(&mFileSystem, partitionName, 1) != FR_OK)
	{
		mLogger.Write(GetKernelName(), LogError, "Cannot mount partition: %s", partitionName);
		return false;
	}

	// Initialize newlib stdio with a reference to Circle's file system
	CGlueStdioInit(mFileSystem);

	if (!mConfig.Initialize("mt32-pi.cfg"))
		mLogger.Write(GetKernelName(), LogWarning, "Unable to find or parse config file; using defaults");

	if (!mI2CMaster.Initialize())
		return false;

	if (mConfig.mLCDType == CConfig::LCDType::HD44780FourBit)
		mLCD = new CHD44780FourBit(mConfig.mLCDWidth, mConfig.mLCDHeight);
	else if (mConfig.mLCDType == CConfig::LCDType::HD44780I2C)
		mLCD = new CHD44780I2C(&mI2CMaster, mConfig.mLCDI2CLCDAddress, mConfig.mLCDWidth, mConfig.mLCDHeight);
	else if (mConfig.mLCDType == CConfig::LCDType::SSD1306I2C)
		mLCD = new CSSD1306(&mI2CMaster, mConfig.mLCDI2CLCDAddress, mConfig.mLCDHeight);
	
	if (mLCD)
	{
		if (mLCD->Initialize())
			mLCD->Print("mt32-pi " MT32_PI_VERSION, 0, 0, false, true);
		else
		{
			mLogger.Write(GetKernelName(), LogWarning, "LCD init failed; invalid dimensions?");
			delete mLCD;
			mLCD = nullptr;
		}
	}

#if !defined(__aarch64__) || !defined(LEAVE_QEMU_ON_HALT)
	// The USB driver is not supported under 64-bit QEMU, so
	// the initialization must be skipped in this case, or an
	// exit happens here under 64-bit QEMU.
	LCDLog("Init USB");
	if (mConfig.mMIDIUSB && !mUSBHCI.Initialize())
		return false;
#endif

	if (mConfig.mAudioOutputDevice == CConfig::AudioOutputDevice::I2SDAC)
	{
		LCDLog("Init mt32emu (I2S)");
		if (mConfig.mAudioI2CDACInit == CConfig::AudioI2CDACInit::PCM51xx)
			InitPCM51xx(mConfig.mAudioI2CDACAddress);

		mSynth = new CMT32SynthI2S(&mInterrupt, mConfig.mAudioSampleRate, mConfig.mMT32EmuResamplerQuality, mConfig.mAudioChunkSize);
	}
	else
	{
		LCDLog("Init mt32emu (PWM)");
		mSynth = new CMT32SynthPWM(&mInterrupt, mConfig.mAudioSampleRate, mConfig.mMT32EmuResamplerQuality, mConfig.mAudioChunkSize);
	}

	if (!mSynth->Initialize())
		return false;

	mLogger.Write(GetKernelName(), LogNotice, "mt32-pi " MT32_PI_VERSION);
	mLogger.Write(GetKernelName(), LogNotice, "Compile time: " __DATE__ " " __TIME__);
	CCPUThrottle::Get()->DumpStatus();

	return true;
}

// TODO: Generic configurable DAC init class
bool CKernel::InitPCM51xx(u8 pAddress)
{
	unsigned char initBytes[][2] =
	{
		// Set PLL reference clock to BCK (set SREF to 001b)
		{ 0x0d, 0x10 },

		// Ignore clock halt detection (set IDCH to 1)
		{ 0x25, 0x08 },

		// Disable auto mute
		{ 0x41, 0x04 }
	};

	for (auto& command : initBytes)
	{
		if (mI2CMaster.Write(pAddress, &command, sizeof(command)) != sizeof(command))
		{
			mLogger.Write(GetKernelName(), LogWarning, "I2C write error (DAC init sequence)");
			return false;
		}
	}

	return true;
}

CStdlibApp::TShutdownMode CKernel::Run(void)
{
	CUSBMIDIDevice *pMIDIDevice = static_cast<CUSBMIDIDevice *>(CDeviceNameService::Get()->GetDevice("umidi1", FALSE));
	if (pMIDIDevice)
	{
		pMIDIDevice->RegisterPacketHandler(USBMIDIPacketHandler);
		mLogger.Write(GetKernelName(), LogNotice, "Using USB MIDI interface");
		mSerialMIDIEnabled = false;
	}
	else
	{
		if (mSerialMIDIEnabled)
			mLogger.Write(GetKernelName(), LogNotice, "Using serial MIDI interface");
		else
		{
			mLogger.Write(GetKernelName(), LogError, "No USB MIDI device detected and serial port in use - please restart.");
			return ShutdownHalt;
		}
	}

	mSynth->SetLCDMessageHandler(LCDMessageHandler);

	// Start audio
	//mSynth->Start();
	LCDLog("Ready.");

	while (true)
	{
		unsigned ticks = mTimer.GetTicks();

		// Update serial GPIO MIDI
		if (mSerialMIDIEnabled)
			UpdateSerialMIDI();

		// Update activity LED
		if (mLEDOn && (ticks - mLEDOnTime) >= MSEC2HZ(LED_TIMEOUT_MILLIS))
		{
			mActLED.Off();
			mLEDOn = false;
		}

		// Update LCD
		if (mLCD)
		{
			if (mLCDLogTime)
			{
				if ((ticks - mLCDLogTime) >= MSEC2HZ(LCD_LOG_TIME_MILLIS))
				{
					mLCDLogTime = 0;
					mLCD->Clear();
				}
			}
			else if ((ticks - mLCDUpdateTime) >= MSEC2HZ(LCD_UPDATE_PERIOD_MILLIS))
			{
				mLCD->Update(mSynth);
				mLCDUpdateTime = ticks;
			}
		}

		// Check for active sensing timeout (300 milliseconds)
		// Based on http://midi.teragonaudio.com/tech/midispec/sense.htm
		if (mActiveSenseFlag && (ticks - mActiveSenseTime) >= MSEC2HZ(ACTIVE_SENSE_TIMEOUT_MILLIS))
		{
			mSynth->AllSoundOff();
			mActiveSenseFlag = false;
			mLogger.Write(GetKernelName(), LogNotice, "Active sense timeout - turning notes off");
		}

		if (mShouldReboot)
		{
			// Stop audio and reboot
			//mSynth->Cancel();

			// Clear screen
			if (mLCD)
				mLCD->Clear();

			return ShutdownReboot;
		}
	}

	return ShutdownHalt;
}

void CKernel::OnShortMessage(u32 pMessage)
{
	// Active sensing
	if (pMessage == 0xFE)
	{
		mActiveSenseFlag = true;
		return;
	}

	// Flash LED on note on or off
	if ((pMessage & 0x80) == 0x80)
		LEDOn();

	//mLogger.Write(pThis->GetKernelName(), LogNotice, "midi 0x%08x", pMessage);
	mSynth->HandleMIDIShortMessage(pMessage);
}

void CKernel::OnSysExMessage(const u8* pData, size_t pSize)
{
	// If we don't consume the SysEx message, forward it to mt32emu
	if (!ParseCustomSysEx(pData, pSize))
		mSynth->HandleMIDISysExMessage(pData, pSize);
}

void CKernel::OnUnexpectedStatus()
{
	CMIDIParser::OnUnexpectedStatus();
	LCDLog("Unexp. MIDI status!");
}

void CKernel::OnSysExOverflow()
{
	CMIDIParser::OnSysExOverflow();
	LCDLog("SysEx overflow!");
}

bool CKernel::ParseCustomSysEx(const u8* pData, size_t pSize)
{
	if (pSize < 4)
		return false;

	// 'Educational' manufacturer
	if (pData[1] != 0x7D)
		return false;

	// Reboot (F0 7D 00 F7)
	if (pData[2] == 0x00)
	{
		mLogger.Write(GetKernelName(), LogNotice, "midi: Reboot command received");
		mShouldReboot = true;
		return true;
	}

	return false;
}

void CKernel::UpdateSerialMIDI()
{
	// Read serial MIDI data
	u8 buffer[SERIAL_BUF_SIZE];
	int nResult = mSerial.Read(buffer, sizeof(buffer));

	// No data
	if (nResult == 0)
		return;

	// Error
	if (nResult < 0)
	{
		const char* errorString;
		switch (nResult)
		{
			case -SERIAL_ERROR_BREAK:
				errorString = "break error";
				break;

			case -SERIAL_ERROR_OVERRUN:
				errorString = "overrun error";
				break;

			case -SERIAL_ERROR_FRAMING:
				errorString = "framing error";
				break;

			default:
				errorString = "unknown error";
				break;
		}

		mLogger.Write("serialmidi", LogWarning, errorString);
		LCDLog(errorString);
		return;
	}

	// Replay received MIDI data out via the serial port ('software thru')
	if (mConfig.mMIDIGPIOThru)
	{
		int nSendResult = mSerial.Write(buffer, nResult);
		if (nSendResult != nResult)
		{
			mLogger.Write("serialmidi", LogWarning, "received %d bytes, but only sent %d bytes", nResult, nSendResult);
			LCDLog("serial TX error");
		}
	}

	// Reset the Active Sense timer
	mActiveSenseTime = mTimer.GetTicks();

	// Process MIDI messages
	ParseMIDIBytes(buffer, nResult);
}

void CKernel::LEDOn()
{
	mActLED.On();
	mLEDOnTime = mTimer.GetTicks();
	mLEDOn = true;
}

void CKernel::LCDLog(const char* pMessage)
{
	if (!mLCD)
		return;

	mLCDLogTime = mTimer.GetTicks();

	mLCD->Print("~", 0, 1);
	mLCD->Print(pMessage, 2, 1, true, true);
}

void CKernel::USBMIDIPacketHandler(unsigned nCable, u8 *pPacket, unsigned nLength)
{
	assert(pThis != nullptr);

	// Reset the Active Sense timer
	pThis->mActiveSenseTime = pThis->mTimer.GetTicks();

	// Process MIDI messages
	pThis->ParseMIDIBytes(pPacket, nLength);
}

void CKernel::LCDMessageHandler(const char* pMessage)
{
	assert(pThis != nullptr);
	if (pThis->mLCD)
		pThis->mLCD->SetMessage(pMessage);
}
