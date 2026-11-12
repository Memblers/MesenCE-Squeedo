using Avalonia.Threading;
using Mesen.Debugger;
using Mesen.Debugger.Labels;
using Mesen.Debugger.Utilities;
using Mesen.Interop;
using Mesen.Mcp.Models;
using ModelContextProtocol;
using System;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization.Metadata;

namespace Mesen.Mcp.Tools
{
	public static class McpToolHelper
	{
		private static volatile bool _debuggerInitialized;

		public static bool IsDebuggerReady()
		{
			return _debuggerInitialized && EmuApi.IsRunning();
		}

		public static void MarkDebuggerInitialized()
		{
			_debuggerInitialized = true;
		}

		public static string GetDefaultTraceFormat(CpuType cpuType)
		{
			return cpuType switch {
				CpuType.Snes or CpuType.Sa1 =>
					"[Disassembly][Align,24] A:[A,4h] X:[X,4h] Y:[Y,4h] S:[SP,4h] D:[D,4h] DB:[DB,2h] P:[P,8]",
				CpuType.Nes =>
					"[Disassembly][Align,24] A:[A,2h] X:[X,2h] Y:[Y,2h] S:[SP,2h] P:[P,8]",
				CpuType.Gameboy =>
					"[Disassembly][Align,24] A:[A,2h] B:[B,2h] C:[C,2h] D:[D,2h] E:[E,2h] F:[F,2h] H:[H,2h] L:[L,2h] SP:[SP,4h]",
				CpuType.Gba =>
					"[Disassembly][Align,42] ",
				CpuType.Pic18 =>
					"[Disassembly][Align,24] W:[W,2h] BSR:[BSR,2h] STKPTR:[STKPTR,2h] STATUS:[PS,5]",
				_ =>
					"[Disassembly][Align,24] "
			};
		}

		public static string Serialize<T>(T value)
		{
			JsonTypeInfo<T>? typeInfo = McpJsonContext.Default.GetTypeInfo(typeof(T)) as JsonTypeInfo<T>;
			if(typeInfo == null) {
				throw new InvalidOperationException("Type " + typeof(T).Name + " is not registered in McpJsonContext");
			}
			return JsonSerializer.Serialize(value, typeInfo);
		}

		public static void EnsureDebuggerReady()
		{
			try {
				if(!EmuApi.IsRunning()) {
					throw new McpException("No ROM loaded.");
				}

				bool debuggerRestarted = !DebugApi.IsDebuggerRunning();
				if(debuggerRestarted) {
					//The core debugger was either never started, or was released (e.g. when the
					//last debug window was closed) and is now being recreated with empty tables.
					//Tracing stays opt-in (mesen_set_trace_options) - auto-enabling full instruction
					//tracing is too expensive during garbage/crash storms.
					DebugApi.InitializeDebugger();
					_debuggerInitialized = true;
				}

				if(!DebugApi.IsDebuggerRunning()) {
					//Init was attempted but no debugger is reachable: while the core blocks
					//debugger requests (ROM load/unload in progress, or a leaked block count
					//after an interrupted load), every debug API silently returns empty data
					//(memory size $0000, PC=$0000, blank windows). Report it instead.
					if(DebugApi.IsDebuggerBlocked()) {
						int count = DebugApi.GetDebuggerBlockCount();
						throw new McpException(
							"Core debugger requests are blocked (block count " + count + "). " +
							(count > 1
								? "The count looks leaked (no load in progress can produce this) - recovery: call mesen_reset with type 'hard'; if that fails, restart Mesen."
								: "A ROM load/unload may be in progress - retry in a moment; if this persists, call mesen_reset with type 'hard' or restart Mesen."));
					}
					throw new McpException("Core debugger could not be initialized (no ROM running or the debugger failed to start).");
				}

				//Make sure the debug workspace (symbol files like <rom>.mlb) is loaded for this ROM
				DebugWorkspaceManager.EnsureLoaded();

				if(debuggerRestarted) {
					//A fresh core debugger starts with empty label/breakpoint tables - re-push them
					LabelManager.RefreshLabels(false);
					BreakpointManager.SetBreakpoints();
				}
			} catch(McpException) {
				throw;
			} catch(Exception ex) {
				//Never surface raw exceptions (they show up as a bare "An error occurred invoking ...")
				throw new McpException("Debugger initialization failed: " + ex.Message);
			}
		}

