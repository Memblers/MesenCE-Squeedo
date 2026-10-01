# Mesen Community Edition - Squeedo

Mesen is a multi-system emulator for Windows, Linux, and macOS. It supports NES, SNES, Game Boy (GB/SGB/GBC), Game Boy Advance, PC Engine, SMS/Game Gear, and WonderSwan (WS/WSC).

This is a community-managed fork, created to maintain and expand this emulator into the future.

This fork of Mesen Community Edition adds support for the Squeedo mapper, a flash cart prototype from 2005, which was also used for certain builds of MIDINES.  Squeedo includes a PIC18F4620, with a bidirectional parallel port interface to the NES data bus.

## Squeedo Specs

PIC18F4620 is an MCU released in 2004, and was the best available with this parallel port feature in a DIP-40 package.  During development, other parts considered for cost reasons were PIC16F877, and PIC18F452.
* 64kB Program Flash, 16-bit instruction width
* 3.9kB Data RAM
* 8-bit PIC18F CPU, 10 MIPS @ 40 Mhz, single cycle 8x8 multiply
* 512kB PRG SST39SF040 Flash ROM, 32kB PRG pages
* 32kB PRG RAM, 8kB pages
* 32kB CHR RAM, 8kB pages, fixed 4-screen nametable

## Firmware

Firmware must be provided in Intel Hex format, with the same name as the .NES file.  Updated firmware may be found in a different repo TBD.  The squeedo folder includes a historical build.

## Disclosure

LLM-generated code is present in this build.  Token costs were roughly $10 to write it (mostly MiMo V2.5 Pro), and $13 to debug it (mostly Deepseek V4 Pro), with the initial version developed over 4 days.  My additions are provided under MIT License.

## Compiling

See [COMPILING.md](COMPILING.md)

## License

Mesen is available under the GPL V3 license.  Full text here: <http://www.gnu.org/licenses/gpl-3.0.en.html>

Copyright (C) 2014-2026 Sour, 2026 contributors

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
