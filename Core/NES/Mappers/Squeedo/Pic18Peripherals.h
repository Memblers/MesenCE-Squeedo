#pragma once
#include "pch.h"
#include "NES/Mappers/Squeedo/Pic18Types.h"
#include "Utilities/ISerializable.h"
#include "Utilities/Serializer.h"

class Pic18Cpu;
class NesConsole;

class Pic18Peripherals : public ISerializable
{
private:
	Pic18CpuState& _state;
	Pic18Cpu* _cpu = nullptr;
	NesConsole* _console = nullptr;

	// PSP (Parallel Slave Port) state
	uint8_t _pspLatchAddr = 0;    // Latched A0-A4 from NES address
	uint8_t _pspWriteData = 0;    // Data written by NES
	bool _pspReadStrobe = false;  // NES read active
	bool _pspWriteStrobe = false; // NES write active

	// Timer0 state (16-bit)
	uint16_t _tmr0Prescaler = 0;
	uint16_t _tmr0PrescalerCounter = 0;  // uint16 needed for bulk arithmetic (prescaler up to 256)

	// Timer1 state (16-bit)
	uint16_t _tmr1PrescalerCounter = 0;

	// Timer3 state (16-bit)
	uint16_t _tmr3PrescalerCounter = 0;

	// UART state
	bool _txBusy = false;
	uint8_t _txShiftReg = 0;
	int _txBitCount = 0;
	int _txCycleCounter = 0;
	uint8_t _rxShiftReg = 0;
	int _rxBitCount = 0;
	int _rxCycleCounter = 0;
	bool _rxDataReady = false;
	deque<uint8_t> _rxBuffer;

	// IRQ callback to mapper
	std::function<void(bool)> _irqCallback;

	// Set by WriteSfr when PORTA/B/C or LATA/B/C are written,
	// so the mapper can skip ApplyGpioBanking when nothing changed.
	bool _gpioDirty = false;

	// Helper to get prescaler bits
	uint8_t GetTimer0PrescalerDiv();
	uint8_t GetTimer1PrescalerDiv();
	uint8_t GetTimer3PrescalerDiv();
	uint16_t GetFSRAddr(int index);

public:
	Pic18Peripherals(Pic18CpuState& state, NesConsole* console);

	void SetCpu(Pic18Cpu* cpu) { _cpu = cpu; }
	void SetIrqCallback(std::function<void(bool)> callback) { _irqCallback = callback; }

	// Called once per PIC instruction cycle
	void ClockTimers(int cycles);

	// Returns true if PORTA/B/C were written since last check (clears the flag)
	bool CheckAndClearGpioDirty() { if(_gpioDirty) { _gpioDirty = false; return true; } return false; }

	// NES-side PSP interface
	uint8_t NesRead(uint8_t regAddr);
	void NesWrite(uint8_t regAddr, uint8_t value);

	// UART interface (for future MIDI/serial support)
	void UartReceiveByte(uint8_t byte);
	bool UartHasReceivedData() const { return _rxDataReady; }

	// SFR read/write (called by PIC18Cpu)
	uint8_t ReadSfr(uint16_t addr);
	void WriteSfr(uint16_t addr, uint8_t value);

	// Assert/de-assert NES IRQ
	void SetNesIrq(bool active);

	void Serialize(Serializer& s) override;
};