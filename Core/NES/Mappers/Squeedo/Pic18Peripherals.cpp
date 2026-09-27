#include "pch.h"
#include "NES/Mappers/Squeedo/Pic18Peripherals.h"
#include "NES/Mappers/Squeedo/Pic18Cpu.h"
#include "NES/NesConsole.h"
#include "NES/NesCpu.h"

Pic18Peripherals::Pic18Peripherals(Pic18CpuState& state, NesConsole* console)
	: _state(state), _console(console)
{
}

uint8_t Pic18Peripherals::GetTimer0PrescalerDiv()
{
	// T0CON bits 2:0 = T0PS, prescaler divider = 2^(n+1)
	uint8_t ps = _state.Data[Pic18Sfr::T0CON & 0xFFF] & Pic18T0conBits::T0PS_MASK;
	if(_state.Data[Pic18Sfr::T0CON & 0xFFF] & (1 << Pic18T0conBits::PSA)) {
		return 1;  // Prescaler assigned to WDT, timer gets 1:1
	}
	return (uint8_t)(1 << (ps + 1));
}

uint8_t Pic18Peripherals::GetTimer1PrescalerDiv()
{
	uint8_t t1ckps = (_state.Data[Pic18Sfr::T1CON & 0xFFF] & Pic18T1conBits::T1CKPS_MASK) >> 4;
	return (uint8_t)(1 << (t1ckps * 2));  // 00=1:1, 01=1:2, 10=1:4, 11=1:8
}

uint8_t Pic18Peripherals::GetTimer3PrescalerDiv()
{
	uint8_t t3ckps = (_state.Data[Pic18Sfr::T3CON & 0xFFF] & Pic18T3conBits::T3CKPS_MASK) >> 4;
	return (uint8_t)(1 << (t3ckps * 2));
}

