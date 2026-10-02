using Mesen.Interop;
using ModelContextProtocol.Server;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;

namespace Mesen.Mcp
{
	[McpServerResourceType]
	public class MesenMcpResources
	{
		[McpServerResource(UriTemplate = "mesen://memory-map/{consoleType}", Name = "Console Memory Map")]
		[Description("Get the memory map documentation for a specific console (NES, SNES, GB, GBA, PCE, SMS, WS)")]
		public static string GetMemoryMap(string consoleType)
		{
			return consoleType.ToUpperInvariant() switch {
				"NES" => @"# NES Memory Map

## CPU Address Space ($0000-$FFFF)
| Range | Size | Description |
|-------|------|-------------|
| $0000-$07FF | 2KB | Internal RAM (Work RAM) |
| $0800-$1FFF | - | Mirrors of $0000-$07FF |
| $2000-$2007 | 8 | PPU Registers |
| $2008-$3FFF | - | Mirrors of PPU Registers |
| $4000-$4017 | 24 | APU and I/O Registers |
| $4018-$401F | 8 | Normally disabled APU/IO |
| $4020-$5FFF | ~8KB | Cartridge expansion (mappers) |
| $6000-$7FFF | 8KB | PRG RAM (battery save) |
| $8000-$BFFF | 16KB | PRG ROM (lower bank) |
| $C000-$FFFF | 16KB | PRG ROM (upper bank) |

## PPU Registers
| Addr | Name | Description |
|------|------|-------------|
| $2000 | PPUCTRL | NMI enable, sprite size, BG tile select |
| $2001 | PPUMASK | Color emphasis, sprite/BG enable |
| $2002 | PPUSTATUS | VBlank flag, sprite 0 hit |
| $2003 | OAMADDR | Sprite RAM address |
| $2004 | OAMDATA | Sprite RAM data |
| $2005 | PPUSCROLL | Scroll position (2 writes) |
| $2006 | PPUADDR | VRAM address (2 writes) |
| $2007 | PPUDATA | VRAM data |
| $4014 | OAMDMA | Sprite DMA transfer |

## Vectors
| Address | Vector |
|---------|--------|
| $FFFA | NMI (VBlank) |
| $FFFC | RESET |
| $FFFE | IRQ/BRK |

## Mesen Memory Types
- NesMemory: Full CPU address space ($0000-$FFFF)
- NesPrgRom: PRG ROM data
- NesWorkRam: Internal RAM ($0000-$07FF)
- NesSaveRam: PRG RAM/SRAM ($6000-$7FFF)
- NesChrRom/NesChrRam: Pattern table data
- NesNametableRam: Nametable memory
- NesSpriteRam: OAM sprite RAM
- NesPaletteRam: Palette memory",

				"SNES" => @"# SNES Memory Map

## Banks $00-$3F (LoROM typical)
| Range | Description |
|-------|-------------|
| $0000-$1FFF | Mirror of Work RAM (first 8KB) |
| $2100-$21FF | PPU Registers |
| $2200-$22FF | (Reserved) |
| $4000-$43FF | CPU Registers (DMA, Joypad, etc.) |
| $6000-$7FFF | Expansion RAM |
| $8000-$FFFF | ROM Data |

## Key PPU Registers
| Addr | Name | Description |
|------|------|-------------|
| $2100 | INIDISP | Screen display, brightness |
| $2105 | BGMODE | BG mode and tile size |
| $210D | BG1HOFS | BG1 horizontal scroll |
| $2115 | VMAIN | VRAM address increment |
| $2116 | VMADDL | VRAM address (low) |
| $2118 | VMDATAL | VRAM data (low) |
| $2121 | CGADD | Palette address |
| $2122 | CGDATA | Palette data |
| $2138 | RDOAM | OAM data read |

## Mesen Memory Types
- SnesMemory: Full CPU address space
- SnesPrgRom: Program ROM
- SnesWorkRam: 128KB Work RAM
- SnesSaveRam: Battery save RAM
- SnesVideoRam: 64KB VRAM
- SnesSpriteRam: OAM (512+32 bytes)
- SnesCgRam: Palette RAM (512 bytes)
- SnesRegister: Register memory",

				"GB" or "GBC" or "GAMEBOY" => @"# Game Boy Memory Map

## Address Space ($0000-$FFFF)
| Range | Size | Description |
|-------|------|-------------|
| $0000-$3FFF | 16KB | ROM Bank 0 (fixed) |
| $4000-$7FFF | 16KB | ROM Bank 1-N (switchable) |
| $8000-$9FFF | 8KB | VRAM (Video RAM) |
| $A000-$BFFF | 8KB | External RAM (cartridge) |
| $C000-$CFFF | 4KB | Work RAM Bank 0 |
| $D000-$DFFF | 4KB | Work RAM Bank 1-7 (CGB) |
| $E000-$FDFF | - | Mirror of $C000-$DDFF |
| $FE00-$FE9F | 160 | OAM (Sprite Attribute Table) |
| $FF00-$FF7F | 128 | I/O Registers |
| $FF80-$FFFE | 127 | High RAM (HRAM) |
| $FFFF | 1 | Interrupt Enable register |

## Key I/O Registers
| Addr | Name | Description |
|------|------|-------------|
| $FF00 | P1/JOYP | Joypad |
| $FF40 | LCDC | LCD Control |
| $FF41 | STAT | LCD Status |
| $FF42 | SCY | Scroll Y |
| $FF43 | SCX | Scroll X |
| $FF44 | LY | Current scanline |
| $FF46 | DMA | OAM DMA transfer |
| $FF47 | BGP | BG palette (DMG) |

## Mesen Memory Types
- GameboyMemory: Full address space
- GbPrgRom: Cartridge ROM
- GbWorkRam: Work RAM
- GbCartRam: External/save RAM
- GbVideoRam: VRAM
- GbSpriteRam: OAM",

				"GBA" => @"# GBA Memory Map

## Address Space
| Range | Size | Description |
|-------|------|-------------|
| 0x00000000-0x00003FFF | 16KB | BIOS ROM |
| 0x02000000-0x0203FFFF | 256KB | External Work RAM (EWRAM) |
| 0x03000000-0x03007FFF | 32KB | Internal Work RAM (IWRAM) |
| 0x04000000-0x040003FE | 1KB | I/O Registers |
| 0x05000000-0x050003FF | 1KB | Palette RAM (BG + OBJ) |
| 0x06000000-0x06017FFF | 96KB | VRAM |
| 0x07000000-0x070003FF | 1KB | OAM (Sprite Attributes) |
| 0x08000000-0x09FFFFFF | 32MB | Game Pak ROM (Wait State 0) |
| 0x0A000000-0x0BFFFFFF | 32MB | Game Pak ROM (Wait State 1) |
| 0x0C000000-0x0DFFFFFF | 32MB | Game Pak ROM (Wait State 2) |
| 0x0E000000-0x0E00FFFF | 64KB | Game Pak SRAM |

## Key I/O Registers
| Addr | Name | Description |
|------|------|-------------|
| 0x04000000 | DISPCNT | Display control |
| 0x04000004 | DISPSTAT | Display status |
| 0x04000006 | VCOUNT | Current scanline |
| 0x04000008 | BG0CNT | BG0 control |
| 0x04000010 | BG0HOFS | BG0 horizontal scroll |
| 0x04000130 | KEYINPUT | Key status |
| 0x04000200 | IE | Interrupt enable |
| 0x04000208 | IME | Master interrupt enable |

## Mesen Memory Types
- GbaMemory: Full address space
- GbaPrgRom: Cartridge ROM
- GbaIntWorkRam: IWRAM (32KB)
- GbaExtWorkRam: EWRAM (256KB)
- GbaSaveRam: SRAM
- GbaVideoRam: VRAM (96KB)
- GbaSpriteRam: OAM
- GbaPaletteRam: Palette RAM",

				"PCE" => @"# PC Engine / TurboGrafx-16 Memory Map

## CPU Address Space ($0000-$FFFF, banked via MMR)
| Range | Description |
|-------|-------------|
| $0000-$1FFF | Page mapped via MMR[0] |
| $2000-$3FFF | Page mapped via MMR[1] |
| ...   | ... |
| $E000-$FFFF | Page mapped via MMR[7] — I/O at $0000-$0400 |

## I/O Ports ($0000-$0400 in bank $FF)
| Range | Device |
|-------|--------|
| $0000-$0003 | VDC (Video Display Controller) |
| $0004-$0007 | VCE (Video Color Encoder) |
| $0800-$0807 | PSG (Sound) |
| $0C00 | Timer |
| $1000 | Joypad |
| $1400 | IRQ control |

## Mesen Memory Types
- PceMemory: CPU address space
- PcePrgRom: HuCard ROM
- PceWorkRam: Work RAM (8KB)
- PceVideoRam: VRAM
- PcePaletteRam: Palette RAM
- PceSpriteRam: SAT (sprite attributes)",

				"SMS" or "GG" => @"# Sega Master System / Game Gear Memory Map

## CPU Address Space ($0000-$FFFF)
| Range | Size | Description |
|-------|------|-------------|
| $0000-$03FF | 1KB | ROM (unpaged, always visible) |
| $0400-$3FFF | ~16KB | ROM Slot 0 (page 0) |
| $4000-$7FFF | 16KB | ROM Slot 1 (page 1) |
| $8000-$BFFF | 16KB | ROM Slot 2 (page 2, or cartridge RAM) |
| $C000-$DFFF | 8KB | System RAM |
| $E000-$FFFF | 8KB | Mirror of system RAM |

## I/O Ports
| Port | Description |
|------|-------------|
| $7E | VDP V counter |
| $7F | VDP H counter |
| $BE | VDP data |
| $BF | VDP control/status |
| $DC | Joypad port 1 |
| $DD | Joypad port 2 |
| $3F | Nationalization |

## Mesen Memory Types
- SmsMemory: CPU address space
- SmsPrgRom: Cartridge ROM
- SmsWorkRam: System RAM (8KB)
- SmsCartRam: Cartridge RAM
- SmsVideoRam: VRAM (16KB)
- SmsPaletteRam: CRAM palette",

				"WS" or "WSC" or "WONDERSWAN" => @"# WonderSwan Memory Map

## Address Space (20-bit, $00000-$FFFFF)
| Range | Description |
|-------|-------------|
| $00000-$03FFF | Internal RAM (16KB) |
| $04000-$0FFFF | (Reserved) |
| $10000-$1FFFF | SRAM (if present) |
| $20000-$FFFFF | Cartridge ROM (banked) |

## I/O Ports ($00-$FF)
| Range | Description |
|-------|-------------|
| $00-$3F | Display controller |
| $40-$5F | (Reserved) |
| $60-$6F | Sound |
| $80-$9F | System control |
| $A0-$BF | Timer, DMA |
| $B0-$BF | Keypad |

## Mesen Memory Types
- WsMemory: CPU address space
- WsPrgRom: Cartridge ROM
- WsWorkRam: Internal RAM
- WsCartRam: SRAM
- WsCartEeprom: Cartridge EEPROM
- WsInternalEeprom: Internal EEPROM",

				_ => $"# Memory Map\n\nNo detailed memory map available for console type '{consoleType}'.\n\nAvailable types: NES, SNES, GB, GBA, PCE, SMS, WS\n\nUse `mesen_list_memory_types` to see available memory regions for the currently loaded ROM."
			};
		}

