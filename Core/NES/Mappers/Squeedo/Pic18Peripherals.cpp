#include "pch.h"
#include "NES/Mappers/Squeedo/Pic18Peripherals.h"
#include "NES/Mappers/Squeedo/Pic18Cpu.h"
#include "NES/Mappers/Squeedo/Pic18CrashDiag.h"
#include "NES/NesConsole.h"
#include "NES/NesCpu.h"
#include "Shared/Emulator.h"

Pic18Peripherals::Pic18Peripherals(Pic18CpuState& state, NesConsole* console)
	: _state(state), _console(console)
{
}

uint16_t Pic18Peripherals::GetTimer0PrescalerDiv()
{
	// T0CON bits 2:0 = T0PS, prescaler divider = 2^(n+1)
	// (T0PS=111 -> 256, which is why this returns uint16_t)
	uint8_t ps = _state.Data[Pic18Sfr::T0CON & 0xFFF] & Pic18T0conBits::T0PS_MASK;
	if(_state.Data[Pic18Sfr::T0CON & 0xFFF] & (1 << Pic18T0conBits::PSA)) {
		return 1;  // Prescaler assigned to WDT, timer gets 1:1
	}
	return (uint16_t)(1 << (ps + 1));
}

uint8_t Pic18Peripherals::GetTimer1PrescalerDiv()
{
	uint8_t t1ckps = (_state.Data[Pic18Sfr::T1CON & 0xFFF] & Pic18T1conBits::T1CKPS_MASK) >> 4;
	return (uint8_t)(1 << t1ckps);  // 00=1:1, 01=1:2, 10=1:4, 11=1:8
}

uint8_t Pic18Peripherals::GetTimer3PrescalerDiv()
{
	uint8_t t3ckps = (_state.Data[Pic18Sfr::T3CON & 0xFFF] & Pic18T3conBits::T3CKPS_MASK) >> 4;
	return (uint8_t)(1 << t3ckps);  // 00=1:1, 01=1:2, 10=1:4, 11=1:8
}

void Pic18Peripherals::ClockTimers(int cycles)
{
	uint8_t t0con = _state.Data[Pic18Sfr::T0CON & 0xFFF];
	uint8_t t1con = _state.Data[Pic18Sfr::T1CON & 0xFFF];
	uint8_t t3con = _state.Data[Pic18Sfr::T3CON & 0xFFF];

	// Timer0
	if(t0con & (1 << Pic18T0conBits::TMR0ON)) {
		bool useInternalClock = !(t0con & (1 << Pic18T0conBits::T0CS));

		if(useInternalClock) {
			uint16_t prescaler = GetTimer0PrescalerDiv();
			_tmr0PrescalerCounter += cycles;
			uint16_t ticks = _tmr0PrescalerCounter / prescaler;
			_tmr0PrescalerCounter %= prescaler;
			IncrementTimer0(ticks);
		}
		// T0CS=1 (external clock on T0CKI): TMR0 is ticked by T0CKI edges
		// instead — see ClockTimer0External (PPU A12 is wired to T0CKI).
	}

	// Timer1
	if(t1con & (1 << Pic18T1conBits::TMR1ON)) {
		bool useInternalClock = !(t1con & (1 << Pic18T1conBits::TMR1CS));
		if(useInternalClock) {
			uint8_t prescaler = GetTimer1PrescalerDiv();
			_tmr1PrescalerCounter += cycles;
			uint16_t ticks = _tmr1PrescalerCounter / prescaler;
			_tmr1PrescalerCounter %= prescaler;

			if(ticks > 0) {
				uint32_t tmr1 = ((uint32_t)_state.Data[Pic18Sfr::TMR1H & 0xFFF] << 8) | _state.Data[Pic18Sfr::TMR1L & 0xFFF];
				tmr1 += ticks;
				if(tmr1 > 0xFFFF) {
					tmr1 = 0;
					_state.Data[Pic18Sfr::PIR1 & 0xFFF] |= Pic18Pir1Bits::TMR1IF;
					_state.InterruptPending = true;
				}
				_state.Data[Pic18Sfr::TMR1L & 0xFFF] = (uint8_t)tmr1;
				_state.Data[Pic18Sfr::TMR1H & 0xFFF] = (uint8_t)(tmr1 >> 8);
			}
		}
	}

	// Timer3
	if(t3con & (1 << Pic18T3conBits::TMR3ON)) {
		bool useInternalClock = !(t3con & (1 << Pic18T3conBits::TMR3CS));
		if(useInternalClock) {
			uint8_t prescaler = GetTimer3PrescalerDiv();
			_tmr3PrescalerCounter += cycles;
			uint16_t ticks = _tmr3PrescalerCounter / prescaler;
			_tmr3PrescalerCounter %= prescaler;

			if(ticks > 0) {
				uint32_t tmr3 = ((uint32_t)_state.Data[Pic18Sfr::TMR3H & 0xFFF] << 8) | _state.Data[Pic18Sfr::TMR3L & 0xFFF];
				tmr3 += ticks;
				if(tmr3 > 0xFFFF) {
					tmr3 = 0;
					_state.Data[Pic18Sfr::PIR2 & 0xFFF] |= Pic18Pir2Bits::TMR3IF;
					_state.InterruptPending = true;
				}
				_state.Data[Pic18Sfr::TMR3L & 0xFFF] = (uint8_t)tmr3;
				_state.Data[Pic18Sfr::TMR3H & 0xFFF] = (uint8_t)(tmr3 >> 8);
			}
		}
	}

	// UART TX (simplified — no actual serial timing, just immediate)
	uint8_t txsta = _state.Data[Pic18Sfr::TXSTA & 0xFFF];
	if((txsta & (1 << Pic18TxstaBits::TXEN)) && _txBusy) {
		_txCycleCounter -= cycles;
		if(_txCycleCounter <= 0) {
			_txBusy = false;
			_state.Data[Pic18Sfr::PIR1 & 0xFFF] |= Pic18Pir1Bits::TXIF;  // TX buffer empty
			_state.Data[Pic18Sfr::TXSTA & 0xFFF] |= (1 << Pic18TxstaBits::TRMT);  // Shift register empty
		}
	}
}

