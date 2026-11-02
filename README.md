# Mesen Community Edition - Squeedo

Mesen is a multi-system emulator for Windows, Linux, and macOS. It supports NES, SNES, Game Boy (GB/SGB/GBC), Game Boy Advance, PC Engine, SMS/Game Gear, and WonderSwan (WS/WSC).

This fork of Mesen Community Edition adds support for the Squeedo mapper, a flash cart prototype from 2005.  Squeedo includes an 8-bit PIC18F MCU, with a bidirectional parallel port interface to the NES data bus.

Another feature is serial port emulation.  It supports bit-banged serial I/O through the controller port, as well as for Squeedo's UART.  To use this, create a linked pair of virtual serial ports in your operating system (I use com0com in Windows).  Use both ends of the pair as a virtual serial port cable, to link the emulated NES to a remote program.

This fork includes MCP server features from the sadnescity fork of MesenCE available here: <https://github.com/sadnescity/MesenCE>
The MCP server has been further enhanced and expanded in this fork, now includes event notifications in every response, suspected crash reporting, bankswitching history, and better access to Lua.

## Squeedo Specs

PIC18F4620 is an MCU released in 2004, in Microchip's PIC18F family at the time, it offered the largest ROM/RAM size while also including this parallel port feature in a DIP-40 package.
* 8-bit PIC18F CPU, 10 MIPS @ 40 Mhz, single cycle 8x8 multiply
* 64kB Program Flash, 16-bit instruction width
* 3.9kB Data RAM

* 512kB PRG SST39SF040 Flash ROM, 32kB PRG pages
* 32kB PRG RAM, 8kB pages
* 32kB CHR RAM, 8kB pages, fixed 4-screen nametable

## Firmware

Firmware must be provided in Intel Hex format, with the same name as the .NES file.  Updated firmware may be found in a different repo TBD.  The squeedo folder includes a historical build.

## Squeedo Historical Information

Squeedo was intended to be useful as a development cartridge, and also as a new mapper with a coprocessor, including a small lo-fi wavetable audio expansion.  Though it went unreleased in that form, the board was used by Wayfar Labs as a platform for MIDINES, and so it was able to see production in that form as a musical instrument.  MIDINES firmware is not included, I can't emulate it because I don't have a copy of that firmware, it's unrelated to Squeedo firmware.  It uses a similar, lower-cost pin-compatible PIC.

At the time of design, EPROM was cheaper than flash, only the smaller memory MCUs seemed cheap.  PIC MCUs only started using flash memory just a few years ago.  This board uses jumpers to select between EPROM or Flash for PRG.  Originally intended to use PIC16F871, as soon as it worked I immediately upgraded to the PIC18F, with it being more fun to program (much less bankswitching), and more useful with the multiply instruction.  The 18F452 was used for most development because it was readily available, though I ordered samples of and kept an eye on the newest series with 18F4525/4620.

The first-rev board can support a 27C080 EPROM, but DIP-32 flash maxes out at 512kB.  Initially, I used AMD flash with 64kB sectors, later moved to SST flash with 4kB sectors.

Second revision of the board in 2005 changed to surface mount parts (except for the PIC), dropped EPROM, widened address latch, added hardware for MIDI input, and is a much smaller board.

Future upgrade plans were to use PIC24, then PIC32MZ, but those designs also suffered from scope creep and neither of them made it as far along as this original one.  If I were to redo this today, I would use a dsPIC.

## Disclosure

LLM-generated code is present in this project.  My additions are provided under MIT License.  Most of it is MiMo V2.5 Pro, V2.6 Pro.  It is largely slop-coded.

PIC18 emulation is a performance pig.  For my PC at max speed, NES emulation gets about 1200FPS, Squeedo gets 120 FPS.  40Mhz PIC18 is roughly 5 times beefier than the 2A03, it seems like performance could be better.  PIC18 peripheral emulation attempts to be cycle-accurate, this may be part of it.


## Compiling

See [COMPILING.md](COMPILING.md)

## License

Mesen is available under the GPL V3 license.  Full text here: <http://www.gnu.org/licenses/gpl-3.0.en.html>

Copyright (C) 2014-2026 Sour, a2026 contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
