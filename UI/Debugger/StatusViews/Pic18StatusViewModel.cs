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

[DllImport("MesenCore.dll", EntryPoint = "SetPic18CpuRegisters")]
private static extern void SetPic18CpuRegistersNative(IntPtr state);

private static unsafe Pic18CpuRegisters GetPic18CpuRegisters()
{
Pic18CpuRegisters state = new();
GetPic18CpuRegistersNative((IntPtr)(&state));
return state;
}

private static unsafe void SetPic18CpuRegisters(Pic18CpuRegisters state)
{
SetPic18CpuRegistersNative((IntPtr)(&state));
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

this.ObserveProp([
nameof(FlagGIE), nameof(FlagPEIE), nameof(FlagTMR0IE), nameof(FlagINT0IE), nameof(FlagTMR0IF), nameof(FlagINT0IF)
], () => UpdateIntconValue());

this.ObserveProp([
nameof(FlagPSPIF), nameof(FlagTXIF), nameof(FlagRCIF), nameof(FlagADIF), nameof(FlagTMR1IF),
nameof(FlagPSPIE), nameof(FlagTXIE), nameof(FlagRCIE)
], () => UpdateInterruptRegValues());
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

private void UpdateIntconValue()
{
//Only the 6 managed bits are rebuilt - GIE/PEIE/TMR0IE/INT0IE/TMR0IF/INT0IF (0xF6)
RegINTCON = (byte)((RegINTCON & ~0xF6) |
(FlagGIE ? 0x80 : 0) |
(FlagPEIE ? 0x40 : 0) |
(FlagTMR0IE ? 0x20 : 0) |
(FlagINT0IE ? 0x10 : 0) |
(FlagTMR0IF ? 0x04 : 0) |
(FlagINT0IF ? 0x02 : 0)
);
}

private void UpdateInterruptRegValues()
{
//PIR1 managed bits: PSPIF(7) ADIF(6) RCIF(5) TXIF(4) TMR1IF(0) = 0xF1
RegPIR1 = (byte)((RegPIR1 & ~0xF1) |
(FlagPSPIF ? 0x80 : 0) |
(FlagADIF ? 0x40 : 0) |
(FlagRCIF ? 0x20 : 0) |
(FlagTXIF ? 0x10 : 0) |
(FlagTMR1IF ? 0x01 : 0)
);

//PIE1 managed bits: PSPIE(7) RCIE(5) TXIE(4) = 0xB0
RegPIE1 = (byte)((RegPIE1 & ~0xB0) |
(FlagPSPIE ? 0x80 : 0) |
(FlagRCIE ? 0x20 : 0) |
(FlagTXIE ? 0x10 : 0)
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
for(int i = (int)sp - 1; i >= 0; i--) {
sb.Append($"${cpu.Stack[i] & 0x1FFFFF:X5} ");
}
StackPreview = sb.ToString().TrimEnd();
}

protected override void InternalUpdateConsoleState()
{
Pic18CpuRegisters cpu = GetPic18CpuRegisters();

cpu.PC = RegPC;
cpu.W = RegW;
cpu.BSR = RegBSR;
cpu.STATUS = RegSTATUS;
cpu.INTCON = RegINTCON;
cpu.STKPTR = RegSTKPTR;
cpu.FSR[0] = (ushort)RegFSR0;
cpu.FSR[1] = (ushort)RegFSR1;
cpu.FSR[2] = (ushort)RegFSR2;
cpu.TBLPTR = RegTBLPTR;
cpu.TABLAT = RegTABLAT;
cpu.PRODH = (byte)(RegPROD >> 8);
cpu.PRODL = (byte)(RegPROD & 0xFF);
cpu.RCON = RegRCON;
cpu.INTCON2 = RegINTCON2;
cpu.INTCON3 = RegINTCON3;
cpu.PIR1 = RegPIR1;
cpu.PIR2 = RegPIR2;
cpu.PIE1 = RegPIE1;
cpu.PIE2 = RegPIE2;

SetPic18CpuRegisters(cpu);
}
}