//
// mt32pi.cpp
//
// mt32-pi - A baremetal MIDI synthesizer for Raspberry Pi
// Copyright (C) 2020-2022 Dale Whinham <daleyo@gmail.com>
//
// This file is part of mt32-pi.
//
// mt32-pi is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// mt32-pi is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// mt32-pi. If not, see <http://www.gnu.org/licenses/>.
//


//This allow to switch between darwin style left hand (24 buttons, same sound on pull/pull) and traditionnal 18 basses layout
#define DARWIN

// Use to send some info on UART, this will disable MIDI over USB (as it is used for UART)
// #define DEBUG
#define DISABLE_MIDI_USB

#ifdef DEBUG
#define DISABLE_MIDI_USB
#endif


#include <circle/hdmisoundbasedevice.h>
#include <circle/i2ssoundbasedevice.h>
#include <circle/memory.h>
#include <circle/pwmsoundbasedevice.h>
#include <circle/serial.h>
#include <circle/timer.h>

#include <cstdarg>
#include <math.h>

#include "lcd/drivers/hd44780.h"
#include "lcd/drivers/ssd1306.h"
#include "lcd/ui.h"
#include "mt32pi.h"
#include "SSD1306Ascii.h"
#include "SSD1306AsciiWire.h"
#include "SFE_BMP180.h"
#include "MCP23017.h"
#include <MIDI.h>
//Menu lib
#include <menu.h>
#include <menuIO/SSD1306AsciiOut.h>
#include <menuIO/stringIn.h>
#include "midi_helper.h"
#include "mt32.h"


#define MCP23017_ADDRESS 0x20
#define MCP23017_RH_0_SUB_ADDRESS 0x0
#define MCP23017_RH_1_SUB_ADDRESS 0x1
#define MCP23017_LH_0_SUB_ADDRESS 0x2
#define MCP23017_LH_1_SUB_ADDRESS 0x3

//-----------------------------------
// Menu/OLED config
//-----------------------------------
#define OLED_I2C_ADDRESS 0x3C
using namespace Menu;

//Uncomment this to use large font
// #define LARGE_FONT Verdana12

//Max menu depth
#define MAX_DEPTH 2

 /*Do not change the values(recomended)*/
#ifdef LARGE_FONT
    #define menuFont LARGE_FONT
    #define fontW 8
    #define fontH 16
#else
    // #define menuFont System5x7
    #define menuFont lcd5x7
    #define fontW 5
    #define fontH 8
#endif





//-----------------------------------
// MT32 stuff
//-----------------------------------
#define MT32_PI_NAME "mt32-pi"
const char MT32PiName[] = MT32_PI_NAME;

//-----------------------------------
//Program config
//-----------------------------------
#define ROW_NUMBER_R 11
#define COLUMN_NUMBER_R 3

#define ROW_NUMBER_L 6
#define COLUMN_NUMBER_L 4

//Other keys
#define MISSING_KEY_1 15
#define MISSING_KEY_2 17
#define MENU_KEY_COL 2
#define MENU_KEY_ROW 10
#define LOOP_KEY_COL 1
#define LOOP_KEY_ROW 11
//Useful define
#define PUSH 1
#define NOPUSH 2
#define PULL 0

//-----------------------------------
//Global variables
//-----------------------------------
//Contains pressed keys
uint32_t keys_rh;
uint32_t keys_lh;
uint16_t keys_rh_row[COLUMN_NUMBER_R];
uint16_t keys_rh_row_raw[COLUMN_NUMBER_R];
uint8_t  keys_lh_row[COLUMN_NUMBER_L];


//Contain the information 'is button pressed ?' for right keyboard
bool R_press[COLUMN_NUMBER_R][ROW_NUMBER_R+1];
bool L_press[COLUMN_NUMBER_L][ROW_NUMBER_L];
//Contain the information 'was button pressed last iteration?' for left keyboard
bool R_prev_press[COLUMN_NUMBER_R][ROW_NUMBER_R+1];
bool L_prev_press[COLUMN_NUMBER_L][ROW_NUMBER_L];

//Contains notes to play, this use a bit more memory than using lists with not to play/remove but Arduino Due can afford it and it prevent glitchs when same note is played several time
uint8_t R_played_note[mid_B9];      //RH won't go higher than B9
uint8_t R_played_note_prev[mid_B9]; //RH won't go higher than B9

uint8_t L_played_note[mid_B6];      //LH won't go higher then B6
uint8_t L_played_note_prev[mid_B6]; //LH won't go higher then B6


//Notes definitions for pull
uint8_t R_notesT[COLUMN_NUMBER_R][ROW_NUMBER_R] = {
        {mid_F3+1, mid_A3  , mid_C4, mid_E4  , mid_F4+1, mid_A4  , mid_C5  , mid_E5  , mid_F5+1, mid_G5  , mid_C6  }, //1rst row
        {mid_G3  , mid_B3  , mid_D4, mid_F4  , mid_A4  , mid_B4  , mid_D5  , mid_F5  , mid_A5  , mid_B5  , mid_D6  }, //2nd row
        {mid_B3-1, mid_C4+1, mid_G4, mid_A4-1, mid_B4-1, mid_C5+1, mid_E5-1, mid_A5-1, mid_B5-1, mid_C6+1, mid_E6-1}};//3rd row
//Notes definitions for push
uint8_t R_notesP[COLUMN_NUMBER_R][ROW_NUMBER_R] = {
        {mid_D3  , mid_G3  , mid_B3  , mid_D4  , mid_G4  , mid_B4  , mid_D5  , mid_G5  , mid_B5  , mid_D6  , mid_G6  }, //1rst row
        {mid_E3  , mid_G3  , mid_C4  , mid_E4  , mid_G4  , mid_C5  , mid_E5  , mid_G5  , mid_C6  , mid_E6  , mid_G6  }, //2nd row
        {mid_A3-1, mid_B3-1, mid_E4-1, mid_A4-1, mid_B4-1, mid_E5-1, mid_A5-1, mid_B5-1, mid_E6-1, mid_A6-1, mid_B6-1}};//3rd row


//Variation for BC, add 4 semitone to 1rst ROW
    // uint8_t R_notesT[COLUMN_NUMBER_R][ROW_NUMBER_R] = {
    //    {mid_F3+1+4, mid_A3+4, mid_C4+4, mid_E4+4, mid_F4+1+4, mid_A4+4, mid_C5+4, mid_E5+4, mid_F5+1+4, mid_G5+4, mid_C6+4},
    //    {mid_G3, mid_B3, mid_D4, mid_F4, mid_A4, mid_B4, mid_D5, mid_F5, mid_A5, mid_B5, mid_D6},
    //    {mid_B3-1, mid_C4+1, mid_G4, mid_A4-1, mid_B4-1, mid_C5+1, mid_E5-1, mid_A5-1, mid_B5-1, mid_C6+1, mid_E6-1}};
    // uint8_t R_notesP[COLUMN_NUMBER_R][ROW_NUMBER_R] = {
    //    {mid_D3+4, mid_G3+4, mid_B3+4, mid_D4+4, mid_G4+4, mid_B4+4, mid_D5+4, mid_G5+4, mid_B5+4, mid_D6+4, mid_G6+4},
    //    {mid_E3, mid_G3, mid_C4, mid_E4, mid_G4, mid_C5, mid_E5, mid_G5, mid_C6, mid_E6, mid_G6},
    //    {mid_A3-1, mid_B3-1, mid_E4-1, mid_A4-1, mid_B4-1, mid_E5-1, mid_A5-1, mid_B5-1, mid_E6-1, mid_A6-1, mid_B6-1}};


