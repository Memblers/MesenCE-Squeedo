using CommunityToolkit.Mvvm.ComponentModel;
using Mesen.Interop;
using Mesen.Utilities;
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Mesen.Debugger.StatusViews;

public unsafe partial class Pic18StatusViewModel : BaseConsoleStatusViewModel
{
[DllImport("MesenCore.dll", EntryPoint = "GetPic18CpuRegisters")]
private static extern void GetPic18CpuRegistersNative(IntPtr state);

private static unsafe Pic18CpuRegisters GetPic18CpuRegisters()
{
Pic18CpuRegisters state = new();
GetPic18CpuRegistersNative((IntPtr)(&state));
return state;
}

[ObservableProperty] public partial UInt32 RegPC { get; set; }
[ObservableProperty] public partial byte RegW { get; set; }
[ObservableProperty] public partial byte RegBSR { get; set; }
[ObservableProperty] public partial byte RegSTATUS { get; set; }
[ObservableProperty] public partial byte RegINTCON { get; set; }
[ObservableProperty] public partial byte RegSTKPTR { get; set; }
[ObservableProperty] public partial UInt32 RegFSR0 { get; set; }
[ObservableProperty] public partial UInt32 RegFSR1 { get; set; }
[ObservableProperty] public partial UInt32 RegFSR2 { get; set; }
[ObservableProperty] public partial UInt32 RegTBLPTR { get; set; }
[ObservableProperty] public partial byte RegTABLAT { get; set; }
[ObservableProperty] public partial UInt16 RegPROD { get; set; }
[ObservableProperty] public partial byte RegRCON { get; set; }
[ObservableProperty] public partial string StackPreview { get; set; } = "";

// STATUS flags
[ObservableProperty] public partial bool FlagC { get; set; }
[ObservableProperty] public partial bool FlagDC { get; set; }
[ObservableProperty] public partial bool FlagZ { get; set; }
[ObservableProperty] public partial bool FlagOV { get; set; }
[ObservableProperty] public partial bool FlagN { get; set; }

// Interrupt flags (read-only)
[ObservableProperty] public partial byte RegINTCON2 { get; set; }
[ObservableProperty] public partial byte RegINTCON3 { get; set; }
[ObservableProperty] public partial byte RegPIR1 { get; set; }
[ObservableProperty] public partial byte RegPIR2 { get; set; }
[ObservableProperty] public partial byte RegPIE1 { get; set; }
[ObservableProperty] public partial byte RegPIE2 { get; set; }

// INTCON flags
[ObservableProperty] public partial bool FlagGIE { get; set; }
[ObservableProperty] public partial bool FlagPEIE { get; set; }
[ObservableProperty] public partial bool FlagTMR0IE { get; set; }
[ObservableProperty] public partial bool FlagINT0IE { get; set; }
[ObservableProperty] public partial bool FlagTMR0IF { get; set; }
[ObservableProperty] public partial bool FlagINT0IF { get; set; }

// PIR1 flags
[ObservableProperty] public partial bool FlagPSPIF { get; set; }
[ObservableProperty] public partial bool FlagTXIF { get; set; }
[ObservableProperty] public partial bool FlagRCIF { get; set; }
[ObservableProperty] public partial bool FlagADIF { get; set; }
[ObservableProperty] public partial bool FlagTMR1IF { get; set; }

// PIE1 flags
[ObservableProperty] public partial bool FlagPSPIE { get; set; }
[ObservableProperty] public partial bool FlagTXIE { get; set; }
[ObservableProperty] public partial bool FlagRCIE { get; set; }

public Pic18StatusViewModel()
{
this.ObserveProp([
nameof(FlagC), nameof(FlagDC), nameof(FlagZ), nameof(FlagOV), nameof(FlagN)
], () => UpdateFlags());
}

private void UpdateFlags()
{
RegSTATUS = (byte)(
(FlagC ? 0x01 : 0) |
(FlagDC ? 0x02 : 0) |
(FlagZ ? 0x04 : 0) |
(FlagOV ? 0x08 : 0) |
(FlagN ? 0x10 : 0)
);
}

protected override void InternalUpdateUiState()
{
Pic18CpuRegisters cpu = GetPic18CpuRegisters();
UpdateCycleCount((UInt64)cpu.CycleCount);

RegPC = cpu.PC;
RegW = cpu.W;
RegBSR = cpu.BSR;
RegSTATUS = cpu.STATUS;
RegINTCON = cpu.INTCON;
RegSTKPTR = cpu.STKPTR;
RegFSR0 = cpu.FSR[0];
RegFSR1 = cpu.FSR[1];
RegFSR2 = cpu.FSR[2];
RegTBLPTR = cpu.TBLPTR;
RegTABLAT = cpu.TABLAT;
RegPROD = (UInt16)((cpu.PRODH << 8) | cpu.PRODL);
RegRCON = cpu.RCON;

// Interrupt registers
RegINTCON2 = cpu.INTCON2;
RegINTCON3 = cpu.INTCON3;
RegPIR1 = cpu.PIR1;
RegPIR2 = cpu.PIR2;
RegPIE1 = cpu.PIE1;
RegPIE2 = cpu.PIE2;

FlagC = (cpu.STATUS & 0x01) != 0;
FlagDC = (cpu.STATUS & 0x02) != 0;
FlagZ = (cpu.STATUS & 0x04) != 0;
FlagOV = (cpu.STATUS & 0x08) != 0;
FlagN = (cpu.STATUS & 0x10) != 0;

// INTCON flags
FlagGIE = (cpu.INTCON & 0x80) != 0;
FlagPEIE = (cpu.INTCON & 0x40) != 0;
FlagTMR0IE = (cpu.INTCON & 0x20) != 0;
FlagINT0IE = (cpu.INTCON & 0x10) != 0;
FlagTMR0IF = (cpu.INTCON & 0x04) != 0;
FlagINT0IF = (cpu.INTCON & 0x02) != 0;

// PIR1 flags
FlagPSPIF = (cpu.PIR1 & 0x80) != 0;
FlagTXIF = (cpu.PIR1 & 0x10) != 0;
FlagRCIF = (cpu.PIR1 & 0x20) != 0;
FlagADIF = (cpu.PIR1 & 0x40) != 0;
FlagTMR1IF = (cpu.PIR1 & 0x01) != 0;

// PIE1 flags
FlagPSPIE = (cpu.PIE1 & 0x80) != 0;
FlagTXIE = (cpu.PIE1 & 0x10) != 0;
FlagRCIE = (cpu.PIE1 & 0x20) != 0;

StringBuilder sb = new();
uint sp = (uint)(cpu.STKPTR & 0x1F);
for(int i = (int)sp; i >= 0 && i < 31; i--) {
sb.Append($"${cpu.Stack[i] & 0x1FFFFF:X5} ");
}
StackPreview = sb.ToString().TrimEnd();
}

protected override void InternalUpdateConsoleState()
{
// Read-write not supported for PIC18 status panel yet
}
}