		[McpServerResource(UriTemplate = "mesen://cpu-instructions/{cpuArch}", Name = "CPU Instruction Reference")]
		[Description("Quick reference for CPU instructions (6502, 65816, Z80, ARM7, HuC6280)")]
		public static string GetCpuInstructions(string cpuArch)
		{
			return cpuArch.ToUpperInvariant() switch {
				"6502" or "NES" => @"# 6502 CPU Instruction Quick Reference (NES)

## Load/Store
LDA #$val / LDA addr — Load A | LDX / LDY — Load X/Y
STA addr — Store A | STX / STY — Store X/Y

## Arithmetic
ADC — Add with carry | SBC — Subtract with carry
INC / DEC — Increment/decrement memory | INX/INY/DEX/DEY — Inc/Dec X/Y
CMP / CPX / CPY — Compare

## Logic
AND / ORA / EOR — Bitwise ops | ASL / LSR — Shift left/right
ROL / ROR — Rotate | BIT — Test bits

## Branch
BEQ / BNE — Branch if equal/not equal | BCS / BCC — Branch if carry set/clear
BMI / BPL — Branch if minus/plus | BVS / BVC — Branch if overflow set/clear
JMP — Jump | JSR — Jump to subroutine | RTS — Return from subroutine

## Stack
PHA / PLA — Push/pull A | PHP / PLP — Push/pull flags
TSX / TXS — Transfer SP

## Flags
SEC / CLC — Set/clear carry | SED / CLD — Set/clear decimal
SEI / CLI — Set/clear interrupt disable

## Status Flags: N V - B D I Z C
N=Negative V=Overflow B=Break D=Decimal I=IRQ Z=Zero C=Carry

## Addressing Modes
#$val (immediate) | $addr (zero page) | $addr,X (ZP indexed)
$addr (absolute) | $addr,X | $addr,Y | ($addr,X) | ($addr),Y",

				"65816" or "SNES" or "SA1" => @"# 65816 CPU Instruction Quick Reference (SNES)

## Same as 6502, plus:
REP #$val — Reset status bits | SEP #$val — Set status bits
XBA — Exchange A high/low bytes
TCD / TDC — Transfer C↔D (Direct Page)
PHB / PLB — Push/pull Data Bank
PHD / PLD — Push/pull Direct Page
PHK — Push Program Bank

## 16-bit modes (controlled by REP/SEP)
M flag (bit 5): 0 = 16-bit A, 1 = 8-bit A
X flag (bit 4): 0 = 16-bit X/Y, 1 = 8-bit X/Y

## New Addressing Modes
[$addr] — Long indirect | [$addr],Y — Long indirect indexed
addr,S — Stack relative | (addr,S),Y — Stack relative indirect indexed
[dp] — Direct page indirect long

## Long Addressing
JSL / RTL — Long subroutine call/return (24-bit)
JMP [$addr] — Jump indirect long
MVN / MVP — Block move",

				"Z80" or "GB" or "GAMEBOY" or "SMS" => @"# Z80-like CPU Quick Reference (Game Boy / SMS)

## Load
LD r,r — Register to register | LD r,n — Immediate
LD r,(HL) — Indirect | LD (HL),r | LD A,(nn) | LD (nn),A
LDI / LDD — Load and inc/dec HL (GB)
PUSH / POP — Stack operations

## Arithmetic
ADD / ADC / SUB / SBC — Add/subtract
INC / DEC — Increment/decrement
CP — Compare | DAA — Decimal adjust
AND / OR / XOR — Logic

## Shifts/Rotates
RLCA / RRCA / RLA / RRA — Rotate A
RLC / RRC / RL / RR — Rotate register (CB prefix)
SLA / SRA / SRL — Shift (CB prefix)
SWAP — Swap nibbles (CB prefix, GB only)
BIT / SET / RES — Bit test/set/reset (CB prefix)

## Jumps/Calls
JP nn / JP cc,nn — Jump (conditional)
JR n / JR cc,n — Relative jump
CALL nn / CALL cc,nn — Call subroutine
RET / RET cc / RETI — Return

## Registers: A F B C D E H L SP PC
Pairs: AF BC DE HL
Flags: Z N H C (Zero, Subtract, Half-carry, Carry)",

				"ARM" or "ARM7" or "GBA" => @"# ARM7TDMI Quick Reference (GBA)

## ARM Mode (32-bit instructions)
MOV / MVN — Move / Move NOT
ADD / SUB / RSB — Add / Sub / Reverse sub
AND / ORR / EOR / BIC — Bitwise ops
CMP / CMN / TST / TEQ — Compare / Test
LDR / STR — Load/Store word
LDM / STM — Load/Store multiple
B / BL — Branch / Branch with Link
MUL / MLA — Multiply

## THUMB Mode (16-bit instructions)
Same ops but 2-register form, limited immediates
BX — Branch and exchange (switch ARM↔THUMB)

## Registers
R0-R12 — General purpose
R13 (SP) — Stack pointer
R14 (LR) — Link register (return address)
R15 (PC) — Program counter

## CPSR Flags: N Z C V
N=Negative Z=Zero C=Carry V=Overflow

## Condition Codes (suffix)
EQ/NE (Z) | CS/CC (C) | MI/PL (N) | VS/VC (V)
HI/LS (C&!Z) | GE/LT (N==V) | GT/LE (N==V&!Z) | AL (always)",

				_ => $"No instruction reference available for '{cpuArch}'.\nAvailable: 6502 (NES), 65816 (SNES), Z80 (GB/SMS), ARM (GBA)"
			};
		}
	}
}
