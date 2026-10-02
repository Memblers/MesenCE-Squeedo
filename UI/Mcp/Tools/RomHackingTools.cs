using Mesen.Interop;
using Mesen.Mcp.Consoles;
using ModelContextProtocol;
using ModelContextProtocol.Server;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Text;

namespace Mesen.Mcp.Tools
{
	[McpServerToolType]
	public class RomHackingTools
	{
		[McpServerTool(Name = "mesen_save_modified_rom", ReadOnly = false, Destructive = false, OpenWorld = false), Description("Save the currently loaded (and possibly modified) ROM to a file. Can create an IPS patch instead.")]
		public static string SaveModifiedRom(
			[Description("Absolute file path to save the ROM or IPS patch")] string filepath,
			[Description("If true, saves an IPS patch file instead of the full ROM (default false)")] bool saveAsIps = false,
			[Description("CDL strip option: StripNone (default), StripUnused, StripUsed")] string stripOption = "StripNone")
		{
			McpToolHelper.EnsureDebuggerReady();

			if(!Enum.TryParse<CdlStripOption>(stripOption, true, out CdlStripOption strip)) {
				throw new McpException("Invalid strip option: " + stripOption);
			}

			bool success = DebugApi.SaveRomToDisk(filepath, saveAsIps, strip);
			if(!success) throw new McpException("Failed to save ROM.");
			return (saveAsIps ? "IPS patch" : "ROM") + " saved to " + filepath;
		}