// Increment TMR0 by the given number of prescaler ticks (0 = no-op).
// Handles both 8-bit (reload from TMR0H on overflow) and 16-bit modes.
void Pic18Peripherals::IncrementTimer0(uint16_t ticks)
{
	if(ticks == 0) {
		return;
	}

	bool is8Bit = (_state.Data[Pic18Sfr::T0CON & 0xFFF] & (1 << Pic18T0conBits::T08BIT)) != 0;
	if(is8Bit) {
		uint16_t tmr0 = _state.Data[Pic18Sfr::TMR0L & 0xFFF] + ticks;
		if(tmr0 > 0xFF) {
			// Overflow: reload from TMR0H, set interrupt
			tmr0 = _state.Data[Pic18Sfr::TMR0H & 0xFFF] + (tmr0 - 0x100);
			if(tmr0 > 0xFF) tmr0 &= 0xFF;  // Second overflow (rare)
			_state.INTCON |= Pic18IntconBits::TMR0IF;
			_state.InterruptPending = true;
		}
		_state.Data[Pic18Sfr::TMR0L & 0xFFF] = (uint8_t)tmr0;
	} else {
		uint32_t tmr0 = ((uint32_t)_state.Data[Pic18Sfr::TMR0H & 0xFFF] << 8) | _state.Data[Pic18Sfr::TMR0L & 0xFFF];
		tmr0 += ticks;
		if(tmr0 > 0xFFFF) {
			tmr0 = 0;  // 16-bit mode resets to 0
			_state.INTCON |= Pic18IntconBits::TMR0IF;
			_state.InterruptPending = true;
		}
		_state.Data[Pic18Sfr::TMR0L & 0xFFF] = (uint8_t)tmr0;
		_state.Data[Pic18Sfr::TMR0H & 0xFFF] = (uint8_t)(tmr0 >> 8);
	}
}

// One edge on the T0CKI pin (Timer0 external clock source). On the Squeedo
// board the NES PPU's A12 line is wired to T0CKI, giving the firmware an
// MMC3-style scanline counter: with sprites fetched from $1000, A12 rises 8x
// per scanline (once per sprite fetch) and the T0PS prescaler folds those into
// TMR0 ticks — T0CON = 0x22 (T0CS=1 external, T0SE=0 rising edge, T0PS=010
// 1:8 prescaler) yields exactly one TMR0 increment per rendered scanline.
// Only counted when the timer is on (TMR0ON), the external clock is selected
// (T0CS=1) and the edge matches T0SE (0 = rising, 1 = falling).
void Pic18Peripherals::ClockTimer0External(bool risingEdge)
{
	uint8_t t0con = _state.Data[Pic18Sfr::T0CON & 0xFFF];
	if(!(t0con & (1 << Pic18T0conBits::TMR0ON))) {
		return;  // Timer0 disabled
	}
	if(!(t0con & (1 << Pic18T0conBits::T0CS))) {
		return;  // Internal clock selected, T0CKI is ignored
	}
	bool countRisingEdge = !(t0con & (1 << Pic18T0conBits::T0SE));
	if(risingEdge != countRisingEdge) {
		return;  // Edge polarity not selected by T0SE
	}

	// The external pulse feeds the same T0PS prescaler as the internal clock
	// (PSA=1 assigns the prescaler to the WDT, so the timer gets edges 1:1).
	uint16_t prescaler = GetTimer0PrescalerDiv();
	_tmr0PrescalerCounter++;
	uint16_t ticks = _tmr0PrescalerCounter / prescaler;
	_tmr0PrescalerCounter %= prescaler;
	IncrementTimer0(ticks);
}