//Notes definitions for left hand (no push/pull distinction as the keyboard layout is a Serafini Darwin whith same sound in push and pull)
#ifdef DARWIN
uint8_t L_notesT[COLUMN_NUMBER_L][ROW_NUMBER_L] = {
        {mid_D3+1, mid_F3  , mid_G3  , mid_A3  , mid_B3  , mid_C3+1 }, //1rst row
        {mid_A3+1, mid_C3  , mid_D3  , mid_E3  , mid_F3+1, mid_G3+1 }, //2nd row
        {mid_D4+1, mid_F4  , mid_G4  , mid_A4  , mid_B4  , mid_C4+1 }, //3nd row
        {mid_A4+1, mid_C4  , mid_D4  , mid_E4  , mid_F4+1, mid_G4+1 }};//4rd row
//Fifth definitions for left hand (so we can build our own fifth chords)
uint8_t L_notes_fifthT[COLUMN_NUMBER_L][ROW_NUMBER_L] = {
        {0       , 0       , 0       , 0       , 0       , 0        }, //1rst row
        {0       , 0       , 0       , 0       , 0       , 0        }, //2nd row
        {mid_A4+1, mid_C4  , mid_D4  , mid_E4  , mid_F4+1, mid_G4+1 }, //3nd row
        {mid_F4  , mid_G4  , mid_A4  , mid_B4  , mid_C4+1, mid_D4+1 }};//4rd row
//Ugly way to assign P to T as push pull is the same in darwin, this save a little memory
uint8_t (*L_notesP)[ROW_NUMBER_L]       = L_notesT;
uint8_t (*L_notes_fifthP)[ROW_NUMBER_L] = L_notes_fifthT;
#else
uint8_t L_notesT[COLUMN_NUMBER_L][ROW_NUMBER_L] = {
        {0       , 0       , 0       , 0       , 0       , 0        }, //1rst row
        {mid_C3  , mid_E3  , mid_G3+1, mid_F3+1, mid_C3+1, mid_D3+1 }, //2nd row
        {mid_F3  , mid_F4  , mid_A3  , mid_A4  , mid_A3+1, mid_A4+1 }, //3nd row
        {mid_G3  , mid_G4  , mid_D3  , mid_D4  , mid_B3  , mid_B4   }};//4rd row
//Fifth definitions for left hand (so we can build our own fifth chords)
uint8_t L_notes_fifthT[COLUMN_NUMBER_L][ROW_NUMBER_L] = {
        {0       , 0       , 0       , 0       , 0       , 0        }, //1rst row
        {0       , 0       , 0       , 0       , 0       , 0        }, //2nd row
        {0       , mid_C4  , 0       , mid_E4  , 0       , mid_F4   }, //3nd row
        {0       , mid_D4  , 0       , mid_A4  , 0       , mid_F4+1 }};//4rd row
uint8_t L_notesP[COLUMN_NUMBER_L][ROW_NUMBER_L] = {
        {0       , 0       , 0       , 0       , 0       , 0        }, //1rst row
        {mid_D3  , mid_A3  , mid_B3  , mid_F3+1, mid_C3+1, mid_A3+1 }, //2nd row
        {mid_F3  , mid_F4  , mid_E3  , mid_E4  , mid_D3+1, mid_D4+1 }, //3nd row
        {mid_C3  , mid_C4  , mid_G3  , mid_G4  , mid_G3+1, mid_G4+1 }};//4rd row
//Fifth definitions for left hand (so we can build our own fifth chords)
uint8_t L_notes_fifthP[COLUMN_NUMBER_L][ROW_NUMBER_L] = {
        {0       , 0       , 0       , 0       , 0       , 0        }, //1rst row
        {0       , 0       , 0       , 0       , 0       , 0        }, //2nd row
        {0       , mid_C4  , 0       , mid_B4  , 0       , mid_A4+1 }, //3nd row
        {0       , mid_G4  , 0       , mid_D4  , 0       , mid_D4+1 }};//4rd row

#endif



SSD1306AsciiWire oled;
char str_oled[128/fontW];
uint32_t loop_count=0;
uint32_t max_count=0;


//pressure stuff
uint8_t volume, volume_resolved, volume_prev;
uint8_t expression_resolved=127;


//Contains info if current bellow direction is push or pull
uint8_t bellow, bellow_not_null, bellow_prev;
SFE_BMP180 bmp_in;
double p_tare;
double p_offset;
double T, P;
long t_start;
double min_pressure = 0.07;
double max_pressure = 13;
char bmp_status;
bool waiting_t=0;
bool waiting_p=0;

midi::MidiInterface<sharedbuffer, midi_setting_running_status_low_sysex> *MIDIPI_LOCAL;

//Helper functions
void midi_broadcast_control_change(uint8_t cc, uint8_t value, uint8_t channel) {
    #ifndef DISABLE_MIDI_USB
        MIDIUSB.sendControlChange(cc, value, channel);
    #endif
    (*MIDIPI_LOCAL).sendControlChange(cc, value, channel);
}

void midi_broadcast_program_change(uint8_t program, uint8_t channel) {
    #ifndef DISABLE_MIDI_USB
        MIDIUSB.sendProgramChange(program, channel);
    #endif
    (*MIDIPI_LOCAL).sendProgramChange(program, channel);
}
void midi_broadcast_note_on(uint8_t note, uint8_t expression, uint8_t channel) {
    #ifndef DISABLE_MIDI_USB
        MIDIUSB.sendNoteOn(note, expression, channel);
    #endif
    (*MIDIPI_LOCAL).sendNoteOn(note, expression, channel);
}
void midi_broadcast_note_off(uint8_t note, uint8_t expression, uint8_t channel) {
    #ifndef DISABLE_MIDI_USB
        MIDIUSB.sendNoteOn(note, 0, channel);
    #endif
    (*MIDIPI_LOCAL).sendNoteOn(note, 0, channel);
}
void midi_broadcast_send(midi::MidiType command, uint8_t msb, uint8_t lsb, uint8_t channel) {
    #ifndef DISABLE_MIDI_USB
        MIDIUSB.send(command, msb, lsb, channel);
    #endif
    (*MIDIPI_LOCAL).send(command, msb, lsb, channel);
}
void midi_broadcast_pitchbend(int pitchvalue, uint8_t channel) {
    #ifndef DISABLE_MIDI_USB
        MIDIUSB.sendPitchBend(pitchvalue, channel);
    #endif
    (*MIDIPI_LOCAL).sendPitchBend(pitchvalue, channel);
}





//-----------------------------------
//Menu definition
//-----------------------------------
uint8_t volume_attenuation  = 48;
uint8_t expression          = 127;
uint8_t mt32_rom_set        = 0;
uint8_t mt32_soundfont      = 1;
int8_t  octave              = 1;
uint8_t pressuremode        = 2;
uint8_t program_rh          = 0;
uint8_t program_lh          = 1;
uint8_t channel_rh          = 1;
uint8_t channel_lh          = 2;
uint8_t pano_rh             = 38;
uint8_t pano_lh             = 42;
uint8_t reverb_rh           = 8;
uint8_t reverb_lh           = 8;
uint8_t chorus_rh           = 4;
uint8_t chorus_lh           = 0;
bool    mt32_synth          = MT32_SOUNDFONT;
bool    debug_oled          = 0;
bool    dummy               = 0;
#define VOLUME_NORMAL   0
#define VOLUME_REVERTED 1
#define VOLUME_CONSTANT  2
uint8_t volume_type         = VOLUME_NORMAL;
bool    fifth_enable        = 1;
bool    bassoon_enable      = 0;
bool    picolo_enable       = 0;
bool    flute_enable        = 1;
int8_t  vibrato             = 0;
int8_t  vibrato_prev        = 0;
int8_t  transpose           = 0;
#define VIBRATO_CHANNEL       8

