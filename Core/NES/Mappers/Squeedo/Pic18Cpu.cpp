#include "pch.h"
#include "NES/Mappers/Squeedo/Pic18Cpu.h"
#include "NES/Mappers/Squeedo/Pic18Peripherals.h"
#include "NES/Mappers/Squeedo/Pic18CrashDiag.h"
#include "Shared/Emulator.h"

Pic18Cpu::Pic18Cpu(Pic18CpuState& state) : _state(state)
{
}

void Pic18Cpu::Reset()
{
	// Reset vector is at word 0x0000 (firmware boots from $0000; bootloader dropped)
	_state.PC = 0x0000;
	_state.W = 0;
	_state.BSR = 0;
	_state.STATUS = 0;
	_state.INTCON = 0;
	_state.STKPTR = 0;
	_state.RCON = Pic18RconBits::POR | Pic18RconBits::BOR | Pic18RconBits::PD | Pic18RconBits::TO | Pic18RconBits::RI;
	_state.FSR[0] = 0;
	_state.FSR[1] = 0;
	_state.FSR[2] = 0;
	_state.TBLPTR = 0;
	_state.TABLAT = 0;
	_state.PRODH = 0;
	_state.PRODL = 0;
	_state.PCLATU = 0;
	_state.PCLATH = 0;
	_state.CycleCount = 0;
	_state.PspIbf = false;
	_state.PspObf = false;
	_state.PspPortDOutput = 0;
	_state.InterruptPending = false;
	_irqEntered = false;

	memset(_state.Stack, 0, sizeof(_state.Stack));
	memset(_state.StackEntryKind, 0, sizeof(_state.StackEntryKind));
	memset(_state.FastW, 0, sizeof(_state.FastW));
	memset(_state.FastStatus, 0, sizeof(_state.FastStatus));
	memset(_state.FastBsr, 0, sizeof(_state.FastBsr));
	memset(_state.Data, 0, sizeof(_state.Data));

	WriteData(Pic18Sfr::T0CON, 0xFF);
	WriteData(Pic18Sfr::TRISA, 0xFF);
	WriteData(Pic18Sfr::TRISB, 0xFF);
	WriteData(Pic18Sfr::TRISC, 0xFF);
	WriteData(Pic18Sfr::TRISD, 0xFF);
	WriteData(Pic18Sfr::TRISE, 0x07);
	WriteData(Pic18Sfr::ADCON1, 0x0F);
	WriteData(Pic18Sfr::CMCON, 0x07);
	WriteData(Pic18Sfr::PR2, 0xFF);
}

void Pic18Cpu::PushStack(uint32_t value, uint8_t kind)
{
	if((_state.STKPTR & Pic18StkptrBits::STKPTR_MASK) >= 31) {
		_state.STKPTR |= (1 << Pic18StkptrBits::STKFUL);
		return;
	}
	PIC_BOUNDS_ASSERT(_state.STKPTR & Pic18StkptrBits::STKPTR_MASK, 31);
	_state.Stack[_state.STKPTR & Pic18StkptrBits::STKPTR_MASK] = value & 0x1FFFFF;
	_state.StackEntryKind[_state.STKPTR & Pic18StkptrBits::STKPTR_MASK] = kind;
	_state.STKPTR++;
}

void Pic18Cpu::EnterInterrupt(uint32_t vector, uint8_t kind)
{
	// Fast register stack: hardware auto-saves WREG/STATUS/BSR on interrupt entry,
	// one shadow per stack level (the return address is pushed to this level)
	uint8_t level = _state.STKPTR & Pic18StkptrBits::STKPTR_MASK;
	if(level > 30) level = 30;
	PIC_BOUNDS_ASSERT(level, 31);
	_state.FastW[level] = _state.W;
	_state.FastStatus[level] = _state.STATUS;
	_state.FastBsr[level] = _state.BSR;

	// Record the entry so the debugger can push a matching callstack frame
	_irqReturnPc = _state.PC;
	_irqVector = vector;
	_irqEntered = true;

	PushStack(_state.PC, kind);
	_state.PC = vector;

	// Interrupt entry clears the enable bit of the interrupt's own level: GIEH on a
	// high-priority (or legacy) entry, GIEL on a low-priority entry. GIEH stays set
	// during a low-priority ISR so a high-priority interrupt can still preempt it.
	if(kind == Pic18StackEntryKind::LowInt) {
		_state.INTCON &= ~Pic18IntconBits::PEIE;
	} else {
		_state.INTCON &= ~Pic18IntconBits::GIE;
	}
	_state.RCON &= ~Pic18RconBits::PD;
}

uint32_t Pic18Cpu::PopStack()
{
	if((_state.STKPTR & Pic18StkptrBits::STKPTR_MASK) == 0) {
		_state.STKPTR |= (1 << Pic18StkptrBits::STKUNF);
		return 0;
	}
	_state.STKPTR--;
	return _state.Stack[_state.STKPTR & Pic18StkptrBits::STKPTR_MASK];
}

uint8_t Pic18Cpu::ReadData(uint16_t addr)
{
	addr &= 0xFFF;
	PIC_BOUNDS_ASSERT(addr, 4096);
	// Fast path: GPR (below SFR region) — direct array access, no virtual dispatch
	uint8_t v;
	if(addr >= Pic18Sfr::SfrBase && _peripherals) {
		v = _peripherals->ReadSfr(addr);
	} else {
		v = _state.Data[addr];
	}
	if(_emu) _emu->ProcessMemoryRead<CpuType::Pic18>(addr, v, MemoryOperationType::Read);
	return v;
}

