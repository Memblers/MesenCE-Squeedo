using Mesen.Interop;
using ModelContextProtocol;
using ModelContextProtocol.Server;
using System;
using System.ComponentModel;
using System.IO;
using System.Text;

namespace Mesen.Mcp.Tools
{
	[McpServerToolType]
	public class TraceTools
	{
		[McpServerTool(Name = "mesen_set_trace_options", ReadOnly = false, Destructive = false, OpenWorld = false),
		 Description("Configure trace logging options for a CPU. Must be called before reading execution trace with mesen_get_execution_trace (the 30000-row buffer). Verifies the flag actually reached the core and reports failure instead of pretending. Note: rows are only recorded while emulation is running - a paused/crashed emulator records nothing. For an always-on tail that needs no setup, use mesen_get_last_trace.")]
		public static string SetTraceOptions(
			[Description("CPU type: Nes, Snes, Gameboy, Gba, Pce, Sms, Ws, Spc, Sa1, Gsu, Cx4")] string cpuType,
			[Description("Enable tracing for this CPU (default true)")] bool enabled = true,
			[Description("Trace format string (e.g. '[Disassembly][Align,24] A:[A,2h] X:[X,2h]'). Leave empty for default.")] string? format = null,
			[Description("Condition expression to filter trace (e.g. 'A == $42'). Empty = no filter.")] string? condition = null,
			[Description("Use labels in trace output (default true)")] bool useLabels = true,
			[Description("Indent code based on call depth (default false)")] bool indentCode = false)
		{
			McpToolHelper.EnsureDebuggerReady();
			CpuType cpu = McpToolHelper.ParseCpuType(cpuType);

			string traceFormat = format ?? McpToolHelper.GetDefaultTraceFormat(cpu);

			InteropTraceLoggerOptions options = new() {
				Enabled = enabled,
				UseLabels = useLabels,
				IndentCode = indentCode,
				Format = Encoding.UTF8.GetBytes(traceFormat),
				Condition = Encoding.UTF8.GetBytes(condition ?? "")
			};

			Array.Resize(ref options.Format, 1000);
			Array.Resize(ref options.Condition, 1000);

			bool applied;
			try {
				applied = DebugApi.SetTraceOptions(cpu, options);
			} catch(Exception ex) {
				throw new McpException("SetTraceOptions failed: " + ex.Message);
			}

			if(!applied) {
				//Previously this silently no-oped and reported success - the flag never reached the logger
				throw new McpException("Trace options were NOT applied: no trace logger exists for " + cpuType +
					" (core debugger not active or its requests are blocked - check mesen_get_status for debuggerBlocked, then reload the ROM).");
			}

			bool state = DebugApi.IsTraceEnabled(cpu);
			if(state != enabled) {
				throw new McpException("Trace options were applied but the core reports tracing " + (state ? "enabled" : "disabled") +
					" - the flag did not stick (another client may have re-applied its own options, e.g. a closing trace logger window).");
			}

			if(!enabled) {
				return "Trace disabled for " + cpuType + ".";
			}

			bool running = false;
			try { running = EmuApi.IsRunning() && !EmuApi.IsPaused(); } catch { }
			return "Trace enabled for " + cpuType + " (verified in core, 30000-row buffer). " +
				(running ? "Emulation is running - rows are being recorded now." : "Emulation is paused/stopped - rows will only be recorded once it runs again.") +
				" Note: a paused/crashed emulator records nothing; use mesen_get_last_trace for the always-on tail.";
		}

