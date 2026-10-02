using ModelContextProtocol.Server;
using System.Collections.Generic;
using System.ComponentModel;

namespace Mesen.Mcp
{
	[McpServerPromptType]
	public class MesenMcpPrompts
	{
		[McpServerPrompt, Description("Guide for finding and modifying text in a ROM using relative search and TBL tables.")]
		public static string FindGameText(
			[Description("The text you're looking for (e.g. a character name, menu item, or dialogue line)")] string knownText,
			[Description("The console type: NES, SNES, GB, GBA, PCE, SMS, WS")] string consoleType)
		{
			return $@"# ROM Text Finding Workflow

## Goal: Find ""{knownText}"" in a {consoleType} ROM

## Step 1: Try Relative Search First
Use `mesen_relative_search` with the text ""{knownText}"" (uppercase) on the PRG ROM memory type.
- For NES: memoryType = ""NesPrgRom""
- For SNES: memoryType = ""SnesPrgRom""
- For GB/GBC: memoryType = ""GbPrgRom""
- For GBA: memoryType = ""GbaPrgRom""

Relative search finds text even when the encoding is unknown, by matching the *differences* between consecutive bytes.

## Step 2: Verify Results
For each match, read 20-30 bytes around the address with `mesen_read_memory` to see if surrounding data also looks like text (similar byte ranges, recognizable patterns).

## Step 3: Build a TBL
From a confirmed match, you know the byte->character mapping. Create a TBL table:
- First byte value = first character in your search text
- Calculate the offset: if 'A' = $80, then 'B' = $81, 'C' = $82, etc.

Load the TBL with `mesen_load_tbl` and use `mesen_decode_text` to read text from the ROM.

## Step 4: Search with TBL
Once you have a TBL, use `mesen_search_text` to find other occurrences of text in the ROM.

## Tips
- Use search strings of 5+ characters to reduce false positives
- Try ALL UPPERCASE first, as many games only use uppercase
- If no results, the game may use DTE/MTE compression (one byte = multiple characters)
- Spaces and punctuation may break relative search — search for words individually
- Japanese games: try searching for katakana names (sequential encoding is common)";
		}

		[McpServerPrompt, Description("Guide for finding and modifying a game variable (lives, health, score, items, etc.) using memory search and breakpoints.")]
		public static string FindGameVariable(
			[Description("What variable to find (e.g. 'lives', 'health', 'score', 'coins')")] string variableName,
			[Description("The current known value of the variable in the game")] string currentValue)
		{
			return $@"# Finding Game Variable: {variableName}

## Goal: Find the memory address that stores the ""{variableName}"" (current value: {currentValue})

## Step 1: Initial Search
Use `mesen_search_memory` to search for the byte value {currentValue} in work RAM:
- NES: memoryType = ""NesWorkRam""
- SNES: memoryType = ""SnesWorkRam""
- GB: memoryType = ""GbWorkRam""
- GBA: memoryType = ""GbaIntWorkRam"" (try GbaExtWorkRam too)

Note: the value might be stored as BCD (Binary-Coded Decimal) — e.g., score of 1500 stored as $15 $00.

## Step 2: Narrow Down
You'll get many matches. To narrow down:
1. Change the value in-game (lose a life, spend coins, take damage)
2. Search again for the new value
3. The correct address will be in both result sets

## Step 3: Verify with Write
Use `mesen_write_memory` to write a test value to candidate addresses. If the game display updates, you found it.

## Step 4: Set a Write Breakpoint
Use `mesen_breakpoint` with action=""set"" and type=""Write"" on the confirmed address. Resume the game and trigger the change.
The breakpoint will stop execution at the exact instruction that modifies the variable.

## Step 5: Analyze the Code
Use `mesen_disassemble` at the breakpoint PC to see the routine. Use `mesen_get_state` with component=""cpu"" to see register values.

## Step 6: Patch or Freeze
- **Freeze**: Use `mesen_freeze_address` to prevent the game from changing the value (infinite lives!)
- **Patch**: Use `mesen_write_memory` to NOP out the decrement instruction (e.g., replace `DEC` with `NOP`)
- **Save**: Use `mesen_save_modified_rom` to save the patched ROM or create an IPS patch";
		}

