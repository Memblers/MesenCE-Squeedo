using Mesen.Debugger;
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
	public class DisassemblyTools
	{
		[McpServerTool(Name = "mesen_disassemble", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("Disassemble code at a given address. Returns disassembly lines with address, bytecode, and mnemonic.")]
		public static string Disassemble(
			[Description("Start address (decimal or 0x/$ hex)")] string address,
			[Description("Number of lines to disassemble (max 500)")] int lineCount,
			[Description("CPU type: Nes, Snes, Gameboy, Gba, Pce, Sms, Ws, Spc, Sa1, Gsu, Cx4")] string cpuType,
			[Description("Optional file path to save disassembly text")] string? outputFile = null)
		{
			McpToolHelper.EnsureDebuggerReady();
			uint addr = McpToolHelper.ParseAddress(address);
			CpuType cpu = McpToolHelper.ParseCpuType(cpuType);

			lineCount = Math.Min(lineCount, 500);

			int rowIndex = DebugApi.GetDisassemblyRowAddress(cpu, addr, 0);
			if(rowIndex < 0) {
				throw new McpException("Cannot disassemble at address $" + addr.ToString("X4"));
			}

			CodeLineData[] lines = DebugApi.GetDisassemblyOutput(cpu, (uint)rowIndex, (uint)lineCount);

			StringBuilder sb = new();
			foreach(CodeLineData line in lines) {
				if(line.Address >= 0 && !string.IsNullOrWhiteSpace(line.Text)) {
					sb.Append('$').Append(line.Address.ToString("X4")).Append(": ").Append(line.Text);
					if(!string.IsNullOrEmpty(line.Comment)) {
						sb.Append("  ; ").Append(line.Comment);
					}
					sb.AppendLine();
				}
			}

			string text = sb.ToString();
			if(!string.IsNullOrEmpty(outputFile)) {
				File.WriteAllText(outputFile, text);
			}
			return text;
		}

		[McpServerTool(Name = "mesen_find_occurrences", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("Search disassembly for a text pattern. Returns all matching lines (max 500).")]
		public static string FindOccurrences(
			[Description("Text to search for in disassembly (e.g. 'NOP', 'JSR', 'LDA #$')")] string searchString,
			[Description("CPU type: Nes, Snes, Gameboy, Gba, Pce, Sms, Ws, Spc, Sa1, Gsu, Cx4")] string cpuType,
			[Description("Case sensitive search (default false)")] bool matchCase = false,
			[Description("Match whole word only (default false)")] bool matchWholeWord = false)
		{
			McpToolHelper.EnsureDebuggerReady();
			CpuType cpu = McpToolHelper.ParseCpuType(cpuType);

			DisassemblySearchOptions options = new() {
				MatchCase = matchCase,
				MatchWholeWord = matchWholeWord,
				SearchBackwards = false,
				SkipFirstLine = false
			};

			CodeLineData[] results = DebugApi.FindOccurrences(cpu, searchString, options);

			StringBuilder sb = new();
			foreach(CodeLineData line in results) {
				if(line.Address >= 0) {
					sb.Append('$').Append(line.Address.ToString("X4")).Append(": ").AppendLine(line.Text);
				}
			}
			return sb.Length > 0 ? sb.ToString() : "No matches.";
		}

		[McpServerTool(Name = "mesen_assemble", ReadOnly = false, Destructive = false, OpenWorld = false),
		 Description("Assemble code at a given address. Returns assembled bytes or errors.")]
		public static string Assemble(
			[Description("Assembly code (one or more lines, newline separated)")] string code,
			[Description("Start address (decimal or 0x/$ hex)")] string startAddress,
			[Description("CPU type: Nes, Snes, Gameboy, Gba, Pce, Sms, Ws, Spc, Sa1, Gsu, Cx4")] string cpuType)
		{
			McpToolHelper.EnsureDebuggerReady();
			uint addr = McpToolHelper.ParseAddress(startAddress);
			CpuType cpu = McpToolHelper.ParseCpuType(cpuType);

			Int16[] assembled = DebugApi.AssembleCode(cpu, code, addr);

			List<byte> bytes = new();
			List<string> errors = new();

			for(int i = 0; i < assembled.Length; i++) {
				if(assembled[i] >= 0) {
					bytes.Add((byte)assembled[i]);
				} else if(assembled[i] == -1 && i + 1 < assembled.Length) {
					// Error marker: next value indicates the error
					StringBuilder errMsg = new();
					i++;
					while(i < assembled.Length && assembled[i] >= 0) {
						errMsg.Append((char)assembled[i]);
						i++;
					}
					errors.Add(errMsg.ToString());
					i--; // Back up since the for loop will increment
				}
			}

			if(errors.Count > 0) {
				StringBuilder errSb = new("Assembly errors:");
				foreach(string err in errors) {
					errSb.Append('\n').Append(err);
				}
				return errSb.ToString();
			}

			StringBuilder hexStr = new();
			foreach(byte b in bytes) {
				hexStr.Append(b.ToString("X2"));
			}

			return "Assembled " + bytes.Count + " bytes at $" + addr.ToString("X4") + ": " + hexStr;
		}

		[McpServerTool(Name = "mesen_label", ReadOnly = false, Destructive = false, OpenWorld = false),
		 Description("Manage labels. Use action 'set' to set a label on an address, or 'clear_all' to remove all labels.")]
		public static string Label(
			[Description("Action: 'set' or 'clear_all'")] string action,
			[Description("Address (decimal or 0x/$ hex) - required for 'set'")] string? address = null,
			[Description("Memory type (call mesen_list_memory_types first) - required for 'set'")] string? memoryType = null,
			[Description("Label text - required for 'set'")] string? label = null,
			[Description("Optional comment - used with 'set'")] string? comment = null)
		{
			McpToolHelper.EnsureDebuggerReady();

			switch(action.ToLowerInvariant()) {
				case "set":
					if(string.IsNullOrEmpty(address)) {
						throw new McpException("'address' is required for action 'set'.");
					}
					if(string.IsNullOrEmpty(memoryType)) {
						throw new McpException("'memoryType' is required for action 'set'.");
					}
					if(string.IsNullOrEmpty(label)) {
						throw new McpException("'label' is required for action 'set'.");
					}

					uint addr = McpToolHelper.ParseAddress(address);
					MemoryType memType = McpToolHelper.ParseMemoryType(memoryType);

					DebugApi.SetLabel(addr, memType, label, comment ?? "");
					return "Label '" + label + "' set at $" + addr.ToString("X4");

				case "clear_all":
					DebugApi.ClearLabels();
					return "Labels cleared.";

				default:
					throw new McpException("Invalid action: " + action + ". Use 'set' or 'clear_all'.");
			}
		}

		[McpServerTool(Name = "mesen_get_cdl_statistics", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("Get Code Data Logger statistics for a memory region. Shows percentage of code, data, and unknown bytes.")]
		public static string GetCdlStatistics(
			[Description("Memory type (call mesen_list_memory_types first to get valid values for the current ROM)")] string memoryType)
		{
			McpToolHelper.EnsureDebuggerReady();
			MemoryType memType = McpToolHelper.ParseMemoryType(memoryType);

			CdlStatistics stats = DebugApi.GetCdlStatistics(memType);
			uint unknownBytes = stats.TotalBytes - stats.CodeBytes - stats.DataBytes;

			return "Total=" + stats.TotalBytes + " Code=" + stats.CodeBytes + " Data=" + stats.DataBytes +
				" Unknown=" + unknownBytes + " JumpTargets=" + stats.JumpTargetCount + " Functions=" + stats.FunctionCount +
				" CHR=" + stats.DrawnChrBytes + "/" + stats.TotalChrBytes;
		}

		[McpServerTool(Name = "mesen_get_cdl_functions", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("Get list of function entry points detected by the Code Data Logger.")]
		public static string GetCdlFunctions(
			[Description("Memory type (call mesen_list_memory_types first to get valid values for the current ROM)")] string memoryType)
		{
			McpToolHelper.EnsureDebuggerReady();
			MemoryType memType = McpToolHelper.ParseMemoryType(memoryType);

			UInt32[] functions = DebugApi.GetCdlFunctions(memType);
			StringBuilder sb = new();
			foreach(UInt32 funcAddr in functions) {
				if(sb.Length > 0) sb.Append(' ');
				sb.Append('$').Append(funcAddr.ToString("X4"));
			}
			return sb.ToString();
		}

		[McpServerTool(Name = "mesen_mark_bytes_as", ReadOnly = false, Destructive = false, OpenWorld = false),
		 Description("Mark a range of bytes as Code, Data, or unidentified in the Code Data Logger.")]
		public static string MarkBytesAs(
			[Description("Start address (decimal or 0x/$ hex)")] string startAddress,
			[Description("End address (decimal or 0x/$ hex)")] string endAddress,
			[Description("Memory type (call mesen_list_memory_types first to get valid values for the current ROM)")] string memoryType,
			[Description("CDL flags (case-insensitive): None, Code, Data, JumpTarget, SubEntryPoint")] string flags)
		{
			McpToolHelper.EnsureDebuggerReady();
			uint start = McpToolHelper.ParseAddress(startAddress);
			uint end = McpToolHelper.ParseAddress(endAddress);
			MemoryType memType = McpToolHelper.ParseMemoryType(memoryType);

			if(!Enum.TryParse<CdlFlags>(flags, true, out CdlFlags cdlFlags)) {
				throw new McpException("Invalid CDL flags: " + flags + ". Valid values: None, Code, Data, JumpTarget, SubEntryPoint.");
			}

			DebugApi.MarkBytesAs(memType, start, end, cdlFlags);
			return "Marked $" + start.ToString("X4") + "-$" + end.ToString("X4") + " as " + flags;
		}

		[McpServerTool(Name = "mesen_cdl_file", ReadOnly = false, Destructive = false, OpenWorld = false),
		 Description("Save or load Code Data Logger data to/from a file.")]
		public static string CdlFile(
			[Description("Action: 'save' or 'load'")] string action,
			[Description("Memory type (call mesen_list_memory_types first to get valid values for the current ROM)")] string memoryType,
			[Description("Absolute file path for CDL data")] string filepath)
		{
			McpToolHelper.EnsureDebuggerReady();
			MemoryType memType = McpToolHelper.ParseMemoryType(memoryType);

			switch(action.ToLowerInvariant()) {
				case "save":
					DebugApi.SaveCdlFile(memType, filepath);
					return "CDL saved to " + filepath;

				case "load":
					if(!File.Exists(filepath)) {
						throw new McpException("File not found: " + filepath);
					}
					DebugApi.LoadCdlFile(memType, filepath);
					return "CDL loaded from " + filepath;

				default:
					throw new McpException("Invalid action: " + action + ". Use 'save' or 'load'.");
			}
		}
	}
}