uint8_t Pic18Peripherals::NesRead(uint8_t regAddr)
{
	// NES reading from PSP — return whatever PIC has pre-loaded on PortD
	_pspLatchAddr = regAddr & 0x1F;
	_state.PspObf = false;
	_state.Data[Pic18Sfr::PORTB & 0xFFF] = (_state.Data[Pic18Sfr::PORTB & 0xFFF] & 0xE0) | (regAddr & 0x1F);  // Address on PortB 0-4

	// Set PSPIF and trigger high-priority interrupt (PSP fires on both read and write)
	_state.Data[Pic18Sfr::PIR1 & 0xFFF] |= Pic18Pir1Bits::PSPIF;
	_state.InterruptPending = true;

	Pic18CrashDiag::RecordPspAccess(regAddr & 0x1F, _state.PspPortDOutput, false);
	return _state.PspPortDOutput;
}

void Pic18Peripherals::NesWrite(uint8_t regAddr, uint8_t value)
{
	// NES writing to PSP
	Pic18CrashDiag::RecordPspAccess(regAddr & 0x1F, value, true);
	_pspLatchAddr = regAddr & 0x1F;
	_pspWriteData = value;
	if(_state.PspIbf) {
		// Overflow: NES wrote again before the PIC read PORTD (IBOV must be cleared in software)
		_state.PspIbov = true;
	}
	_state.PspIbf = true;
	_state.Data[Pic18Sfr::PORTD & 0xFFF] = value;  // Data appears on PortD
	_state.Data[Pic18Sfr::PORTB & 0xFFF] = (_state.Data[Pic18Sfr::PORTB & 0xFFF] & 0xE0) | (regAddr & 0x1F);  // Address on PortB 0-4

	// Set PSPIF and trigger high-priority interrupt
	_state.Data[Pic18Sfr::PIR1 & 0xFFF] |= Pic18Pir1Bits::PSPIF;
	_state.InterruptPending = true;
}