result menu_midi_pano_change_rh() {
    midi_broadcast_control_change(MIDI_CC_BALANCE,pano_rh, channel_rh);
    return proceed;
}
result menu_midi_pano_change_lh() {
    midi_broadcast_control_change(MIDI_CC_BALANCE,pano_lh, channel_lh);
    return proceed;
}
result menu_midi_reverb_change_rh() {
    midi_broadcast_control_change(MIDI_CC_REVERB,reverb_rh, channel_rh);
    return proceed;
}
result menu_midi_reverb_change_lh() {
    midi_broadcast_control_change(MIDI_CC_REVERB,reverb_lh, channel_lh);
    return proceed;
}
result menu_midi_chorus_change_rh() {
    midi_broadcast_control_change(MIDI_CC_CHORUS,chorus_rh, channel_rh);
    return proceed;
}
result menu_midi_chorus_change_lh() {
    midi_broadcast_control_change(MIDI_CC_CHORUS,chorus_lh, channel_lh);
    return proceed;
}
result menu_midi_program_change_rh() {
    midi_broadcast_program_change(program_rh, channel_rh);
    menu_midi_pano_change_rh();
    menu_midi_reverb_change_rh();
    menu_midi_chorus_change_rh();
    midi_broadcast_control_change(MIDI_CC_VOLUME, 0, channel_rh);
    return proceed;
}
result menu_midi_program_change_lh() {
    midi_broadcast_program_change(program_lh, channel_lh);
    menu_midi_pano_change_lh();
    menu_midi_reverb_change_lh();
    menu_midi_chorus_change_lh();
    midi_broadcast_control_change(MIDI_CC_VOLUME, 0, channel_lh);
    return proceed;
}

result menu_midi_vibrato_pitch() {
    midi_broadcast_program_change(program_rh, VIBRATO_CHANNEL);
    midi_broadcast_control_change(MIDI_CC_MODWHEEL, vibrato, VIBRATO_CHANNEL);
    midi_broadcast_pitchbend(vibrato*64, VIBRATO_CHANNEL); //This is way better than modwheel

    //Disable chorus
    chorus_rh=0;
    menu_midi_chorus_change_rh();
    //Set balance for vibrato and silence it
    midi_broadcast_control_change(MIDI_CC_BALANCE,pano_rh, VIBRATO_CHANNEL);
    midi_broadcast_control_change(MIDI_CC_VOLUME, 0, VIBRATO_CHANNEL);
    return proceed;
}

result menu_mt32_switch_rom_set() {
    mt32_switch_rom_set(mt32_rom_set, (*MIDIPI_LOCAL));
    return proceed;
}
result menu_mt32_switch_soundfont() {
    mt32_switch_soundfont(mt32_soundfont, (*MIDIPI_LOCAL));
    menu_midi_program_change_rh();
    menu_midi_program_change_lh();
    return proceed;
}
result menu_mt32_switch_synth() {
    mt32_switch_synth(mt32_synth, (*MIDIPI_LOCAL));
    menu_midi_program_change_rh();
    menu_midi_program_change_lh();
    return proceed;
}

//MT32 submenu
TOGGLE(mt32_synth, synthctrl, "Synth: ", doNothing, noEvent, noStyle
       , VALUE("SF", HIGH, menu_mt32_switch_synth, noEvent)
       , VALUE("MT32", LOW, menu_mt32_switch_synth, noEvent)
      );
TOGGLE(mt32_rom_set, romctrl, "ROM  : ", doNothing, noEvent, noStyle
       , VALUE("MT32_OLD", 0x00, menu_mt32_switch_rom_set, noEvent)
       , VALUE("MT32_NEW", 0x01, menu_mt32_switch_rom_set, noEvent)
       , VALUE("CM_32L", 0x02, menu_mt32_switch_rom_set, noEvent)
      );
TOGGLE(mt32_soundfont, sfctrl, "SF   : ", doNothing, noEvent, noStyle
       , VALUE("0-GM      ", 0,  menu_mt32_switch_soundfont, noEvent)
       , VALUE("1-Loffet  ", 1,  menu_mt32_switch_soundfont, noEvent)
       , VALUE("2-Seraf   ", 2,  menu_mt32_switch_soundfont, noEvent)
       , VALUE("3-Gaillard", 3,  menu_mt32_switch_soundfont, noEvent)
       , VALUE("4-Diato   ", 4,  menu_mt32_switch_soundfont, noEvent)
       , VALUE("5         ", 5,  menu_mt32_switch_soundfont, noEvent)
       , VALUE("6         ", 6,  menu_mt32_switch_soundfont, noEvent)
       , VALUE("7         ", 7,  menu_mt32_switch_soundfont, noEvent)
       , VALUE("8         ", 8,  menu_mt32_switch_soundfont, noEvent)
       , VALUE("9         ", 9,  menu_mt32_switch_soundfont, noEvent)
       , VALUE("10        ", 10, menu_mt32_switch_soundfont, noEvent)
      );
MENU(mt32_config, "MT32 config", doNothing, noEvent, wrapStyle
     , SUBMENU(sfctrl)
     , SUBMENU(synthctrl)
     , SUBMENU(romctrl)
    );

//MIDICONF submenu
TOGGLE(volume_type, volumetypectrl, "Volume type : ", doNothing, noEvent, noStyle
       , VALUE("NORMAL",   VOLUME_NORMAL,   menu_mt32_switch_synth, noEvent)
       , VALUE("INVERTED", VOLUME_REVERTED, menu_mt32_switch_synth, noEvent)
       , VALUE("CONSTANT", VOLUME_CONSTANT, menu_mt32_switch_synth, noEvent)
      );
MENU(midi_config, "MIDI config", doNothing, noEvent, wrapStyle
     , FIELD(channel_lh, "Channel LH :", "", 0, 15, 1, 1  , doNothing                  , anyEvent, wrapStyle)
     , FIELD(channel_rh, "Channel RH :", "", 0, 15, 1, 1  , doNothing                  , anyEvent, wrapStyle)
     , FIELD(program_lh, "Program LH :", "", 0, 128, 16, 1, menu_midi_program_change_lh, anyEvent, wrapStyle)
     , FIELD(program_rh, "Program RH :", "", 0, 128, 16, 1, menu_midi_program_change_rh, anyEvent, wrapStyle)
     , FIELD(pano_lh   , "Pano LH :"   , "", 0, 127, 16, 1, menu_midi_pano_change_lh   , anyEvent, wrapStyle)
     , FIELD(pano_rh   , "Pano RH :"   , "", 0, 127, 16, 1, menu_midi_pano_change_rh   , anyEvent, wrapStyle)
     , FIELD(reverb_lh , "Reverb LH :" , "", 0, 127, 16, 1, menu_midi_reverb_change_lh , anyEvent, wrapStyle)
     , FIELD(reverb_rh , "Reverb RH :" , "", 0, 127, 16, 1, menu_midi_reverb_change_rh , anyEvent, wrapStyle)
     , FIELD(chorus_lh , "Chorus LH :" , "", 0, 127, 16, 1, menu_midi_chorus_change_lh , anyEvent, wrapStyle)
     , FIELD(chorus_rh , "Chorus RH :" , "", 0, 127, 16, 1, menu_midi_chorus_change_rh , anyEvent, wrapStyle)
     , SUBMENU(volumetypectrl)
    );
//Debug submenu
TOGGLE(debug_oled, debugoledctrl, "Debug OLED : ", doNothing, noEvent, noStyle
       , VALUE("ON", HIGH, doNothing, noEvent)
       , VALUE("OFF", LOW, doNothing, noEvent)
      );
