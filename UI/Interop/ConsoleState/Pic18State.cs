using System;
using System.Runtime.InteropServices;

namespace Mesen.Interop;

// Lightweight PIC18 registers-only state (no 2MB memory arrays)
// Uses fixed arrays to be blittable for P/Invoke pinning
[StructLayout(LayoutKind.Sequential)]
public unsafe struct Pic18CpuRegisters
{
public UInt32 PC;
public byte W;
public byte BSR;
public byte STATUS;
public byte INTCON;
public byte STKPTR;
public byte _pad0;

public fixed UInt32 Stack[31];
public fixed UInt16 FSR[3];

public UInt32 TBLPTR;
public byte TABLAT;
public byte PRODH;
public byte PRODL;
public byte PCLATU;
public byte PCLATH;
public byte RCON;
public byte INTCON2;
public byte INTCON3;
public byte PIR1;
public byte PIR2;
public byte PIE1;
public byte PIE2;

public Int64 CycleCount;
}