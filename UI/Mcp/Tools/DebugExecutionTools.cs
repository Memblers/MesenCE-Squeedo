using Mesen.Debugger;
using Mesen.Interop;
using Mesen.Mcp.Consoles;
using ModelContextProtocol;
using ModelContextProtocol.Server;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Text;

namespace Mesen.Mcp.Tools
{
	[McpServerToolType]
	public class DebugExecutionTools
	{
		[McpServerTool(Name = "mesen_step", ReadOnly = false, Destructive = false, OpenWorld = false),
		 Description("Step the CPU by a number of instructions. Type can be 'Step', 'StepOver', 'StepOut', 'CpuCycleStep', 'PpuStep', 'PpuScanline', 'PpuFrame'.")]
		public static string Step(
			[Description("CPU type: Nes, Snes, Gameboy, Gba, Pce, Sms, Ws, Spc, NecDsp, Sa1, Gsu, Cx4")] string cpuType,
			[Description("Number of steps (default 1)")] int count = 1,
			[Description("Step type: Step, StepOver, StepOut, CpuCycleStep, PpuStep, PpuScanline, PpuFrame")] string stepType = "Step")
		{
			McpToolHelper.EnsureDebuggerReady();
			CpuType cpu = McpToolHelper.ParseCpuType(cpuType);

			if(!Enum.TryParse<StepType>(stepType, true, out StepType step)) {
				throw new McpException("Invalid step type: " + stepType);
			}

			DebugApi.Step(cpu, count, step);

			// Poll until execution pauses, with timeout
			int elapsed = 0;
			int maxWait = step == StepType.StepOut || step == StepType.PpuFrame ? 2000 : 500;
			while(!EmuApi.IsPaused() && elapsed < maxWait) {
				System.Threading.Thread.Sleep(5);
				elapsed += 5;
			}

			return GetCpuStateText(cpu);
		}

		[McpServerTool(Name = "mesen_resume_execution", ReadOnly = false, Destructive = false, OpenWorld = false),
		 Description("Resume execution after a breakpoint or step.")]
		public static string ResumeExecution()
		{
			McpToolHelper.EnsureDebuggerReady();

			DebugApi.ResumeExecution();
			return "Execution resumed.";
		}

