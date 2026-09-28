# Squeedo Mapper — Mesen Emulation Specification

## 1. Overview

Squeedo is a custom NES cartridge designed by Memblers (c. 2004–2005) featuring a
**PIC18F4620 microcontroller** as the mapper coprocessor. The NES CPU communicates
with the PIC through a memory-mapped PSP (Parallel Slave Port) at $5000–$5FFF.
The PIC controls PRG-ROM bankswitching, CHR-RAM bankswitching, SRAM banking, and
IRQ generation via direct GPIO connections to the cartridge bus.

**Critical design principle:** The Squeedo hardware is a *transparent bridge*
between the NES CPU and the PIC microcontroller. The register functions
documented in this spec are determined entirely by the PIC firmware (`.hex`
file), not by the hardware. Different firmware can implement completely different
mapper behaviors on the same board. This document describes both the fixed
hardware and the reference firmware (`tut.asm`).

### Key Hardware (Fixed)

| IC   | Part         | Function                          |
|------|--------------|-----------------------------------|
| U1   | SST39SF040   | 512KB Flash PRG-ROM (DIP32)       |
| U2   | 62256        | 32KB CHR-RAM (also serves 4-screen nametables) |
| U3   | 62256        | 32KB SRAM (battery-backed WRAM)   |
| U4   | PIC18F4620   | Mapper coprocessor @ 40 MHz       |
| U5   | NES 6113     | CIC lockout chip                  |
| U6   | 74HC139      | Dual 2-to-4 address decoder       |
| U7   | 74HC00       | Quad NAND gate (glue logic)       |
| U8   | 74HC374      | Octal D latch (NES address capture) |
| U9   | 74HC04       | Hex inverter (MAP CE → LTC CE)    |

---

## 2. NES Address Map (Hardware)

### 2.1 Address Decoding

The 74HC139 dual decoder (U6) provides chip selects. U6.A performs coarse
decoding; U6.B (enabled by U6.A Y2) provides finer selects for the mapper
register range. The 74HC04 inverter (U9) inverts MAP CE to produce LTC CE,
which clocks the 74HC374 address latch (U8).

**Confirmed decode chain:**

The NES CPU accesses $5000–$5FFF to reach the PIC. Within this range,
A14 = 1, A13 = 0, A12 = 1:

```
NES CPU address $5xxx
  A14=1, A13=0 → U6.A Y2 active
    → enables U6.B
      A12=1 → U6.B output → MAP CE (active low)
        → U9 inverts → LTC CE (clocks U8 address latch)
        → PIC PSP /CS (via PortE)
```

Other U6.A outputs decode WRAM CE ($6000–$7FFF) and PRG ROM CE ($8000+). The
exact A/B/E input wiring for U6.A was not fully traced from the schematic — the
memory map below is derived from the firmware and documentation, confirmed by the
designer.

### 2.2 Full NES Memory Map

| NES Address     | Signal    | Device                  | Notes                          |
|-----------------|-----------|-------------------------|--------------------------------|
| $5000–$5FFF     | MAP CE    | PIC18F4620 PSP          | NES ↔ PIC bridge (A0–A4 select register) |
| $6000–$7FFF     | WRAM CE   | 62256 SRAM (U3)         | 8KB window, PIC controls bank bits |
| $8000–$FFFF     | PRG ROM   | SST39SF040 (U1)         | 32KB window, PIC controls bank bits |

**Note:** The memory map addresses are fixed by hardware wiring. What the PIC
*does* when the NES accesses $5000–$5FFF is entirely determined by firmware.

---

## 3. PIC Interface (Hardware)

### 3.1 PSP (Parallel Slave Port) Mechanism

When the NES accesses $5000–$5FFF:

1. **74HC374 latch (U8)** captures NES address bits A0–A4 on the falling edge of
   MAP CE (inverted by U9 to LTC CE). These appear on PIC PortB bits 0–4.
2. **NES data bus** (D0–D7) connects directly to PIC PortD.
3. **PSP control signals** on PIC PortE:
   - RE0: NES read strobe
   - RE1: NES write strobe
   - RE2: MAP CE (active low)