uint8_t Pic18Cpu::PeekData(uint16_t addr)
{
	addr &= 0xFFF;
	if(addr >= Pic18Sfr::SfrBase && _peripherals) {
		return _peripherals->ReadSfr(addr, true);
	}
	return _state.Data[addr];
}

bool Pic18Cpu::WriteData(uint16_t addr, uint8_t value, bool fromDebug)
{
	addr &= 0xFFF;
	PIC_BOUNDS_ASSERT(addr, 4096);
	if(addr >= Pic18Sfr::SfrBase) {
		// SFR writes: WriteSfr raises the debug write hook (its top handles both the
		// direct SFR access and the resolved target of indirect POSTINC/INDF writes).
		bool written = _peripherals ? _peripherals->WriteSfr(addr, value, fromDebug) : true;
		if(written) {
			_state.Data[addr] = value;
		}
		return written;
	}
	// GPR direct write (hook first so a Lua callback can override the value)
	if(_emu && !Pic18Dbg::InWriteHook()) {
		Pic18Dbg::InWriteHook() = true;
		bool processWrite = _emu->ProcessMemoryWrite<CpuType::Pic18>(addr, value, MemoryOperationType::Write);
		Pic18Dbg::InWriteHook() = false;
		if(!processWrite && !fromDebug) {
			return false;  // frozen address - drop the write
		}
	}
	_state.Data[addr] = value;
	return true;
}

uint8_t Pic18Cpu::ReadProgram(uint32_t addr)
{
	return _state.Program[addr & 0x1FFFFF];
}

// Skip the instruction at the current PC (used by the skip-if instructions).
// On real PIC18 hardware a skip jumps over the WHOLE next instruction, even
// 2-word ones (MOVFF/LFSR/CALL/GOTO). A naive 1-word skip lands on the second
// word, which executes as a phantom opcode: 0xF0xx (usually benign) but an
// LFSR's second word is 0x0xxx = the specials range = PUSH/POP/RETFIE/
// RETURN/RESET! (crash class 2026-10-12: phantom RETFIE/PUSH wrecking the
// hardware stack). Match GetInstructionSize's 2-word formats here.
void Pic18Cpu::SkipInstruction()
{
	uint8_t lo = ReadProgram(_state.PC * 2);
	uint8_t hi = ReadProgram(_state.PC * 2 + 1);
	uint16_t op = ((uint16_t)hi << 8) | lo;
	if((op & 0xF000) == 0xC000 ||          // MOVFF
	   (op & 0xFF00) == 0xEE00 ||          // LFSR
	   (op & 0xFE00) == 0xEC00 ||          // CALL k,s
	   (op & 0xFF00) == 0xEF00) {          // GOTO
		_state.PC++;
	}
	_state.PC++;
}

void Pic18Cpu::WriteProgram(uint32_t addr, uint8_t value)
{
	_state.Program[addr & 0x1FFFFF] = value;
}

uint8_t Pic18Cpu::TableRead()
{
	return _state.Program[_state.TBLPTR & 0x1FFFFF];
}

void Pic18Cpu::TableWrite(uint8_t value)
{
	_state.Program[_state.TBLPTR & 0x1FFFFF] = value;
}

uint16_t Pic18Cpu::ResolveAddr(uint8_t f, bool accessBank)
{
	if(accessBank) {
		return GetAccessBankAddr(f);
	}
	return ((uint16_t)(_state.BSR << 8)) | f;
}

