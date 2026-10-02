using Mesen.Debugger;
using Mesen.Interop;
using Mesen.Mcp.Models;
using Mesen.Mcp.Tools;
using System;
using System.Collections.Generic;
using System.Text;

namespace Mesen.Mcp.Consoles
{
	public class SnesHandler : IConsoleHandler
	{
		// Candidate base addresses for header detection (copier-header variants excluded —
		// Mesen strips those before exposing PrgRom, so only headerless offsets matter).
		private static readonly uint[] HeaderBases = { 0, 0x8000, 0x408000 };

		// Offset of SnesCartInformation within each bank (0x7FB0 from base)
		private const int CartInfoOffset = 0x7FB0;

		// Size of the SnesCartInformation struct (48 bytes: fields + vectors)
		private const int CartInfoSize = 48;

		public Dictionary<string, string>? GetRegisters(CpuType cpu)
		{
			SnesCpuState s = DebugApi.GetCpuState<SnesCpuState>(cpu);
			return new Dictionary<string, string> {
				["A"] = "$" + s.A.ToString("X4"),
				["X"] = "$" + s.X.ToString("X4"),
				["Y"] = "$" + s.Y.ToString("X4"),
				["SP"] = "$" + s.SP.ToString("X4"),
				["D"] = "$" + s.D.ToString("X4"),
				["DBR"] = "$" + s.DBR.ToString("X2"),
				["K"] = "$" + s.K.ToString("X2"),
				["PC"] = "$" + s.PC.ToString("X4"),
				["PS"] = "$" + ((byte)s.PS).ToString("X2"),
				["emulationMode"] = s.EmulationMode.ToString()
			};
		}

		public string SerializePpuState(CpuType cpu)
		{
			SnesPpuState s = (SnesPpuState)DebugApi.GetPpuState(cpu);
			return McpToolHelper.Serialize(new SnesPpuStateResponse {
				CpuType = "Snes",
				Scanline = s.Scanline,
				Cycle = s.Cycle,
				HClock = s.HClock,
				FrameCount = s.FrameCount,
				ForcedBlank = s.ForcedBlank,
				ScreenBrightness = s.ScreenBrightness,
				BgMode = s.BgMode,
				VramAddress = "$" + s.VramAddress.ToString("X4")
			});
		}

		public string? GetRomHeader()
		{
			int prgSize = DebugApi.GetMemorySize(MemoryType.SnesPrgRom);
			if(prgSize == 0) {
				return null;
			}

			byte[] prgRom = DebugApi.GetMemoryState(MemoryType.SnesPrgRom);

			int bestScore = -1;
			uint bestBase = 0;

			foreach(uint baseAddr in HeaderBases) {
				int score = GetHeaderScore(prgRom, prgSize, baseAddr);
				if(score >= 0 && score >= bestScore) {
					bestScore = score;
					bestBase = baseAddr;
				}
			}

			if(bestScore < 0) {
				return null;
			}

			uint headerOffset = bestBase + CartInfoOffset;
			if(headerOffset + CartInfoSize > prgSize) {
				return null;
			}

			// Parse the 48-byte SnesCartInformation struct
			// Struct layout (offsets relative to headerOffset):
			//   +$00  MakerCode[2]          — only valid in extended format ($FFDA == $33)
			//   +$02  GameCode[4]           — only valid in extended format
			//   +$06  Reserved[7]
			//   +$0D  ExpansionRamSize
			//   +$0E  SpecialVersion
			//   +$0F  CartridgeType (sub)
			//   +$10  CartName[21]          — game title (JIS X 0201 / ASCII)
			//   +$25  MapMode
			//   +$26  RomType (chipset)
			//   +$27  RomSize
			//   +$28  SramSize
			//   +$29  DestinationCode
			//   +$2A  Fixed value           — $33 = extended format, otherwise old developer ID
			//   +$2B  Version
			//   +$2C  ChecksumComplement[2]
			//   +$2E  Checksum[2]
			int o = (int)headerOffset;

			string title = Encoding.ASCII.GetString(prgRom, o + 0x10, 21).TrimEnd('\0', ' ');
			byte mapMode = prgRom[o + 0x25];
			byte romType = prgRom[o + 0x26];
			byte romSize = prgRom[o + 0x27];
			byte sramSize = prgRom[o + 0x28];
			byte destinationCode = prgRom[o + 0x29];
			byte fixedValue = prgRom[o + 0x2A];
			byte version = prgRom[o + 0x2B];
			ushort checksumComplement = (ushort)(prgRom[o + 0x2C] | (prgRom[o + 0x2D] << 8));
			ushort checksum = (ushort)(prgRom[o + 0x2E] | (prgRom[o + 0x2F] << 8));

			// Extended header: $FFDA == $33 means MakerCode/GameCode at $FFB0 are valid
			bool extendedFormat = fixedValue == 0x33;
			string? makerCode = null;
			string? gameCode = null;
			string? developerId = null;

			if(extendedFormat) {
				makerCode = Encoding.ASCII.GetString(prgRom, o, 2).TrimEnd('\0', ' ');
				gameCode = Encoding.ASCII.GetString(prgRom, o + 2, 4).TrimEnd('\0', ' ');
			} else {
				developerId = "$" + fixedValue.ToString("X2");
			}

			string mapModeName = DecodeMapMode(mapMode);
			bool fastRom = (mapMode & 0x10) != 0;

			int romSizeKB = romSize < 0x10 ? (1 << romSize) : 0;
			int sramSizeKB = sramSize < 0x08 ? (sramSize > 0 ? (1 << sramSize) : 0) : 0;

			string region = DecodeRegion(destinationCode);
			string romTypeName = DecodeRomType(romType);

			return McpToolHelper.Serialize(new SnesRomHeaderResponse {
				Title = title,
				MapMode = "$" + mapMode.ToString("X2"),
				MapModeName = mapModeName,
				FastRom = fastRom,
				RomType = "$" + romType.ToString("X2"),
				RomTypeName = romTypeName,
				RomSizeKB = romSizeKB,
				SramSizeKB = sramSizeKB,
				Region = region,
				DestinationCode = "$" + destinationCode.ToString("X2"),
				DeveloperId = developerId,
				MakerCode = makerCode,
				GameCode = gameCode,
				Version = "1." + version,
				Checksum = "$" + checksum.ToString("X4"),
				ChecksumComplement = "$" + checksumComplement.ToString("X4"),
				ChecksumValid = (checksum + checksumComplement) == 0xFFFF,
				HeaderOffset = "$" + headerOffset.ToString("X6")
			});
		}