TOGGLE(pressuremode, pressurectrl, "Pressuremode : ", doNothing, noEvent, noStyle
       , VALUE("LOGNILS", 0, doNothing, noEvent)
       , VALUE("EXPJASON", 1, doNothing, noEvent)
       , VALUE("CUBICVAVRA", 2, doNothing, noEvent)
       , VALUE("CUBICNILS", 3, doNothing, noEvent)
      );

MENU(debug_config, "Debug menu", doNothing, noEvent, wrapStyle
     , SUBMENU(debugoledctrl)
     , SUBMENU(pressurectrl) //for some reason, menu must have at least 2 elements
    );
//Keyboard layout submenu TODO
TOGGLE(octave, octavectrl,          "Octaved: ", doNothing, noEvent, noStyle
       , VALUE("-2", -2, doNothing, noEvent)
       , VALUE("-1", -1, doNothing, noEvent)
       , VALUE("2", 2, doNothing, noEvent)
       , VALUE("1", 1, doNothing, noEvent)
       , VALUE("0", 0, doNothing, noEvent)
      );
TOGGLE(fifth_enable, fifthctrl,     "Fifth  : ", doNothing, noEvent, noStyle
       , VALUE("ON", HIGH, doNothing, noEvent)
       , VALUE("OFF", LOW, doNothing, noEvent)
      );
TOGGLE(vibrato, pitchctrl,          "Pitch  : ", doNothing, noEvent, noStyle
       , VALUE("8", 8, menu_midi_vibrato_pitch, noEvent)
       , VALUE("4", 4, menu_midi_vibrato_pitch, noEvent)
       , VALUE("2", 2, menu_midi_vibrato_pitch, noEvent)
       , VALUE("1", 1, menu_midi_vibrato_pitch, noEvent)
       , VALUE("None", 0, menu_midi_vibrato_pitch, noEvent)
      );
TOGGLE(bassoon_enable, bassoonctrl, "Bassoon: ", doNothing, noEvent, noStyle
       , VALUE("ON", HIGH, doNothing, noEvent)
       , VALUE("OFF", LOW, doNothing, noEvent)
      );
TOGGLE(picolo_enable, picoloctrl,   "Picolo : ", doNothing, noEvent, noStyle
       , VALUE("ON", HIGH, doNothing, noEvent)
       , VALUE("OFF", LOW, doNothing, noEvent)
      );
TOGGLE(flute_enable, flutectrl,     "Flute  : ", doNothing, noEvent, noStyle
       , VALUE("ON", HIGH, doNothing, noEvent)
       , VALUE("OFF", LOW, doNothing, noEvent)
      );
TOGGLE(transpose, transposectrl,    "Key    : ", doNothing, noEvent, noStyle
       , VALUE("G/C   ",    0,  doNothing, noEvent)
       , VALUE("Ab/Db 5b +1",  1,  doNothing, noEvent)
       , VALUE("A/D   2# +2",  2,  doNothing, noEvent)
       , VALUE("Bb/Eb 3b +3",  3,  doNothing, noEvent)
       , VALUE("B/E   4# +4",  4,  doNothing, noEvent)
       , VALUE("C/F   1b +5",  5,  doNothing, noEvent)
       , VALUE("C#/F# 6#b ", 6,  doNothing, noEvent)
       , VALUE("D/G   1# +7",  7,  doNothing, noEvent)
       , VALUE("Eb/Ab 4b +8",  8,  doNothing, noEvent)
       , VALUE("E/A   3# +9",  9,  doNothing, noEvent)
       , VALUE("F/Bb 2b +10",  10, doNothing, noEvent)
       , VALUE("F#/B 5# +11",  11, doNothing, noEvent)
      );
MENU(keyboard_config, "Keyboard config", doNothing, noEvent, wrapStyle
     , SUBMENU(octavectrl)
     , SUBMENU(fifthctrl)
     , SUBMENU(bassoonctrl)
     , SUBMENU(flutectrl)
     , SUBMENU(picoloctrl)
     , SUBMENU(pitchctrl)
     , SUBMENU(transposectrl)
    );

//Main menu
TOGGLE(volume_attenuation, volmumectrl, "Volume att: ", doNothing, noEvent, noStyle
       , VALUE("-0", 0, doNothing, noEvent)
       , VALUE("-16", 16, doNothing, noEvent)
       , VALUE("-32", 32, doNothing, noEvent)
       , VALUE("-48", 48, doNothing, noEvent)
       , VALUE("-64", 64, doNothing, noEvent)
       , VALUE("-80", 80, doNothing, noEvent)
      );
TOGGLE(expression, expressionctrl, "Expression: ", doNothing, noEvent, noStyle
       , VALUE("127", 127, doNothing, noEvent)
       , VALUE("100", 100, doNothing, noEvent)
       , VALUE("72", 72, doNothing, noEvent)
       , VALUE("48", 48, doNothing, noEvent)
       , VALUE("20", 20, doNothing, noEvent)
      );
MENU(mainMenu, "Main menu", doNothing, noEvent, wrapStyle
     , SUBMENU(volmumectrl)
     , SUBMENU(expressionctrl)
     , SUBMENU(mt32_config)
     , SUBMENU(midi_config)
     , SUBMENU(keyboard_config)
     , SUBMENU(debug_config)
    );

//Preset functions
#define PRESET_DEFAULT 0    //Default preset, return all to factory --> to use on headset
#define PRESET_MIXED_OUT 1  //Send mixed RH/LH full volume, keep pano and reverb --> to use on external speakers
#define PRESET_SPLIT_OUT 2  //Send split RH/LH on L/R channels, full volume, no pano, no reverb --> to use with REAPER


void set_preset (uint8_t preset) {
    switch (preset)
    {
    case PRESET_DEFAULT:
        volume_attenuation  = 48;
        expression          = 127;
        mt32_rom_set        = 0;
        mt32_soundfont      = 1;
        octave              = 1;
        pressuremode        = 2;
        program_rh          = 0;
        program_lh          = 1;
        channel_rh          = 1;
        channel_lh          = 2;
        pano_rh             = 38;
        pano_lh             = 42;
        reverb_rh           = 8;
        reverb_lh           = 8;
        chorus_rh           = 4;
        chorus_lh           = 0;
        mt32_synth          = MT32_SOUNDFONT;
        volume_type         = VOLUME_NORMAL;
        fifth_enable        = 1;
        bassoon_enable      = 0;
        picolo_enable       = 0;
        flute_enable        = 1;
        vibrato             = 0;
        vibrato_prev        = 0;
        transpose           = 0;
        break;
    case PRESET_SPLIT_OUT:
        set_preset(PRESET_DEFAULT);
        volume_attenuation  = 0;
        pano_rh             = 0;
        pano_lh             = 127;
        reverb_rh           = 0;
        reverb_lh           = 0;
        chorus_rh           = 0;
        break;
    default:
        break;
    }
    menu_midi_program_change_lh();
    menu_midi_vibrato_pitch();
    menu_mt32_switch_rom_set();
    menu_mt32_switch_soundfont();
    menu_mt32_switch_synth();
}

//describing a menu output device without macros
//define at least one panel for menu output
const panel panels[] MEMMODE = {{0, 0, 128 / fontW, 64 / fontH}};
navNode* nodes[sizeof(panels) / sizeof(panel)]; //navNodes to store navigation status
panelsList pList(panels, nodes, 1); //a list of panels and nodes
idx_t tops[MAX_DEPTH] = {0, 0}; //store cursor positions for each level

