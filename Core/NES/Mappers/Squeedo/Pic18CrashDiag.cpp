#include "pch.h"
#include "NES/Mappers/Squeedo/Pic18CrashDiag.h"
#include "NES/Mappers/Squeedo/Pic18Types.h"

#include <cstring>

namespace Pic18CrashDiag
{
	// ---- Ring buffers in static storage (immune to heap corruption) ----
	static constexpr int InstrRingSize = 64;
	static constexpr int PspRingSize = 48;

	struct InstrEntry { uint32_t pc; uint16_t opcode; };
	struct PspEntry { uint8_t reg; uint8_t value; uint8_t isWrite; };

	static InstrEntry s_instrRing[InstrRingSize] = {};
	static unsigned s_instrHead = 0;   // next write slot
	static unsigned s_instrCount = 0;

	static PspEntry s_pspRing[PspRingSize] = {};
	static unsigned s_pspHead = 0;
	static unsigned s_pspCount = 0;

	static Pic18CpuState* s_state = nullptr;
	static bool s_initialized = false;
	static char s_dumpPath[512] = "pic18_crash_dump.txt";

	void SetCpuState(Pic18CpuState* state) { s_state = state; }

	void SetDumpPath(const char* path)
	{
		if(!path) return;
		size_t n = std::strlen(path);
		if(n >= sizeof(s_dumpPath)) n = sizeof(s_dumpPath) - 1;
		std::memcpy(s_dumpPath, path, n);
		s_dumpPath[n] = 0;
	}

	void RecordInstruction(uint32_t pc, uint16_t opcode)
	{
		s_instrRing[s_instrHead].pc = pc;
		s_instrRing[s_instrHead].opcode = opcode;
		s_instrHead = (s_instrHead + 1) % InstrRingSize;
		if(s_instrCount < InstrRingSize) s_instrCount++;
	}

	void RecordPspAccess(uint8_t regAddr, uint8_t value, bool isWrite)
	{
		s_pspRing[s_pspHead].reg = regAddr;
		s_pspRing[s_pspHead].value = value;
		s_pspRing[s_pspHead].isWrite = isWrite ? 1 : 0;
		s_pspHead = (s_pspHead + 1) % PspRingSize;
		if(s_pspCount < PspRingSize) s_pspCount++;
	}

	// ---- Manual text formatting (no CRT stdio: safe to use inside a fault handler) ----
	static char* AppendStr(char* p, const char* s)
	{
		while(*s) { *p++ = *s++; }
		return p;
	}

	static char* AppendHex(char* p, uint64_t v, int digits)
	{
		static const char* hex = "0123456789ABCDEF";
		for(int i = digits - 1; i >= 0; i--) {
			*p++ = hex[(v >> (i * 4)) & 0xF];
		}
		return p;
	}

	static char* AppendDec(char* p, uint64_t v)
	{
		char tmp[20];
		int n = 0;
		if(v == 0) { tmp[n++] = '0'; }
		while(v > 0 && n < 20) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
		while(n > 0) { *p++ = tmp[--n]; }
		return p;
	}

	// ---- Dump writing ----
	// Writes one contiguous text block with the Win32 API directly (no CRT streams).
	static void WriteDumpFile(const char* data, unsigned len);

