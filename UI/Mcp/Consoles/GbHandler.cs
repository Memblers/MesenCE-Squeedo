using Mesen.Debugger;
using Mesen.Interop;
using Mesen.Mcp.Models;
using Mesen.Mcp.Tools;
using System.Collections.Generic;

namespace Mesen.Mcp.Consoles
{
	public class GbHandler : IConsoleHandler
	{
		public Dictionary<string, string>? GetRegisters(CpuType cpu)
		{
			GbCpuState s = DebugApi.GetCpuState<GbCpuState>(cpu);
			return new Dictionary<string, string> {
				["A"] = "$" + s.A.ToString("X2"),
				["B"] = "$" + s.B.ToString("X2"),
				["C"] = "$" + s.C.ToString("X2"),
				["D"] = "$" + s.D.ToString("X2"),
				["E"] = "$" + s.E.ToString("X2"),
				["H"] = "$" + s.H.ToString("X2"),
				["L"] = "$" + s.L.ToString("X2"),
				["SP"] = "$" + s.SP.ToString("X4"),
				["PC"] = "$" + s.PC.ToString("X4"),
				["flags"] = "$" + s.Flags.ToString("X2")
			};
		}

		public string SerializePpuState(CpuType cpu)
		{
			GbPpuState s = (GbPpuState)DebugApi.GetPpuState(cpu);
			return McpToolHelper.Serialize(new GbPpuStateResponse {
				CpuType = "Gameboy",
				Scanline = s.Scanline,
				Cycle = s.Cycle,
				FrameCount = s.FrameCount,
				Ly = s.Ly,
				LyCompare = s.LyCompare,
				Mode = s.Mode.ToString(),
				ScrollX = s.ScrollX,
				ScrollY = s.ScrollY,
				WindowX = s.WindowX,
				WindowY = s.WindowY,
				CgbEnabled = s.CgbEnabled
			});
		}

		public string? GetRomHeader()
		{
			return null;
		}
	}
}
