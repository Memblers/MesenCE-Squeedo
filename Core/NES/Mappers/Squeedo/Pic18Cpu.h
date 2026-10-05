#pragma once
#include "pch.h"
#include "NES/Mappers/Squeedo/Pic18Types.h"
#include "Utilities/ISerializable.h"
#include "Utilities/Serializer.h"

class Pic18Peripherals;

class Pic18Cpu : public ISerializable
{
private:
	Pic18CpuState& _state;
	Pic18Peripherals* _peripherals = nullptr;

	// Inline helpers
	uint16_t ResolveAddr(uint8_t f, bool accessBank);

	__forceinline uint8_t GetF(uint16_t addr, bool accessBank)
	{
		uint16_t fullAddr = accessBank ? GetAccessBankAddr(addr) : ((uint16_t)(_state.BSR << 8) | addr);
		return ReadData(fullAddr);
	}

	__forceinline void PutF(uint16_t addr, uint8_t value, bool accessBank)
	{
		uint16_t fullAddr = accessBank ? GetAccessBankAddr(addr) : ((uint16_t)(_state.BSR << 8) | addr);
		WriteData(fullAddr, value);
	}

	__forceinline uint16_t GetAccessBankAddr(uint16_t addr)
	{
		// Access bank: 0x000-0x07F → low (GPR), 0x080-0x0FF → high (SFRs)
		if(addr < 0x080) {
			return addr;
		}
		return 0xF00 | (addr & 0xFF);
	}

	__forceinline void SetNZ(uint8_t value)
	{
		_state.STATUS &= ~(Pic18StatusBits::Z | Pic18StatusBits::N);
		if(value == 0) _state.STATUS |= Pic18StatusBits::Z;
		if(value & 0x80) _state.STATUS |= Pic18StatusBits::N;
	}

	__forceinline void SetADDFlags(uint8_t a, uint8_t b, uint8_t result)
	{
		_state.STATUS &= ~(Pic18StatusBits::C | Pic18StatusBits::DC | Pic18StatusBits::Z | Pic18StatusBits::OV | Pic18StatusBits::N);
		// Carry
		if((uint16_t)a + (uint16_t)b > 0xFF) _state.STATUS |= Pic18StatusBits::C;
		// Digit carry
		if((a & 0x0F) + (b & 0x0F) > 0x0F) _state.STATUS |= Pic18StatusBits::DC;
		// Zero
		if(result == 0) _state.STATUS |= Pic18StatusBits::Z;
		// Overflow
		if((~(a ^ b) & (a ^ result)) & 0x80) _state.STATUS |= Pic18StatusBits::OV;
		// Negative
		if(result & 0x80) _state.STATUS |= Pic18StatusBits::N;
	}

	__forceinline void SetSUBFlags(uint8_t a, uint8_t b, uint8_t result)
	{
		_state.STATUS &= ~(Pic18StatusBits::C | Pic18StatusBits::DC | Pic18StatusBits::Z | Pic18StatusBits::OV | Pic18StatusBits::N);
		// Carry (no borrow)
		if(a >= b) _state.STATUS |= Pic18StatusBits::C;
		// Digit carry (no borrow)
		if((a & 0x0F) >= (b & 0x0F)) _state.STATUS |= Pic18StatusBits::DC;
		// Zero
		if(result == 0) _state.STATUS |= Pic18StatusBits::Z;
		// Overflow
		if(((a ^ b) & (a ^ result)) & 0x80) _state.STATUS |= Pic18StatusBits::OV;
		// Negative
		if(result & 0x80) _state.STATUS |= Pic18StatusBits::N;
	}

	__forceinline uint16_t GetFSR(int index)
	{
		return _state.FSR[index];
	}

	__forceinline void SetFSR(int index, uint16_t value)
	{
		_state.FSR[index] = value & 0xFFF;
		// Sync Data memory so indirect reads via ReadData() see the updated FSR
		static constexpr uint16_t fsrLAddr[] = { Pic18Sfr::FSR0L & 0xFFF, Pic18Sfr::FSR1L & 0xFFF, Pic18Sfr::FSR2L & 0xFFF };
		static constexpr uint16_t fsrHAddr[] = { Pic18Sfr::FSR0H & 0xFFF, Pic18Sfr::FSR1H & 0xFFF, Pic18Sfr::FSR2H & 0xFFF };
		_state.Data[fsrLAddr[index]] = value & 0xFF;
		_state.Data[fsrHAddr[index]] = (value >> 8) & 0x0F;
	}

	// Stack operations
	void PushStack(uint32_t value);
	uint32_t PopStack();

	// Instruction implementations (return cycles consumed)
	int DecodeAndExecute(uint16_t opcode);

public:
	Pic18Cpu(Pic18CpuState& state);

	void SetPeripherals(Pic18Peripherals* peripherals) { _peripherals = peripherals; }
	Pic18Peripherals* GetPeripherals() { return _peripherals; }

	// Execute PIC cycles, return actual cycles consumed
	int Run(int cyclesToRun);

	// Execute one instruction, return cycles consumed
	int ExecuteInstruction();

	// Memory access (with SFR interception via peripherals)
	uint8_t ReadData(uint16_t addr);
	void WriteData(uint16_t addr, uint8_t value);
	uint8_t PeekData(uint16_t addr);

	// Program memory access (no interception)
	uint8_t ReadProgram(uint32_t addr);
	void WriteProgram(uint32_t addr, uint8_t value);

	// Table read/write (21-bit address)
	uint8_t TableRead();
	void TableWrite(uint8_t value);

	// Check and handle interrupts
	bool CheckInterrupts();

	// Reset CPU to power-on state
	void Reset();

	// Access state
	Pic18CpuState& GetState() { return _state; }

	// ISerializable
	void Serialize(Serializer& s) override;
};