bool Pic18Cpu::CheckInterrupts()
{
	bool priorityEnabled = (_state.RCON & Pic18RconBits::IPEN) != 0;
	bool gie = (_state.INTCON & Pic18IntconBits::GIE) != 0;
	bool peie = (_state.INTCON & Pic18IntconBits::PEIE) != 0;

	uint8_t pir1 = _state.Data[Pic18Sfr::PIR1 & 0xFFF];
	uint8_t pie1 = _state.Data[Pic18Sfr::PIE1 & 0xFFF];
	uint8_t pir2 = _state.Data[Pic18Sfr::PIR2 & 0xFFF];
	uint8_t pie2 = _state.Data[Pic18Sfr::PIE2 & 0xFFF];
	uint8_t ipr1 = _state.Data[Pic18Sfr::IPR1 & 0xFFF];
	uint8_t ipr2 = _state.Data[Pic18Sfr::IPR2 & 0xFFF];
	uint8_t intcon2 = _state.Data[Pic18Sfr::INTCON2 & 0xFFF];
	uint8_t intcon3 = _state.Data[Pic18Sfr::INTCON3 & 0xFFF];

	bool hasHighPriority = false;
	bool hasLowPriority = false;
	// In legacy mode (IPEN=0) everything shares the 0x0008 vector: external/TMR0
	// sources need GIE only, peripheral sources additionally need PEIE.
	bool externalPending = false;
	bool peripheralPending = false;

	// TMR0/INT0/INT1/INT2 live in INTCON/INTCON3 and are not gated by PEIE.
	// TMR0, INT1, INT2 have priority bits; INT0 is always high priority.
	if((_state.INTCON & Pic18IntconBits::TMR0IF) && (_state.INTCON & Pic18IntconBits::TMR0IE)) {
		externalPending = true;
		if(priorityEnabled && !(intcon2 & Pic18IntconBits::TMR0IP)) hasLowPriority = true;
		else hasHighPriority = true;
	}
	if((_state.INTCON & Pic18IntconBits::INT0IF) && (_state.INTCON & Pic18IntconBits::INT0IE)) {
		externalPending = true;
		hasHighPriority = true;
	}
	if((intcon3 & Pic18IntconBits::INT1IF) && (intcon3 & Pic18IntconBits::INT1IE)) {
		externalPending = true;
		if(priorityEnabled && (intcon3 & Pic18IntconBits::INT1IP)) hasHighPriority = true;
		else hasLowPriority = true;
	}
	if((intcon3 & Pic18IntconBits::INT2IF) && (intcon3 & Pic18IntconBits::INT2IE)) {
		externalPending = true;
		if(priorityEnabled && (intcon3 & Pic18IntconBits::INT2IP)) hasHighPriority = true;
		else hasLowPriority = true;
	}

	// Peripheral sources: priority comes from IPR1/IPR2 (including IPR1.PSPIP for the PSP)
	uint8_t periphFlags1 = pir1 & pie1;
	uint8_t periphFlags2 = pir2 & pie2;
	if(periphFlags1 || periphFlags2) {
		peripheralPending = true;
		if(priorityEnabled) {
			if((periphFlags1 & ipr1) || (periphFlags2 & ipr2)) {
				hasHighPriority = true;
			} else {
				hasLowPriority = true;
			}
		} else {
			hasHighPriority = true;
		}
	}

	// Interrupt vectors are byte addresses from the PIC18 datasheet, converted to word addresses.
	// High priority: byte 0x0008 → word 0x0004
	// Low priority:  byte 0x0018 → word 0x000C
	bool taken = false;
	if(priorityEnabled) {
		// GIEH enables high-priority interrupts; low-priority interrupts additionally
		// need GIEL (which is also what prevents low priority from preempting a high ISR)
		if(hasHighPriority && gie) {
			EnterInterrupt(0x0004, Pic18StackEntryKind::HighInt);
			taken = true;
		} else if(hasLowPriority && gie && peie) {
			EnterInterrupt(0x000C, Pic18StackEntryKind::LowInt);
			taken = true;
		}
	} else {
		// Legacy mode: all interrupts share the 0x0008 vector
		if((externalPending && gie) || (peripheralPending && gie && peie)) {
			EnterInterrupt(0x0004, Pic18StackEntryKind::HighInt);
			taken = true;
		}
	}

	// Interrupts are level-sensitive: keep InterruptPending alive while any source is
	// still asserted (flag & enable), even when it is masked by GIE/PEIE or was just
	// taken and the ISR hasn't cleared the flag yet. This way a masked interrupt fires
	// as soon as it is unmasked instead of waiting for the next peripheral event.
	_state.InterruptPending = hasHighPriority || hasLowPriority;
	return taken;
}

int Pic18Cpu::Run(int cyclesToRun)
{
	int cyclesExecuted = 0;
	while(cyclesExecuted < cyclesToRun) {
		cyclesExecuted += ExecuteInstruction();
	}
	return cyclesExecuted;
}

int Pic18Cpu::ExecuteInstruction()
{
	// CheckInterrupts keeps InterruptPending in sync with the asserted (flag & enable)
	// sources, so an interrupt stays pending until it is taken or its flag is cleared.
	if(_state.InterruptPending && CheckInterrupts()) {
		// EnterInterrupt did the stack push and fast register shadow save
		_state.CycleCount += 2;
		return 2;
	}

	// PIC18 program memory is stored little-endian: byte[0]=low, byte[1]=high
	uint8_t lo = ReadProgram(_state.PC * 2);
	uint8_t hi = ReadProgram(_state.PC * 2 + 1);
	uint16_t opcode = ((uint16_t)hi << 8) | lo;
	Pic18CrashDiag::RecordInstruction(_state.PC, opcode);

	_state.PC++;
	int cycles = DecodeAndExecute(opcode);
	_state.CycleCount += cycles;
	return cycles;
}