		[McpServerTool(Name = "mesen_get_rom_header", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("Get parsed ROM header information. Supported for NES (iNes/NES 2.0) and SNES ROMs. Returns structured header fields. For unsupported consoles, use mesen_read_memory to read the internal header from PRG ROM directly.")]
		public static string GetRomHeader()
		{
			McpToolHelper.EnsureDebuggerReady();

			RomInfo romInfo = EmuApi.GetRomInfo();
			IConsoleHandler handler = ConsoleHandlerFactory.GetHandler(romInfo.ConsoleType);

			string? result = handler.GetRomHeader();
			if(result == null) {
				throw new McpException("ROM header parsing is not supported for " + romInfo.ConsoleType + ". Use mesen_read_memory to read the internal header from PRG ROM.");
			}

			return result;
		}

		[McpServerTool(Name = "mesen_get_palette", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false), Description("Get the palette colors for the current system.")]
		public static string GetPalette(
			[Description("CPU type: Nes, Snes, Gameboy, Gba, Pce, Sms, Ws")] string cpuType)
		{
			McpToolHelper.EnsureDebuggerReady();

			CpuType cpu = McpToolHelper.ParseCpuType(cpuType);

			DebugPaletteInfo info = DebugApi.GetPaletteInfo(cpu);
			UInt32[] rgbPalette = info.GetRgbPalette();

			StringBuilder sb = new();
			sb.Append("Colors=").Append(info.ColorCount)
				.Append(" BG=").Append(info.BgColorCount)
				.Append(" Sprite=").Append(info.SpriteColorCount)
				.Append(" PerPalette=").Append(info.ColorsPerPalette)
				.Append(" Format=").AppendLine(info.RawFormat.ToString());

			for(int i = 0; i < rgbPalette.Length; i++) {
				if(i > 0) sb.Append(' ');
				sb.Append((rgbPalette[i] & 0xFFFFFF).ToString("X6"));
			}

			return sb.ToString();
		}

		[McpServerTool(Name = "mesen_set_palette_color", ReadOnly = false, Destructive = false, OpenWorld = false), Description("Set a palette color at runtime.")]
		public static string SetPaletteColor(
			[Description("CPU type: Nes, Snes, Gameboy, Gba, Pce, Sms, Ws")] string cpuType,
			[Description("Color index in the palette (0-based)")] int colorIndex,
			[Description("RGB color value as 6-digit hex (e.g. 'FF0000' for red). Optionally prefixed with '#' or '$'.")] string colorHex)
		{
			McpToolHelper.EnsureDebuggerReady();

			CpuType cpu = McpToolHelper.ParseCpuType(cpuType);

			colorHex = colorHex.TrimStart('#').TrimStart('$');
			if(!UInt32.TryParse(colorHex, System.Globalization.NumberStyles.HexNumber, null, out UInt32 color)) {
				throw new McpException("Invalid color hex: " + colorHex);
			}

			// Ensure alpha is set
			color |= 0xFF000000;

			DebugApi.SetPaletteColor(cpu, colorIndex, color);
			return "Color " + colorIndex + " set to #" + (color & 0xFFFFFF).ToString("X6");
		}

		[McpServerTool(Name = "mesen_get_tile_pixel", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false), Description("Read a single pixel from a tile in memory.")]
		public static string GetTilePixel(
			[Description("Tile address in memory (decimal or 0x/$ hex)")] string tileAddress,
			[Description("Tile pixel format (case-insensitive): Bpp2, Bpp4, Bpp8, NesBpp2, SmsBpp4, GbaBpp4, GbaBpp8, PceBpp4, WsBpp2, DirectColor")] string format,
			[Description("X coordinate within the tile (0-7 or 0-15)")] int x,
			[Description("Y coordinate within the tile (0-7 or 0-15)")] int y,
			[Description("Memory type for the tile data (call mesen_list_memory_types first to get valid values for the current ROM)")] string memoryType)
		{
			McpToolHelper.EnsureDebuggerReady();

			uint addr = McpToolHelper.ParseAddress(tileAddress);

			if(!Enum.TryParse<TileFormat>(format, true, out TileFormat tileFormat)) {
				throw new McpException("Invalid tile format: " + format);
			}

			MemoryType memType = McpToolHelper.ParseMemoryType(memoryType);

			AddressInfo addressInfo = new() { Address = (int)addr, Type = memType };
			int pixel = DebugApi.GetTilePixel(addressInfo, tileFormat, x, y);

			return "Color index: " + pixel;
		}

		[McpServerTool(Name = "mesen_set_tile_pixel", ReadOnly = false, Destructive = false, OpenWorld = false), Description("Set a single pixel in a tile in memory.")]
		public static string SetTilePixel(
			[Description("Tile address in memory (decimal or 0x/$ hex)")] string tileAddress,
			[Description("Tile pixel format (case-insensitive): Bpp2, Bpp4, Bpp8, NesBpp2, SmsBpp4, GbaBpp4, GbaBpp8, PceBpp4, WsBpp2, DirectColor")] string format,
			[Description("X coordinate within the tile")] int x,
			[Description("Y coordinate within the tile")] int y,
			[Description("Color index to set")] int color,
			[Description("Memory type for the tile data (call mesen_list_memory_types first to get valid values for the current ROM)")] string memoryType)
		{
			McpToolHelper.EnsureDebuggerReady();

			uint addr = McpToolHelper.ParseAddress(tileAddress);

			if(!Enum.TryParse<TileFormat>(format, true, out TileFormat tileFormat)) {
				throw new McpException("Invalid tile format: " + format);
			}

			MemoryType memType = McpToolHelper.ParseMemoryType(memoryType);

			AddressInfo addressInfo = new() { Address = (int)addr, Type = memType };
			DebugApi.SetTilePixel(addressInfo, tileFormat, x, y, color);

			return "Pixel set to color " + color;
		}

		[McpServerTool(Name = "mesen_run_lua_script", ReadOnly = false, Destructive = false, OpenWorld = false), Description("Run a Lua script in the emulator and return its log output. Set persistent=true to keep the script running (e.g. for callback-based scripts); use mesen_remove_lua_script to stop it later.")]
		public static string RunLuaScript(
			[Description("Lua script code to execute")] string code,
			[Description("Time to wait for script output in milliseconds (default 200, max 5000)")] int waitMs = 200,
			[Description("If true, keep the script running after returning (default false). Returns scriptId for later removal.")] bool persistent = false)
		{
			McpToolHelper.EnsureDebuggerReady();

			Int32 scriptId = DebugApi.LoadScript("mcp_script", "", code);
			if(scriptId < 0) {
				throw new McpException("Failed to load script");
			}

			// Wait for script to produce output
			System.Threading.Thread.Sleep(Math.Min(waitMs, 5000));

			string log = DebugApi.GetScriptLog(scriptId);

			if(!persistent) {
				DebugApi.RemoveScript(scriptId);
				return string.IsNullOrEmpty(log) ? "(no output)" : log;
			}

			return "Script #" + scriptId + " running (persistent)." + (string.IsNullOrEmpty(log) ? "" : "\n" + log);
		}

		[McpServerTool(Name = "mesen_remove_lua_script", ReadOnly = false, Destructive = false, OpenWorld = false), Description("Remove a running Lua script by its script ID (returned by mesen_run_lua_script with persistent=true).")]
		public static string RemoveLuaScript(
			[Description("Script ID returned by mesen_run_lua_script")] int scriptId)
		{
			McpToolHelper.EnsureDebuggerReady();

			string log = DebugApi.GetScriptLog(scriptId);
			DebugApi.RemoveScript(scriptId);

			return string.IsNullOrEmpty(log) ? "Script #" + scriptId + " removed." : log;
		}

		[McpServerTool(Name = "mesen_set_cheats", ReadOnly = false, Destructive = false, OpenWorld = false), Description("Set cheat codes. Each code is in the format 'Type:Code' (e.g. 'NesGameGenie:SXIOPO', 'SnesProActionReplay:7E0DBF01').")]
		public static string SetCheats(
			[Description("Array of cheat code strings in 'Type:Code' format. Valid types: NesGameGenie, NesCustom, SnesGameGenie, SnesProActionReplay, GbGameGenie, GbGameShark. Example: 'SnesProActionReplay:7E0DBF01'")] string[] codes)
		{
			McpToolHelper.EnsureRunning();

			List<InteropCheatCode> cheats = new();
			List<string> errors = new();

			foreach(string codeStr in codes) {
				int colonIdx = codeStr.IndexOf(':');
				if(colonIdx < 0) {
					errors.Add("Invalid format (expected 'Type:Code'): " + codeStr);
					continue;
				}

				string typePart = codeStr.Substring(0, colonIdx).Trim();
				string codePart = codeStr.Substring(colonIdx + 1).Trim();

				if(!Enum.TryParse<CheatType>(typePart, true, out CheatType cheatType)) {
					errors.Add("Invalid cheat type: " + typePart);
					continue;
				}

				cheats.Add(new InteropCheatCode(cheatType, codePart));
			}

			if(cheats.Count > 0) {
				EmuApi.SetCheats(cheats.ToArray(), (UInt32)cheats.Count);
			}

			string result = cheats.Count + " cheats applied.";
			if(errors.Count > 0) {
				result += "\nErrors:\n" + string.Join("\n", errors);
			}
			return result;
		}

		[McpServerTool(Name = "mesen_clear_cheats", ReadOnly = false, Destructive = true, OpenWorld = false), Description("Clear all active cheat codes.")]
		public static string ClearCheats()
		{
			McpToolHelper.EnsureRunning();

			EmuApi.ClearCheats();
			return "Cheats cleared.";
		}

	}
}
