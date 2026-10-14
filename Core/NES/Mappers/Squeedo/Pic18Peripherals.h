#pragma once
#include "pch.h"
#include "NES/Mappers/Squeedo/Pic18Types.h"
#include "Utilities/ISerializable.h"
#include "Utilities/Serializer.h"

class Pic18Cpu;
class NesConsole;
class Emulator;

// Indirect addressing modes through FSR0/1/2 (INDF/POSTINC/POSTDEC/PREINC/PLUSW).
enum class IndirectOp { Indf, PostInc, PostDec, PreInc, PlusW };

class Pic18Peripherals : public ISerializable
{
private:
	Pic18CpuState& _state;
	Pic18Cpu* _cpu = nullptr;
	NesConsole* _console = nullptr;
	Emulator* _emu = nullptr;  // non-owning; used to raise debug memory events

	// PSP (Parallel Slave Port) state
	uint8_t _pspLatchAddr = 0;    // Latched A0-A4 from NES address
	uint8_t _pspWriteData = 0;    // Data written by NES
	bool _pspReadStrobe = false;  // NES read active
	bool _pspWriteStrobe = false; // NES write active
        uint8_t _sfrRecursionDepth = 0; // FSR-indirect reentrancy guard

        // Timer0 state (16-bit)
        uint16_t _tmr0Prescaler = 0;
        uint16_t _tmr0PrescalerCounter = 0;

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

	// Callbacks to mapper
	std::function<void(bool)> _irqCallback;
	std::function<void(uint8_t)> _txCallback;

	// Set by WriteSfr when PORTA/B/C or LATA/B/C are written,
	// so the mapper can skip ApplyGpioBanking when nothing changed.
	bool _gpioDirty = false;

	// Helper to get prescaler bits
	uint8_t GetTimer0PrescalerDiv();
	uint8_t GetTimer1PrescalerDiv();
	uint8_t GetTimer3PrescalerDiv();
	uint16_t GetFSRAddr(int index);
	uint8_t GetTosIndex();

	// RMW cache: a read-modify-write instruction (e.g. INCF POSTINC2,F) touches the
	// same indirect register twice. The read resolves the target and applies the one
	// pointer update; the write-back reuses the cached target so the pointer updates
	// only once and both halves hit the same address. See AccessIndirect.
	bool _rmwCached = false;
	uint16_t _rmwSfr = 0;
	uint16_t _rmwTarget = 0;
	void UpdateFsr(int index, uint16_t value);
	uint16_t AccessIndirect(int index, uint16_t sfrAddr, IndirectOp op, bool isWrite);
	uint8_t ReadIndirect(int index, uint16_t sfrAddr, IndirectOp op, bool peek);
	void WriteIndirect(int index, uint16_t sfrAddr, IndirectOp op, uint8_t value, bool fromDebug);
	void WriteSfrInternal(uint16_t addr, uint8_t value, bool fromDebug);

public:
	Pic18Peripherals(Pic18CpuState& state, NesConsole* console);

	void SetCpu(Pic18Cpu* cpu) { _cpu = cpu; }
	void SetEmulator(Emulator* emu) { _emu = emu; }
	void SetIrqCallback(std::function<void(bool)> callback) { _irqCallback = callback; }
	void SetTxCallback(std::function<void(uint8_t)> callback) { _txCallback = callback; }

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
	// peek=true returns the same value a read would, but without side effects
	// (no FSR auto-inc/dec, no timer latches, no RCREG pop, no IBF clear)
	uint8_t ReadSfr(uint16_t addr, bool peek = false);
	// Returns false if the write was dropped (frozen address); fromDebug writes always apply.
	bool WriteSfr(uint16_t addr, uint8_t value, bool fromDebug = false);

	// Called at instruction boundaries (RAII in the CPU) so a RMW's cached indirect
	// target can't leak into the next instruction or an out-of-band access.
	void ResetRmwState() { _rmwCached = false; }

	// Assert/de-assert NES IRQ
	void SetNesIrq(bool active);

	void Serialize(Serializer& s) override;
};