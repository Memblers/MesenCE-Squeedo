using Mesen.Interop;
using Mesen.Mcp.Models;
using ModelContextProtocol;
using ModelContextProtocol.Server;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Text;

namespace Mesen.Mcp.Tools
{
	[McpServerToolType]
	public class DebugEventTools
	{
		[McpServerTool(Name = "mesen_get_events", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("Get the aggregated debug event log: crashes, break-on events (BRK, unofficial opcodes, ...), Lua errors, OSD messages. Events are aggregated by (kind, source, PC) with counts, frames and rates. Returns JSON with state (running/paused/hung/crashed), crash info and alerts.")]
		public static string GetEvents(
			[Description("Include aggregated OSD message events (default false)")] bool includeMessages = false)
		{
			return DebugEventAggregator.GetEventsReport(includeMessages);
		}

		[McpServerTool(Name = "mesen_set_event_policy", ReadOnly = false, Destructive = false, OpenWorld = false),
		 Description("Set what happens when a debug event fires: mode 'ignore' (silent), 'report' (record + surface in mesen_get_events, do NOT stop emulation), or 'break' (pause emulation and report). Use source='BreakOnBrk' etc. for one event type, or 'fatal'/'suspicious'/'all' for tiers. Default: fatal=break, suspicious=report. 'report' is recommended for sources games legitimately use (BRK, unofficial opcodes).")]
		public static string SetEventPolicy(
			[Description("BreakSource name (e.g. 'BreakOnBrk', 'NesBreakOnCpuCrash') or tier: 'fatal', 'suspicious', 'all'")] string source,
			[Description("Policy mode: 'ignore', 'report' or 'break'")] string mode)
		{
			if(!Enum.TryParse<DebugEventPolicy>(mode, true, out DebugEventPolicy policy)) {
				throw new McpException("Invalid mode: " + mode + ". Use 'ignore', 'report' or 'break'.");
			}

			switch(source.ToLowerInvariant()) {
				case "fatal":
				case "suspicious":
				case "all":
				case "*":
					DebugEventAggregator.SetPolicyForTier(source, policy);
					return "Policy for tier '" + source + "' set to " + policy + ".";

				default:
					if(!Enum.TryParse<BreakSource>(source, true, out BreakSource breakSource)) {
						throw new McpException("Invalid source: " + source + ". Use a BreakSource name (see mesen_get_event_policies) or 'fatal'/'suspicious'/'all'.");
					}
					DebugEventAggregator.SetPolicy(breakSource, policy);
					return breakSource + " policy set to " + policy + ".";
			}
		}

		[McpServerTool(Name = "mesen_get_event_policies", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("List all break-event sources with their current event policy (ignore/report/break) and severity tier.")]
		public static string GetEventPolicies()
		{
			return DebugEventAggregator.GetPolicyListing();
		}

		[McpServerTool(Name = "mesen_mark_event_expected", ReadOnly = false, Destructive = false, OpenWorld = false),
		 Description("Mark a break-event site as expected/legitimate (e.g. BRK used as a syscall). Expected sites are still counted in mesen_get_events but don't raise alerts and don't trigger rate-spike detection. Use expected=false to unmark.")]
		public static string MarkEventExpected(
			[Description("BreakSource name, e.g. 'BreakOnBrk'")] string source,
			[Description("Address (decimal or 0x/$ hex) of the event site")] string address,
			[Description("Mark as expected (default true); false removes the mark")] bool expected = true,
			[Description("Optional note, e.g. 'syscall trampoline'")] string? note = null)
		{
			if(!Enum.TryParse<BreakSource>(source, true, out BreakSource breakSource)) {
				throw new McpException("Invalid source: " + source + ".");
			}
			uint addr = McpToolHelper.ParseAddress(address);
			DebugEventAggregator.MarkExpected(breakSource, addr, expected, note);
			return (expected ? "Marked" : "Unmarked") + " " + breakSource + " at $" + addr.ToString("X4") + (string.IsNullOrEmpty(note) ? "" : " (" + note + ")");
		}

		[McpServerTool(Name = "mesen_get_crash_report", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("Get a crash report: crash location (PC, frame), CPU registers, callstack, the last executed instructions (trace tail) and recent break events. Fatal events (CPU crashes, invalid opcodes) are always recorded even when their policy is 'ignore'.")]
		public static string GetCrashReport()
		{
			return DebugEventAggregator.GetCrashReport();
		}

		[McpServerTool(Name = "mesen_get_last_trace", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("Get the last N executed instructions from the in-memory trace ring buffer (chronological order, oldest first). Unlike mesen_get_execution_trace this is always a tail view - useful to see what ran right before a crash or glitch.")]
		public static string GetLastTrace(
			[Description("Number of instructions to return (default 50, max 1000)")] int count = 50,
			[Description("Optional CPU type filter (Nes, Snes, Gameboy, Gba, Pce, Sms, Ws)")] string? cpuType = null)
		{
			McpToolHelper.EnsureDebuggerReady();

			CpuType? filterCpu = null;
			if(cpuType != null) {
				filterCpu = McpToolHelper.ParseCpuType(cpuType);
			}

			count = Math.Clamp(count, 1, 1000);
			TraceRow[] rows = DebugApi.GetExecutionTrace(0, (uint)count);

			//Rows come back newest-first; output chronological (oldest first)
			StringBuilder sb = new();
			for(int i = rows.Length - 1; i >= 0; i--) {
				if(filterCpu.HasValue && rows[i].Type != filterCpu.Value) {
					continue;
				}
				sb.Append('$').Append(rows[i].ProgramCounter.ToString("X4")).Append(": ").AppendLine(rows[i].GetOutput());
			}
			return sb.Length > 0 ? sb.ToString() : "(trace buffer empty - tracing is enabled automatically when the MCP server starts)";
		}

		[McpServerTool(Name = "mesen_get_lua_status", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("List all running Lua scripts with their status: id, name, whether they loaded successfully (initDone), and the last error (syntax/runtime) if any. Use mesen_get_lua_output for a script's log.")]
		public static string GetLuaStatus()
		{
			McpToolHelper.EnsureDebuggerReady();

			Int32[] ids = DebugApi.GetScriptIds();
			LuaStatusReportDto report = new();
			foreach(Int32 id in ids) {
				if(DebugApi.GetScriptInfo(id, out ScriptInfoResult info)) {
					report.Scripts.Add(new LuaScriptInfoDto {
						Id = id,
						Name = info.GetName(),
						InitDone = info.InitDone != 0,
						HasError = info.HasError != 0,
						LastError = info.HasError != 0 ? info.GetLastErrorMessage() : null
					});
				}
			}
			return McpToolHelper.Serialize(report);
		}

		[McpServerTool(Name = "mesen_get_lua_output", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("Get the log output of a Lua script (emu.log output and errors), including syntax/runtime errors that would otherwise only show in the emulator UI.")]
		public static string GetLuaOutput(
			[Description("Script ID (from mesen_run_lua_script or mesen_get_lua_status)")] int scriptId)
		{
			McpToolHelper.EnsureDebuggerReady();

			if(!DebugApi.GetScriptInfo(scriptId, out ScriptInfoResult info)) {
				throw new McpException("No script with id " + scriptId + " (it may have been removed or failed to load - see mesen_get_lua_status).");
			}

			string log = DebugApi.GetScriptLog(scriptId);
			StringBuilder sb = new();
			if(info.HasError != 0) {
				sb.AppendLine("[error] " + info.GetLastErrorMessage());
			}
			sb.Append(string.IsNullOrEmpty(log) ? "(no output)" : log);
			return sb.ToString();
		}

		[McpServerTool(Name = "mesen_validate_lua", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("Validate Lua script syntax without executing it (compile-only check). Returns 'OK' or the exact syntax error with line number. Use this before mesen_run_lua_script to catch syntax errors early.")]
		public static string ValidateLua(
			[Description("Lua script code to validate")] string code,
			[Description("Script name used in error messages (default 'mcp_script')")] string name = "mcp_script")
		{
			bool valid = DebugApi.ValidateLuaScript(name, code, out string error);
			return valid ? "OK" : "Syntax error: " + error;
		}

		[McpServerTool(Name = "mesen_watch_memory", ReadOnly = false, Destructive = false, OpenWorld = false),
		 Description("Track writes to a CPU address range: after arming, call mesen_get_memory_writers to see who wrote there (PC + value + frame of each write). Action 'arm' (startAddress+endAddress), 'disarm' or 'status'. The memory type must be the CPU address space (NesMemory, SnesMemory, GameboyMemory, GbaMemory, PceMemory, SmsMemory, WsMemory) - defaults to the main CPU's.")]
		public static string WatchMemory(
			[Description("Action: 'arm', 'disarm' or 'status'")] string action,
			[Description("Start address (required for arm, decimal or 0x/$ hex)")] string? startAddress = null,
			[Description("End address (required for arm, decimal or 0x/$ hex)")] string? endAddress = null,
			[Description("Memory type (default: main CPU address space, e.g. NesMemory)")] string? memoryType = null)
		{
			McpToolHelper.EnsureDebuggerReady();

			switch(action.ToLowerInvariant()) {
				case "arm": {
					if(startAddress == null || endAddress == null) {
						throw new McpException("startAddress and endAddress are required for action 'arm'.");
					}
					uint start = McpToolHelper.ParseAddress(startAddress);
					uint end = McpToolHelper.ParseAddress(endAddress);
					if(end < start) {
						throw new McpException("endAddress must be >= startAddress.");
					}
					MemoryType memType;
					if(memoryType != null) {
						memType = McpToolHelper.ParseMemoryType(memoryType);
					} else {
						memType = EmuApi.GetRomInfo().ConsoleType.GetMainCpuType().ToMemoryType();
					}
					DebugEventAggregator.ArmWriteWatch(start, end, memType);
					return "Write watch armed on $" + start.ToString("X4") + "-$" + end.ToString("X4") + " (" + memType + "). Use mesen_get_memory_writers to read results.";
				}

				case "disarm":
					DebugEventAggregator.DisarmWriteWatch();
					return "Write watch disarmed.";

				case "status":
					return DebugEventAggregator.GetMemoryWritersReport(1);

				default:
					throw new McpException("Invalid action: " + action + ". Use 'arm', 'disarm' or 'status'.");
			}
		}

		[McpServerTool(Name = "mesen_get_memory_writers", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("Get recent writes captured by mesen_watch_memory: each entry has address, value, the PC of the instruction that wrote it and the frame number. Answers 'what wrote this RAM location?'.")]
		public static string GetMemoryWriters(
			[Description("Max entries to return (default 50, max 256)")] int count = 50)
		{
			McpToolHelper.EnsureDebuggerReady();
			return DebugEventAggregator.GetMemoryWritersReport(Math.Clamp(count, 1, 256));
		}

		[McpServerTool(Name = "mesen_get_bank_log", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("Get the NES mapper bankswitch log: each PRG/CHR mapping change with address range, old/new bank offset, the PC that caused it and frame number. Useful to debug 'code disappeared after a bankswitch' issues.")]
		public static string GetBankLog(
			[Description("Max entries to return (default 100, max 512)")] int count = 100,
			[Description("Only return changes since the last call (default true). false = return the whole buffer.")] bool sinceLast = true)
		{
			McpToolHelper.EnsureDebuggerReady();
			return DebugEventAggregator.GetBankLogReport(Math.Clamp(count, 1, 512), sinceLast);
		}
	}
}
