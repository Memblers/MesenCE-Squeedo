#include "pch.h"
#include "NES/Mappers/Squeedo/Pic18Cpu.h"
#include "NES/Mappers/Squeedo/Pic18Peripherals.h"

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

	memset(_state.Stack, 0, sizeof(_state.Stack));
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

void Pic18Cpu::PushStack(uint32_t value)
{
	if((_state.STKPTR & Pic18StkptrBits::STKPTR_MASK) >= 31) {
		_state.STKPTR |= (1 << Pic18StkptrBits::STKFUL);
		return;
	}
	_state.Stack[_state.STKPTR & Pic18StkptrBits::STKPTR_MASK] = value & 0x1FFFFF;
	_state.STKPTR++;
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
	// Fast path: GPR (below SFR region) — direct array access, no virtual dispatch
	if(addr >= Pic18Sfr::SfrBase && _peripherals) {
		return _peripherals->ReadSfr(addr);
	}
	return _state.Data[addr];
}

void Pic18Cpu::WriteData(uint16_t addr, uint8_t value)
{
	addr &= 0xFFF;
	// Fast path: GPR (below SFR region) — direct array write, no virtual dispatch
	if(addr >= Pic18Sfr::SfrBase && _peripherals) {
		_peripherals->WriteSfr(addr, value);
	}
	_state.Data[addr] = value;
}

uint8_t Pic18Cpu::ReadProgram(uint32_t addr)
{
	return _state.Program[addr & 0x1FFFFF];
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
	uint8_t intcon3 = _state.Data[Pic18Sfr::INTCON3 & 0xFFF];

	bool hasHighPriority = false;
	bool hasLowPriority = false;

	if((pir1 & pie1 & Pic18Pir1Bits::PSPIF)) hasHighPriority = true;
	if((_state.INTCON & Pic18IntconBits::TMR0IF) && (_state.INTCON & Pic18IntconBits::TMR0IE)) hasHighPriority = true;
	if((_state.INTCON & Pic18IntconBits::INT0IF) && (_state.INTCON & Pic18IntconBits::INT0IE)) hasHighPriority = true;

	if((intcon3 & Pic18IntconBits::INT1IF) && (intcon3 & Pic18IntconBits::INT1IE)) {
		if(priorityEnabled && (intcon3 & Pic18IntconBits::INT1IP)) hasHighPriority = true;
		else hasLowPriority = true;
	}
	if((intcon3 & Pic18IntconBits::INT2IF) && (intcon3 & Pic18IntconBits::INT2IE)) {
		if(priorityEnabled && (intcon3 & Pic18IntconBits::INT2IP)) hasHighPriority = true;
		else hasLowPriority = true;
	}

	uint8_t periphFlags1 = pir1 & pie1 & ~Pic18Pir1Bits::PSPIF;
	uint8_t periphFlags2 = pir2 & pie2;

	if(periphFlags1 || periphFlags2) {
		if(priorityEnabled) {
			if((periphFlags1 & ipr1) || (periphFlags2 & _state.Data[Pic18Sfr::IPR2 & 0xFFF])) {
				hasHighPriority = true;
			} else {
				hasLowPriority = true;
			}
		} else {
			hasHighPriority = true;
		}
	}

	if(priorityEnabled) {
		if(hasHighPriority && gie) {
			PushStack(_state.PC);
			_state.PC = 0x0008;
			_state.RCON &= ~Pic18RconBits::PD;
			return true;
		}
		if(hasLowPriority && gie && peie) {
			PushStack(_state.PC);
			_state.PC = 0x0018;
			_state.RCON &= ~Pic18RconBits::PD;
			return true;
		}
	} else {
		if((hasHighPriority && gie) || (hasLowPriority && gie && peie)) {
			PushStack(_state.PC);
			_state.PC = 0x0008;
			_state.RCON &= ~Pic18RconBits::PD;
			return true;
		}
	}

	return false;
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
	if(_state.InterruptPending) {
		if(CheckInterrupts()) {
			_state.InterruptPending = false;
			_state.CycleCount += 2;
			return 2;
		}
		_state.InterruptPending = false;
	}

	// PIC18 program memory is stored little-endian: byte[0]=low, byte[1]=high
	uint8_t lo = ReadProgram(_state.PC * 2);
	uint8_t hi = ReadProgram(_state.PC * 2 + 1);
	uint16_t opcode = ((uint16_t)hi << 8) | lo;

	_state.PC++;
	int cycles = DecodeAndExecute(opcode);
	_state.CycleCount += cycles;
	return cycles;
}