		[McpServerTool(Name = "mesen_breakpoint", ReadOnly = false, Destructive = false, OpenWorld = false),
		 Description("Manage breakpoints. Action: 'set', 'remove', 'remove_all', 'list'.")]
		public static string Breakpoint(
			[Description("Action: 'set', 'remove', 'remove_all', 'list'")] string action,
			[Description("Address (required for set/remove, decimal or 0x/$ hex)")] string? address = null,
			[Description("Breakpoint type for set: Execute, Read, Write (comma-separated)")] string? type = null,
			[Description("Memory type (required for set/remove)")] string? memoryType = null,
			[Description("CPU type (required for set)")] string? cpuType = null,
			[Description("End address for range breakpoints (for set)")] string? endAddress = null,
			[Description("Condition expression (for set, e.g. 'A == $42')")] string? condition = null)
		{
			McpToolHelper.EnsureDebuggerReady();

			switch(action.ToLowerInvariant()) {
				case "set": {
					if(address == null) {
						throw new McpException("'address' is required for action 'set'.");
					}
					if(type == null) {
						throw new McpException("'type' is required for action 'set'.");
					}
					if(memoryType == null) {
						throw new McpException("'memoryType' is required for action 'set'.");
					}
					if(cpuType == null) {
						throw new McpException("'cpuType' is required for action 'set'.");
					}

					uint addr = McpToolHelper.ParseAddress(address);
					CpuType cpu = McpToolHelper.ParseCpuType(cpuType);
					MemoryType memType = McpToolHelper.ParseMemoryType(memoryType);

					bool breakOnExec = false, breakOnRead = false, breakOnWrite = false;
					foreach(string t in type.Split(',', StringSplitOptions.TrimEntries)) {
						if(t.Equals("Execute", StringComparison.OrdinalIgnoreCase)) breakOnExec = true;
						else if(t.Equals("Read", StringComparison.OrdinalIgnoreCase)) breakOnRead = true;
						else if(t.Equals("Write", StringComparison.OrdinalIgnoreCase)) breakOnWrite = true;
					}

					if(!breakOnExec && !breakOnRead && !breakOnWrite) {
						throw new McpException("Invalid breakpoint type. Use Execute, Read, Write, or a combination.");
					}

					UInt32 endAddr = addr;
					if(endAddress != null) {
						endAddr = McpToolHelper.ParseAddress(endAddress);
					}

					Breakpoint bp = new Breakpoint() {
						CpuType = cpu,
						MemoryType = memType,
						BreakOnExec = breakOnExec,
						BreakOnRead = breakOnRead,
						BreakOnWrite = breakOnWrite,
						StartAddress = addr,
						EndAddress = endAddr,
						Enabled = true,
						MarkEvent = false,
						IgnoreDummyOperations = false,
						Condition = condition ?? ""
					};

					BreakpointManager.AddCpuType(cpu);
					BreakpointManager.AddBreakpoint(bp);

					string summary = "Breakpoint set: $" + addr.ToString("X4");
					if(endAddr != addr) {
						summary += "-$" + endAddr.ToString("X4");
					}
					summary += " " + type + " on " + memoryType + " (" + cpuType + ")";
					if(!string.IsNullOrEmpty(condition)) {
						summary += " if " + condition;
					}
					return summary;
				}

				case "remove": {
					if(address == null) {
						throw new McpException("'address' is required for action 'remove'.");
					}
					if(memoryType == null) {
						throw new McpException("'memoryType' is required for action 'remove'.");
					}

					uint addr = McpToolHelper.ParseAddress(address);
					MemoryType memType = McpToolHelper.ParseMemoryType(memoryType);

					Breakpoint? target = null;
					foreach(Breakpoint bp in BreakpointManager.Breakpoints) {
						if(bp.StartAddress == addr && bp.MemoryType == memType) {
							target = bp;
							break;
						}
					}

					if(target == null) {
						throw new McpException($"No breakpoint found at ${addr:X4} ({memoryType}). Use mesen_breakpoint with action 'list' to see active breakpoints.");
					}

					BreakpointManager.RemoveBreakpoint(target);
					return "Removed breakpoint at $" + target.StartAddress.ToString("X4") + " (" + target.MemoryType + ")";
				}

				case "remove_all": {
					BreakpointManager.ClearBreakpoints();
					return "All breakpoints removed.";
				}

				case "list": {
					StringBuilder sb = new();
					int index = 0;
					foreach(Breakpoint bp in BreakpointManager.Breakpoints) {
						string rwx = (bp.BreakOnExec ? "X" : "-") + (bp.BreakOnRead ? "R" : "-") + (bp.BreakOnWrite ? "W" : "-");
						sb.Append(index++).Append('\t')
							.Append(bp.Enabled ? "on" : "off").Append('\t')
							.Append('$').Append(bp.StartAddress.ToString("X4"));
						if(bp.EndAddress != bp.StartAddress) {
							sb.Append("-$").Append(bp.EndAddress.ToString("X4"));
						}
						sb.Append('\t').Append(bp.MemoryType)
							.Append('\t').Append(bp.CpuType)
							.Append('\t').Append(rwx);
						if(!string.IsNullOrEmpty(bp.Condition)) {
							sb.Append('\t').Append(bp.Condition);
						}
						sb.AppendLine();
					}
					return sb.Length > 0 ? sb.ToString() : "(no breakpoints)";
				}

				default:
					throw new McpException("Invalid action: " + action + ". Use 'set', 'remove', 'remove_all', or 'list'.");
			}
		}