4. The PSP hardware sets **IBF** (Input Buffer Full) on writes, **OBF** (Output
   Buffer Full) on reads, and generates **PSPIF** (high-priority interrupt).

### 3.2 PIC GPIO Connections (Hardware — Fixed)

These are direct PCB traces from the PIC to cartridge bus lines. The firmware
controls them as GPIO outputs.

#### PRG-ROM Bank Lines (to SST39SF040 A15–A18)

| PIC Pin | Flash Address Line | Function               |
|---------|-------------------|------------------------|
| RA0     | A15               | PRG bank bit 0         |
| RA1     | A16               | PRG bank bit 1         |
| RA2     | A17               | PRG bank bit 2         |
| RA3     | A18               | PRG bank bit 3         |

4 bits → 16 × 32KB = 512KB full ROM addressable.

#### CHR-RAM Bank Lines (to 62256 via NAND gates)

| PIC Pin | CHR Address Line | Function               |
|---------|-----------------|------------------------|
| RC1     | A13             | CHR bank bit 0         |
| RC2     | A14             | CHR bank bit 1         |

2 bits → 4 × 8KB = 32KB full CHR-RAM addressable. Gated through U7:C/U7:D
NAND gates with PPU address lines.

#### SRAM Bank Lines (to 62256)

| PIC Pin | SRAM Address Line | Function              |
|---------|-------------------|-----------------------|
| RB6     | A13               | SRAM bank bit 0       |
| RB7     | A14               | SRAM bank bit 1       |

2 bits → 4 × 8KB = 32KB full SRAM addressable.

#### Other GPIO

| PIC Pin | Signal       | Direction | Function                    |
|---------|-------------|-----------|-----------------------------|
| RA5     | NES /IRQ    | Output    | Direct to NES IRQ pin       |
| RC3     | LED         | Output    | Status LED                  |
| RC6     | UART TX     | Output    | Serial transmit (via MAX202) |
| RC7     | UART RX     | Input     | Serial receive (via MAX202)  |
| RC0     | M2          | Input     | NES clock phase             |

#### PSP Data/Control (Hardware — Fixed)

| PIC Port | Signal           | Direction | Function                    |
|----------|-----------------|-----------|-----------------------------|
| RD0–RD7  | NES data bus D0–D7 | Bidirectional | PSP data bus            |
| RB0–RB4  | Latched A0–A4    | Input     | Register address from U8    |
| RE0      | NES /RD          | Input     | PSP read strobe             |
| RE1      | NES /WR          | Input     | PSP write strobe            |
| RE2      | MAP CE           | Input     | PSP chip select             |

---

## 4. Reference Firmware (`tut.asm`)

Everything below describes the **reference firmware's** behavior. The register
map, bankswitching scheme, IRQ system, and audio engine are all implemented in
PIC assembly code and can be completely changed by loading a different `.hex`
file.

### 4.1 Register Map ($5000–$501F)

The firmware defines 32 registers at $5000–$501F, selected by latched A0–A4.
All registers are readable and writable unless noted.

#### Bankswitching

| Register | Name | Write | Read |
|----------|------|-------|------|
| $5000    | PRG Page Select | D0–D3: Select 32KB PRG-ROM page (0–15) | Current value |
| $5001    | RAM Page Select | D0–D1: Select 8KB SRAM page (0–3) | Current value |
| $5002    | CHR Page Select | D0–D1: Select 8KB CHR-RAM page (0–3) | Current value |

#### IRQ Control

| Register | Name | Write | Read |
|----------|------|-------|------|
| $5003    | NMI Acknowledge | Any write — Intended to sync mapper to NES vblank for display-list-style CHR banking. | — |
| $5004    | IRQ Acknowledge | Any write clears NES /IRQ line | — |
| $5005    | IRQ Source Select | D0: PPU IRQ enable, D1: CPU IRQ enable, D2: UART RX IRQ, D3: UART TX IRQ, D4: Auto CHR page cycling | D0: PPU IRQ active, D1: CPU IRQ active, D2: UART RX ready, D3: UART TX ready |
| $5006    | CPU Cycle Count Lo | D0–D7: Low byte of CPU-cycle IRQ timer | — |
| $5007    | CPU Cycle Count Hi | D0–D7: High byte of CPU-cycle IRQ timer | — |
| $5008    | PPU Scanline Count | D0–D7: Scanline count for PPU IRQ | — |

