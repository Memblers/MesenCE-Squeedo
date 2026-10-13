#pragma once
#include <cstdint>

// Forward declaration — only a pointer is held, so the 2MB Pic18CpuState stays out of this header.
struct Pic18CpuState;

// Crash diagnostics for the Squeedo PIC18 core.
//
// The managed heap can be corrupted by a native overrun, after which the process
// dies with a hard access violation somewhere unrelated (e.g. inside coreclr). A
// running trace can't capture the trigger because the evidence is gone by the time
// the AV fires. This module keeps a tiny, always-on record of the most recent PIC
// activity in static storage (immune to heap corruption) and, on Windows, installs a
// vectored exception handler that writes a self-contained dump file the moment an AV
// occurs — before the process dies with the evidence.
//
// Recording is intentionally cheap: two ring writes per instruction / PSP access, so
// it can stay enabled in release builds with negligible cost.
namespace Pic18CrashDiag
{
	// Registers the platform crash handler (Windows vectored exception handler).
	// Idempotent and a no-op on non-Windows platforms. Safe to call from anywhere.
	void Init();

	// Point the dumper at the live PIC18 state so register values can be captured in
	// the dump. Read best-effort at crash time (after the rings, which are always safe).
	void SetCpuState(Pic18CpuState* state);

	// Set the dump file path (defaults to "pic18_crash_dump.txt" in the working dir).
	void SetDumpPath(const char* path);

	// Ring-buffer recorders. pc is the PIC word address, opcode the fetched word.
	void RecordInstruction(uint32_t pc, uint16_t opcode);

	// Records an NES-side PSP access (regAddr is the 5-bit PSP register, isWrite for
	// NES writes vs reads).
	void RecordPspAccess(uint8_t regAddr, uint8_t value, bool isWrite);
}