		public static void OnUIThread(Action action)
		{
			//Workspace/label/breakpoint changes raise events that update debug window
			//view models - always run them on the UI thread
			if(Dispatcher.UIThread.CheckAccess()) {
				action();
			} else {
				Dispatcher.UIThread.InvokeAsync(action).Wait();
			}
		}

		public static void EnsureRunning()
		{
			if(!EmuApi.IsRunning()) {
				throw new McpException("No ROM loaded.");
			}
		}

		public static uint ParseAddress(string address)
		{
			if(!TryParseAddress(address, out uint result)) {
				throw new McpException("Invalid address: " + address);
			}
			return result;
		}

		public static MemoryType ParseMemoryType(string memoryType)
		{
			if(!Enum.TryParse<MemoryType>(memoryType, true, out MemoryType memType) || !Enum.IsDefined(memType)) {
				//Self-contained error: the agent may not be able to call mesen_list_memory_types
				//(its tool group can be disabled in the IDE), so include the actual list here.
				throw new McpException("Invalid memory type: " + memoryType + ". Valid memory types for the current ROM: " + GetMemoryTypeList());
			}
			return memType;
		}

		public static string GetMemoryTypeList()
		{
			try {
				if(!EmuApi.IsRunning()) {
					return "(no ROM loaded)";
				}

				System.Text.StringBuilder sb = new();
				foreach(MemoryType memType in Enum.GetValues<MemoryType>()) {
					try {
						if(DebugApi.GetMemorySize(memType) > 0) {
							if(sb.Length > 0) {
								sb.Append(", ");
							}
							sb.Append(memType);
						}
					} catch { }
				}
				return sb.Length > 0 ? sb.ToString() : "(none available)";
			} catch {
				return "(could not list memory types)";
			}
		}

		public static void ValidateMemorySize(MemoryType memType, Int32 memSize)
		{
			if(memSize > 0) {
				return;
			}

			//Size 0 means either the type doesn't exist on this ROM, or the core debugger is
			//unreachable (blocked/stopped) - the latter used to surface as a bare
			//"Memory size: $0000" for every type after a crash/hang, which looked like data.
			if(!DebugApi.IsDebuggerRunning()) {
				string reason = DebugApi.IsDebuggerBlocked()
					? "debugger requests are blocked (block count " + DebugApi.GetDebuggerBlockCount() + ")"
					: "the core debugger is not active";
				throw new McpException($"Cannot read {memType}: the core debugger is unreachable ({reason}). " +
					"Call mesen_reset type 'hard' to recover, or restart Mesen if it persists. Valid types meanwhile: " + GetMemoryTypeList());
			}
			throw new McpException($"Memory type {memType} has size 0 for this ROM. Valid types: " + GetMemoryTypeList());
		}

		public static CpuType ParseCpuType(string cpuType)
		{
			if(!Enum.TryParse<CpuType>(cpuType, true, out CpuType cpu)) {
				throw new McpException("Invalid CPU type: " + cpuType + ". Call mesen_list_cpu_types to see valid values.");
			}
			return cpu;
		}

		public static bool TryParseAddress(string address, out uint result)
		{
			address = address.Trim();
			if(address.StartsWith("0x", StringComparison.OrdinalIgnoreCase)) {
				return uint.TryParse(address.Substring(2), System.Globalization.NumberStyles.HexNumber, null, out result);
			} else if(address.StartsWith("$")) {
				return uint.TryParse(address.Substring(1), System.Globalization.NumberStyles.HexNumber, null, out result);
			} else {
				return uint.TryParse(address, out result);
			}
		}

		public static string FormatFlags6502(byte ps)
		{
			return string.Concat(
				(ps & 0x80) != 0 ? "N" : "n",
				(ps & 0x40) != 0 ? "V" : "v",
				"-",
				(ps & 0x10) != 0 ? "B" : "b",
				(ps & 0x08) != 0 ? "D" : "d",
				(ps & 0x04) != 0 ? "I" : "i",
				(ps & 0x02) != 0 ? "Z" : "z",
				(ps & 0x01) != 0 ? "C" : "c"
			);
		}

		public static bool TryParseValue(string value, out byte result)
		{
			value = value.Trim();
			if(value.StartsWith("0x", StringComparison.OrdinalIgnoreCase)) {
				return byte.TryParse(value.Substring(2), System.Globalization.NumberStyles.HexNumber, null, out result);
			} else if(value.StartsWith("$")) {
				return byte.TryParse(value.Substring(1), System.Globalization.NumberStyles.HexNumber, null, out result);
			} else {
				return byte.TryParse(value, out result);
			}
		}
	}
}