int Pic18Cpu::DecodeAndExecute(uint16_t opcode)
{
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
			_state.INTCON |= Pic18IntconBits::GIE;
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

		// MOVLB: 0000 0000 0001 kkkk (but NOT RETFIE/RETURN which are 0x10-0x13)
		if((opcode & 0xFFF0) == 0x0010) {
			if(opcode >= 0x0014) {
				_state.BSR = opcode & 0x0F;
				return 1;
			}
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

		// Byte-oriented in 0x0: ADDWFC (0x00xx-0x01xx), MULWF (0x02xx-0x03xx), DECF (0x04xx-0x07xx)
		if(opc8 <= 0x07) {
			bool d = (opcode >> 9) & 1;
			bool a = (opcode >> 8) & 1;
			uint16_t addr = ResolveAddr(f, !a);

			if(opc8 <= 0x03) {
				if(d) {
					// MULWF (bit 9 fixed=1, bit 8 = a)
					uint16_t prod = (uint16_t)_state.W * ReadData(addr);
					_state.PRODL = prod & 0xFF;
					_state.PRODH = prod >> 8;
					return 1;
				} else {
					// ADDWFC d=0 (result -> W)
					val = ReadData(addr);
					{ uint8_t cIn = (_state.STATUS & Pic18StatusBits::C) ? 1 : 0;
					result = _state.W + val + cIn;
					_state.STATUS &= ~(Pic18StatusBits::C | Pic18StatusBits::DC | Pic18StatusBits::Z | Pic18StatusBits::OV | Pic18StatusBits::N);
					if((uint16_t)_state.W + val + cIn > 0xFF) _state.STATUS |= Pic18StatusBits::C;
					if((_state.W & 0x0F) + (val & 0x0F) + cIn > 0x0F) _state.STATUS |= Pic18StatusBits::DC;
					if(result == 0) _state.STATUS |= Pic18StatusBits::Z;
					if((~(_state.W ^ val) & (_state.W ^ result)) & 0x80) _state.STATUS |= Pic18StatusBits::OV;
					if(result & 0x80) _state.STATUS |= Pic18StatusBits::N;
					_state.W = result; }
					return 1;
				}
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
		case 0: break; // NOP (unused: 0x2000-0x23FF)
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
			if(result == 0) { _state.PC++; return 2; }
			return 1;
		}
		break;
	}

	// ----------------------------------------------------------------
	// 0x3: RLCF, RRCF, SWAPF, INCFSZ (byte-oriented)
	// ----------------------------------------------------------------
	case 0x3: {
		bool d = (opcode >> 9) & 1;
		bool a = (opcode >> 8) & 1;
		uint16_t addr = ResolveAddr(f, !a);

		switch((opcode >> 10) & 0x03) {
		case 0: // RLCF
			val = ReadData(addr);
			{ uint8_t oldC = (_state.STATUS & Pic18StatusBits::C) ? 1 : 0;
			result = (val << 1) | oldC;
			_state.STATUS &= ~(Pic18StatusBits::C | Pic18StatusBits::Z | Pic18StatusBits::N);
			if(val & 0x80) _state.STATUS |= Pic18StatusBits::C;
			if(result == 0) _state.STATUS |= Pic18StatusBits::Z;
			if(result & 0x80) _state.STATUS |= Pic18StatusBits::N;
			if(d) WriteData(addr, result); else _state.W = result; }
			return 1;
		case 1: // RRCF
			val = ReadData(addr);
			{ uint8_t oldC2 = (_state.STATUS & Pic18StatusBits::C) ? 0x80 : 0;
			result = (val >> 1) | oldC2;
			_state.STATUS &= ~(Pic18StatusBits::C | Pic18StatusBits::Z | Pic18StatusBits::N);
			if(val & 1) _state.STATUS |= Pic18StatusBits::C;
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
			if(result == 0) { _state.PC++; return 2; }
			return 1;
		}
		break;
	}

	// ----------------------------------------------------------------
	// 0x4: RLNCF, RRNCF, INFSNZ, DCFSNZ (byte-oriented)
	// ----------------------------------------------------------------
	case 0x4: {
		bool d = (opcode >> 9) & 1;
		bool a = (opcode >> 8) & 1;
		uint16_t addr = ResolveAddr(f, !a);

		switch((opcode >> 10) & 0x03) {
		case 0: // RLNCF
			val = ReadData(addr);
			result = (val << 1) | (val >> 7);
			SetNZ(result);
			if(d) WriteData(addr, result); else _state.W = result;
			return 1;
		case 1: // RRNCF
			val = ReadData(addr);
			result = (val >> 1) | (val << 7);
			SetNZ(result);
			if(d) WriteData(addr, result); else _state.W = result;
			return 1;
		case 2: // INFSNZ
			val = ReadData(addr);
			result = val + 1;
			if(d) WriteData(addr, result); else _state.W = result;
			if(result != 0) { _state.PC++; return 2; }
			return 1;
		case 3: // DCFSNZ
			val = ReadData(addr);
			result = val - 1;
			if(d) WriteData(addr, result); else _state.W = result;
			if(result != 0) { _state.PC++; return 2; }
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
		case 3: break; // NOP (unused: 0x5C00-0x5FFF)
		}
		break;
	}

	// ----------------------------------------------------------------
	// 0x6: SETF, CPFSEQ, TSTFSZ, CPFSGT, CPFSLT, CLRF, NEGF, MOVWF
	// ----------------------------------------------------------------
	case 0x6: {
		bool a = (opcode >> 8) & 1;
		uint16_t addr = ResolveAddr(f, !a);

		switch((opcode >> 9) & 0x07) {
		case 0: // SETF
			WriteData(addr, 0xFF);
			return 1;
		case 1: // CPFSEQ
			val = ReadData(addr);
			if(val == _state.W) { _state.PC++; return 2; }
			return 1;
		case 2: // TSTFSZ
			if(ReadData(addr) == 0) { _state.PC++; return 2; }
			return 1;
		case 3: // CPFSGT
			val = ReadData(addr);
			if(val > _state.W) { _state.PC++; return 2; }
			return 1;
		case 4: // CPFSLT
			val = ReadData(addr);
			if(val < _state.W) { _state.PC++; return 2; }
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
		if(ReadData(addr) & mask) { _state.PC++; return 2; }
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
		if(!(ReadData(addr) & mask)) { _state.PC++; return 2; }
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
			PushStack(_state.PC);
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
			case 0: take = (_state.STATUS & Pic18StatusBits::C) != 0; break;   // BC
			case 1: take = (_state.STATUS & Pic18StatusBits::OV) != 0; break;  // BNOV
			case 2: take = (_state.STATUS & Pic18StatusBits::Z) != 0; break;   // BZ
			case 3: take = (_state.STATUS & Pic18StatusBits::N) != 0; break;   // BN
			case 4: take = !(_state.STATUS & Pic18StatusBits::C); break;       // BNC
			case 5: take = !(_state.STATUS & Pic18StatusBits::OV); break;      // BNOV
			case 6: take = !(_state.STATUS & Pic18StatusBits::Z); break;       // BNZ
			case 7: take = !(_state.STATUS & Pic18StatusBits::N); break;       // BNN
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
	SV(_state.CycleCount);
	SV(_state.PspIbf);
	SV(_state.PspObf);
	SV(_state.PspPortDOutput);
	SV(_state.InterruptPending);
	SVArray(_state.Data, 4096);
}