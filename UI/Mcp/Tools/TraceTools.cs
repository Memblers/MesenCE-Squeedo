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
		 Description("Configure trace logging options for a CPU. Must be called before reading execution trace.")]
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

			DebugApi.SetTraceOptions(cpu, options);

			return "Trace " + (enabled ? "enabled" : "disabled") + " for " + cpuType + ".";
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
		 Description("Start or stop logging execution trace to a file. Use action 'start' to begin logging, 'stop' to end.")]
		public static string TraceFile(
			[Description("Action: 'start' or 'stop'")] string action,
			[Description("File path for trace log (required for start)")] string? filepath = null)
		{
			McpToolHelper.EnsureDebuggerReady();

			switch(action.ToLowerInvariant()) {
				case "start":
					if(string.IsNullOrEmpty(filepath)) {
						throw new McpException("File path is required when action is 'start'.");
					}
					DebugApi.StartLogTraceToFile(filepath);
					return "Trace logging to " + filepath;

				case "stop":
					DebugApi.StopLogTraceToFile();
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