		[McpServerPrompt, Description("Guide for analyzing a game routine using the debugger: step through code, set breakpoints, trace execution.")]
		public static string AnalyzeRoutine(
			[Description("The address to analyze (hex, e.g. '$C000' or '0x8000')")] string address,
			[Description("What the routine does or what you want to understand about it")] string purpose)
		{
			return $@"# Analyzing Routine at {address}

## Goal: Understand what the code at {address} does ({purpose})

## Step 1: Initialize & Pause
Ensure the debugger is initialized (`mesen_init_debugger`). Pause the emulator (`mesen_playback` with action=""pause"").

## Step 2: Disassemble
Use `mesen_disassemble` at address {address} with ~50 lines to see the code flow.

## Step 3: Set Execution Breakpoint
Use `mesen_breakpoint` with action=""set"" at {address} with type=""Execute"" to catch when this code runs.
Resume emulation and wait for the breakpoint to hit.

## Step 4: Step Through
When stopped at the breakpoint:
- `mesen_step` (Step) — execute one instruction
- `mesen_step` (StepOver) — execute one instruction, skip subroutine calls
- `mesen_step` (StepOut) — execute until the current subroutine returns
After each step, check `mesen_get_state` with component=""cpu"" for register values.

## Step 5: Trace Execution
For a broader view, use `mesen_set_trace_options` to enable tracing, resume execution briefly, then `mesen_get_execution_trace` to see what was executed.

## Step 6: Get Context
- `mesen_get_callstack` — see how this routine was called
- `mesen_evaluate_expression` — evaluate expressions like ""[$4016]"" or ""A + X""
- `mesen_memory_access_counts` with action=""get"" — see which addresses are read/written most

## Step 7: Label and Document
Use `mesen_label` with action=""set"" to annotate addresses you've identified:
- Label the routine entry point
- Label important variables and I/O addresses

## Tips
- Look for JSR/JSL (subroutine calls) and JMP (jumps) to understand control flow
- LDA/STA patterns reveal data access: LDA loads data, STA stores it
- CMP/BEQ/BNE are comparisons and branches — key for understanding game logic
- Common patterns: loop = LDX #count / DEX / BNE loop; table lookup = LDA table,X";
		}

		[McpServerPrompt, Description("Guide for analyzing the VBlank/NMI routine of a game — the main frame update routine.")]
		public static string AnalyzeVBlank(
			[Description("Console type: NES, SNES, GB, GBA")] string consoleType)
		{
			string vectorInfo = consoleType.ToUpperInvariant() switch {
				"NES" => "NMI vector at $FFFA-$FFFB (read 2 bytes little-endian from NesPrgRom at the end of the ROM)",
				"SNES" => "NMI vector at $00:FFEA-$00:FFEB in the vector table",
				"GB" or "GBC" => "VBlank handler at $0040",
				"GBA" => "VBlank interrupt handler in the IRQ table at 0x03007FFC",
				_ => "Check the system's interrupt vector table"
			};

			return $@"# Analyzing VBlank/NMI Routine ({consoleType})

## What is VBlank?
The VBlank (Vertical Blank) / NMI routine is the most important routine in retro games. It runs once per frame (~60 times/second) during the vertical blanking period — the only safe time to update VRAM, sprites, and scroll registers.

## Finding the VBlank Handler
{vectorInfo}

Use `mesen_read_memory` to read the vector address, then `mesen_disassemble` at that address.

## Alternative: Use Breakpoint
Set a breakpoint with `mesen_breakpoint` (action=""set"") at the NMI vector address with type=""Execute"". The breakpoint will hit every frame.

## What to Look For in VBlank
1. **OAM DMA** — Sprite data transfer (NES: write to $4014, SNES: write to $2100-series)
2. **PPU register writes** — Scroll position, control registers
3. **VRAM updates** — Tile data, nametable updates
4. **Sound engine call** — Usually called from VBlank
5. **Input reading** — Controller polling ($4016/$4017 on NES)
6. **Frame counter increment** — Often a variable incremented each VBlank

## Tracing VBlank
Enable trace logging with `mesen_set_trace_options`, run for a few frames, then examine the trace with `mesen_get_execution_trace` to see the full VBlank execution flow.";
		}
	}
}