	static void BuildAndWriteDump(unsigned long code, void* exRecord)
	{
		// EXCEPTION_RECORD layout (x64 ABI compatible subset we need).
		// For ACCESS_VIOLATION: ExceptionInformation[0] = 0 read / 1 write / 8 DEP,
		//                        ExceptionInformation[1] = faulting virtual address.
		unsigned long* er = (unsigned long*)exRecord;
		unsigned long exceptionCode = er[0];
		void* exceptionAddress = nullptr;
		unsigned long numParams = 0;
		unsigned long long faultAddr = 0;
		unsigned long long accessKind = 0;

		// Offsets into EXCEPTION_RECORD (x64):
		//   +0x00 ExceptionCode (ulong)
		//   +0x04 ExceptionFlags (ulong)
		//   +0x08 ExceptionRecord (ptr)
		//   +0x10 ExceptionAddress (ptr)
		//   +0x18 NumberParameters (ulong)
		//   +0x20 ExceptionInformation[15] (ulong64)
		exceptionAddress = *(void**)((char*)exRecord + 0x10);
		numParams = *(unsigned long*)((char*)exRecord + 0x18);
		if(numParams >= 2) {
			accessKind = *(unsigned long long*)((char*)exRecord + 0x20);
			faultAddr = *(unsigned long long*)((char*)exRecord + 0x28);
		}

		static char buf[32768];
		char* p = buf;

		p = AppendStr(p, "=== MesenCE Squeedo PIC18 crash dump ===\r\n");
		p = AppendStr(p, "ExceptionCode=0x");
		p = AppendHex(p, exceptionCode, 8);
		p = AppendStr(p, "  ExceptionAddress=0x");
		p = AppendHex(p, (uint64_t)exceptionAddress, 16);
		p = AppendStr(p, "\r\n");
		if(exceptionCode == 0xC0000005 && numParams >= 2) {
			p = AppendStr(p, "AV access: ");
			p = AppendStr(p, accessKind == 0 ? "READ" : accessKind == 1 ? "WRITE" : accessKind == 8 ? "DEP" : "?");
			p = AppendStr(p, "  FaultAddr=0x");
			p = AppendHex(p, faultAddr, 16);
			p = AppendStr(p, "\r\n");
		}
		p = AppendStr(p, "ExceptionCode 0x0C0000005=AV 0xC000001D=ILL 0xC00000FD=STKOVF 0xC0000094=DIV0\r\n\r\n");

		// PIC18 registers (best-effort: live state may be corrupted; this runs after the
		// rings are formatted, so a re-fault here only loses this section).
		p = AppendStr(p, "--- PIC18 registers ---\r\n");
		if(s_state) {
			p = AppendStr(p, "PC=0x");     p = AppendHex(p, s_state->PC, 6);
			p = AppendStr(p, " W=0x");     p = AppendHex(p, s_state->W, 2);
			p = AppendStr(p, " STATUS=0x");p = AppendHex(p, s_state->STATUS, 2);
			p = AppendStr(p, " BSR=0x");   p = AppendHex(p, s_state->BSR, 2);
			p = AppendStr(p, " INTCON=0x");p = AppendHex(p, s_state->INTCON, 2);
			p = AppendStr(p, " RCON=0x");  p = AppendHex(p, s_state->RCON, 2);
			p = AppendStr(p, "\r\n");
			p = AppendStr(p, "STKPTR=0x"); p = AppendHex(p, s_state->STKPTR, 2);
			p = AppendStr(p, " TBLPTR=0x");p = AppendHex(p, s_state->TBLPTR, 6);
			p = AppendStr(p, " TABLAT=0x");p = AppendHex(p, s_state->TABLAT, 2);
			p = AppendStr(p, " PRODH:DL=0x"); p = AppendHex(p, s_state->PRODH, 2); p = AppendHex(p, s_state->PRODL, 2);
			p = AppendStr(p, "\r\n");
			p = AppendStr(p, "FSR0=0x");   p = AppendHex(p, s_state->FSR[0], 3);
			p = AppendStr(p, " FSR1=0x");  p = AppendHex(p, s_state->FSR[1], 3);
			p = AppendStr(p, " FSR2=0x");  p = AppendHex(p, s_state->FSR[2], 3);
			p = AppendStr(p, " PCLATU:H=0x"); p = AppendHex(p, s_state->PCLATU, 2); p = AppendHex(p, s_state->PCLATH, 2);
			p = AppendStr(p, "\r\n");
			p = AppendStr(p, "CycleCount="); p = AppendDec(p, (uint64_t)s_state->CycleCount);
			p = AppendStr(p, "  PspIbf="); p = AppendDec(p, s_state->PspIbf ? 1 : 0);
			p = AppendStr(p, " PspObf="); p = AppendDec(p, s_state->PspObf ? 1 : 0);
			p = AppendStr(p, " PspIbov="); p = AppendDec(p, s_state->PspIbov ? 1 : 0);
			p = AppendStr(p, " IRQ="); p = AppendDec(p, s_state->InterruptPending ? 1 : 0);
			p = AppendStr(p, "\r\n");
			// Hardware stack
			p = AppendStr(p, "Stack (STKPTR levels, top last): ");
			unsigned levels = s_state->STKPTR & 0x1F;
			if(levels > 31) levels = 31;
			for(unsigned i = 0; i < levels; i++) {
				p = AppendStr(p, "\r\n  ["); p = AppendDec(p, i); p = AppendStr(p, "] 0x");
				p = AppendHex(p, s_state->Stack[i], 6);
			}
			p = AppendStr(p, "\r\n");
		} else {
			p = AppendStr(p, "(no PIC18 state pointer registered)\r\n");
		}

		// Instruction ring
		p = AppendStr(p, "\r\n--- Last PIC18 instructions (oldest first) ---\r\n");
		unsigned start = (s_instrHead + InstrRingSize - s_instrCount) % InstrRingSize;
		for(unsigned i = 0; i < s_instrCount; i++) {
			unsigned idx = (start + i) % InstrRingSize;
			p = AppendStr(p, "  PC=0x"); p = AppendHex(p, s_instrRing[idx].pc, 6);
			p = AppendStr(p, "  op=0x"); p = AppendHex(p, s_instrRing[idx].opcode, 4);
			p = AppendStr(p, "\r\n");
		}

		// PSP ring
		p = AppendStr(p, "\r\n--- Last NES<->PSP accesses (oldest first) ---\r\n");
		start = (s_pspHead + PspRingSize - s_pspCount) % PspRingSize;
		for(unsigned i = 0; i < s_pspCount; i++) {
			unsigned idx = (start + i) % PspRingSize;
			p = AppendStr(p, "  ");
			p = AppendStr(p, s_pspRing[idx].isWrite ? "NES->PIC wr" : "NES<-PIC rd");
			p = AppendStr(p, " reg=0x"); p = AppendHex(p, s_pspRing[idx].reg, 2);
			p = AppendStr(p, " val=0x"); p = AppendHex(p, s_pspRing[idx].value, 2);
			p = AppendStr(p, "\r\n");
		}

		p = AppendStr(p, "\r\n=== end of dump ===\r\n");
		WriteDumpFile(buf, (unsigned)(p - buf));
		(void)code;
	}

#ifdef _WIN32
	// ---- Win32 API declarations (extern "C", no <windows.h>: Core uses /Za) ----
	// This mirrors Core/Shared/SerialPort.cpp. On x64 all calling conventions are the
	// same so no __stdcall is needed. kernel32.lib is linked by default.
	extern "C" {
		void* CreateFileA(const char* lpFileName, unsigned long dwDesiredAccess, unsigned long dwShareMode, void* lpSecurityAttributes, unsigned long dwCreationDisposition, unsigned long dwFlagsAndAttributes, void* hTemplateFile);
		int   WriteFile(void* hFile, const void* lpBuffer, unsigned long nNumberOfBytesToWrite, unsigned long* lpNumberOfBytesWritten, void* lpOverlapped);
		int   CloseHandle(void* hObject);
		void* AddVectoredExceptionHandler(unsigned long First, long (*Handler)(void* exceptionInfo));
	}