		[McpServerTool(Name = "mesen_get_execution_trace", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("Get the execution trace (last N executed instructions with register state).")]
		public static string GetExecutionTrace(
			[Description("Number of trace rows to retrieve (max 30000)")] int count,
			[Description("Optional CPU type filter. If omitted, returns all CPU types.")] string? cpuType = null,
			[Description("Optional file path to save trace output")] string? outputFile = null)
		{
			McpToolHelper.EnsureDebuggerReady();

			CpuType? filterCpu = null;
			if(cpuType != null) {
				filterCpu = McpToolHelper.ParseCpuType(cpuType);
			}

			count = Math.Min(count, DebugApi.TraceLogBufferSize);
			TraceRow[] rows = DebugApi.GetExecutionTrace(0, (uint)count);

			bool showCpuType = !filterCpu.HasValue;
			StringBuilder sb = new();

			foreach(TraceRow row in rows) {
				if(filterCpu.HasValue && row.Type != filterCpu.Value) {
					continue;
				}

				if(showCpuType) {
					sb.Append('[').Append(row.Type).Append("] ");
				}
				sb.Append('$').Append(row.ProgramCounter.ToString("X4")).Append(": ").AppendLine(row.GetOutput());
			}

			string text = sb.ToString();
			if(!string.IsNullOrEmpty(outputFile)) {
				File.WriteAllText(outputFile, text);
			}
			if(string.IsNullOrEmpty(text)) {
				return "(trace buffer empty - the 30000-row buffer requires mesen_set_trace_options first and only fills while emulation runs. " +
					"Use mesen_get_last_trace for the always-on tail that needs no setup.)";
			}
			return text;
		}

		[McpServerTool(Name = "mesen_clear_execution_trace", ReadOnly = false, Destructive = true, OpenWorld = false),
		 Description("Clear the execution trace buffer.")]
		public static string ClearExecutionTrace()
		{
			McpToolHelper.EnsureDebuggerReady();

			DebugApi.ClearExecutionTrace();
			return "Trace cleared.";
		}

		[McpServerTool(Name = "mesen_trace_file", ReadOnly = false, Destructive = false, OpenWorld = false),
		 Description("Start or stop logging execution trace to a file. Use action 'start' to begin logging, 'stop' to end. Requires tracing to be enabled (mesen_set_trace_options) for rows to appear. Validates the path and reports the underlying error on failure.")]
		public static string TraceFile(
			[Description("Action: 'start' or 'stop'")] string action,
			[Description("File path for trace log (required for start)")] string? filepath = null)
		{
			switch(action.ToLowerInvariant()) {
				case "start": {
					if(string.IsNullOrEmpty(filepath)) {
						throw new McpException("File path is required when action is 'start'.");
					}

					McpToolHelper.EnsureDebuggerReady();

					string? dir = Path.GetDirectoryName(Path.GetFullPath(filepath));
					if(!string.IsNullOrEmpty(dir) && !Directory.Exists(dir)) {
						throw new McpException("Directory does not exist: " + dir);
					}

					try {
						if(!DebugApi.StartLogTraceToFile(filepath)) {
							throw new McpException("Core could not open trace file for writing: " + filepath + " (path invalid, locked, or no active debugger).");
						}
					} catch(McpException) {
						throw;
					} catch(Exception ex) {
						throw new McpException("Failed to start trace logging to " + filepath + ": " + ex.Message);
					}
					CpuType mainCpu;
					try { mainCpu = EmuApi.GetRomInfo().ConsoleType.GetMainCpuType(); } catch { mainCpu = CpuType.Nes; }
					return "Trace logging to " + filepath + (DebugApi.IsTraceEnabled(mainCpu) ? "" : " (note: tracing is currently disabled - enable it with mesen_set_trace_options or no rows will be written)");
				}

				case "stop":
					try {
						DebugApi.StopLogTraceToFile();
					} catch(Exception ex) {
						throw new McpException("Failed to stop trace logging: " + ex.Message);
					}
					return "Trace logging stopped.";

				default:
					throw new McpException("Invalid action: " + action + ". Use 'start' or 'stop'.");
			}
		}

		[McpServerTool(Name = "mesen_profiler", ReadOnly = false, Destructive = false, OpenWorld = false),
		 Description("Get or reset profiler data for a CPU. Use action 'get' for per-function statistics (call count, cycles, min/max), or 'reset' to clear.")]
		public static string Profiler(
			[Description("Action: 'get' or 'reset'")] string action,
			[Description("CPU type: Nes, Snes, Gameboy, Gba, Pce, Sms, Ws, Spc, Sa1")] string cpuType,
			[Description("Optional file path to save profiler data (only used for 'get')")] string? outputFile = null)
		{
			McpToolHelper.EnsureDebuggerReady();
			CpuType cpu = McpToolHelper.ParseCpuType(cpuType);

			switch(action.ToLowerInvariant()) {
				case "get": {
					ProfiledFunction[] buffer = new ProfiledFunction[100000];
					int count = DebugApi.GetProfilerData(cpu, ref buffer);

					StringBuilder sb = new();
					for(int i = 0; i < count; i++) {
						ProfiledFunction f = buffer[i];
						if(f.CallCount == 0) {
							continue;
						}

						sb.Append('$').Append(f.Address.Address.ToString("X4"))
							.Append(' ').Append(f.Address.Type)
							.Append(" calls=").Append(f.CallCount)
							.Append(" incl=").Append(f.InclusiveCycles)
							.Append(" excl=").Append(f.ExclusiveCycles)
							.Append(" min=").Append(f.MinCycles)
							.Append(" max=").Append(f.MaxCycles)
							.AppendLine();
					}

					string text = sb.ToString();
					if(!string.IsNullOrEmpty(outputFile)) {
						File.WriteAllText(outputFile, text);
					}
					return text;
				}

				case "reset":
					DebugApi.ResetProfiler(cpu);
					return "Profiler reset.";

				default:
					throw new McpException("Invalid action: " + action + ". Use 'get' or 'reset'.");
			}
		}

		}
}