int Pic18Cpu::DecodeAndExecute(uint16_t opcode)
{
	// Reset the peripherals' RMW cache at instruction boundaries so a
	// read-modify-write's cached indirect target can't leak into the next
	// instruction or an out-of-band (debugger/Lua) access.
	struct RmwGuard {
		Pic18Peripherals* p;
		explicit RmwGuard(Pic18Peripherals* x) : p(x) { if(p) p->ResetRmwState(); }
		~RmwGuard() { if(p) p->ResetRmwState(); }
	} rmwGuard(_peripherals);

	// Decode common fields
	// Note: d/a bits differ by instruction type:
	//   Byte-oriented: bit 9 = d (dest), bit 8 = a (access)
	//   Bit-oriented:  bits [11:9] = b, bit 8 = a (access)
	uint8_t f = opcode & 0xFF;
	uint8_t val, result;

	// ====================================================================
	// Dispatch on bits [15:12]
	// ====================================================================
	switch(opcode >> 12) {

	// ----------------------------------------------------------------
	// 0x0: Specials, MOVFF, literal ops, ADDWFC/MULWF/DECF
	// ----------------------------------------------------------------
	case 0x0: {
		// Single-word specials in lower byte
		if(opcode == 0x0000) return 1; // NOP
		if(opcode == 0x0003) { _state.RCON &= ~Pic18RconBits::PD; _state.RCON |= Pic18RconBits::TO; return 1; } // SLEEP
		if(opcode == 0x0004) { _state.RCON |= Pic18RconBits::TO | Pic18RconBits::PD; return 1; } // CLRWDT
		if(opcode == 0x0005) { PushStack(_state.PC); return 1; } // PUSH
		if(opcode == 0x0006) { PopStack(); return 1; } // POP
		if(opcode == 0x0007) { // DAW
			uint8_t nibLo = _state.W & 0x0F;
			if(nibLo > 9 || (_state.STATUS & Pic18StatusBits::DC)) { _state.W += 0x06; }
			if((_state.W & 0xF0) > 0x90 || (_state.STATUS & Pic18StatusBits::C)) { _state.W += 0x60; }
			return 1;
		}
		if(opcode == 0x00FF) { Reset(); return 1; } // RESET

		// RETFIE: 0000 0000 0001 000s
		if((opcode & 0xFFFE) == 0x0010) {
			_state.PC = PopStack();
			// The popped level records how it was pushed (Pic18StackEntryKind)
			uint8_t level = _state.STKPTR & Pic18StkptrBits::STKPTR_MASK;
			if(opcode & 0x01) {
				// Fast return: WREG/STATUS/BSR auto-restored from the fast register stack
				_state.W = _state.FastW[level];
				_state.STATUS = _state.FastStatus[level];
				_state.BSR = _state.FastBsr[level] & 0x0F;
				_state.Data[Pic18Sfr::STATUS & 0xFFF] = _state.STATUS;
				_state.Data[Pic18Sfr::BSR & 0xFFF] = _state.BSR;
			}
			// RETFIE re-enables the level that was disabled on entry: GIEH for a
			// high-priority (or legacy) ISR, GIEL for a low-priority ISR. A level
			// pushed by CALL/PUSH has no interrupt context — treat as high/legacy.
			if(_state.StackEntryKind[level] == Pic18StackEntryKind::LowInt) {
				_state.INTCON |= Pic18IntconBits::PEIE;
			} else {
				_state.INTCON |= Pic18IntconBits::GIE;
			}
			_state.Data[Pic18Sfr::INTCON & 0xFFF] = _state.INTCON;
			_state.InterruptPending = true;  // INTCON changed, re-evaluate pending sources
			return 2;
		}
		// RETURN: 0000 0000 0001 001s
		if((opcode & 0xFFFE) == 0x0012) {
			_state.PC = PopStack();
			return 2;
		}

		// TBLRD/TBLWT: 0000 0000 0000 1nnn
		if((opcode & 0xFFF8) == 0x0008) {
			uint8_t tblOp = opcode & 0x07;
			if(tblOp < 4) {
				_state.TABLAT = TableRead();
				if((tblOp & 3) == 1) _state.TBLPTR++;
				else if((tblOp & 3) == 2) _state.TBLPTR--;
				else if((tblOp & 3) == 3) _state.TBLPTR++;
			} else {
				TableWrite(_state.TABLAT);
				if((tblOp & 3) == 1) _state.TBLPTR++;
				else if((tblOp & 3) == 2) _state.TBLPTR--;
				else if((tblOp & 3) == 3) _state.TBLPTR++;
			}
			return 2;
		}

		// MOVLB: 0000 0001 0000 kkkk (verified against pic-as: MOVLB 5 -> 0x0105)
		if((opcode & 0xFFF0) == 0x0100) {
			_state.BSR = opcode & 0x0F;
			return 1;
		}

		// Literal ops: high byte determines operation (0x08xx-0x0Fxx)
		uint8_t opc8 = (opcode >> 8) & 0xFF;
		switch(opc8) {
		case 0x0E: _state.W = f; return 1;                                          // MOVLW
		case 0x0F: result = _state.W + f; SetADDFlags(_state.W, f, result); _state.W = result; return 1; // ADDLW
		case 0x09: _state.W |= f; SetNZ(_state.W); return 1;                       // IORLW
		case 0x0B: _state.W &= f; SetNZ(_state.W); return 1;                       // ANDLW
		case 0x0A: _state.W ^= f; SetNZ(_state.W); return 1;                       // XORLW
		case 0x08: result = f - _state.W; SetSUBFlags(f, _state.W, result); _state.W = result; return 1; // SUBLW
		case 0x0C: _state.W = f; _state.PC = PopStack(); return 2;                  // RETLW
		case 0x0D: { uint16_t p = (uint16_t)_state.W * f; _state.PRODL = p; _state.PRODH = p >> 8; return 1; } // MULLW
		}

		// Byte-oriented in 0x0: MULWF (0x02xx-0x03xx), DECF (0x04xx-0x07xx)
		// (ADDWFC is at 0x20xx, handled in case 0x2 — not here)
		if(opc8 >= 0x02 && opc8 <= 0x07) {
			bool d = (opcode >> 9) & 1;
			bool a = (opcode >> 8) & 1;
			uint16_t addr = ResolveAddr(f, !a);

			if(opc8 <= 0x03) {
				// MULWF (always writes to PRODL:PRODH)
				uint16_t prod = (uint16_t)_state.W * ReadData(addr);
				_state.PRODL = prod & 0xFF;
				_state.PRODH = prod >> 8;
				return 1;
			} else {
				// DECF
				val = ReadData(addr);
				result = val - 1;
				SetNZ(result);
				if(d) WriteData(addr, result); else _state.W = result;
				return 1;
			}
		}

		break;
	}

	// ----------------------------------------------------------------
	// 0x1: IORWF, ANDWF, XORWF, COMF (byte-oriented)
	// ----------------------------------------------------------------
	case 0x1: {
		bool d = (opcode >> 9) & 1;
		bool a = (opcode >> 8) & 1;
		uint16_t addr = ResolveAddr(f, !a);

		switch((opcode >> 10) & 0x03) {
		case 0: // IORWF
			val = ReadData(addr);
			result = _state.W | val;
			SetNZ(result);
			if(d) WriteData(addr, result); else _state.W = result;
			return 1;
		case 1: // ANDWF
			val = ReadData(addr);
			result = _state.W & val;
			SetNZ(result);
			if(d) WriteData(addr, result); else _state.W = result;
			return 1;
		case 2: // XORWF
			val = ReadData(addr);
			result = _state.W ^ val;
			SetNZ(result);
			if(d) WriteData(addr, result); else _state.W = result;
			return 1;
		case 3: // COMF
			val = ReadData(addr);
			result = ~val;
			SetNZ(result);
			if(d) WriteData(addr, result); else _state.W = result;
			return 1;
		}
		break;
	}

	// ----------------------------------------------------------------
	// 0x2: ADDWF, INCF, DECFSZ (byte-oriented)
	// ----------------------------------------------------------------
	case 0x2: {
		bool d = (opcode >> 9) & 1;
		bool a = (opcode >> 8) & 1;
		uint16_t addr = ResolveAddr(f, !a);

		switch((opcode >> 10) & 0x03) {
		case 0: // ADDWFC
			val = ReadData(addr);
			{ uint8_t cIn = (_state.STATUS & Pic18StatusBits::C) ? 1 : 0;
			result = _state.W + val + cIn;
			_state.STATUS &= ~(Pic18StatusBits::C | Pic18StatusBits::DC | Pic18StatusBits::Z | Pic18StatusBits::OV | Pic18StatusBits::N);
			if((uint16_t)_state.W + val + cIn > 0xFF) _state.STATUS |= Pic18StatusBits::C;
			if((_state.W & 0x0F) + (val & 0x0F) + cIn > 0x0F) _state.STATUS |= Pic18StatusBits::DC;
			if(result == 0) _state.STATUS |= Pic18StatusBits::Z;
			if((~(_state.W ^ val) & (_state.W ^ result)) & 0x80) _state.STATUS |= Pic18StatusBits::OV;
			if(result & 0x80) _state.STATUS |= Pic18StatusBits::N;
			if(d) WriteData(addr, result); else _state.W = result; }
			return 1;
		case 1: // ADDWF
			val = ReadData(addr);
			result = _state.W + val;
			SetADDFlags(_state.W, val, result);
			if(d) WriteData(addr, result); else _state.W = result;
			return 1;
		case 2: // INCF
			val = ReadData(addr);
			result = val + 1;
			SetNZ(result);
			if(d) WriteData(addr, result); else _state.W = result;
			return 1;
		case 3: // DECFSZ
			val = ReadData(addr);
			result = val - 1;
			if(d) WriteData(addr, result); else _state.W = result;
			if(result == 0) { SkipInstruction(); return 2; }
			return 1;
		}
		break;
	}

	// ----------------------------------------------------------------
	// 0x3: RRCF/RLCF/SWAPF (byte-oriented)
	// ----------------------------------------------------------------
	case 0x3: {
		bool d = (opcode >> 9) & 1;
		bool a = (opcode >> 8) & 1;
		uint16_t addr = ResolveAddr(f, !a);

		// Encoding: 0011 00da = RRCF (case 0), 0011 01da = RLCF (case 1)
		switch((opcode >> 10) & 0x03) {
		case 0: // RRCF
			val = ReadData(addr);
			{ uint8_t oldC2 = (_state.STATUS & Pic18StatusBits::C) ? 0x80 : 0;
			result = (val >> 1) | oldC2;
			_state.STATUS &= ~(Pic18StatusBits::C | Pic18StatusBits::Z | Pic18StatusBits::N);
			if(val & 1) _state.STATUS |= Pic18StatusBits::C;
			if(result == 0) _state.STATUS |= Pic18StatusBits::Z;
			if(result & 0x80) _state.STATUS |= Pic18StatusBits::N;
			if(d) WriteData(addr, result); else _state.W = result; }
			return 1;
		case 1: // RLCF
			val = ReadData(addr);
			{ uint8_t oldC = (_state.STATUS & Pic18StatusBits::C) ? 1 : 0;
			result = (val << 1) | oldC;
			_state.STATUS &= ~(Pic18StatusBits::C | Pic18StatusBits::Z | Pic18StatusBits::N);
			if(val & 0x80) _state.STATUS |= Pic18StatusBits::C;
			if(result == 0) _state.STATUS |= Pic18StatusBits::Z;
			if(result & 0x80) _state.STATUS |= Pic18StatusBits::N;
			if(d) WriteData(addr, result); else _state.W = result; }
			return 1;
		case 2: // SWAPF
			val = ReadData(addr);
			result = (val >> 4) | (val << 4);
			if(d) WriteData(addr, result); else _state.W = result;
			return 1;
		case 3: // INCFSZ
			val = ReadData(addr);
			result = val + 1;
			if(d) WriteData(addr, result); else _state.W = result;
			if(result == 0) { SkipInstruction(); return 2; }
			return 1;
		}
		break;
	}

	// ----------------------------------------------------------------
	// 0x4: RRNCF, RLNCF, INFSNZ, DCFSNZ (byte-oriented)
	// ----------------------------------------------------------------
	case 0x4: {
		bool d = (opcode >> 9) & 1;
		bool a = (opcode >> 8) & 1;
		uint16_t addr = ResolveAddr(f, !a);

		switch((opcode >> 10) & 0x03) {
		case 0: // RRNCF (encoding 0100 00da)
			val = ReadData(addr);
			result = (val >> 1) | (val << 7);
			SetNZ(result);
			if(d) WriteData(addr, result); else _state.W = result;
			return 1;
		case 1: // RLNCF (encoding 0100 01da)
			val = ReadData(addr);
			result = (val << 1) | (val >> 7);
			SetNZ(result);
			if(d) WriteData(addr, result); else _state.W = result;
			return 1;
		case 2: // INFSNZ
			val = ReadData(addr);
			result = val + 1;
			if(d) WriteData(addr, result); else _state.W = result;
			if(result != 0) { SkipInstruction(); return 2; }
			return 1;
		case 3: // DCFSNZ
			val = ReadData(addr);
			result = val - 1;
			if(d) WriteData(addr, result); else _state.W = result;
			if(result != 0) { SkipInstruction(); return 2; }
			return 1;
		}
		break;
	}

	// ----------------------------------------------------------------
	// 0x5: MOVF, SUBFWB, SUBWFB (byte-oriented)
	// ----------------------------------------------------------------
	case 0x5: {
		bool d = (opcode >> 9) & 1;
		bool a = (opcode >> 8) & 1;
		uint16_t addr = ResolveAddr(f, !a);

		switch((opcode >> 10) & 0x03) {
		case 0: // MOVF
			val = ReadData(addr);
			SetNZ(val);
			if(d) WriteData(addr, val); else _state.W = val;
			return 1;
		case 1: // SUBFWB
			val = ReadData(addr);
			{ uint8_t bw = (_state.STATUS & Pic18StatusBits::C) ? 0 : 1;
			result = _state.W - val - bw;
			_state.STATUS &= ~(Pic18StatusBits::C | Pic18StatusBits::DC | Pic18StatusBits::Z | Pic18StatusBits::OV | Pic18StatusBits::N);
			if((uint16_t)_state.W >= (uint16_t)val + bw) _state.STATUS |= Pic18StatusBits::C;
			if((_state.W & 0x0F) >= (val & 0x0F) + bw) _state.STATUS |= Pic18StatusBits::DC;
			if(result == 0) _state.STATUS |= Pic18StatusBits::Z;
			if(((_state.W ^ val) & (_state.W ^ result)) & 0x80) _state.STATUS |= Pic18StatusBits::OV;
			if(result & 0x80) _state.STATUS |= Pic18StatusBits::N;
			if(d) WriteData(addr, result); else _state.W = result; }
			return 1;
		case 2: // SUBWFB
			val = ReadData(addr);
			{ uint8_t bw2 = (_state.STATUS & Pic18StatusBits::C) ? 0 : 1;
			result = val - _state.W - bw2;
			_state.STATUS &= ~(Pic18StatusBits::C | Pic18StatusBits::DC | Pic18StatusBits::Z | Pic18StatusBits::OV | Pic18StatusBits::N);
			if((uint16_t)val >= (uint16_t)_state.W + bw2) _state.STATUS |= Pic18StatusBits::C;
			if((val & 0x0F) >= (_state.W & 0x0F) + bw2) _state.STATUS |= Pic18StatusBits::DC;
			if(result == 0) _state.STATUS |= Pic18StatusBits::Z;
			if(((val ^ _state.W) & (val ^ result)) & 0x80) _state.STATUS |= Pic18StatusBits::OV;
			if(result & 0x80) _state.STATUS |= Pic18StatusBits::N;
			if(d) WriteData(addr, result); else _state.W = result; }
			return 1;
		case 3: // SUBWF
			val = ReadData(addr);
			{ result = val - _state.W;
			_state.STATUS &= ~(Pic18StatusBits::C | Pic18StatusBits::DC | Pic18StatusBits::Z | Pic18StatusBits::OV | Pic18StatusBits::N);
			if((uint16_t)val >= (uint16_t)_state.W) _state.STATUS |= Pic18StatusBits::C;
			if((val & 0x0F) >= (_state.W & 0x0F)) _state.STATUS |= Pic18StatusBits::DC;
			if(result == 0) _state.STATUS |= Pic18StatusBits::Z;
			if(((val ^ _state.W) & (val ^ result)) & 0x80) _state.STATUS |= Pic18StatusBits::OV;
			if(result & 0x80) _state.STATUS |= Pic18StatusBits::N;
			if(d) WriteData(addr, result); else _state.W = result; }
			return 1;
		}
		break;
	}

	// ----------------------------------------------------------------
	// 0x6: CPFSLT, CPFSEQ, CPFSGT, TSTFSZ, SETF, CLRF, NEGF, MOVWF
	// (sub-op = bits 11:9. Verified against pic-as XC8 v3.00 output:
	// CPFSLT=0x60xx, CPFSEQ=0x62xx, CPFSGT=0x64xx, TSTFSZ=0x66xx,
	// SETF=0x68xx, CLRF=0x6Axx, NEGF=0x6Cxx, MOVWF=0x6Exx)
	// ----------------------------------------------------------------
	case 0x6: {
		bool a = (opcode >> 8) & 1;
		uint16_t addr = ResolveAddr(f, !a);

		switch((opcode >> 9) & 0x07) {
		case 0: // CPFSLT (0x60xx)
			val = ReadData(addr);
			if(val < _state.W) { SkipInstruction(); return 2; }
			return 1;
		case 1: // CPFSEQ (0x62xx)
			val = ReadData(addr);
			if(val == _state.W) { SkipInstruction(); return 2; }
			return 1;
		case 2: // CPFSGT (0x64xx)
			val = ReadData(addr);
			if(val > _state.W) { SkipInstruction(); return 2; }
			return 1;
		case 3: // TSTFSZ (0x66xx)
			if(ReadData(addr) == 0) { SkipInstruction(); return 2; }
			return 1;
		case 4: // SETF
			WriteData(addr, 0xFF);
			return 1;
		case 5: // CLRF
			WriteData(addr, 0);
			_state.STATUS |= Pic18StatusBits::Z;
			_state.STATUS &= ~Pic18StatusBits::N;
			return 1;
		case 6: // NEGF
			val = ReadData(addr);
			result = 0 - val;
			_state.STATUS &= ~(Pic18StatusBits::C | Pic18StatusBits::DC | Pic18StatusBits::Z | Pic18StatusBits::OV | Pic18StatusBits::N);
			if(val == 0) _state.STATUS |= Pic18StatusBits::C;
			if((val & 0x0F) == 0) _state.STATUS |= Pic18StatusBits::DC;
			if(result == 0) _state.STATUS |= Pic18StatusBits::Z;
			if(val == 0x80) _state.STATUS |= Pic18StatusBits::OV;
			if(result & 0x80) _state.STATUS |= Pic18StatusBits::N;
			WriteData(addr, result);
			return 1;
		case 7: // MOVWF
			WriteData(addr, _state.W);
			return 1;
		}
		break;
	}

	// ----------------------------------------------------------------
	// 0x7: BTG (bit-oriented)
	// ----------------------------------------------------------------
	case 0x7: {
		uint8_t b = (opcode >> 9) & 0x07;
		bool a = !((opcode >> 8) & 1);
		uint16_t addr = ResolveAddr(f, a);
		uint8_t mask = 1 << b;
		WriteData(addr, ReadData(addr) ^ mask);
		return 1;
	}

	// ----------------------------------------------------------------
	// 0x8: BSF (bit-oriented)
	// ----------------------------------------------------------------
	case 0x8: {
		uint8_t b = (opcode >> 9) & 0x07;
		bool a = !((opcode >> 8) & 1);
		uint16_t addr = ResolveAddr(f, a);
		uint8_t mask = 1 << b;
		WriteData(addr, ReadData(addr) | mask);
		return 1;
	}

	// ----------------------------------------------------------------
	// 0x9: BCF (bit-oriented)
	// ----------------------------------------------------------------
	case 0x9: {
		uint8_t b = (opcode >> 9) & 0x07;
		bool a = !((opcode >> 8) & 1);
		uint16_t addr = ResolveAddr(f, a);
		uint8_t mask = 1 << b;
		WriteData(addr, ReadData(addr) & ~mask);
		return 1;
	}

	// ----------------------------------------------------------------
	// 0xA: BTFSS (bit-oriented)
	// ----------------------------------------------------------------
	case 0xA: {
		uint8_t b = (opcode >> 9) & 0x07;
		bool a = !((opcode >> 8) & 1);
		uint16_t addr = ResolveAddr(f, a);
		uint8_t mask = 1 << b;
		if(ReadData(addr) & mask) { SkipInstruction(); return 2; }
		return 1;
	}

	// ----------------------------------------------------------------
	// 0xB: BTFSC (bit-oriented)
	// ----------------------------------------------------------------
	case 0xB: {
		uint8_t b = (opcode >> 9) & 0x07;
		bool a = !((opcode >> 8) & 1);
		uint16_t addr = ResolveAddr(f, a);
		uint8_t mask = 1 << b;
		if(!(ReadData(addr) & mask)) { SkipInstruction(); return 2; }
		return 1;
	}

	// ----------------------------------------------------------------
	// 0xC: MOVFF (1100 ffff ffff ffff / 1111 ffff ffff ffff)
	// ----------------------------------------------------------------
	case 0xC: {
		uint16_t src = opcode & 0xFFF;
		uint8_t lo2 = ReadProgram(_state.PC * 2);
		uint8_t hi2 = ReadProgram(_state.PC * 2 + 1);
		uint16_t dst = ((uint16_t)(hi2 & 0x0F) << 8) | lo2;
		_state.PC++;
		WriteData(dst, ReadData(src));
		return 2;
	}

	// ----------------------------------------------------------------
	// 0xD: BRA / RCALL
	// ----------------------------------------------------------------
	case 0xD: {
		int16_t rel11 = opcode & 0x7FF;
		if(rel11 & 0x400) rel11 |= ~0x7FF;

		uint8_t subop = (opcode >> 11) & 0x1F;
		if(subop == 0x1A) {
			// BRA: unconditional (1101 0xxx)
			_state.PC += rel11;
			return 2;
		}
		if(subop == 0x1B) {
			// RCALL (1101 1xxx)
			PushStack(_state.PC);
			_state.PC += rel11;
			return 2;
		}
		break;
	}

	// ----------------------------------------------------------------
	// 0xE: CALL, GOTO, LFSR, conditional branches
	// ----------------------------------------------------------------
	case 0xE: {
		uint8_t opc8 = (opcode >> 8) & 0xFF;

		// CALL k,s: 1110 110s kkkk kkkk
		if((opc8 & 0xFE) == 0xEC) {
			uint8_t lo2 = ReadProgram(_state.PC * 2);
			uint8_t hi2 = ReadProgram(_state.PC * 2 + 1);
			uint16_t secondWord = ((uint16_t)hi2 << 8) | lo2;
			// 20-bit target: k<19:8> from second word, k<7:0> from first word
			uint32_t dest = ((uint32_t)(secondWord & 0xFFF) << 8) | (opcode & 0xFF);
			_state.PC++;                       // skip the second word first
			PushStack(_state.PC);              // return = word AFTER the 2-word CALL
			_state.PC = dest;
			return 2;
		}

		// GOTO k: 1110 1111 kkkk kkkk
		if(opc8 == 0xEF) {
			uint8_t lo2 = ReadProgram(_state.PC * 2);
			uint8_t hi2 = ReadProgram(_state.PC * 2 + 1);
			uint16_t secondWord = ((uint16_t)hi2 << 8) | lo2;
			uint32_t dest = ((uint32_t)(secondWord & 0xFFF) << 8) | (opcode & 0xFF);
			_state.PC = dest;
			return 2;
		}

		// LFSR: 1110 1110 00ff kkkk
		if(opc8 == 0xEE) {
			uint8_t fsr = (opcode >> 4) & 0x03;
			uint16_t literal = (uint16_t)(opcode & 0x0F) << 8;
			uint8_t lo2 = ReadProgram(_state.PC * 2);
			uint8_t hi2 = ReadProgram(_state.PC * 2 + 1);
			literal |= ((uint16_t)hi2 << 8) | lo2;
			_state.PC++;
			SetFSR(fsr, literal);
			return 2;
		}

		// Conditional branches: 1110 000n nkkk kkkk
		if((opc8 & 0xF8) == 0xE0) {
			int8_t rel8 = (int8_t)(opcode & 0xFF);
			bool take = false;
			switch(opc8 & 0x07) {
			case 0: take = (_state.STATUS & Pic18StatusBits::Z) != 0; break;   // BZ   (E0)
			case 1: take = !(_state.STATUS & Pic18StatusBits::Z); break;       // BNZ  (E1)
			case 2: take = (_state.STATUS & Pic18StatusBits::C) != 0; break;   // BC   (E2)
			case 3: take = !(_state.STATUS & Pic18StatusBits::C); break;       // BNC  (E3)
			case 4: take = (_state.STATUS & Pic18StatusBits::OV) != 0; break;  // BOV  (E4)
			case 5: take = !(_state.STATUS & Pic18StatusBits::OV); break;      // BNOV (E5)
			case 6: take = (_state.STATUS & Pic18StatusBits::N) != 0; break;   // BN   (E6)
			case 7: take = !(_state.STATUS & Pic18StatusBits::N); break;       // BNN  (E7)
			}
			if(take) { _state.PC += rel8; return 2; }
			return 1;
		}

		break;
	}

	// ----------------------------------------------------------------
	// 0xF: Literal operations (0xFxxx encoding)
	// ----------------------------------------------------------------
	case 0xF: {
		if(opcode == 0xF000) return 1; // NOP
		uint8_t opc8 = (opcode >> 8) & 0xFF;
		switch(opc8) {
		case 0xFE: _state.W = f; return 1;                                          // MOVLW
		case 0xFF: result = _state.W + f; SetADDFlags(_state.W, f, result); _state.W = result; return 1; // ADDLW
		case 0xF9: _state.W |= f; SetNZ(_state.W); return 1;                       // IORLW
		case 0xFB: _state.W &= f; SetNZ(_state.W); return 1;                       // ANDLW
		case 0xFA: _state.W ^= f; SetNZ(_state.W); return 1;                       // XORLW
		case 0xF8: result = f - _state.W; SetSUBFlags(f, _state.W, result); _state.W = result; return 1; // SUBLW
		case 0xFC: _state.W = f; _state.PC = PopStack(); return 2;                  // RETLW
		case 0xFD: { uint16_t p2 = (uint16_t)_state.W * f; _state.PRODL = p2; _state.PRODH = p2 >> 8; return 1; } // MULLW
		}
		break;
	}

	} // end switch

	return 1;  // Unknown opcode -> NOP
}

void Pic18Cpu::Serialize(Serializer& s)
{
	SV(_state.PC);
	SV(_state.W);
	SV(_state.BSR);
	SV(_state.STATUS);
	SV(_state.INTCON);
	SV(_state.STKPTR);
	SV(_state.RCON);
	SVArray(_state.Stack, 31);
	SVArray(_state.FSR, 3);
	SV(_state.TBLPTR);
	SV(_state.TABLAT);
	SV(_state.PRODH);
	SV(_state.PRODL);
	SV(_state.PCLATU);
	SV(_state.PCLATH);
	SVArray(_state.StackEntryKind, 31);
	SVArray(_state.FastW, 31);
	SVArray(_state.FastStatus, 31);
	SVArray(_state.FastBsr, 31);
	SV(_state.CycleCount);
	SV(_state.PspIbf);
	SV(_state.PspObf);
	SV(_state.PspPortDOutput);
	SV(_state.InterruptPending);
	SVArray(_state.Data, 4096);
}