void Pic18Peripherals::UartReceiveByte(uint8_t byte)
{
	_rxBuffer.push_back(byte);
	if(_rxBuffer.size() > 1) {
		_state.Data[Pic18Sfr::RCSTA & 0xFFF] |= (1 << Pic18RcstaBits::OERR);  // Overrun
	} else {
		_state.Data[Pic18Sfr::RCREG & 0xFFF] = byte;
		_state.Data[Pic18Sfr::PIR1 & 0xFFF] |= Pic18Pir1Bits::RCIF;
		_state.InterruptPending = true;
		_rxDataReady = true;
	}
}
// FSR-indirect registers (INDF/POSTINC/POSTDEC/PREINC/PLUSW) resolve through
// the FSR value and recurse into ReadSfr/WriteSfr. If FSR itself points at an
// indirect register (garbage FSR), the side effect resolves to itself and the
// recursion overflows the native stack (crash dump 2026-10-12: FSR2=$FDD =
// POSTINC2 -> self-recursion). This guard bounds that chain to one real
// resolution; deeper re-entry degrades to a raw Data[] access.
namespace {
struct SfrRecursionGuard {
        uint8_t& depth;
        explicit SfrRecursionGuard(uint8_t& d) : depth(d) { ++depth; }
        ~SfrRecursionGuard() { --depth; }
};
}
uint8_t Pic18Peripherals::ReadSfr(uint16_t addr, bool peek)
{
	addr &= 0xFFF;  // PIC18 uses 12-bit addressing
	SfrRecursionGuard guard(_sfrRecursionDepth);
	if(_sfrRecursionDepth > 4) {
		return _state.Data[addr];
	}
	switch(addr) {
	// Timer reads with latching
	case Pic18Sfr::TMR0L:
		if(!peek) {
			_state.TMR0ReadLatch = _state.Data[Pic18Sfr::TMR0H & 0xFFF];
		}
		return _state.Data[Pic18Sfr::TMR0L & 0xFFF];
	case Pic18Sfr::TMR0H:
		return peek ? _state.Data[Pic18Sfr::TMR0H & 0xFFF] : _state.TMR0ReadLatch;

	case Pic18Sfr::TMR1L:
		if(!peek) {
			_state.TMR1ReadLatch = _state.Data[Pic18Sfr::TMR1H & 0xFFF];
		}
		return _state.Data[Pic18Sfr::TMR1L & 0xFFF];
	case Pic18Sfr::TMR1H:
		return peek ? _state.Data[Pic18Sfr::TMR1H & 0xFFF] : _state.TMR1ReadLatch;

	case Pic18Sfr::TMR3L:
		if(!peek) {
			_state.TMR3ReadLatch = _state.Data[Pic18Sfr::TMR3H & 0xFFF];
		}
		return _state.Data[Pic18Sfr::TMR3L & 0xFFF];
	case Pic18Sfr::TMR3H:
		return peek ? _state.Data[Pic18Sfr::TMR3H & 0xFFF] : _state.TMR3ReadLatch;

	// UART receive
	case Pic18Sfr::RCREG:
		if(peek) {
			return _state.Data[Pic18Sfr::RCREG & 0xFFF];
		}
		if(!_rxBuffer.empty()) {
			_rxBuffer.pop_front();
			if(_rxBuffer.empty()) {
				_state.Data[Pic18Sfr::PIR1 & 0xFFF] &= ~Pic18Pir1Bits::RCIF;
				_rxDataReady = false;
			} else {
				_state.Data[Pic18Sfr::RCREG & 0xFFF] = _rxBuffer.front();
			}
		}
		return _state.Data[Pic18Sfr::RCREG & 0xFFF];

	// PORTD read in PSP mode returns the input buffer (data written by the NES)
	// and clears IBF — this is the only way IBF gets cleared on real hardware
	case Pic18Sfr::PORTD:
		if(!peek) {
			_state.PspIbf = false;
		}
		return _pspWriteData;

	// TRISE: bits 7-5 are read-only PSP status (IBF, OBF, IBOV), must reflect live state
	// PIC18F4525 datasheet: IBF=bit7, OBF=bit6, IBOV=bit5, PSPMODE=bit4
	case Pic18Sfr::TRISE:
		return (_state.Data[Pic18Sfr::TRISE & 0xFFF] & 0x1F)
			| (_state.PspIbf ? 0x80 : 0x00)   // bit 7 = IBF
			| (_state.PspObf ? 0x40 : 0x00)   // bit 6 = OBF
			| (_state.PspIbov ? 0x20 : 0x00); // bit 5 = IBOV

	// Core registers that must return live state (PC/flags live in _state, not Data)
	case Pic18Sfr::STATUS: return _state.STATUS;
	case Pic18Sfr::WREG: return _state.W;
	case Pic18Sfr::PCL: return (uint8_t)((_state.PC * 2) & 0xFF);
	case Pic18Sfr::PCLATH: return _state.PCLATH;
	case Pic18Sfr::PCLATU: return _state.PCLATU;

	// Registers updated in _state by the CPU/timers without syncing Data[]:
	// interrupts set INTCON flags, PUSH/POP move STKPTR, MULLW/MULWF write
	// PRODH:L, TBLRD auto-increments TBLPTR and writes TABLAT.
	case Pic18Sfr::INTCON: return _state.INTCON;
	case Pic18Sfr::RCON: return _state.RCON;
	case Pic18Sfr::STKPTR: return _state.STKPTR;

	// TOS registers are a window onto the hardware stack; STKPTR selects the level
	// (write STKPTR to inspect other stack entries, like on real hardware)
	case Pic18Sfr::TOSL: return (uint8_t)(_state.Stack[GetTosIndex()] & 0xFF);
	case Pic18Sfr::TOSH: return (uint8_t)((_state.Stack[GetTosIndex()] >> 8) & 0xFF);
	case Pic18Sfr::TOSU: return (uint8_t)((_state.Stack[GetTosIndex()] >> 16) & 0x1F);
	case Pic18Sfr::BSR: return _state.BSR;
	case Pic18Sfr::PRODH: return _state.PRODH;
	case Pic18Sfr::PRODL: return _state.PRODL;
	case Pic18Sfr::TABLAT: return _state.TABLAT;
	case Pic18Sfr::TBLPTRL: return (uint8_t)(_state.TBLPTR & 0xFF);
	case Pic18Sfr::TBLPTRH: return (uint8_t)((_state.TBLPTR >> 8) & 0xFF);
	case Pic18Sfr::TBLPTRU: return (uint8_t)((_state.TBLPTR >> 16) & 0x1F);

	// FSR reads - return live values from _state.FSR (not stale Data memory)
	case Pic18Sfr::FSR0L: return _state.FSR[0] & 0xFF;
	case Pic18Sfr::FSR0H: return (_state.FSR[0] >> 8) & 0x0F;
	case Pic18Sfr::FSR1L: return _state.FSR[1] & 0xFF;
	case Pic18Sfr::FSR1H: return (_state.FSR[1] >> 8) & 0x0F;
	case Pic18Sfr::FSR2L: return _state.FSR[2] & 0xFF;
	case Pic18Sfr::FSR2H: return (_state.FSR[2] >> 8) & 0x0F;

	// INDF/POSTINC/POSTDEC/PREINC/PLUSW reads (indirect addressing)
	case Pic18Sfr::INDF0: return ReadIndirect(0, addr, IndirectOp::Indf, peek);
	case Pic18Sfr::POSTINC0: return ReadIndirect(0, addr, IndirectOp::PostInc, peek);
	case Pic18Sfr::POSTDEC0: return ReadIndirect(0, addr, IndirectOp::PostDec, peek);
	case Pic18Sfr::PREINC0: return ReadIndirect(0, addr, IndirectOp::PreInc, peek);
	case Pic18Sfr::PLUSW0: return ReadIndirect(0, addr, IndirectOp::PlusW, peek);
	case Pic18Sfr::INDF1: return ReadIndirect(1, addr, IndirectOp::Indf, peek);
	case Pic18Sfr::POSTINC1: return ReadIndirect(1, addr, IndirectOp::PostInc, peek);
	case Pic18Sfr::POSTDEC1: return ReadIndirect(1, addr, IndirectOp::PostDec, peek);
	case Pic18Sfr::PREINC1: return ReadIndirect(1, addr, IndirectOp::PreInc, peek);
	case Pic18Sfr::PLUSW1: return ReadIndirect(1, addr, IndirectOp::PlusW, peek);
	case Pic18Sfr::INDF2: return ReadIndirect(2, addr, IndirectOp::Indf, peek);
	case Pic18Sfr::POSTINC2: return ReadIndirect(2, addr, IndirectOp::PostInc, peek);
	case Pic18Sfr::POSTDEC2: return ReadIndirect(2, addr, IndirectOp::PostDec, peek);
	case Pic18Sfr::PREINC2: return ReadIndirect(2, addr, IndirectOp::PreInc, peek);
	case Pic18Sfr::PLUSW2: return ReadIndirect(2, addr, IndirectOp::PlusW, peek);

	default:
		return _state.Data[addr & 0xFFF];
	}
}