#### Synthesizer

| Register | Name | Write | Read |
|----------|------|-------|------|
| $5010    | Wave Address Lo | Set wavetable write pointer (low byte) | — |
| $5011    | Wave Address Hi | Set wavetable write pointer (high byte) | — |
| $5012    | Wave Data | Write byte to wavetable, auto-increment pointer | — |
| $5013    | Channel Select | D0–D1: Select synth channel (0–3) for subsequent writes | — |
| $5014    | Channel Volume | Set selected channel's volume (0–255) | — |
| $5015    | Channel Freq Fine | Set selected channel's rate fine byte (inverted: stored XOR $FF) | — |
| $5016    | Channel Freq Lo | Set selected channel's rate low byte | — |
| $5017    | Channel Freq Hi | Set selected channel's rate high byte | — |

#### UART

| Register | Name | Write | Read |
|----------|------|-------|------|
| $500B    | UART Data | Byte to transmit via serial | Received byte from serial |

#### Status / Misc

| Register | Name | Write | Read |
|----------|------|-------|------|
| $5009    | — | — | — |
| $500A    | — | — | — |
| $500C    | LED Off | Turns status LED off | — |
| $500D    | LED On  | Turns status LED on  | — |
| $500E–$500F | — | — | — |
| $5018–$501B | — | Reserved / user commands | — |
| $501C–$501F | — | Reserved / user data | — |

#### Register Mirrors

All registers are mirrored within $5000–$5FFF (A5–A14 ignored for register
selection). The register number is determined solely by A0–A4.

---

## 5. PRG-ROM Banking (Firmware)

The hardware connects 4 PIC GPIO pins (RA0–RA3) directly to SST39SF040
address lines A15–A18. The firmware controls which 32KB page appears at
$8000–$FFFF by writing to these pins.

### 5.1 Reference Firmware Behavior

The reference firmware uses register $5000 (bits D0–D3) to select the PRG page.
When the NES writes to $5000, the PIC's ISR updates PortA bits 0–3:

```
PRG_ROM_address = (bank << 15) | (cpu_addr & $7FFF)
```

Where `bank` = value written to $5000 (0–15).

Both $8000–$BFFF and $C000–$FFFF come from the same 32KB bank — there is no
independent switchover for the upper and lower halves. The entire $8000–$FFFF
window switches as a unit.

### 5.2 Flash Programming

The SST39SF040 supports in-system sector erase and byte programming. The NES
CPU can reflash the PRG-ROM through the PIC, which controls the flash /WE and
/OE lines. Sector size is 4KB. Programming requires the standard SST command
sequence ($AA→$5555, $55→$2AAA, $A0→$5555, then data). Flash programming is
handled by firmware — the hardware only provides the electrical connections.

---

## 6. CHR-RAM Banking (Firmware)

The hardware connects 2 PIC GPIO pins (RC1, RC2) to CHR-RAM address lines
A13–A14 via NAND gates. The firmware controls which 8KB page of the 32KB
CHR-RAM is active at PPU $0000–$1FFF.

### 6.1 Reference Firmware Behavior

The reference firmware uses register $5002 (bits D0–D1) to select the CHR page:

```
CHR_RAM_address = (bank << 13) | (ppu_addr & $1FFF)
```

Where `bank` = value written to $5002 (0–3).

### 6.2 Auto CHR Page Cycling

When IRQ source D4 (Auto CHR IRQ) is enabled in $5005, the CHR bank register
automatically advances to the next page on each PPU-sourced IRQ. This allows
multi-bank CHR animation driven by scanline timing without NES CPU intervention.

**Note:** The vblank-reset mechanism via $5003 (NMI acknowledge) was planned but
is **not implemented** — no NMI line from the NES is connected to the PIC on this
board revision. The auto-cycling resets would need an alternative sync mechanism.

---

## 7. 4-Screen Nametable Memory (Hardware)