	#pragma comment(lib, "kernel32.lib")

	static const long long INVALID_HANDLE_VALUE_LL = -1;
	static constexpr unsigned long _GENERIC_WRITE = 0x40000000;
	static constexpr unsigned long _CREATE_ALWAYS = 2;
	static constexpr unsigned long _FILE_ATTRIBUTE_NORMAL = 0x80;

	static void WriteDumpFile(const char* data, unsigned len)
	{
		void* h = CreateFileA(s_dumpPath, _GENERIC_WRITE, 0, nullptr, _CREATE_ALWAYS, _FILE_ATTRIBUTE_NORMAL, nullptr);
		if(h == (void*)INVALID_HANDLE_VALUE_LL) {
			return;
		}
		unsigned long written = 0;
		WriteFile(h, data, len, &written, nullptr);
		CloseHandle(h);
	}

	// Vectored exception handler: dump on the crash we care about, then let the
	// process continue searching so it still terminates and preserves the crash.
	static long CrashHandler(void* exceptionInfo)
	{
		static volatile long s_inDump = 0;
		if(s_inDump) {
			return 0;  // EXCEPTION_CONTINUE_SEARCH (avoid recursion if the dump re-faults)
		}

		// EXCEPTION_POINTERS: { EXCEPTION_RECORD* ExceptionRecord; CONTEXT* ContextRecord; }
		char* ep = (char*)exceptionInfo;
		char* er = *(char**)(ep + 0x00);
		unsigned long code = *(unsigned long*)(er + 0x00);

		// Only handle fatal faults: access violation, illegal instruction, stack
		// overflow, integer divide-by-zero. Everything else keeps normal behavior.
		if(code != 0xC0000005 && code != 0xC000001D && code != 0xC00000FD && code != 0xC0000094) {
			return 0;
		}

		s_inDump = 1;
		BuildAndWriteDump(code, er);
		s_inDump = 0;
		return 0;  // EXCEPTION_CONTINUE_SEARCH
	}
#else
	static void WriteDumpFile(const char* data, unsigned len)
	{
		// Non-Windows: write with the CRT as a fallback (no vectored handler available).
		// This won't survive a hard crash on Linux/macOS but keeps the recorder usable.
		(void)data; (void)len;
	}
#endif

	void Init()
	{
#ifdef _WIN32
		if(!s_initialized) {
			s_initialized = true;
			AddVectoredExceptionHandler(1, CrashHandler);  // First=1 → run before other handlers
		}
#endif
	}
}
