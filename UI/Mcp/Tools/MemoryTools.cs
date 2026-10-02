using Mesen.Interop;
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
	public class MemoryTools
	{
		[McpServerTool(Name = "mesen_read_memory", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("Read memory from the emulator. Returns a compact hex string. Use mesen_list_memory_types to see valid memory types for the current ROM.")]
		public static string ReadMemory(
			[Description("Start address (decimal or 0x/$ hex)")] string address,
			[Description("Number of bytes to read (max 4096)")] int length,
			[Description("Memory type (call mesen_list_memory_types first to get valid values for the current ROM)")] string memoryType,
			[Description("Optional file path to save the raw binary dump")] string? outputFile = null)
		{
			McpToolHelper.EnsureDebuggerReady();
			uint addr = McpToolHelper.ParseAddress(address);
			MemoryType memType = McpToolHelper.ParseMemoryType(memoryType);

			length = Math.Min(length, 4096);
			Int32 memSize = DebugApi.GetMemorySize(memType);
			if(addr >= memSize) {
				throw new McpException($"Address ${addr:X4} out of range. Memory size: ${memSize:X4}");
			}
			length = (int)Math.Min(length, memSize - addr);

			byte[] data = DebugApi.GetMemoryValues(memType, addr, (uint)(addr + length - 1));

			if(!string.IsNullOrEmpty(outputFile)) {
				File.WriteAllBytes(outputFile, data);
				return "Dumped " + data.Length + " bytes from $" + addr.ToString("X4") + " to " + outputFile;
			}

			StringBuilder hex = new(data.Length * 3);
			for(int i = 0; i < data.Length; i++) {
				if(i > 0) hex.Append(' ');
				hex.Append(data[i].ToString("X2"));
			}
			return hex.ToString();
		}

		[McpServerTool(Name = "mesen_write_memory", ReadOnly = false, Destructive = false, OpenWorld = false),
		 Description("Write data to emulator memory.")]
		public static string WriteMemory(
			[Description("Start address (decimal or 0x/$ hex)")] string address,
			[Description("Hex string of bytes to write (e.g. 'EAEA' for two NOPs)")] string hexData,
			[Description("Memory type (call mesen_list_memory_types first to get valid values for the current ROM)")] string memoryType)
		{
			McpToolHelper.EnsureDebuggerReady();
			uint addr = McpToolHelper.ParseAddress(address);
			MemoryType memType = McpToolHelper.ParseMemoryType(memoryType);

			// Parse hex string to bytes
			hexData = hexData.Replace(" ", "").Replace("-", "");
			if(hexData.Length % 2 != 0) {
				throw new McpException("Hex data must have an even number of characters");
			}

			byte[] data = new byte[hexData.Length / 2];
			for(int i = 0; i < data.Length; i++) {
				if(!byte.TryParse(hexData.Substring(i * 2, 2), System.Globalization.NumberStyles.HexNumber, null, out data[i])) {
					throw new McpException("Invalid hex data at position " + (i * 2));
				}
			}

			DebugApi.SetMemoryValues(memType, addr, data, data.Length);
			return "Wrote " + data.Length + " bytes at $" + addr.ToString("X4");
		}

		[McpServerTool(Name = "mesen_search_memory", ReadOnly = true, Destructive = false, OpenWorld = false),
		 Description("Search memory for a hex pattern. Returns matching addresses. Use mesen_list_memory_types to see valid memory types.")]
		public static string SearchMemory(
			[Description("Hex pattern to search for (e.g. 'AD0020' or '03')")] string patternHex,
			[Description("Memory type to search in (call mesen_list_memory_types first to get valid values for the current ROM)")] string memoryType,
			[Description("Start address (default 0)")] string? startAddress = null,
			[Description("End address (default: end of memory, capped at 1MB range)")] string? endAddress = null,
			[Description("Maximum results to return (default 50)")] int maxResults = 50)
		{
			McpToolHelper.EnsureDebuggerReady();
			MemoryType memType = McpToolHelper.ParseMemoryType(memoryType);

			// Parse pattern
			patternHex = patternHex.Replace(" ", "").Replace("-", "");
			if(patternHex.Length % 2 != 0) {
				throw new McpException("Pattern must have an even number of hex characters");
			}

			byte[] pattern = new byte[patternHex.Length / 2];
			for(int i = 0; i < pattern.Length; i++) {
				if(!byte.TryParse(patternHex.Substring(i * 2, 2), System.Globalization.NumberStyles.HexNumber, null, out pattern[i])) {
					throw new McpException("Invalid hex at position " + (i * 2));
				}
			}

			Int32 memSize = DebugApi.GetMemorySize(memType);
			uint start = 0;
			uint end = (uint)(memSize - 1);

			if(startAddress != null) start = McpToolHelper.ParseAddress(startAddress);
			if(endAddress != null) end = McpToolHelper.ParseAddress(endAddress);

			end = Math.Min(end, (uint)(memSize - 1));

			// Read memory range and search
			byte[] memory = DebugApi.GetMemoryValues(memType, start, end);
			List<string> matches = new();

			for(int i = 0; i <= memory.Length - pattern.Length && matches.Count < maxResults; i++) {
				bool found = true;
				for(int j = 0; j < pattern.Length; j++) {
					if(memory[i + j] != pattern[j]) {
						found = false;
						break;
					}
				}
				if(found) {
					matches.Add("$" + (start + i).ToString("X4"));
				}
			}

			if(matches.Count == 0) {
				return "Pattern: " + patternHex + "  No matches.";
			}
			return "Pattern: " + patternHex + "  Matches: " + matches.Count + "\n" + string.Join(" ", matches);
		}

		[McpServerTool(Name = "mesen_freeze_address", ReadOnly = false, Destructive = false, OpenWorld = false),
		 Description("Freeze or unfreeze a memory address range (prevent/allow game writes).")]
		public static string FreezeAddress(
			[Description("Start address (decimal or 0x/$ hex)")] string startAddress,
			[Description("End address (same as start for single byte)")] string endAddress,
			[Description("CPU type: Nes, Snes, Gameboy, Gba, Pce, Sms, Ws")] string cpuType,
			[Description("True to freeze (prevent writes), false to unfreeze")] bool freeze)
		{
			McpToolHelper.EnsureDebuggerReady();
			uint start = McpToolHelper.ParseAddress(startAddress);
			uint end = McpToolHelper.ParseAddress(endAddress);
			CpuType cpu = McpToolHelper.ParseCpuType(cpuType);

			DebugApi.UpdateFrozenAddresses(cpu, start, end, freeze);
			return (freeze ? "Frozen" : "Unfrozen") + " $" + start.ToString("X4") + "-$" + end.ToString("X4");
		}

		[McpServerTool(Name = "mesen_get_address_info", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("Convert between CPU (relative) and absolute (physical) addresses.")]
		public static string GetAddressInfo(
			[Description("Address to convert (decimal or 0x/$ hex)")] string address,
			[Description("CPU type: Nes, Snes, Gameboy, Gba, Pce, Sms, Ws")] string cpuType)
		{
			McpToolHelper.EnsureDebuggerReady();
			uint addr = McpToolHelper.ParseAddress(address);
			CpuType cpu = McpToolHelper.ParseCpuType(cpuType);

			AddressInfo relAddr = new AddressInfo() {
				Address = (int)addr,
				Type = cpu.ToMemoryType()
			};

			AddressInfo absAddr = DebugApi.GetAbsoluteAddress(relAddr);

			if(absAddr.Address >= 0) {
				return "$" + addr.ToString("X4") + " -> $" + absAddr.Address.ToString("X4") + " (" + absAddr.Type + ")";
			}
			return "$" + addr.ToString("X4") + " -> unmapped";
		}

		[McpServerTool(Name = "mesen_memory_access_counts", ReadOnly = false, Destructive = false, OpenWorld = false),
		 Description("Get or reset memory access counters. Action: 'get' (returns read/write/exec counts) or 'reset' (clears all counters).")]
		public static string MemoryAccessCounts(
			[Description("Action: 'get' or 'reset'")] string action,
			[Description("Start address for get (decimal or 0x/$ hex)")] string? address = null,
			[Description("Number of bytes for get (max 256)")] int length = 256,
			[Description("Memory type for get")] string? memoryType = null)
		{
			McpToolHelper.EnsureDebuggerReady();

			switch(action.ToLowerInvariant()) {
				case "reset":
					DebugApi.ResetMemoryAccessCounts();
					return "Access counters reset.";

				case "get":
					if(address == null) {
						throw new McpException("Address is required for 'get' action.");
					}
					if(memoryType == null) {
						throw new McpException("Memory type is required for 'get' action.");
					}

					uint addr = McpToolHelper.ParseAddress(address);
					MemoryType memType = McpToolHelper.ParseMemoryType(memoryType);

					length = Math.Min(length, 256);
					Int32 memSize = DebugApi.GetMemorySize(memType);
					if(addr >= memSize) {
						throw new McpException($"Address ${addr:X4} out of range. Memory size: ${memSize:X4}");
					}
					length = (int)Math.Min(length, memSize - addr);

					AddressCounters[] counts = DebugApi.GetMemoryAccessCounts(addr, (uint)length, memType);

					StringBuilder sb = new();
					for(int i = 0; i < counts.Length; i++) {
						AddressCounters c = counts[i];
						if(c.ReadCounter > 0 || c.WriteCounter > 0 || c.ExecCounter > 0) {
							sb.Append('$').Append((addr + i).ToString("X4"));
							if(c.ReadCounter > 0) sb.Append(" R=").Append(c.ReadCounter);
							if(c.WriteCounter > 0) sb.Append(" W=").Append(c.WriteCounter);
							if(c.ExecCounter > 0) sb.Append(" X=").Append(c.ExecCounter);
							sb.AppendLine();
						}
					}
					return sb.Length > 0 ? sb.ToString() : "(no accesses)";

				default:
					throw new McpException("Invalid action: " + action + ". Use 'get' or 'reset'.");
			}
		}
	}
}
