using Mesen.Interop;
using System.Collections.Generic;

namespace Mesen.Mcp.Consoles
{
	public interface IConsoleHandler
	{
		Dictionary<string, string>? GetRegisters(CpuType cpu);
		string SerializePpuState(CpuType cpu);
		string? GetRomHeader();
	}
}