void Pic18Peripherals::ClockTimers(int cycles)
{
	uint8_t t0con = _state.Data[Pic18Sfr::T0CON & 0xFFF];
	uint8_t t1con = _state.Data[Pic18Sfr::T1CON & 0xFFF];
	uint8_t t3con = _state.Data[Pic18Sfr::T3CON & 0xFFF];

	// Timer0
	if(t0con & (1 << Pic18T0conBits::TMR0ON)) {
		bool is8Bit = (t0con & (1 << Pic18T0conBits::T08BIT)) != 0;
		bool useInternalClock = !(t0con & (1 << Pic18T0conBits::T0CS));

		if(useInternalClock) {
			uint8_t prescaler = GetTimer0PrescalerDiv();
			for(int i = 0; i < cycles; i++) {
				_tmr0PrescalerCounter++;
				if(_tmr0PrescalerCounter >= prescaler) {
					_tmr0PrescalerCounter = 0;

					uint16_t tmr0;
					if(is8Bit) {
						tmr0 = _state.Data[Pic18Sfr::TMR0L & 0xFFF];
						tmr0++;
						if(tmr0 > 0xFF) {
							tmr0 = _state.Data[Pic18Sfr::TMR0H & 0xFFF];  // Reload
							_state.INTCON |= Pic18IntconBits::TMR0IF;
							_state.InterruptPending = true;
						}
						_state.Data[Pic18Sfr::TMR0L & 0xFFF] = (uint8_t)tmr0;
					} else {
						tmr0 = ((uint16_t)_state.Data[Pic18Sfr::TMR0H & 0xFFF] << 8) | _state.Data[Pic18Sfr::TMR0L & 0xFFF];
						tmr0++;
						if(tmr0 > 0xFFFF) {
							tmr0 = 0;  // Reset (no TMR0H reload in 16-bit mode)
							_state.INTCON |= Pic18IntconBits::TMR0IF;
							_state.InterruptPending = true;
						}
						_state.Data[Pic18Sfr::TMR0L & 0xFFF] = (uint8_t)tmr0;
						_state.Data[Pic18Sfr::TMR0H & 0xFFF] = (uint8_t)(tmr0 >> 8);
					}
				}
			}
		}
	}

	// Timer1
	if(t1con & (1 << Pic18T1conBits::TMR1ON)) {
		bool useInternalClock = !(t1con & (1 << Pic18T1conBits::TMR1CS));
		if(useInternalClock) {
			uint8_t prescaler = GetTimer1PrescalerDiv();
			for(int i = 0; i < cycles; i++) {
				_tmr1PrescalerCounter++;
				if(_tmr1PrescalerCounter >= prescaler) {
					_tmr1PrescalerCounter = 0;
					uint16_t tmr1 = ((uint16_t)_state.Data[Pic18Sfr::TMR1H & 0xFFF] << 8) | _state.Data[Pic18Sfr::TMR1L & 0xFFF];
					tmr1++;
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
	}

	// Timer3
	if(t3con & (1 << Pic18T3conBits::TMR3ON)) {
		bool useInternalClock = !(t3con & (1 << Pic18T3conBits::TMR3CS));
		if(useInternalClock) {
			uint8_t prescaler = GetTimer3PrescalerDiv();
			for(int i = 0; i < cycles; i++) {
				_tmr3PrescalerCounter++;
				if(_tmr3PrescalerCounter >= prescaler) {
					_tmr3PrescalerCounter = 0;
					uint16_t tmr3 = ((uint16_t)_state.Data[Pic18Sfr::TMR3H & 0xFFF] << 8) | _state.Data[Pic18Sfr::TMR3L & 0xFFF];
					tmr3++;
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

uint8_t Pic18Peripherals::NesRead(uint8_t regAddr)
{
	// NES reading from PSP — return whatever PIC has pre-loaded on PortD
	_state.PspObf = false;
	return _state.PspPortDOutput;
}

void Pic18Peripherals::NesWrite(uint8_t regAddr, uint8_t value)
{
	// NES writing to PSP
	_pspLatchAddr = regAddr & 0x1F;
	_pspWriteData = value;
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

uint8_t Pic18Peripherals::ReadSfr(uint16_t addr)
{
	switch(addr) {
	// Timer reads with latching
	case Pic18Sfr::TMR0L:
		_state.TMR0ReadLatch = _state.Data[Pic18Sfr::TMR0H & 0xFFF];
		return _state.Data[Pic18Sfr::TMR0L & 0xFFF];
	case Pic18Sfr::TMR0H:
		return _state.TMR0ReadLatch;

	case Pic18Sfr::TMR1L:
		_state.TMR1ReadLatch = _state.Data[Pic18Sfr::TMR1H & 0xFFF];
		return _state.Data[Pic18Sfr::TMR1L & 0xFFF];
	case Pic18Sfr::TMR1H:
		return _state.TMR1ReadLatch;

	case Pic18Sfr::TMR3L:
		_state.TMR3ReadLatch = _state.Data[Pic18Sfr::TMR3H & 0xFFF];
		return _state.Data[Pic18Sfr::TMR3L & 0xFFF];
	case Pic18Sfr::TMR3H:
		return _state.TMR3ReadLatch;

	// UART receive
	case Pic18Sfr::RCREG:
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

	// FSR reads - return live values from _state.FSR (not stale Data memory)
	case Pic18Sfr::FSR0L: return _state.FSR[0] & 0xFF;
	case Pic18Sfr::FSR0H: return (_state.FSR[0] >> 8) & 0x0F;
	case Pic18Sfr::FSR1L: return _state.FSR[1] & 0xFF;
	case Pic18Sfr::FSR1H: return (_state.FSR[1] >> 8) & 0x0F;
	case Pic18Sfr::FSR2L: return _state.FSR[2] & 0xFF;
	case Pic18Sfr::FSR2H: return (_state.FSR[2] >> 8) & 0x0F;

	// INDF reads (indirect addressing)
	case Pic18Sfr::INDF0: return ReadSfr(GetFSRAddr(0));
	case Pic18Sfr::POSTINC0: { uint8_t v = ReadSfr(GetFSRAddr(0)); _state.FSR[0]++; return v; }
	case Pic18Sfr::POSTDEC0: { uint8_t v = ReadSfr(GetFSRAddr(0)); _state.FSR[0]--; return v; }
	case Pic18Sfr::PREINC0: { _state.FSR[0]++; return ReadSfr(GetFSRAddr(0)); }
	case Pic18Sfr::PLUSW0: return ReadSfr((_state.FSR[0] + (int8_t)_state.W) & 0xFFF);
	case Pic18Sfr::INDF1: return ReadSfr(GetFSRAddr(1));
	case Pic18Sfr::POSTINC1: { uint8_t v = ReadSfr(GetFSRAddr(1)); _state.FSR[1]++; return v; }
	case Pic18Sfr::POSTDEC1: { uint8_t v = ReadSfr(GetFSRAddr(1)); _state.FSR[1]--; return v; }
	case Pic18Sfr::PREINC1: { _state.FSR[1]++; return ReadSfr(GetFSRAddr(1)); }
	case Pic18Sfr::PLUSW1: return ReadSfr((_state.FSR[1] + (int8_t)_state.W) & 0xFFF);
	case Pic18Sfr::INDF2: return ReadSfr(GetFSRAddr(2));
	case Pic18Sfr::POSTINC2: { uint8_t v = ReadSfr(GetFSRAddr(2)); _state.FSR[2]++; return v; }
	case Pic18Sfr::POSTDEC2: { uint8_t v = ReadSfr(GetFSRAddr(2)); _state.FSR[2]--; return v; }
	case Pic18Sfr::PREINC2: { _state.FSR[2]++; return ReadSfr(GetFSRAddr(2)); }
	case Pic18Sfr::PLUSW2: return ReadSfr((_state.FSR[2] + (int8_t)_state.W) & 0xFFF);

	default:
		return _state.Data[addr & 0xFFF];
	}
}

void Pic18Peripherals::WriteSfr(uint16_t addr, uint8_t value)
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
		return;

	case Pic18Sfr::PORTA:
		_state.Data[Pic18Sfr::LATA & 0xFFF] = value;
		_state.Data[Pic18Sfr::PORTA & 0xFFF] = value;
		return;
	case Pic18Sfr::PORTB:
		_state.Data[Pic18Sfr::LATB & 0xFFF] = value;
		_state.Data[Pic18Sfr::PORTB & 0xFFF] = value;
		return;
	case Pic18Sfr::PORTC:
		_state.Data[Pic18Sfr::LATC & 0xFFF] = value;
		_state.Data[Pic18Sfr::PORTC & 0xFFF] = value;
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

	case Pic18Sfr::LATA: _state.Data[Pic18Sfr::LATA & 0xFFF] = value; _state.Data[Pic18Sfr::PORTA & 0xFFF] = value; return;
	case Pic18Sfr::LATB: _state.Data[Pic18Sfr::LATB & 0xFFF] = value; _state.Data[Pic18Sfr::PORTB & 0xFFF] = value; return;
	case Pic18Sfr::LATC: _state.Data[Pic18Sfr::LATC & 0xFFF] = value; _state.Data[Pic18Sfr::PORTC & 0xFFF] = value; return;
	case Pic18Sfr::LATD: _state.Data[Pic18Sfr::LATD & 0xFFF] = value; _state.Data[Pic18Sfr::PORTD & 0xFFF] = value; _state.PspPortDOutput = value; return;
	case Pic18Sfr::LATE: _state.Data[Pic18Sfr::LATE & 0xFFF] = value; _state.Data[Pic18Sfr::PORTE & 0xFFF] = value; return;

	// Indirect writes
	case Pic18Sfr::INDF0: WriteSfr(GetFSRAddr(0), value); return;
	case Pic18Sfr::POSTINC0: { WriteSfr(GetFSRAddr(0), value); uint16_t v = (_state.FSR[0] + 1) & 0xFFF; _state.FSR[0] = v; _state.Data[Pic18Sfr::FSR0L & 0xFFF] = v & 0xFF; _state.Data[Pic18Sfr::FSR0H & 0xFFF] = (v >> 8) & 0x0F; return; }
	case Pic18Sfr::POSTDEC0: { WriteSfr(GetFSRAddr(0), value); uint16_t v = (_state.FSR[0] - 1) & 0xFFF; _state.FSR[0] = v; _state.Data[Pic18Sfr::FSR0L & 0xFFF] = v & 0xFF; _state.Data[Pic18Sfr::FSR0H & 0xFFF] = (v >> 8) & 0x0F; return; }
	case Pic18Sfr::PREINC0: { uint16_t v = (_state.FSR[0] + 1) & 0xFFF; _state.FSR[0] = v; _state.Data[Pic18Sfr::FSR0L & 0xFFF] = v & 0xFF; _state.Data[Pic18Sfr::FSR0H & 0xFFF] = (v >> 8) & 0x0F; WriteSfr(v, value); return; }
	case Pic18Sfr::PLUSW0: WriteSfr((_state.FSR[0] + (int8_t)_state.W) & 0xFFF, value); return;
	case Pic18Sfr::INDF1: WriteSfr(GetFSRAddr(1), value); return;
	case Pic18Sfr::POSTINC1: { WriteSfr(GetFSRAddr(1), value); uint16_t v = (_state.FSR[1] + 1) & 0xFFF; _state.FSR[1] = v; _state.Data[Pic18Sfr::FSR1L & 0xFFF] = v & 0xFF; _state.Data[Pic18Sfr::FSR1H & 0xFFF] = (v >> 8) & 0x0F; return; }
	case Pic18Sfr::POSTDEC1: { WriteSfr(GetFSRAddr(1), value); uint16_t v = (_state.FSR[1] - 1) & 0xFFF; _state.FSR[1] = v; _state.Data[Pic18Sfr::FSR1L & 0xFFF] = v & 0xFF; _state.Data[Pic18Sfr::FSR1H & 0xFFF] = (v >> 8) & 0x0F; return; }
	case Pic18Sfr::PREINC1: { uint16_t v = (_state.FSR[1] + 1) & 0xFFF; _state.FSR[1] = v; _state.Data[Pic18Sfr::FSR1L & 0xFFF] = v & 0xFF; _state.Data[Pic18Sfr::FSR1H & 0xFFF] = (v >> 8) & 0x0F; WriteSfr(v, value); return; }
	case Pic18Sfr::PLUSW1: WriteSfr((_state.FSR[1] + (int8_t)_state.W) & 0xFFF, value); return;
	case Pic18Sfr::INDF2: WriteSfr(GetFSRAddr(2), value); return;
	case Pic18Sfr::POSTINC2: { WriteSfr(GetFSRAddr(2), value); uint16_t v = (_state.FSR[2] + 1) & 0xFFF; _state.FSR[2] = v; _state.Data[Pic18Sfr::FSR2L & 0xFFF] = v & 0xFF; _state.Data[Pic18Sfr::FSR2H & 0xFFF] = (v >> 8) & 0x0F; return; }
	case Pic18Sfr::POSTDEC2: { WriteSfr(GetFSRAddr(2), value); uint16_t v = (_state.FSR[2] - 1) & 0xFFF; _state.FSR[2] = v; _state.Data[Pic18Sfr::FSR2L & 0xFFF] = v & 0xFF; _state.Data[Pic18Sfr::FSR2H & 0xFFF] = (v >> 8) & 0x0F; return; }
	case Pic18Sfr::PREINC2: { uint16_t v = (_state.FSR[2] + 1) & 0xFFF; _state.FSR[2] = v; _state.Data[Pic18Sfr::FSR2L & 0xFFF] = v & 0xFF; _state.Data[Pic18Sfr::FSR2H & 0xFFF] = (v >> 8) & 0x0F; WriteSfr(v, value); return; }
	case Pic18Sfr::PLUSW2: WriteSfr((_state.FSR[2] + (int8_t)_state.W) & 0xFFF, value); return;

	// Core registers that should update the state struct
	case Pic18Sfr::INTCON: _state.INTCON = value; _state.Data[addr & 0xFFF] = value; return;
	case Pic18Sfr::RCON: _state.RCON = value; _state.Data[addr & 0xFFF] = value; return;
	case Pic18Sfr::STKPTR: _state.STKPTR = value; _state.Data[addr & 0xFFF] = value; return;
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
	case Pic18Sfr::PCL: _state.Data[addr & 0xFFF] = value; return;  // PCL writes update PC low byte
	case Pic18Sfr::WREG: _state.W = value; return;  // WREG is a pseudo-register

	default:
		_state.Data[addr & 0xFFF] = value;
		return;
	}
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