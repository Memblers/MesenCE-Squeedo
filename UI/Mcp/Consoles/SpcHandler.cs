using Mesen.Debugger;
using Mesen.Interop;
using Mesen.Mcp.Tools;
using ModelContextProtocol;
using System.Collections.Generic;

namespace Mesen.Mcp.Consoles
{
	public class SpcHandler : IConsoleHandler
	{
		public Dictionary<string, string>? GetRegisters(CpuType cpu)
		{
			SpcState s = DebugApi.GetCpuState<SpcState>(cpu);
			return new Dictionary<string, string> {
				["A"] = "$" + s.A.ToString("X2"),
				["X"] = "$" + s.X.ToString("X2"),
				["Y"] = "$" + s.Y.ToString("X2"),
				["SP"] = "$" + s.SP.ToString("X2"),
				["PC"] = "$" + s.PC.ToString("X4"),
				["PS"] = "$" + ((byte)s.PS).ToString("X2")
			};
		}

		public string SerializePpuState(CpuType cpu)
		{
			throw new McpException("PPU state not available for SPC.");
		}

		public string? GetRomHeader()
		{
			return null;
		}
	}
}
