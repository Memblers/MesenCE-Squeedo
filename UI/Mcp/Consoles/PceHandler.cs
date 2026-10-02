using Mesen.Debugger;
using Mesen.Interop;
using Mesen.Mcp.Models;
using Mesen.Mcp.Tools;
using System.Collections.Generic;

namespace Mesen.Mcp.Consoles
{
	public class PceHandler : IConsoleHandler
	{
		public Dictionary<string, string>? GetRegisters(CpuType cpu)
		{
			PceCpuState s = DebugApi.GetCpuState<PceCpuState>(cpu);
			return new Dictionary<string, string> {
				["A"] = "$" + s.A.ToString("X2"),
				["X"] = "$" + s.X.ToString("X2"),
				["Y"] = "$" + s.Y.ToString("X2"),
				["SP"] = "$" + s.SP.ToString("X2"),
				["PC"] = "$" + s.PC.ToString("X4"),
				["PS"] = "$" + s.PS.ToString("X2"),
				["flags"] = McpToolHelper.FormatFlags6502(s.PS)
			};
		}

		public string SerializePpuState(CpuType cpu)
		{
			PceVideoState s = (PceVideoState)DebugApi.GetPpuState(cpu);
			return McpToolHelper.Serialize(new PcePpuStateResponse {
				CpuType = "Pce",
				Scanline = s.Vdc.Scanline,
				HClock = s.Vdc.HClock,
				FrameCount = s.Vdc.FrameCount,
				RcrCounter = s.Vdc.RcrCounter
			});
		}

		public string? GetRomHeader()
		{
			return null;
		}
	}
}
