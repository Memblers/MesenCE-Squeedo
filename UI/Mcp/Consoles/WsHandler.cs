using Mesen.Debugger;
using Mesen.Interop;
using Mesen.Mcp.Models;
using Mesen.Mcp.Tools;
using System.Collections.Generic;

namespace Mesen.Mcp.Consoles
{
	public class WsHandler : IConsoleHandler
	{
		public Dictionary<string, string>? GetRegisters(CpuType cpu)
		{
			WsCpuState s = DebugApi.GetCpuState<WsCpuState>(cpu);
			return new Dictionary<string, string> {
				["AX"] = "$" + s.AX.ToString("X4"),
				["BX"] = "$" + s.BX.ToString("X4"),
				["CX"] = "$" + s.CX.ToString("X4"),
				["DX"] = "$" + s.DX.ToString("X4"),
				["SP"] = "$" + s.SP.ToString("X4"),
				["BP"] = "$" + s.BP.ToString("X4"),
				["SI"] = "$" + s.SI.ToString("X4"),
				["DI"] = "$" + s.DI.ToString("X4"),
				["CS"] = "$" + s.CS.ToString("X4"),
				["DS"] = "$" + s.DS.ToString("X4"),
				["ES"] = "$" + s.ES.ToString("X4"),
				["SS"] = "$" + s.SS.ToString("X4"),
				["IP"] = "$" + s.IP.ToString("X4"),
				["Flags"] = "$" + s.Flags.Get().ToString("X4")
			};
		}

		public string SerializePpuState(CpuType cpu)
		{
			WsPpuState s = (WsPpuState)DebugApi.GetPpuState(cpu);
			return McpToolHelper.Serialize(new WsPpuStateResponse {
				CpuType = "Ws",
				Scanline = s.Scanline,
				Cycle = s.Cycle,
				FrameCount = s.FrameCount,
				Mode = s.Mode.ToString(),
				LcdEnabled = s.LcdEnabled
			});
		}

		public string? GetRomHeader()
		{
			return null;
		}
	}
}