bool Pic18Peripherals::WriteSfr(uint16_t addr, uint8_t value, bool fromDebug)
{
	addr &= 0xFFF;  // PIC18 uses 12-bit addressing (BSR<<8|f wraps into $F00-$FFF SFR space)
	SfrRecursionGuard guard(_sfrRecursionDepth);
	if(_sfrRecursionDepth > 4) {
		_state.Data[addr] = value;
		return true;
	}
	// Debug write hook: raised for the SFR access itself and, via the indirect cases in
	// WriteSfrInternal (POSTINC/INDF/etc. recurse back through here), for the resolved
	// target address too. Frozen addresses drop the write (unless it came from the debugger).
	if(_emu && !Pic18Dbg::InWriteHook()) {
		Pic18Dbg::InWriteHook() = true;
		bool processWrite = _emu->ProcessMemoryWrite<CpuType::Pic18>(addr, value, MemoryOperationType::Write);
		Pic18Dbg::InWriteHook() = false;
		if(!processWrite && !fromDebug) {
			return false;
		}
	}
	WriteSfrInternal(addr, value, fromDebug);
	return true;
}

void Pic18Peripherals::WriteSfrInternal(uint16_t addr, uint8_t value, bool fromDebug)
{
	switch(addr) {
	case Pic18Sfr::TMR0L:
		_state.Data[Pic18Sfr::TMR0L & 0xFFF] = value;
		return;
	case Pic18Sfr::TMR1L:
		_state.Data[Pic18Sfr::TMR1L & 0xFFF] = value;
		return;
	case Pic18Sfr::TMR1H:
		_state.Data[Pic18Sfr::TMR1H & 0xFFF] = value;
		return;
	case Pic18Sfr::TMR3L:
		_state.Data[Pic18Sfr::TMR3L & 0xFFF] = value;
		return;
	case Pic18Sfr::TMR3H:
		_state.Data[Pic18Sfr::TMR3H & 0xFFF] = value;
		return;

	case Pic18Sfr::TXREG:
		_state.Data[Pic18Sfr::TXREG & 0xFFF] = value;
		_state.Data[Pic18Sfr::PIR1 & 0xFFF] &= ~Pic18Pir1Bits::TXIF;  // TX buffer full
		_state.Data[Pic18Sfr::TXSTA & 0xFFF] &= ~(1 << Pic18TxstaBits::TRMT);  // Shift reg busy
		_txBusy = true;
		_txCycleCounter = 100;  // Simplified: complete after ~100 PIC cycles
		if(_txCallback) {
			_txCallback(value);  // Send byte to serial port bridge
		}
		return;

	case Pic18Sfr::PORTA:
		_state.Data[Pic18Sfr::LATA & 0xFFF] = value;
		_state.Data[Pic18Sfr::PORTA & 0xFFF] = value;
		_gpioDirty = true;
		return;
	case Pic18Sfr::PORTB:
		_state.Data[Pic18Sfr::LATB & 0xFFF] = value;
		_state.Data[Pic18Sfr::PORTB & 0xFFF] = value;
		_gpioDirty = true;
		return;
	case Pic18Sfr::PORTC:
		_state.Data[Pic18Sfr::LATC & 0xFFF] = value;
		_state.Data[Pic18Sfr::PORTC & 0xFFF] = value;
		_gpioDirty = true;
		return;
	case Pic18Sfr::PORTD:
		_state.Data[Pic18Sfr::LATD & 0xFFF] = value;
		_state.Data[Pic18Sfr::PORTD & 0xFFF] = value;
		_state.PspPortDOutput = value;  // Pre-load data for NES reads
		_state.PspObf = true;
		return;
	case Pic18Sfr::PORTE:
		_state.Data[Pic18Sfr::LATE & 0xFFF] = value;
		_state.Data[Pic18Sfr::PORTE & 0xFFF] = value;
		return;

	case Pic18Sfr::LATA: _state.Data[Pic18Sfr::LATA & 0xFFF] = value; _state.Data[Pic18Sfr::PORTA & 0xFFF] = value; _gpioDirty = true; return;
	case Pic18Sfr::LATB: _state.Data[Pic18Sfr::LATB & 0xFFF] = value; _state.Data[Pic18Sfr::PORTB & 0xFFF] = value; _gpioDirty = true; return;
	case Pic18Sfr::LATC: _state.Data[Pic18Sfr::LATC & 0xFFF] = value; _state.Data[Pic18Sfr::PORTC & 0xFFF] = value; _gpioDirty = true; return;
	case Pic18Sfr::LATD: _state.Data[Pic18Sfr::LATD & 0xFFF] = value; _state.Data[Pic18Sfr::PORTD & 0xFFF] = value; _state.PspPortDOutput = value; _state.PspObf = true; return;
	case Pic18Sfr::LATE: _state.Data[Pic18Sfr::LATE & 0xFFF] = value; _state.Data[Pic18Sfr::PORTE & 0xFFF] = value; return;

	// TRISE: bits 7-6 (IBF/OBF) are read-only; bit 5 (IBOV) is software-clearable (write 0 to clear)
	case Pic18Sfr::TRISE:
		_state.Data[Pic18Sfr::TRISE & 0xFFF] = value & 0x1F;
		if(!(value & 0x20)) {
			_state.PspIbov = false;
		}
		return;

	// Indirect writes (INDF/POSTINC/POSTDEC/PREINC/PLUSW)
	case Pic18Sfr::INDF0: WriteIndirect(0, addr, IndirectOp::Indf, value, fromDebug); return;
	case Pic18Sfr::POSTINC0: WriteIndirect(0, addr, IndirectOp::PostInc, value, fromDebug); return;
	case Pic18Sfr::POSTDEC0: WriteIndirect(0, addr, IndirectOp::PostDec, value, fromDebug); return;
	case Pic18Sfr::PREINC0: WriteIndirect(0, addr, IndirectOp::PreInc, value, fromDebug); return;
	case Pic18Sfr::PLUSW0: WriteIndirect(0, addr, IndirectOp::PlusW, value, fromDebug); return;
	case Pic18Sfr::INDF1: WriteIndirect(1, addr, IndirectOp::Indf, value, fromDebug); return;
	case Pic18Sfr::POSTINC1: WriteIndirect(1, addr, IndirectOp::PostInc, value, fromDebug); return;
	case Pic18Sfr::POSTDEC1: WriteIndirect(1, addr, IndirectOp::PostDec, value, fromDebug); return;
	case Pic18Sfr::PREINC1: WriteIndirect(1, addr, IndirectOp::PreInc, value, fromDebug); return;
	case Pic18Sfr::PLUSW1: WriteIndirect(1, addr, IndirectOp::PlusW, value, fromDebug); return;
	case Pic18Sfr::INDF2: WriteIndirect(2, addr, IndirectOp::Indf, value, fromDebug); return;
	case Pic18Sfr::POSTINC2: WriteIndirect(2, addr, IndirectOp::PostInc, value, fromDebug); return;
	case Pic18Sfr::POSTDEC2: WriteIndirect(2, addr, IndirectOp::PostDec, value, fromDebug); return;
	case Pic18Sfr::PREINC2: WriteIndirect(2, addr, IndirectOp::PreInc, value, fromDebug); return;
	case Pic18Sfr::PLUSW2: WriteIndirect(2, addr, IndirectOp::PlusW, value, fromDebug); return;

	// Core registers that should update the state struct
	case Pic18Sfr::INTCON: _state.INTCON = value; _state.Data[addr & 0xFFF] = value; _state.InterruptPending = true; return;
	case Pic18Sfr::RCON: _state.RCON = value; _state.Data[addr & 0xFFF] = value; return;
	case Pic18Sfr::STKPTR: _state.STKPTR = value; _state.Data[addr & 0xFFF] = value; return;

	// Setting interrupt flags/enables can assert an interrupt immediately (e.g. enabling
	// TMR3IE while TMR3IF is already set) — re-evaluate pending interrupts on these writes
	case Pic18Sfr::PIR1:
	case Pic18Sfr::PIR2:
	case Pic18Sfr::PIE1:
	case Pic18Sfr::PIE2:
	case Pic18Sfr::INTCON3:
		_state.Data[addr & 0xFFF] = value;
		_state.InterruptPending = true;
		return;

	// TOS registers write through to the selected hardware stack level
	case Pic18Sfr::TOSL: _state.Stack[GetTosIndex()] = (_state.Stack[GetTosIndex()] & 0x1FFF00) | value; return;
	case Pic18Sfr::TOSH: _state.Stack[GetTosIndex()] = (_state.Stack[GetTosIndex()] & 0x1F00FF) | ((uint32_t)value << 8); return;
	case Pic18Sfr::TOSU: _state.Stack[GetTosIndex()] = (_state.Stack[GetTosIndex()] & 0x0FFFFF) | ((uint32_t)(value & 0x1F) << 16); return;
	case Pic18Sfr::BSR: _state.BSR = value & 0x0F; _state.Data[addr & 0xFFF] = value & 0x0F; return;
	case Pic18Sfr::FSR0L: _state.FSR[0] = (_state.FSR[0] & 0xF00) | value; _state.Data[addr & 0xFFF] = value; return;
	case Pic18Sfr::FSR0H: _state.FSR[0] = (_state.FSR[0] & 0x0FF) | ((uint16_t)(value & 0x0F) << 8); _state.Data[addr & 0xFFF] = value & 0x0F; return;
	case Pic18Sfr::FSR1L: _state.FSR[1] = (_state.FSR[1] & 0xF00) | value; _state.Data[addr & 0xFFF] = value; return;
	case Pic18Sfr::FSR1H: _state.FSR[1] = (_state.FSR[1] & 0x0FF) | ((uint16_t)(value & 0x0F) << 8); _state.Data[addr & 0xFFF] = value & 0x0F; return;
	case Pic18Sfr::FSR2L: _state.FSR[2] = (_state.FSR[2] & 0xF00) | value; _state.Data[addr & 0xFFF] = value; return;
	case Pic18Sfr::FSR2H: _state.FSR[2] = (_state.FSR[2] & 0x0FF) | ((uint16_t)(value & 0x0F) << 8); _state.Data[addr & 0xFFF] = value & 0x0F; return;
	case Pic18Sfr::TBLPTRL: _state.TBLPTR = (_state.TBLPTR & 0x1FFF00) | value; _state.Data[addr & 0xFFF] = value; return;
	case Pic18Sfr::TBLPTRH: _state.TBLPTR = (_state.TBLPTR & 0x1F00FF) | ((uint32_t)value << 8); _state.Data[addr & 0xFFF] = value; return;
	case Pic18Sfr::TBLPTRU: _state.TBLPTR = (_state.TBLPTR & 0x0FFFFF) | ((uint32_t)(value & 0x1F) << 16); _state.Data[addr & 0xFFF] = value & 0x1F; return;
	case Pic18Sfr::TABLAT: _state.TABLAT = value; _state.Data[addr & 0xFFF] = value; return;
// Writing PCL performs a computed goto: PC<20:8> from PCLATU:PCLATH, PC<7:0> from the write
		case Pic18Sfr::PCL: {
			_state.Data[addr & 0xFFF] = value;
			uint32_t byteAddr = ((uint32_t)(_state.PCLATU & 0x7F) << 16) | ((uint32_t)_state.PCLATH << 8) | value;
			_state.PC = byteAddr >> 1;
			return;
		}
		case Pic18Sfr::PCLATH: _state.PCLATH = value; _state.Data[addr & 0xFFF] = value; return;
		case Pic18Sfr::PCLATU: _state.PCLATU = value & 0x7F; _state.Data[addr & 0xFFF] = value & 0x7F; return;
		case Pic18Sfr::STATUS: _state.STATUS = value; _state.Data[addr & 0xFFF] = value; return;
	case Pic18Sfr::WREG: _state.W = value; return;  // WREG is a pseudo-register

	default:
		_state.Data[addr & 0xFFF] = value;
		return;
	}
}