### 7.1 Configuration

The cartridge provides **4-screen nametable RAM** — all four 1KB nametable pages
($2000, $2400, $2800, $2C00) are unique and fixed, stored in the CHR-RAM (U2).

- NES internal 2KB VRAM is **disabled** (CIRAM /CE held high via cartridge)
- The cartridge 62256 serves both pattern tables ($0000–$1FFF) and nametables
  ($2000–$2FFF)
- PPU A13 distinguishes pattern table (A13=0) from nametable (A13=1) access
- The PIC's bank bits (RC1/RC2) are gated by NAND logic so they only affect
  pattern table addresses (A13=0). Nametables are always at fixed addresses.

### 7.2 PPU Address Space Layout

| PPU Address       | Content           | CHR-RAM Address                |
|-------------------|-------------------|--------------------------------|
| $0000–$0FFF       | Pattern Table 0   | bank*$2000 + ppu_addr          |
| $1000–$1FFF       | Pattern Table 1   | bank*$2000 + ppu_addr          |
| $2000–$23FF       | Nametable 0       | ppu_addr (fixed)               |
| $2400–$27FF       | Nametable 1       | ppu_addr (fixed)               |
| $2800–$2BFF       | Nametable 2       | ppu_addr (fixed)               |
| $2C00–$2FFF       | Nametable 3       | ppu_addr (fixed)               |
| $3000–$3FFF       | Mirror of $2000   | (hardware mirror)              |

Pattern tables ($0000–$1FFF) are banked by the PIC via RC1/RC2. The four
nametables ($2000–$2FFF) are at fixed addresses in CHR-RAM and are not affected
by CHR bank switching.

### 7.3 Attribute Tables

Each nametable has a 64-byte attribute table at offset $3C0–$3FF within its 1KB
page. With 4-screen, all four attribute tables are independent.

### 7.4 Implementation Notes for Mesen

- Set mirroring mode to **4-screen** (NT_MIRROR::FOUR_SCREEN)
- Allocate 4KB of dedicated nametable RAM on the cartridge
- CIRAM /CE is always high — NES internal VRAM is never accessed
- CHR banking only affects pattern tables ($0000–$1FFF), not nametables

---

## 8. SRAM (Firmware)

### 8.1 Configuration

- **RAM size:** 32KB (62256, U3) organized as 4 × 8KB pages
- **Address range:** $6000–$7FFF
- **Battery-backed:** Yes (retains data with power removed)

The hardware connects 2 PIC GPIO pins (RB6, RB7) to SRAM address lines A13–A14.
The firmware controls which 8KB page is active.

### 8.2 Reference Firmware Behavior

The reference firmware uses register $5001 (bits D0–D1) to select the SRAM page:

```
SRAM_address = (bank << 13) | (cpu_addr & $1FFF)
```

Where `bank` = value written to $5001 (0–3).

---

## 9. IRQ System (Firmware)

### 9.1 IRQ Output (Hardware)

The PIC drives NES /IRQ directly from a GPIO pin (active low). When the PIC
asserts /IRQ low, the NES CPU's IRQ vector is triggered (if the I flag is clear
in the NES CPU's status register).

### 9.2 Reference Firmware IRQ Behavior

The reference firmware configures IRQ sources via register $5005 (write).
Multiple sources can be enabled simultaneously. The NES CPU acknowledges IRQs
by writing to $5004, which tells the firmware to de-assert the line.

#### PPU IRQ (Scanline Timer)

- Uses PIC **Timer0**, counting NES PPU scanline events
- Period configured by $5008 (scanline count)
- Generates IRQ to NES after the specified number of scanlines
- If Auto CHR IRQ ($5005 D4) is enabled, CHR bank advances on each PPU IRQ
  without generating NES IRQs (except the first?)

#### CPU IRQ (Cycle Timer)

- Uses PIC **Timer1**, counting NES CPU cycles (M2 clock)
- 16-bit period configured by $5006 (low) and $5007 (high)
- Generates IRQ to NES after the specified number of CPU cycles

#### UART RX IRQ

- Fires when a byte is received on the PIC's serial port
- NES code reads the byte from $500B

