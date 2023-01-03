#
# Build kernel
#

include Config.mk

OBJS		:=	src/arduino_lib/SSD1306Ascii/src/SSD1306Ascii.o \
				src/arduino_lib/BMP180/src/SFE_BMP180.o \
				src/arduino_lib/MCP23017/src/MCP23017.o \
				src/arduino_lib/MIDI_Library/src/MIDI.o \
				src/arduino_lib/ArduinoMenu/src/menu.o \
				src/arduino_lib/ArduinoMenu/src/menuBase.o \
				src/arduino_lib/ArduinoMenu/src/menuIo.o \
				src/arduino_lib/ArduinoMenu/src/items.o \
				src/arduino_lib/ArduinoMenu/src/nav.o \
				src/arduino_lib/Queue/src/cppQueue.o \
				src/arduino/Stream.o \
				src/arduino/Print.o \
				src/arduino/WString.o \
				src/arduino/itoa.o \
				src/arduino/avr/dtostrf.o \
				src/config.o \
				src/control/control.o \
				src/control/mister.o \
				src/control/rotaryencoder.o \
				src/control/simplebuttons.o \
				src/control/simpleencoder.o \
				src/kernel.o \
				src/lcd/drivers/hd44780.o \
				src/lcd/drivers/hd44780fourbit.o \
				src/lcd/drivers/hd44780i2c.o \
				src/lcd/drivers/sh1106.o \
				src/lcd/drivers/ssd1306.o \
				src/lcd/ui.o \
				src/main.o \
				src/midimonitor.o \
				src/midiparser.o \
				src/mt32pi.o \
				src/diatotask.o \
				src/sharedbuffer.o \
				src/net/applemidi.o \
				src/net/ftpdaemon.o \
				src/net/ftpworker.o \
				src/net/udpmidi.o \
				src/pisound.o \
				src/power.o \
				src/rommanager.o \
				src/soundfontmanager.o \
				src/synth/mt32synth.o \
				src/synth/soundfontsynth.o \
				src/zoneallocator.o

EXTRACLEAN	+=	src/*.d src/*.o \
				src/arduino_lib/SSD1306Ascii/src/*.d src/arduino_lib/SSD1306Ascii/src/*.o \
				src/arduino_lib/BMP180/src/*.d src/arduino_lib/BMP180/src/*.o \
				src/arduino_lib/MCP23017/src/*.d src/arduino_lib/MCP23017/src/*.o \
				src/arduino_lib/MIDI_Library/src/*.d src/arduino_lib/MIDI_Library/src/*.o \
				src/arduino_lib/ArduinoMenu/src/*.d src/arduino_lib/ArduinoMenu/src/*.o \
				src/arduino_lib/cppQueue/src/*.d src/arduino_lib/cppQueue/*.o \
				src/arduino/*.d src/arduino/*.o \
				src/control/*.d src/control/*.o \
				src/lcd/*.d src/lcd/*.o \
				src/lcd/drivers/*.d src/lcd/drivers/*.o \
				src/net/*.d src/net/*.o \
				src/synth/*.d src/synth/*.o



INCLUDE		+=	-I src/arduino_lib/SSD1306Ascii/src/
INCLUDE		+=	-I src/arduino_lib/BMP180/src/
INCLUDE		+=	-I src/arduino_lib/MCP23017/src/
INCLUDE		+=	-I src/arduino_lib/MIDI_Library/src/
INCLUDE		+=	-I src/arduino_lib/ArduinoMenu/src/
INCLUDE		+=	-I src/arduino_lib/Queue/src/
INCLUDE		+=	-I src/arduino/

#
# inih
#
OBJS		+=	$(INIHHOME)/ini.o
INCLUDE		+=	-I $(INIHHOME)
EXTRACLEAN	+=	$(INIHHOME)/ini.d \
				$(INIHHOME)/ini.o

include $(CIRCLEHOME)/Rules.mk

CFLAGS		+=	-Wextra -Wno-unused-parameter -fpermissive -Wno-cast-function-type

CFLAGS		+=	-I "$(NEWLIBDIR)/include" \
				-I $(STDDEF_INCPATH) \
				-I $(CIRCLESTDLIBHOME)/include \
				-I include \
				-I .

LIBS 		:=	$(CIRCLE_STDLIB_LIBS) \
				$(CIRCLEHOME)/addon/fatfs/libfatfs.a \
				$(CIRCLEHOME)/addon/SDCard/libsdcard.a \
				$(CIRCLEHOME)/addon/wlan/hostap/wpa_supplicant/libwpa_supplicant.a \
				$(CIRCLEHOME)/addon/wlan/libwlan.a \
				$(CIRCLEHOME)/lib/fs/libfs.a \
				$(CIRCLEHOME)/lib/input/libinput.a \
				$(CIRCLEHOME)/lib/libcircle.a \
				$(CIRCLEHOME)/lib/net/libnet.a \
				$(CIRCLEHOME)/lib/sched/libsched.a \
				$(CIRCLEHOME)/lib/usb/libusb.a

ifeq ($(HDMI_CONSOLE), 1)
DEFINE		+=	-D HDMI_CONSOLE
endif

-include $(DEPS)

INCLUDE		+=	-I $(MT32EMUBUILDDIR)/include
EXTRALIBS	+=	$(MT32EMULIB)

INCLUDE		+=	-I $(FLUIDSYNTHBUILDDIR)/include \
				-I $(FLUIDSYNTHHOME)/include
EXTRALIBS	+=	$(FLUIDSYNTHLIB)

#
# Generate version string from git tag
#
VERSION=$(shell git describe --tags --dirty --always 2>/dev/null)
ifneq ($(VERSION),)
DEFINE		+=	-D MT32_PI_VERSION=\"$(VERSION)\"
endif