		[McpServerTool(Name = "mesen_get_state", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("Get CPU or PPU state (registers, flags, program counter). Component: 'cpu' or 'ppu'.")]
		public static string GetState(
			[Description("Component: 'cpu' or 'ppu'")] string component,
			[Description("CPU type: Nes, Snes, Gameboy, Gba, Pce, Sms, Ws, Spc")] string cpuType)
		{
			McpToolHelper.EnsureDebuggerReady();
			CpuType cpu = McpToolHelper.ParseCpuType(cpuType);

			switch(component.ToLowerInvariant()) {
				case "cpu":
					return GetCpuStateText(cpu);

				case "ppu": {
					IConsoleHandler handler = ConsoleHandlerFactory.GetHandler(cpu);
					return handler.SerializePpuState(cpu);
				}

				default:
					throw new McpException("Invalid component: " + component + ". Use 'cpu' or 'ppu'.");
			}
		}

		[McpServerTool(Name = "mesen_set_program_counter", ReadOnly = false, Destructive = false, OpenWorld = false),
		 Description("Set the program counter to a specific address.")]
		public static string SetProgramCounter(
			[Description("CPU type: Nes, Snes, Gameboy, Gba, Pce, Sms, Ws")] string cpuType,
			[Description("Address to set the PC to (decimal or 0x/$ hex)")] string address)
		{
			McpToolHelper.EnsureDebuggerReady();
			CpuType cpu = McpToolHelper.ParseCpuType(cpuType);
			uint addr = McpToolHelper.ParseAddress(address);

			DebugApi.SetProgramCounter(cpu, addr);
			return "PC=$" + addr.ToString("X4");
		}

		[McpServerTool(Name = "mesen_get_callstack", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("Get the current call stack.")]
		public static string GetCallstack(
			[Description("CPU type: Nes, Snes, Gameboy, Gba, Pce, Sms, Ws")] string cpuType)
		{
			McpToolHelper.EnsureDebuggerReady();
			CpuType cpu = McpToolHelper.ParseCpuType(cpuType);

			StackFrameInfo[] callstack = DebugApi.GetCallstack(cpu);
			StringBuilder sb = new();
			foreach(StackFrameInfo frame in callstack) {
				sb.Append('$').Append(frame.Source.ToString("X4"))
					.Append(" -> $").Append(frame.Target.ToString("X4"))
					.Append(" ret=$").Append(frame.Return.ToString("X4"));
				string flags = frame.Flags.ToString();
				if(flags != "None") {
					sb.Append(' ').Append(flags);
				}
				sb.AppendLine();
			}
			return sb.Length > 0 ? sb.ToString() : "(empty)";
		}

		[McpServerTool(Name = "mesen_evaluate_expression", ReadOnly = true, Destructive = false, OpenWorld = false),
		 Description("Evaluate a debugger expression (e.g. 'A + X', '$4016', '[0x2000]').")]
		public static string EvaluateExpression(
			[Description("Expression to evaluate")] string expression,
			[Description("CPU type: Nes, Snes, Gameboy, Gba, Pce, Sms, Ws")] string cpuType)
		{
			McpToolHelper.EnsureDebuggerReady();
			CpuType cpu = McpToolHelper.ParseCpuType(cpuType);

			EvalResultType resultType;
			Int64 result = DebugApi.EvaluateExpression(expression, cpu, out resultType, true);

			return result + " ($" + result.ToString("X") + ")";
		}

		private static string GetCpuStateText(CpuType cpu)
		{
			UInt32 pc = DebugApi.GetProgramCounter(cpu, false);

			StringBuilder sb = new();
			sb.Append(cpu).Append(" PC=$").Append(pc.ToString("X4"));

			try {
				IConsoleHandler handler = ConsoleHandlerFactory.GetHandler(cpu);
				Dictionary<string, string>? regs = handler.GetRegisters(cpu);
				if(regs != null) {
					foreach(var kv in regs) {
						sb.Append(' ').Append(kv.Key).Append('=').Append(kv.Value);
					}
				}
			} catch {
				// Register state not available
			}

			return sb.ToString();
		}

	}
}
