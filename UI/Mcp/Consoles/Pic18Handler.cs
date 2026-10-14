using Mesen.Interop;
using Mesen.Mcp.Tools;
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Mesen.Mcp.Consoles
{
	public class Pic18Handler : IConsoleHandler
	{
		[DllImport("MesenCore.dll", EntryPoint = "GetPic18CpuRegisters")]
		private static extern void GetPic18CpuRegistersNative(IntPtr state);

		private static unsafe Pic18CpuRegisters GetPic18CpuRegisters()
		{
			Pic18CpuRegisters state = new();
			GetPic18CpuRegistersNative((IntPtr)(&state));
			return state;
		}

		public Dictionary<string, string>? GetRegisters(CpuType cpu)
		{
			Pic18CpuRegisters s = GetPic18CpuRegisters();
			ushort fsr0, fsr1, fsr2;
			string stack;
			unsafe {
				fsr0 = s.FSR[0];
				fsr1 = s.FSR[1];
				fsr2 = s.FSR[2];

				//Stack entries are word addresses - show byte addresses to match PC/disassembly
				List<string> entries = new();
				for(int i = (int)(s.STKPTR & 0x1F) - 1; i >= 0; i--) {
					entries.Add("$" + ((s.Stack[i] & 0x1FFFFF) * 2).ToString("X5"));
				}
				stack = string.Join(" ", entries);
			}
			return new Dictionary<string, string> {
				["PC"] = "$" + (s.PC * 2).ToString("X4"),
				["W"] = "$" + s.W.ToString("X2"),
				["BSR"] = "$" + s.BSR.ToString("X2"),
				["STATUS"] = "$" + s.STATUS.ToString("X2"),
				["STKPTR"] = "$" + s.STKPTR.ToString("X2"),
				["FSR0"] = "$" + fsr0.ToString("X4"),
				["FSR1"] = "$" + fsr1.ToString("X4"),
				["FSR2"] = "$" + fsr2.ToString("X4"),
				["TBLPTR"] = "$" + s.TBLPTR.ToString("X6"),
				["TABLAT"] = "$" + s.TABLAT.ToString("X2"),
				["PROD"] = "$" + ((s.PRODH << 8) | s.PRODL).ToString("X4"),
				["INTCON"] = "$" + s.INTCON.ToString("X2"),
				["RCON"] = "$" + s.RCON.ToString("X2"),
				["stack"] = stack,
				["flags"] = FormatFlagsPic18(s.STATUS)
			};
		}

		public string SerializePpuState(CpuType cpu)
		{
			return "{}";
		}

		public string? GetRomHeader()
		{
			return null;
		}

		private static string FormatFlagsPic18(byte status)
		{
			return string.Concat(
				(status & 0x10) != 0 ? "N" : "n",  // Negative
				(status & 0x08) != 0 ? "O" : "o",  // Overflow
				(status & 0x02) != 0 ? "D" : "d",  // Digit Carry
				(status & 0x01) != 0 ? "C" : "c",  // Carry
				(status & 0x04) != 0 ? "Z" : "z"   // Zero
			);
		}
	}
}