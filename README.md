# This is a fork of the [MT32-PI](https://github.com/dwhinham/mt32-pi) project, this readme will only list the differences and specifics of my project, visit the original project page for more informations about MT32-PI

In this project, I integrated the work from my [previous arduino+RPI based MIDI instrument](https://github.com/Benz0X/midi_diatonic_accordion) into the MT32-PI software in order to have a bare metal raspberry pi software that synthetise audio and ALSO does the MIDI signal generation (thus removing the need of an Arduino, reducing latency and simplificating the wiring).

For any informations about the previous project, visit [its repository](https://github.com/Benz0X/midi_diatonic_accordion).

# Software
The idea was to port the whole software running on the arduino on the RPI, as the RPI also have I2C capabilities. As one of the RPI core was free, it was enough to add a new task in the MT32-PI class to handle the IOs and the MIDI generation. This is done in the diatotask function.

Two main issues were present:
- Sending the MIDI signals to MT32PI : this has been done using a shared buffer (implemented by a queue), spinlocked, filled by the new task and emptied with small modifications to the main task
- Porting the Arduino libraries : both the libraries and a bit of boilerplate code from the Arduino framework had to be slightly modified in order to work with circle and the ARM toolchain

As a bonus, thanks to the embeded FTP server, it is possible to make over the air updates to the software, configuration files or soundfonts!

# New functionnalities
As the RPI has plenty of memory available, an audio looper is implemented (basic Record/Overdub with only 1 layer of loop). It allows to superpose sounds from the same of different soundfonts (to investigate: audio glitches sometime happens when switching soundfonts)

# TODO
Bring back MIDI through USB ! As the RPI does not have an UART to USB bridge on it's power port, this functionnality has been broken.
However, it should be easy to implement using an UART to USB adapter or using an arduino or a microcontroller to do it.