uint8_t Pic18Peripherals::GetTosIndex()
{
	// PushStack stores at Stack[STKPTR] then increments, so the most recently pushed
	// entry (the one RETURN/RETFIE pop) sits at STKPTR-1. Writing STKPTR selects which
	// stack level the TOS registers expose (real-hardware stack inspection technique).
	// STKPTR=0 (empty stack) falls back to level 0.
	uint8_t sp = _state.STKPTR & Pic18StkptrBits::STKPTR_MASK;
	return sp == 0 ? 0 : (uint8_t)(sp - 1);
}

void Pic18Peripherals::SetNesIrq(bool active)
{
	if(_irqCallback) {
		_irqCallback(active);
	}
}

uint16_t Pic18Peripherals::GetFSRAddr(int index)
{
	return _state.FSR[index] & 0xFFF;
}

void Pic18Peripherals::UpdateFsr(int index, uint16_t value)
{
	value &= 0xFFF;
	_state.FSR[index] = value;
	static constexpr uint16_t fsrL[3] = { Pic18Sfr::FSR0L, Pic18Sfr::FSR1L, Pic18Sfr::FSR2L };
	static constexpr uint16_t fsrH[3] = { Pic18Sfr::FSR0H, Pic18Sfr::FSR1H, Pic18Sfr::FSR2H };
	_state.Data[fsrL[index] & 0xFFF] = (uint8_t)(value & 0xFF);
	_state.Data[fsrH[index] & 0xFFF] = (uint8_t)((value >> 8) & 0x0F);
}

