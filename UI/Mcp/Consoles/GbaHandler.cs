using Mesen.Debugger;
using Mesen.Interop;
using Mesen.Mcp.Models;
using Mesen.Mcp.Tools;
using System.Collections.Generic;

namespace Mesen.Mcp.Consoles
{
	public class GbaHandler : IConsoleHandler
	{
		public Dictionary<string, string>? GetRegisters(CpuType cpu)
		{
			GbaCpuState s = DebugApi.GetCpuState<GbaCpuState>(cpu);
			Dictionary<string, string> regs = new();
			for(int i = 0; i < 16; i++) {
				regs["R" + i] = "$" + s.R[i].ToString("X8");
			}
			regs["mode"] = s.CPSR.Mode.ToString();
			regs["thumb"] = s.CPSR.Thumb.ToString();
			regs["N"] = s.CPSR.Negative.ToString();
			regs["Z"] = s.CPSR.Zero.ToString();
			regs["C"] = s.CPSR.Carry.ToString();
			regs["V"] = s.CPSR.Overflow.ToString();
			regs["I"] = s.CPSR.IrqDisable.ToString();
			regs["F"] = s.CPSR.FiqDisable.ToString();
			return regs;
		}

		public string SerializePpuState(CpuType cpu)
		{
			GbaPpuState s = (GbaPpuState)DebugApi.GetPpuState(cpu);
			return McpToolHelper.Serialize(new GbaPpuStateResponse {
				CpuType = "Gba",
				Scanline = s.Scanline,
				Cycle = s.Cycle,
				FrameCount = s.FrameCount,
				BgMode = s.BgMode,
				ForcedBlank = s.ForcedBlank
			});
		}

		public string? GetRomHeader()
		{
			return null;
		}
	}
}
