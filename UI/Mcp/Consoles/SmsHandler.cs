using Mesen.Debugger;
using Mesen.Interop;
using Mesen.Mcp.Models;
using Mesen.Mcp.Tools;
using System.Collections.Generic;

namespace Mesen.Mcp.Consoles
{
	public class SmsHandler : IConsoleHandler
	{
		public Dictionary<string, string>? GetRegisters(CpuType cpu)
		{
			SmsCpuState s = DebugApi.GetCpuState<SmsCpuState>(cpu);
			return new Dictionary<string, string> {
				["A"] = "$" + s.A.ToString("X2"),
				["B"] = "$" + s.B.ToString("X2"),
				["C"] = "$" + s.C.ToString("X2"),
				["D"] = "$" + s.D.ToString("X2"),
				["E"] = "$" + s.E.ToString("X2"),
				["H"] = "$" + s.H.ToString("X2"),
				["L"] = "$" + s.L.ToString("X2"),
				["Flags"] = "$" + s.Flags.ToString("X2"),
				["SP"] = "$" + s.SP.ToString("X4"),
				["PC"] = "$" + s.PC.ToString("X4"),
				["IX"] = "$" + ((s.IXH << 8) | s.IXL).ToString("X4"),
				["IY"] = "$" + ((s.IYH << 8) | s.IYL).ToString("X4"),
				["I"] = "$" + s.I.ToString("X2"),
				["R"] = "$" + s.R.ToString("X2"),
				["IM"] = s.IM.ToString(),
				["IFF1"] = s.IFF1.ToString(),
				["IFF2"] = s.IFF2.ToString()
			};
		}

		public string SerializePpuState(CpuType cpu)
		{
			SmsVdpState s = (SmsVdpState)DebugApi.GetPpuState(cpu);
			return McpToolHelper.Serialize(new SmsPpuStateResponse {
				CpuType = "Sms",
				Scanline = s.Scanline,
				Cycle = s.Cycle,
				FrameCount = s.FrameCount,
				VCounter = s.VCounter,
				HorizontalScroll = s.HorizontalScroll,
				VerticalScroll = s.VerticalScroll
			});
		}

		public string? GetRomHeader()
		{
			return null;
		}
	}
}