// Resolve an indirect access (INDF/POSTINC/POSTDEC/PREINC/PLUSW) to its target,
// applying the pointer update for POSTINC/POSTDEC/PREINC exactly once. A
// read-modify-write instruction touches the same indirect register twice (read
// then write); on hardware that is ONE data access with ONE pointer update. The
// read caches the resolved target and the write-back reuses it without a second
// pointer update, so RMW writes land on the same byte the read returned.
uint16_t Pic18Peripherals::AccessIndirect(int index, uint16_t sfrAddr, IndirectOp op, bool isWrite)
{
	if(isWrite && _rmwCached && _rmwSfr == sfrAddr) {
		// RMW write-back: reuse the read's target, skip the second pointer update.
		_rmwCached = false;
		return _rmwTarget;
	}

	uint16_t target;
	switch(op) {
		case IndirectOp::PreInc:
			UpdateFsr(index, (uint16_t)(_state.FSR[index] + 1));
			target = GetFSRAddr(index);
			break;
		case IndirectOp::PostInc:
			target = GetFSRAddr(index);
			UpdateFsr(index, (uint16_t)(_state.FSR[index] + 1));
			break;
		case IndirectOp::PostDec:
			target = GetFSRAddr(index);
			UpdateFsr(index, (uint16_t)(_state.FSR[index] - 1));
			break;
		case IndirectOp::PlusW:
			target = (uint16_t)((_state.FSR[index] + (int8_t)_state.W) & 0xFFF);
			break;
		case IndirectOp::Indf:
		default:
			target = GetFSRAddr(index);
			break;
	}

	if(!isWrite) {
		// A read arms the cache so a RMW write-back can reuse this target.
		_rmwCached = true;
		_rmwSfr = sfrAddr;
		_rmwTarget = target;
	} else {
		_rmwCached = false;
	}
	return target;
}

