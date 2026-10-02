using Mesen.Debugger;
using Mesen.Interop;
using Mesen.Mcp.Models;
using Mesen.Mcp.Tools;
using System;
using System.Collections.Generic;


namespace Mesen.Mcp.Consoles
{
	public class NesHandler : IConsoleHandler
	{
		public Dictionary<string, string>? GetRegisters(CpuType cpu)
		{
			NesCpuState s = DebugApi.GetCpuState<NesCpuState>(cpu);
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
			NesPpuState s = (NesPpuState)DebugApi.GetPpuState(cpu);
			return McpToolHelper.Serialize(new NesPpuStateResponse {
				CpuType = "Nes",
				Scanline = s.Scanline,
				Cycle = s.Cycle,
				FrameCount = s.FrameCount,
				VerticalBlank = s.StatusFlags.VerticalBlank,
				Sprite0Hit = s.StatusFlags.Sprite0Hit,
				SpriteOverflow = s.StatusFlags.SpriteOverflow,
				VideoRamAddr = "$" + s.VideoRamAddr.ToString("X4"),
				ScrollX = s.ScrollX
			});
		}

		public string? GetRomHeader()
		{
			byte[] header = DebugApi.GetRomHeader();
			if(header.Length < 16 || header[0] != 0x4E || header[1] != 0x45 || header[2] != 0x53 || header[3] != 0x1A) {
				return null;
			}

			bool isNes2 = (header[7] & 0x0C) == 0x08;
			int mapper = isNes2
				? ((header[8] & 0x0F) << 8) | (header[7] & 0xF0) | (header[6] >> 4)
				: (header[7] & 0xF0) | (header[6] >> 4);
			int subMapper = isNes2 ? (header[8] & 0xF0) >> 4 : 0;

			int prgSize;
			int chrSize;
			if(isNes2) {
				prgSize = ((header[9] & 0x0F) == 0x0F)
					? (int)(Math.Pow(2, header[4] >> 2) * ((header[4] & 0x03) * 2 + 1))
					: (((header[9] & 0x0F) << 8) | header[4]) * 16384;
				chrSize = ((header[9] & 0xF0) == 0xF0)
					? (int)(Math.Pow(2, header[5] >> 2) * ((header[5] & 0x03) * 2 + 1))
					: (((header[9] & 0xF0) >> 4 << 8) | header[5]) * 8192;
			} else {
				prgSize = header[4] * 16384;
				chrSize = header[5] * 8192;
			}

			string mirroring = (header[6] & 0x08) != 0 ? "FourScreen"
				: (header[6] & 0x01) != 0 ? "Vertical" : "Horizontal";

			return McpToolHelper.Serialize(new NesRomHeaderResponse {
				Format = isNes2 ? "NES 2.0" : "iNes",
				Mapper = mapper,
				SubMapper = subMapper > 0 ? subMapper : null,
				PrgRomSize = prgSize,
				ChrRomSize = chrSize,
				Mirroring = mirroring,
				Battery = (header[6] & 0x02) != 0,
				Trainer = (header[6] & 0x04) != 0
			});
		}
	}
}
