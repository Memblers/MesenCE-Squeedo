using Mesen.Config.Shortcuts;
using Mesen.Interop;
using ModelContextProtocol;
using ModelContextProtocol.Server;
using System;
using System.ComponentModel;
using System.Text;

namespace Mesen.Mcp.Tools
{
	[McpServerToolType]
	public class EmulatorConfigTools
	{
		[McpServerTool(Name = "mesen_reset", ReadOnly = false, Destructive = true, OpenWorld = false),
		 Description("Reset the console. Soft reset simulates pressing the reset button. Hard reset power cycles the console, clearing all RAM.")]
		public static string Reset(
			[Description("Reset type: 'soft' (reset button) or 'hard' (power cycle, clears RAM)")] string type)
		{
			McpToolHelper.EnsureRunning();

			switch(type.ToLowerInvariant()) {
				case "soft":
					EmuApi.ExecuteShortcut(new ExecuteShortcutParams() { Shortcut = EmulatorShortcut.Reset });
					break;
				case "hard":
					EmuApi.ExecuteShortcut(new ExecuteShortcutParams() { Shortcut = EmulatorShortcut.PowerCycle });
					break;
				default:
					throw new McpException("Invalid reset type: " + type + ". Use 'soft' or 'hard'.");
			}

			return "Reset (" + type.ToLowerInvariant() + ").";
		}

		[McpServerTool(Name = "mesen_list_memory_types", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("IMPORTANT: Call this tool FIRST before using any tool that requires a 'memoryType' parameter. Returns all valid memory type names and sizes for the currently loaded ROM. Memory types vary by system (NES, SNES, Game Boy, etc.) and by cartridge/mapper.")]
		public static string ListMemoryTypes()
		{
			McpToolHelper.EnsureRunning();

			StringBuilder sb = new();
			foreach(MemoryType memType in Enum.GetValues<MemoryType>()) {
				Int32 size = DebugApi.GetMemorySize(memType);
				if(size > 0) {
					sb.Append(memType).Append('\t').Append(size).AppendLine();
				}
			}
			return sb.ToString();
		}

		[McpServerTool(Name = "mesen_list_cpu_types", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("IMPORTANT: Call this tool FIRST before using any tool that requires a 'cpuType' parameter. Returns all active CPU types for the currently loaded ROM. Main CPU types: Nes, Snes, Gameboy, Gba, Pce, Sms, Ws. Co-processors (SNES only): Spc, Sa1, Gsu, Cx4, NecDsp, St018.")]
		public static string ListCpuTypes()
		{
			McpToolHelper.EnsureRunning();

			RomInfo info = EmuApi.GetRomInfo();
			CpuType mainCpu = info.ConsoleType.GetMainCpuType();

			StringBuilder sb = new();
			sb.Append("Console: ").Append(info.ConsoleType).Append("  MainCPU: ").AppendLine(mainCpu.ToString());

			foreach(CpuType cpu in Enum.GetValues<CpuType>()) {
				try {
					MemoryType memType = cpu.ToMemoryType();
					Int32 memSize = DebugApi.GetMemorySize(memType);
					if(memSize > 0) {
						sb.Append(cpu);
						if(cpu == mainCpu) sb.Append('*');
						sb.Append(' ').Append(memType).Append(' ').Append(memSize).AppendLine();
					}
				} catch {
					// CPU type not available for this system
				}
			}

			return sb.ToString();
		}
	}
}