uint8_t Pic18Peripherals::ReadIndirect(int index, uint16_t sfrAddr, IndirectOp op, bool peek)
{
	if(peek) {
		// Debugger peek: no pointer update, no cache. Read at the un-updated target.
		uint16_t target = (op == IndirectOp::PlusW)
			? (uint16_t)((_state.FSR[index] + (int8_t)_state.W) & 0xFFF)
			: GetFSRAddr(index);
		return ReadSfr(target, true);
	}
	uint16_t target = AccessIndirect(index, sfrAddr, op, false);
	return ReadSfr(target, false);
}

void Pic18Peripherals::WriteIndirect(int index, uint16_t sfrAddr, IndirectOp op, uint8_t value, bool fromDebug)
{
	uint16_t target = AccessIndirect(index, sfrAddr, op, true);
	WriteSfr(target, value, fromDebug);
}

void Pic18Peripherals::Serialize(Serializer& s)
{
	SV(_pspLatchAddr);
	SV(_pspWriteData);
	SV(_pspReadStrobe);
	SV(_pspWriteStrobe);
	SV(_tmr0Prescaler);
	SV(_tmr0PrescalerCounter);
	SV(_tmr1PrescalerCounter);
	SV(_tmr3PrescalerCounter);
	SV(_txBusy);
	SV(_txCycleCounter);
	SV(_rxDataReady);
}
