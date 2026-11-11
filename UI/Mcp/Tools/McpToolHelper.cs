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

			//Make sure the debug workspace (symbol files like <rom>.mlb) is loaded for this ROM
			DebugWorkspaceManager.EnsureLoaded();

			if(debuggerRestarted) {
				//A fresh core debugger starts with empty label/breakpoint tables - re-push them
				LabelManager.RefreshLabels(false);
				BreakpointManager.SetBreakpoints();
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
			if(!Enum.TryParse<MemoryType>(memoryType, true, out MemoryType memType)) {
				throw new McpException("Invalid memory type: " + memoryType + ". Call mesen_list_memory_types to see valid values.");
			}
			return memType;
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