#### UART TX IRQ

- Fires when the PIC's serial transmit buffer is empty
- NES code can send the next byte to $500B

### 9.3 PSP Timing

The PIC runs at 40 MHz (HSPLL, 20 MHz crystal × 2), executing one instruction
per 4 clock cycles = 10 MIPS. Timer0, Timer1, and Timer3 run on the PIC's
instruction clock or prescaled variants.

The NES runs at ~1.789773 MHz (NTSC). The PIC's interrupt response time is
3–7 instruction cycles (1.2–2.8 µs). The NES's M2 clock period is ~558 ns.

For the PSP interrupt path:
1. NES writes to $5000–$5FFF → MAP CE goes low
2. 74HC374 latches A0–A4 on LTC CE edge
3. PIC PSP sets IBF flag and triggers PSPIF (high-priority interrupt)
4. PIC ISR reads PortD (data) and PortB (latched address)
5. PIC processes register write
6. Total latency from NES write to PIC processing: ~1–2 µs

---

## 10. Audio Subsystem (Firmware)

### 10.1 Synthesizer Engine

The reference firmware runs a **4-channel wavetable synthesizer** at 11025 Hz
sample rate (via PIC Timer3).

Each channel has:
- **24-bit phase accumulator** (index_fine : index_lo : index)
- **24-bit frequency** (rate_fine : rate_lo : rate_msb)
- **8-bit volume** (0–255)
- **8-bit wavetable lookup** (256-entry waveform in PIC RAM at $0280)

Phase accumulation per sample:
```
phase += rate    (24-bit add with carry)
sample = wavetable[phase >> 16]   (top 8 bits index the waveform)
```

Mixing:
```
output = Σ (channel_sample × channel_volume)    (24-bit accumulator)
output >>= 3    (divide by 8 to normalize 4-channel mix)
output |= $80   (convert signed to unsigned for $4011)
```

### 10.2 Audio Output Path

The synthesizer output byte is placed on **PIC PortD** (the PSP data bus). The PIC
then **asserts NES /IRQ** to notify the NES CPU.

The NES IRQ handler:
1. Reads the mapper register (e.g., $500B) to get the sample byte from PIC PortD
2. Writes the byte to **NES APU register $4011** (DMC direct load, 7-bit DAC)
3. De-asserts IRQ by writing to $5004

This delivers audio through the **NES APU's standard output path**, requiring no
custom audio mixing in the emulator.

**Important PSP read behavior:** The NES can only read data that the PIC has
**pre-loaded** into PortD before the read occurs. The NES CPU read strobe captures
whatever byte is already on the bus — by the time the PIC's PSP interrupt fires
and the PIC could react to the latched register address, the NES read cycle is
already over. For audio, the PIC pre-loads the sample byte onto PortD and then
asserts /IRQ, so the subsequent NES read gets the correct sample.

### 10.3 Wavetable Upload

The NES CPU uploads waveform data to the PIC's RAM via registers $5010–$5012:

```
$5010 = address_low    (set write pointer, low byte)
$5011 = address_high   (set write pointer, high byte)
$5012 = data           (write byte, pointer auto-increments)
```

The wavetable is stored at PIC RAM address $0280 (256 bytes). The NES can also
upload waveforms to arbitrary PIC RAM addresses for more complex setups.

### 10.4 Channel Control

Select a channel (0–3) via $5013, then set parameters via $5014–$5017:

| Register | Parameter    | Effect                              |
|----------|-------------|-------------------------------------|
| $5014    | Volume      | 0 = silent, 255 = maximum           |
| $5015    | Rate Fine   | Fractional phase increment (inverted: stored XOR $FF) |
| $5016    | Rate Lo     | Low byte of phase increment         |
| $5017    | Rate Hi     | High byte of phase increment        |

#### Frequency Calculation

```
frequency = (rate_value × sample_rate) / 2^24
frequency = (rate_value × 11025) / 16777216
```

Example: To produce A-440 Hz:
```
rate = (440 × 16777216) / 11025 = 671,294 = $0A3E7E
$5015 = $7E XOR $FF = $81    (fine byte, inverted)
$5016 = $3E                   (low byte)
$5017 = $0A                   (high byte)
```