#ifdef LARGE_FONT
SSD1306AsciiOut outOLED(&oled, tops, pList, 8, 2); //oled output device menu driver
#else
SSD1306AsciiOut outOLED(&oled, tops, pList, 5, 1); //oled output device menu driver
#endif

menuOut* constMEM outputs[]  MEMMODE  = {&outOLED}; //list of output devices
outputsList out(outputs, 1); //outputs list

//Menu input
stringIn<0> strIn;//buffer size: use 0 for a single byte
noInput none;//uses its own API
NAVROOT(nav,mainMenu,MAX_DEPTH,none,out);


//////////////////////////////////////////////////////////
// Helper functions
//////////////////////////////////////////////////////////
uint16_t read_burst16_mcp(uint8_t addr, CI2CMaster* pI2CMaster) {
    uint16_t buff;
	uint8_t buffer[2];
    pI2CMaster->Read(MCP23017_ADDRESS | addr,buffer, 2);
	buff = buffer[0]<<8;
	buff |= buffer[1];
	return buff;
}
//Functions to determine volume from pressure, several pressure mode are tested here
uint8_t compute_volume(double x){
    if(pressuremode==0) {
        return uint8_t((log(double(x) / 40) + 6) * 25) ;
    } else if (pressuremode==1) {
        return uint8_t((pow(x,2.1)+245)/5+2*x) ;
    } else if (pressuremode==2) {
        // return uint8_t(0.1*pow(x-6,3)+x+79) ;
        return uint8_t(0.1*pow(x-7,3)+2*x+79) ;
    } else { // if (pressuremode==3) {
        //0.07579105*x*x*x-2.00077888*x*x+22.258*x+4.993545
        return uint8_t(0.07579105*x*x*x-2.00077888*x*x+22.258*x+4.993545) ;
    }
}
//This will remap the vector from the key acquisition to the physical buttons as it is easier to handle
void remap_left_keys(uint32_t key_in, uint8_t* out_array){
    for (uint8_t i = 0; i < COLUMN_NUMBER_L; i++) {
        out_array[i]=0;
    }
    out_array[0] |= ((key_in&(0x1 << 11)) ?  0x1<<5 : 0 );
    out_array[0] |= ((key_in&(0x1 << 22)) ?  0x1<<4 : 0 );
    out_array[0] |= ((key_in&(0x1 << 13)) ?  0x1<<3 : 0 );
    out_array[0] |= ((key_in&(0x1 <<  8)) ?  0x1<<2 : 0 );
    out_array[0] |= ((key_in&(0x1 <<  9)) ?  0x1<<1 : 0 );
    out_array[0] |= ((key_in&(0x1 << 20)) ?  0x1<<0 : 0 );
    out_array[1] |= ((key_in&(0x1 << 15)) ?  0x1<<5 : 0 );
    out_array[1] |= ((key_in&(0x1 << 10)) ?  0x1<<4 : 0 );
    out_array[1] |= ((key_in&(0x1 << 21)) ?  0x1<<3 : 0 );
    out_array[1] |= ((key_in&(0x1 << 12)) ?  0x1<<2 : 0 );
    out_array[1] |= ((key_in&(0x1 << 23)) ?  0x1<<1 : 0 );
    out_array[1] |= ((key_in&(0x1 << 14)) ?  0x1<<0 : 0 );
    out_array[2] |= ((key_in&(0x1 << 31)) ?  0x1<<5 : 0 );
    out_array[2] |= ((key_in&(0x1 << 26)) ?  0x1<<4 : 0 );
    out_array[2] |= ((key_in&(0x1 << 17)) ?  0x1<<3 : 0 );
    out_array[2] |= ((key_in&(0x1 << 28)) ?  0x1<<2 : 0 );
    out_array[2] |= ((key_in&(0x1 << 19)) ?  0x1<<1 : 0 );
    out_array[2] |= ((key_in&(0x1 << 30)) ?  0x1<<0 : 0 );
    out_array[3] |= ((key_in&(0x1 << 16)) ?  0x1<<5 : 0 );
    out_array[3] |= ((key_in&(0x1 << 27)) ?  0x1<<4 : 0 );
    out_array[3] |= ((key_in&(0x1 << 25)) ?  0x1<<3 : 0 );
    out_array[3] |= ((key_in&(0x1 << 29)) ?  0x1<<2 : 0 );
    out_array[3] |= ((key_in&(0x1 << 24)) ?  0x1<<1 : 0 );
    out_array[3] |= ((key_in&(0x1 << 18)) ?  0x1<<0 : 0 );
}
//This function will ensure basses notes stay in the basses range, and same for chords notes, usefull during transpose
uint8_t transpose_left_hand(uint8_t note_in, uint8_t transpose) {
    if (note_in<mid_C4 && note_in>=mid_C3 ) { //Bass
        return (note_in+transpose)%12+mid_C3;
    } else if(note_in>=mid_C4 && note_in<mid_C5){
        return (note_in+transpose)%12+mid_C4;
    }
    return note_in;
}

//###############################################
//Presets
//###############################################
#define TOGGLE_BASSOON 0
#define TOGGLE_FLUTE   1
#define TOGGLE_PICCOLO 2
#define TOGGLE_VIBRATO 3

void set_stops(uint8_t stops){
    switch (stops) {
        case TOGGLE_BASSOON:
            bassoon_enable=!bassoon_enable;
            break;
        case TOGGLE_FLUTE:
            flute_enable=!flute_enable;
            break;
        case TOGGLE_PICCOLO:
            picolo_enable=!picolo_enable;
            break;
        case TOGGLE_VIBRATO:
            if(vibrato){ //Remove vibrato
                vibrato_prev=vibrato;
                vibrato=0;
            } else if (vibrato==0 && vibrato_prev==0) { //Add vibrato from 0
                vibrato=2;
            } else {
                vibrato=vibrato_prev;
            }
            menu_midi_vibrato_pitch();
            break;
        default:
            break;
    }
}