		private static int GetHeaderScore(byte[] prgRom, int prgSize, uint baseAddr)
		{
			if(prgSize < baseAddr + 0x8000) {
				return -1;
			}

			uint o = baseAddr + (uint)CartInfoOffset;
			byte mapMode = prgRom[o + 0x25];
			byte romType = prgRom[o + 0x26];
			byte romSize = prgRom[o + 0x27];
			byte sramSize = prgRom[o + 0x28];

			int score = 0;
			byte mode = (byte)(mapMode & ~0x10);
			if((mode == 0x20 || mode == 0x22) && baseAddr < 0x8000) {
				score++;
			} else if((mode == 0x21 || mode == 0x25) && baseAddr >= 0x8000) {
				score++;
			}

			if(romType < 0x08) {
				score++;
			}
			if(romSize < 0x10) {
				score++;
			}
			if(sramSize < 0x08) {
				score++;
			}

			ushort checksum = (ushort)(prgRom[o + 0x2E] | (prgRom[o + 0x2F] << 8));
			ushort complement = (ushort)(prgRom[o + 0x2C] | (prgRom[o + 0x2D] << 8));
			if(checksum + complement == 0xFFFF && checksum != 0 && complement != 0) {
				score += 8;
			}

			uint resetVectorAddr = baseAddr + 0x7FFC;
			uint resetVector = (uint)(prgRom[resetVectorAddr] | (prgRom[resetVectorAddr + 1] << 8));
			if(resetVector < 0x8000) {
				return -1;
			}

			byte op = prgRom[baseAddr + (resetVector & 0x7FFF)];
			if(op == 0x18 || op == 0x78 || op == 0x4C || op == 0x5C || op == 0x20 || op == 0x22 || op == 0x9C) {
				// CLI, SEI, JMP, JML, JSR, JSL, STZ
				score += 8;
			} else if(op == 0xC2 || op == 0xE2 || op == 0xA9 || op == 0xA2 || op == 0xA0) {
				// REP, SEP, LDA, LDX, LDY
				score += 4;
			} else if(op == 0x00 || op == 0xFF || op == 0xCC) {
				// BRK, SBC, CPY
				score -= 8;
			}

			return Math.Max(0, score);
		}

		private static string DecodeMapMode(byte mapMode)
		{
			// Bit 4 = speed (handled separately as FastRom), mask it out
			byte mode = (byte)(mapMode & 0x2F);
			return mode switch {
				0x20 => "LoROM",
				0x21 => "HiROM",
				0x22 => "ExLoROM",
				0x23 => "SA-1",
				0x25 => "ExHiROM",
				_ => "Unknown ($" + mode.ToString("X2") + ")"
			};
		}

		private static string DecodeRomType(byte romType)
		{
			// Low nibble: ROM/RAM/battery configuration
			// High nibble: coprocessor type (when low nibble >= $03)
			string[] configs = {
				"ROM",               // $x0
				"ROM + RAM",         // $x1
				"ROM + RAM + Battery", // $x2
				"ROM + Coprocessor", // $x3
				"ROM + Coprocessor + RAM", // $x4
				"ROM + Coprocessor + RAM + Battery", // $x5
				"ROM + Coprocessor + Battery"  // $x6
			};

			int lo = romType & 0x0F;
			int hi = romType >> 4;

			string config = lo < configs.Length ? configs[lo] : "Unknown config ($" + lo.ToString("X") + ")";

			if(lo < 3) {
				return config;
			}

			// Coprocessor identified by high nibble
			string coproc = hi switch {
				0x0 => "DSP",
				0x1 => "SuperFX",
				0x2 => "OBC-1",
				0x3 => "SA-1",
				0x4 => "SDD-1",
				0x5 => "S-RTC",
				0xE => "Other",
				0xF => "Custom",
				_ => "Unknown ($" + hi.ToString("X") + ")"
			};

			return config.Replace("Coprocessor", coproc);
		}

		private static string DecodeRegion(byte code)
		{
			return code switch {
				0x00 => "Japan",
				0x01 => "North America",
				0x02 => "Europe",
				0x03 => "Sweden/Scandinavia",
				0x04 => "Finland",
				0x05 => "Denmark",
				0x06 => "France",
				0x07 => "Netherlands",
				0x08 => "Spain",
				0x09 => "Germany",
				0x0A => "Italy",
				0x0B => "China",
				0x0C => "Indonesia",
				0x0D => "Korea",
				0x0E => "International",
				0x0F => "Canada",
				0x10 => "Brazil",
				0x11 => "Australia",
				_ => "Unknown ($" + code.ToString("X2") + ")"
			};
		}
	}
}