---

## 11. NES-side Audio IRQ Handler (Reference)

The NES CPU must run an IRQ handler that reads samples from the PIC and writes
them to $4011. At 11025 Hz, the IRQ fires approximately every 162 NTSC CPU
cycles. A minimal handler:

```asm
; NES IRQ handler — called at 11025 Hz
irq_handler:
    pha
    txa
    pha

    lda $500B       ; read sample byte from PIC
    sta $4011       ; write to NES APU DMC DAC

    lda $5004       ; acknowledge IRQ (de-assert PIC /IRQ)

    pla
    tax
    pla
    rti
```

**Timing budget:** 162 cycles per sample. The handler above uses ~30 cycles,
leaving ~132 cycles for game logic per IRQ. If the handler takes too long,
samples will be missed and audio will glitch.

---

## 12. PIC Firmware Considerations

### 12.1 Boot Sequence

The PIC18F4620 boots from its **boot block** ($0000–$07FF) where the bootloader
(Philpem's BOOT18) resides. The bootloader:
1. Sends "H" on UART at 115200 baud
2. Waits 500ms for "i" response from host
3. If received: enters bootloader mode ("K" response, accepts Intel HEX uploads)
4. If timeout: jumps to user code at **$0800** (the reset vector)

For emulation, the bootloader is skipped — the emulator loads user firmware
directly at $0800.

### 12.2 Firmware Loading

The PIC firmware is distributed as a **Microchip Intel HEX file** (`.hex`), the
native output format of MPLAB / MPASM / XC8. The `.hex` file contains the firmware
image with base address $0800 (user code area above the bootloader).

Distribution:
```
game.nes       — Standard iNES/iNES 2.0 ROM file
game.hex       — PIC18 firmware in Intel HEX format
```

The emulator loads `game.hex` (matched by filename) into PIC program memory.
The Intel HEX format includes address records, so the emulator parses the
hex file and writes bytes to the correct PIC flash addresses (typically
$0800–$FFFF). If no `.hex` file is found, the emulator boots the PIC with
erased flash ($FF) and the NES code can upload firmware at runtime via
$5010–$5012.

This matches the real hardware workflow: MPLAB produces a `.hex` file, the
bootloader's uploader sends it over serial, and the PIC programs its own flash.

### 12.3 PIC Memory Map

| Address Range  | Content                          |
|----------------|----------------------------------|
| $0000–$07FF    | Bootloader (BOOT18) — read-only in emulation |
| $0800–$FFFF    | User firmware (code + constants) |
| $0000–$05FF    | Access bank RAM (variables, buffers) |
| $0600–$07FF    | Unused RAM                       |
| $F00–$FFF      | Access bank high (SFR mirror area) |

### 12.4 Interrupt Vectors

| PIC Address | Vector            | Used By                          |
|-------------|-------------------|----------------------------------|
| $0008       | High priority     | PSP (NES register access)        |
| $0018       | Low priority      | Timer0, Timer1, Timer3, UART     |

---

## 13. Mesen Implementation Guide

### 13.1 Mapper Registration

```
Mapper Name:    Squeedo
iNES Mapper:    TBD (request from NES community)
PRG-ROM Size:   512KB (SST39SF040)
CHR-RAM Size:   32KB (fixed)
SRAM Size:      32KB (4 × 8KB pages)
Mirroring:      4-screen (cartridge-provided)
IRQ:            Yes (via PIC coprocessor)
Audio:          Yes (4-channel wavetable via $4011 DMC)
```

### 13.2 Required Emulated Components

1. **PIC18F4620 CPU core** — instruction set, access bank, interrupt controller
2. **PIC peripherals** — PSP, Timer0, Timer1, Timer3, UART, ADCON (digital pin config)
3. **Wavetable synthesizer** — 4-channel, 24-bit phase accumulator, 11025 Hz sample rate
4. **NES mapper glue** — address decoding, bankswitching, IRQ to NES CPU
5. **Audio bridge** — synth output → PIC PortD → NES IRQ → $4011

### 13.3 PIC Core Requirements

| Feature | Priority | Notes |
|---------|----------|-------|
| 75 instructions (PIC18 ISA) | Critical | All standard PIC18 opcodes |
| Interrupt priority (high/low) | Critical | PSP is high, timers/UART are low |
| Parallel Slave Port | Critical | PortD data, PortE control |
| Timer0 (8/16-bit) | Critical | PPU scanline IRQ |
| Timer1 (16-bit) | Critical | CPU cycle IRQ |
| Timer3 (16-bit) | Critical | 11025 Hz sample clock |
| UART (TX/RX) | Medium | Serial passthrough, MIDI (future) |
| Table read (TBLRD) | Low | Used in startup only |
| EEPROM | Low | Not used in current firmware |
| Watchdog | Low | Disabled in config |

### 13.4 Approximate PIC Cycle Budget

At 40 MHz / 4 = 10 MIPS, the PIC executes ~907 instructions per NES scanline
(113.667 NES cycles × 558ns = 63.4µs, × 10 MIPS = 634 PIC cycles per scanline).

Per audio sample (11025 Hz = ~162 NES cycles = ~90.4 µs):
- 4 × resample_chan: ~20 cycles each = 80
- 4 × volmix_chan: ~15 cycles each = 60
- Mixdown + output: ~30 cycles
- ISR overhead: ~30 cycles
- **Total: ~200 PIC cycles per sample** (20 µs at 10 MIPS)

This leaves ample headroom — the PIC is idle ~78% of the time between samples.

---

## 14. File Format Recommendations

### 14.1 Distribution

A Squeedo game is distributed as two files:

```
game.nes       — Standard iNES/iNES 2.0 ROM file
game.hex       — PIC18 firmware in Intel HEX format (Microchip native)
```

The `.hex` file is the standard output of MPLAB/MPASM/XC8 toolchains. It contains
address records that tell the emulator exactly where to place each byte in PIC
program memory (typically $0800–$7FFF). This is the same file format used by the
real BOOT18 serial uploader on physical hardware.

### 14.2 Emulator Loading

1. The emulator looks for `game.hex` alongside the `.nes` file (matched by
   filename stem).
2. If found: parse Intel HEX records and load into PIC flash at the recorded
   addresses.
3. If not found: boot PIC with erased flash ($FF fill). The NES code can still
   upload firmware at runtime via $5010–$5012 if desired.

### 14.3 Why Not Embed in the NES ROM?

Embedding PIC firmware as a data block inside the NES ROM is possible — the NES
code could upload it to the PIC via $5010–$5012 at startup. However, this adds
complexity to every game's init code, inflates the NES ROM, and couples the two
firmware images. A companion `.hex` file keeps the PIC firmware independent and
matches the real hardware development workflow.

---

## 15. Known Firmware Limitations

The current PIC firmware (`tut.asm`) is a prototype with several incomplete
features:

1. **Synth not connected to mainloop** — the PCM flag is set by Timer3 but the
   mainloop never checks it (the `btfsc main_state,PCM` / `bra sound_synth`
   lines are commented out).
2. **Audio output not sent to NES** — `output_buffer` is computed but never
   placed on the bus for the NES to read.
3. **NES IRQ never asserted** — the `bcf PORTA,IRQ` lines in the timer ISRs
   are commented out with `;*`.
4. **Channel 3 init bug** — `chan1_rate_fine` is overwritten instead of
   `chan3_rate_fine` during debug initialization.
5. **All volumes initialized to zero** — channel 0 volume init is commented
   out; channels 1–3 are set to $00.
6. **Pulse/noise generators bypassed** — unconditional `goto` skips both.
7. **NES register reads incomplete** — `nes_read` returns immediately without
   placing data on the bus.

These are documented as known issues in the firmware, not mapper specification
bugs. The mapper spec describes the **intended hardware behavior**, not the
current firmware state.

---

*Document version: 2024-09-24*
*Based on: Squeedo Rev 1/Rev 2 schematic, PIC18F4620 firmware (tut.asm),
micromapper.txt register documentation, and Rev 1/Rev 2 BOMs.*