void CMT32Pi::DiatoTask()
{
	/////////////////////////////////////////////////////////////////
	// Initial code
	/////////////////////////////////////////////////////////////////
	oled.begin(&Adafruit128x64, OLED_I2C_ADDRESS, m_pI2CMaster);
	oled.setFont(System5x7);
	oled.clear();
	oled.println("Init PI");
 	if (!bmp_in.begin(m_pI2CMaster)) {
        oled.println("BMP180 init fail !");
        while (1); // Pause forever.
    }

    //Midi creation
    MIDI_CREATE_CUSTOM_INSTANCE(sharedbuffer, m_sharedbuffer, MIDIPI, midi_setting_running_status_low_sysex);
    MIDIPI_LOCAL=&MIDIPI;

    //GPIO creation for the two missing keys mapped to GPIOs (34 buttons vs 32 on MCP23017)
	CGPIOPin  missing_key_1 =  CGPIOPin (MISSING_KEY_1, GPIOModeInputPullUp);
	CGPIOPin  missing_key_2 =  CGPIOPin (MISSING_KEY_2, GPIOModeInputPullUp);

	m_pLogger->Write(MT32PiName, LogNotice, "Diato task on Core 3 starting up");

	MCP23017  mcp_rh_0 = MCP23017(MCP23017_ADDRESS | MCP23017_RH_0_SUB_ADDRESS, m_pI2CMaster);
	MCP23017  mcp_rh_1 = MCP23017(MCP23017_ADDRESS | MCP23017_RH_1_SUB_ADDRESS, m_pI2CMaster);
	MCP23017  mcp_lh_0 = MCP23017(MCP23017_ADDRESS | MCP23017_LH_0_SUB_ADDRESS, m_pI2CMaster);
	MCP23017  mcp_lh_1 = MCP23017(MCP23017_ADDRESS | MCP23017_LH_1_SUB_ADDRESS, m_pI2CMaster);
	
    mcp_rh_0.init();
    mcp_rh_1.init();
    mcp_lh_0.init();
    mcp_lh_1.init();
    //Configure MCPs to all input PULLUP
    mcp_rh_0.portMode(MCP23017Port::A, 0xFF, 0xFF); //Input PULLUP
    mcp_rh_0.portMode(MCP23017Port::B, 0xFF, 0xFF); //Input PULLUP
    mcp_rh_1.portMode(MCP23017Port::A, 0xFF, 0xFF); //Input PULLUP
    mcp_rh_1.portMode(MCP23017Port::B, 0xFF, 0xFF); //Input PULLUP
    mcp_lh_0.portMode(MCP23017Port::A, 0xFF, 0xFF); //Input PULLUP
    mcp_lh_0.portMode(MCP23017Port::B, 0xFF, 0xFF); //Input PULLUP
    mcp_lh_1.portMode(MCP23017Port::A, 0xFF, 0xFF); //Input PULLUP
    mcp_lh_1.portMode(MCP23017Port::B, 0xFF, 0xFF); //Input PULLUP
    //Dummy read to point to register
    mcp_rh_0.readPort(MCP23017Port::A);
    mcp_rh_1.readPort(MCP23017Port::A);
    mcp_lh_0.readPort(MCP23017Port::B);
    mcp_lh_1.readPort(MCP23017Port::B);



    //init some variable
    memset(R_prev_press,       0, sizeof(R_prev_press      ));
    memset(L_prev_press,       0, sizeof(L_prev_press      ));
    memset(R_played_note,      0, sizeof(R_played_note     ));
    memset(R_played_note_prev, 0, sizeof(R_played_note_prev));
    memset(L_played_note,      0, sizeof(L_played_note     ));
    memset(L_played_note_prev, 0, sizeof(L_played_note_prev));

    volume_prev=0;
    bellow_prev=NOPUSH;

	//Initial pressure measurement
	bmp_status = bmp_in.startTemperature();
    CTimer::SimpleMsDelay(bmp_status);
    bmp_status = bmp_in.getTemperature(T);
    // Start a pressure measurement:
    bmp_status = bmp_in.startPressure(3);

    t_start = CTimer::GetClockTicks()/1000;
    while (CTimer::GetClockTicks()/1000 - t_start < bmp_status);
    bmp_status = bmp_in.getPressure(P, T);
    p_tare=P;


    //Then we can send the configuration
    oled.clear();
    // mt32_switch_synth(mt32_synth, MIDIPI);
    menu_midi_program_change_lh();
    menu_midi_vibrato_pitch();
    menu_mt32_switch_rom_set();
    menu_mt32_switch_soundfont();
    menu_mt32_switch_synth();
    // menu_mt32_switch_soundfont();
    // menu_midi_program_change_rh();
    // menu_midi_program_change_lh();
    // menu_midi_pano_change_lh();
    // menu_midi_pano_change_rh();
    // menu_midi_reverb_change_rh();
    // menu_midi_reverb_change_lh();
    // menu_midi_chorus_change_rh();
    // menu_midi_chorus_change_lh();
    // midi_broadcast_control_change(MIDI_CC_VOLUME, 0, channel_lh);
    // midi_broadcast_control_change(MIDI_CC_VOLUME, 0, channel_rh);


    oled.clear();
    oled.print("INIT OK !");
    // delay(1000);
    // oled.clear();

	/////////////////////////////////////////////////////////////////
	// Loop code
	/////////////////////////////////////////////////////////////////
    uint32_t t__dbg_loop, t__dbg_init, t__dbg_press, t__dbg_key, t__dbg_menu, t__dbg_prep_midi, t__dbg_send_midi, t__dbg_temp, t__dbg_start;
	while (1) //This task never stops
	{
		// write_to_log((const char *)"Startup diato task");
		// CTimer::SimpleMsDelay(10);


        #ifdef DEBUG
        t__dbg_temp=micros();
        t__dbg_start=micros();
        #endif
        // Initialise loop
        //-----------------------------------
        //remember old pressed touch and notes
        memcpy(R_prev_press, R_press, sizeof(R_press));
        memcpy(L_prev_press, L_press, sizeof(L_press));
        memcpy(R_played_note_prev, R_played_note, sizeof(R_played_note));
        memcpy(L_played_note_prev, L_played_note, sizeof(L_played_note));

        //Reset note count
        memset(R_played_note,      0, sizeof(R_played_note));
        memset(L_played_note,      0, sizeof(L_played_note));
        //Zero some variables in doubt
        memset(R_press, 0, sizeof(R_press));
        memset(L_press, 0, sizeof(L_press));
        memset(keys_rh_row, 0, sizeof(keys_rh_row));
        memset(keys_rh_row_raw, 0, sizeof(keys_rh_row_raw));

        #ifdef DEBUG
        if(micros()-t__dbg_temp>t__dbg_init){t__dbg_init=micros()-t__dbg_temp;}
        t__dbg_temp=micros();
        #endif

        //-----------------------------------
        // Temp & pressure mesurement
        //-----------------------------------
        //This process is semi asynchronous from the loop thanks to variables waiting_t, waiting_p
        if(!waiting_t && !waiting_p){
            // Start temp mesurement
            bmp_status = bmp_in.startTemperature();
            t_start=millis();
            waiting_t=true;
        } else if(waiting_t && ((millis()-t_start) > bmp_status) ){
            // Retrieve the completed temperature measurement:
            bmp_status = bmp_in.getTemperature(T);

            // Start a pressure measurement:
            bmp_status = bmp_in.startPressure(0);
            t_start = millis();
            waiting_t=false;
            waiting_p=true;
        } else if (waiting_p && ((millis() - t_start) > bmp_status) ) {
            // Retrieve the completed pressure measurement:
            bmp_status = bmp_in.getPressure(P, T);
            waiting_p=false;

            // Update pressure & volume variables
            p_offset = P - p_tare;
            //Trunk the pressure if too big/low
            if (p_offset > max_pressure) {p_offset  = max_pressure;}
            if (p_offset < -max_pressure) {p_offset = -max_pressure;}
            // If no pressure, no volume
            if (p_offset < min_pressure && -p_offset<min_pressure) {
                bellow = NOPUSH;
                volume = 0;
            } else {
                if (p_offset < 0) { //PULL
                    bellow          = PULL;
                } else {            //PUSH
                    bellow          = PUSH;
                }
                bellow_not_null = bellow;
                //volume calculation :
                if(p_offset<0){
                    p_offset=-p_offset;
                }
                volume=compute_volume(p_offset);
                if (volume_attenuation>=volume) {
                    volume=0;
                } else {
                    volume-=volume_attenuation;
                }
            }

            // volume=100;
            // bellow=PUSH;
            // bellow_not_null=PUSH;
            //This can be used to visualise pressure/volume on OLED
            // oled.setCursor(0, 64/fontH-4);
            // sprintf(str_oled, "P=%lf\nP_tare=%lf\np_offset=%lf, \nvol=%d", P, p_tare, p_offset, volume);
            // oled.print(str_oled);
            // oled.print("   ");
            // oled.setCursor(0, 64/fontH-1);
            // oled.print("vol=");
            // oled.print(volume, DEC);
            // oled.print("   ");
        }


        #ifdef DEBUG
        if(micros()-t__dbg_temp>t__dbg_press){t__dbg_press=micros()-t__dbg_temp;}
        t__dbg_temp=micros();
        #endif


        if(volume_type==VOLUME_CONSTANT){
            bellow_not_null = bellow;
            volume = 100 - volume_attenuation ;
        }

        //-----------------------------------
        // Key acquisition from MCP
        //-----------------------------------
        //Mistake in PCB: RH keys are mapped from knee to head, we have to reverse it

        //For some reason, sometimes the periph seems to miss an access and its address are shifted
        //Once in  a while, re set address, this will add a small delay
        if(loop_count%200==0){
            mcp_rh_0.readPort(MCP23017Port::A);
            mcp_rh_1.readPort(MCP23017Port::A);
            mcp_lh_0.readPort(MCP23017Port::B);
            mcp_lh_1.readPort(MCP23017Port::B);
        }

        //Use burst instead of dedicated read to increase acquisition speed
        keys_rh =   (uint32_t)read_burst16_mcp(MCP23017_RH_0_SUB_ADDRESS, m_pI2CMaster)&0xffff;
        keys_rh |= ((uint32_t)read_burst16_mcp(MCP23017_RH_1_SUB_ADDRESS, m_pI2CMaster)&0xffff)<<16;

        keys_lh =   (uint32_t)read_burst16_mcp(MCP23017_LH_0_SUB_ADDRESS, m_pI2CMaster)&0xffff;
        keys_lh |= ((uint32_t)read_burst16_mcp(MCP23017_LH_1_SUB_ADDRESS, m_pI2CMaster)&0xffff)<<16;



        //Conversion to rows, then to R_press (bool)
        keys_rh_row_raw[0] =  (keys_rh & 0x7FF);
        keys_rh_row_raw[1] =  ((keys_rh >> (  ROW_NUMBER_R)) & 0x7FF) | (missing_key_1.Read()<<ROW_NUMBER_R);
        keys_rh_row_raw[2] =  ((keys_rh >> (2*ROW_NUMBER_R)) & 0x3FF) | (missing_key_2.Read()<<(ROW_NUMBER_R-1));
        for (uint8_t i = 0; i < ROW_NUMBER_R+1; i++)
        {
            if(i<ROW_NUMBER_R){
                keys_rh_row[0]|=((keys_rh_row_raw[0] & (1<<(ROW_NUMBER_R-i-1)))>>(ROW_NUMBER_R-i-1))<<i;
                keys_rh_row[2]|=((keys_rh_row_raw[2] & (1<<(ROW_NUMBER_R-i-1)))>>(ROW_NUMBER_R-i-1))<<i;
            }
            keys_rh_row[1]|=((keys_rh_row_raw[1] & (1<<(ROW_NUMBER_R-i)))>>(ROW_NUMBER_R-i))<<i;
        }
        
        // oled.setCursor(0, 64/fontH-4);
        // sprintf(str_oled, "%08x", keys_rh);
        // oled.print(str_oled);
        // oled.setCursor(0, 64/fontH-3);
        // sprintf(str_oled, "%04x %04x %04x", keys_rh_row_raw[0], keys_rh_row_raw[1], keys_rh_row_raw[2]);
        // oled.print(str_oled);
        // oled.setCursor(0, 64/fontH-2);
        // sprintf(str_oled, "%04x %04x %04x", keys_rh_row[0], keys_rh_row[1], keys_rh_row[2]);
        // oled.print(str_oled);

        for(uint8_t i=0;i<COLUMN_NUMBER_R;i++){
            for(uint8_t j=0;j<ROW_NUMBER_R;j++){
                R_press[i][j]=(keys_rh_row[i] & (1<<(j)))>>(j);
            }
        }

        remap_left_keys(keys_lh, keys_lh_row);
        for(uint8_t i=0;i<COLUMN_NUMBER_L;i++){
            for(uint8_t j=0;j<ROW_NUMBER_L;j++){
                L_press[i][j]=(keys_lh_row[i] & (1<<(j)))>>(j);
            }
        }



        #ifdef DEBUG
        if(micros()-t__dbg_temp>t__dbg_key){t__dbg_key=micros()-t__dbg_temp;}
        t__dbg_temp=micros();
        #endif
        //-----------------------------------
        //Menu navigation & presets
        //-----------------------------------
        //For some reason, going low on a FIELD crash the menu, try to avoid it
        if(R_press[MENU_KEY_COL][MENU_KEY_ROW]) {
            if (R_press[1][9] && ! R_prev_press[1][9]) {
                strIn.write('+'); //Up
                nav.doInput(strIn);
            }
            if (R_press[1][10] && ! R_prev_press[1][10]) {
                strIn.write('-'); //Down
                nav.doInput(strIn);
            }
            if (R_press[2][9] && ! R_prev_press[2][9]) {
                strIn.write('/'); //Prev
                nav.doInput(strIn);
            }
            if (R_press[0][9] && ! R_prev_press[0][9]) {
                strIn.write('*'); //Next
                nav.doInput(strIn);
            }
            nav.poll();
            //Registers
            if (R_press[2][8] && ! R_prev_press[2][8]) {
                set_stops(TOGGLE_BASSOON);
            }
            if (R_press[2][7] && ! R_prev_press[2][7]) {
                set_stops(TOGGLE_VIBRATO);
            }
            if (R_press[2][6] && ! R_prev_press[2][6]) {
                set_stops(TOGGLE_PICCOLO);
            }
            if (R_press[2][5] && ! R_prev_press[2][5]) {
                set_stops(TOGGLE_FLUTE);
            }

            //Presets
            if (R_press[1][8] && ! R_prev_press[1][8]) {
                set_preset(PRESET_DEFAULT);
            }
            if (R_press[1][7] && ! R_prev_press[1][7]) {
                set_preset(PRESET_SPLIT_OUT);
            }
            if (R_press[1][6] && ! R_prev_press[1][6]) {
                set_preset(PRESET_MIXED_OUT);
            }

            if(!debug_oled){
                oled.setCursor((128-2*fontW), 0);
                if(bassoon_enable){
                    oled.print("B");
                } else {
                    oled.print(" ");
                }
                oled.setCursor((128-2*fontW), 1);
                if(vibrato!=0){
                    oled.print("V");
                } else {
                    oled.print(" ");
                }
                oled.setCursor((128-2*fontW), 2);
                if(picolo_enable){
                    oled.print("P");
                } else {
                    oled.print(" ");
                }
                oled.setCursor((128-2*fontW), 3);
                if(flute_enable){
                    oled.print("F");
                } else {
                    oled.print(" ");
                }
            }
        }

        #ifdef DEBUG
        if(micros()-t__dbg_temp>t__dbg_menu){t__dbg_menu=micros()-t__dbg_temp;}
        t__dbg_temp=micros();
        #endif

        //-----------------------------------
        // Prepare MIDI message
        //-----------------------------------
        if(!R_press[MENU_KEY_COL][MENU_KEY_ROW]) {
            //Right hand
            for (size_t i = 0; i < COLUMN_NUMBER_R; i++) {
                for (size_t j = 0; j < ROW_NUMBER_R; j++) {
                    if (R_press[i][j]) {
                        if (bellow == PULL) {
                            R_played_note[R_notesT[i][j]+transpose]++;
                        } else if (bellow == PUSH) {
                            R_played_note[R_notesP[i][j]+transpose]++;
                        }
                    }
                }
            }

            //Left hand
            for (size_t i = 0; i < COLUMN_NUMBER_L; i++) {
                for (size_t j = 0; j < ROW_NUMBER_L; j++) {
                    if (L_press[i][j]) {
                        if (bellow == PULL) {
                            L_played_note[transpose_left_hand(L_notesT[i][j], transpose)]++;
                            if (fifth_enable) {
                                L_played_note[transpose_left_hand(L_notes_fifthT[i][j], transpose)]++;
                            }
                        } else if (bellow == PUSH) {
                            L_played_note[transpose_left_hand(L_notesP[i][j], transpose)]++;
                            if (fifth_enable) {
                                L_played_note[transpose_left_hand(L_notes_fifthP[i][j], transpose)]++;
                            }
                        }
                    }
                }
            }
        }
        
        #ifdef DEBUG
        if(micros()-t__dbg_temp>t__dbg_prep_midi){t__dbg_prep_midi=micros()-t__dbg_temp;}
        t__dbg_temp=micros();
        #endif

        //-----------------------------------
        // Send MIDI message
        //-----------------------------------
        if(volume_type==VOLUME_REVERTED){
            volume_resolved = (expression>volume_attenuation) ? expression-volume_attenuation : 0;
            expression_resolved = (volume != 0) ? volume+volume_attenuation : 0;
        } else {
            volume_resolved=volume;
            expression_resolved=expression;
        }

        //Right hand on channel_rh
        //Remove/add the notes for RH channel
        for (uint8_t i = 0; i < sizeof(R_played_note); ++i) {
            if (!R_played_note[i] && R_played_note_prev[i]) { //Remove if no longer played
                if (bassoon_enable) {
                    midi_broadcast_note_off(i+12*octave-12, 0, channel_rh);
                }
                if (picolo_enable) {
                    midi_broadcast_note_off(i+12*octave+12, 0, channel_rh);
                }
                if (flute_enable) {
                    midi_broadcast_note_off(i+12*octave, 0, channel_rh);
                }
            }
            if (R_played_note[i] && (!R_played_note_prev[i] || bellow_not_null != bellow_prev)) { //Add/relaunch note if bellow changed direction
                if (bassoon_enable) {
                    midi_broadcast_note_on(i+12*octave-12, expression_resolved, channel_rh);
                }
                if (picolo_enable) {
                    midi_broadcast_note_on(i+12*octave+12, expression_resolved, channel_rh);
                }
                if (flute_enable) {
                    midi_broadcast_note_on(i+12*octave, expression_resolved, channel_rh);
                }
            }
        }
        //We do vibrato separatedly even if it duplicates of code because to optimise with MIDI RunningStatus
        for (uint8_t i = 0; i < sizeof(R_played_note); ++i) {
            if (!R_played_note[i] && R_played_note_prev[i]) {
                if (vibrato!=0) {
                    midi_broadcast_note_off(i+12*octave, 0, VIBRATO_CHANNEL);
                }
            }
            if (R_played_note[i] && (!R_played_note_prev[i] || bellow_not_null != bellow_prev)) { //We must also relaunch note if bellow changed direction
                if (vibrato!=0) {
                    midi_broadcast_note_on(i+12*octave, expression_resolved, VIBRATO_CHANNEL);
                }
            }
        }

        //Left hand on channel_lh
        //Remove all the notes first
        for (uint8_t i = 0; i < sizeof(L_played_note); ++i) {
            if (!L_played_note[i] && L_played_note_prev[i]) {
                midi_broadcast_note_off(i, 0, channel_lh);
            }
            if (L_played_note[i] && (!L_played_note_prev[i] || bellow_not_null != bellow_prev)) {
                midi_broadcast_note_on(i, expression_resolved, channel_lh);
            }
        }


        //Send Volume
        if (bellow != NOPUSH) {bellow_prev = bellow;}
        if(volume_prev!=volume_resolved || loop_count%200==0){
            midi_broadcast_control_change(MIDI_CC_VOLUME, volume_resolved, channel_lh);
            midi_broadcast_control_change(MIDI_CC_VOLUME, volume_resolved, channel_rh);
            if(vibrato!=0){
                midi_broadcast_control_change(MIDI_CC_VOLUME, volume_resolved, VIBRATO_CHANNEL);
            }
        }
        volume_prev=volume_resolved;

        #ifdef DEBUG
        if(micros()-t__dbg_temp>t__dbg_send_midi){t__dbg_send_midi=micros()-t__dbg_temp;}
        t__dbg_temp=micros();
        #endif

        if(debug_oled){
            //Fancy displays (take a while and eat screen space so we limit it to debug)
            //Volume
            oled.setCursor(0, 64/fontH-1);
            for (uint8_t i = 0; i < 128/fontW ; i++) {
                if (volume/(fontW+1) > i) {
                    if(bellow==PUSH) {
                        str_oled[i] = '+';
                    } else {
                        str_oled[i] = '-';
                    }
                } else {
                    str_oled[i] = ' ';
                }
            }
            oled.print(str_oled);
            for (uint8_t i = 0; i < 128/fontW ; i++) {
                str_oled[i] = '\0';
            }
            //Keyboard right hand
            for (uint8_t i = 0; i < COLUMN_NUMBER_R; i++) {
                oled.setCursor(0, 64/fontH-4+i);
                for (uint8_t j = 0;  j < ROW_NUMBER_R; j++) {
                    if(R_press[i][j]){
                        str_oled[j] = '+';
                    } else {
                        str_oled[j] = '-';
                    }
                }
                oled.print(str_oled);
            }
            for (uint8_t i = 0; i < 128/fontW ; i++) {
                str_oled[i] = '\0';
            }
            //Keyboard left hand
            for (uint8_t i = 0; i < ROW_NUMBER_L; i++) {
                oled.setCursor((128-(COLUMN_NUMBER_L+1)*fontW), 64/fontH-7+i);
                for (uint8_t j = 0;  j < COLUMN_NUMBER_L; j++) {
                    if(L_press[j][i]){
                        str_oled[j] = '+';
                    } else {
                        str_oled[j] = '-';
                    }
                }
                oled.print(str_oled);
            }
        }

        #ifdef DEBUG
        if(micros()-t__dbg_start>t__dbg_loop){t__dbg_loop=micros()-t__dbg_start;}
        #endif
        #ifdef DEBUG
            // Serial.println("DEBUG DURATION");
            if(loop_count>1000){
                loop_count=0;
                Serial.print(millis(),DEC);
                Serial.print(";");
                Serial.print(p_offset,DEC);
                Serial.print(";");
                Serial.print(volume,DEC);
                Serial.print(";\n");

                Serial.println("init");
                Serial.println(t__dbg_init    , DEC);
                Serial.println("press1");
                Serial.println(t__dbg_press      , DEC);
                Serial.println("key");
                Serial.println(t__dbg_key    , DEC);
                Serial.println("menu");
                Serial.println(t__dbg_menu      , DEC);
                Serial.println("press wait");
                Serial.println(t__dbg_prep_midi      , DEC);
                Serial.println("send midi");
                Serial.println(t__dbg_send_midi , DEC);
                Serial.println("total");
                Serial.println(t__dbg_loop      , DEC);
                t__dbg_loop      = 0;
                t__dbg_init      = 0;
                t__dbg_press     = 0;
                t__dbg_key       = 0;
                t__dbg_menu      = 0;
                t__dbg_prep_midi = 0;
                t__dbg_send_midi = 0;
                // delay(100);
            }
            loop_count++;
        
            // Serial.println("END\n\r");
            // Serial.write(27);       // ESC command
            // Serial.print("[2J");    // clear screen command
            // Serial.write(27);
            // Serial.print("[H");     // cursor to home command
            // for (size_t j = 0; j < COLUMN_NUMBER_R; j++) {
            //     for (size_t i = 0; i < ROW_NUMBER_R; i++) {
            //         if(R_press[j][i]) {
            //             Serial.print("X");
            //         } else {
            //             Serial.print("-");
            //         }
            //     }
            //     Serial.print("   ");
            // }
            // Serial.print("\n\r");
        #endif
        loop_count++;
	}

}

