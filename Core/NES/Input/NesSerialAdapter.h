#pragma once
#include "pch.h"
#include "Shared/BaseControlDevice.h"
#include "Shared/Emulator.h"
#include "NES/NesConsole.h"
#include "NES/NesConstants.h"
#include "Utilities/Serializer.h"

class SerialPortBridge;

// NES Serial Port Adapter — async bit-banged serial via controller port.
//
// Hardware: TTL-to-RS232 adapter in controller port.
//   TX: NES writes $4016 OUT0 = serial line (async bit-banged)
//   RX: NES reads $4017 D0 = serial receive line
//
// This is a controller port device. Placed on Port 2 ($4017 for RX).
// Monitors ALL $4016 writes for TX (OUT0), regardless of which port
// the NES reads for RX.
//
// The NES software drives the baud rate timing. The adapter samples
// the line at the configured baud rate to reconstruct bytes.
class NesSerialAdapter : public BaseControlDevice
{
private:
	// Baud rate config
	uint32_t _baudRate = 9600;
	uint8_t _stopBits = 1;
	uint32_t _bitCycles = 0;  // NES CPU cycles per serial bit

	// ---- TX state: reconstruct bytes from $4016 OUT0 writes ----
	// Async serial frame: [idle=HIGH] [start=LOW] [D0..D7] [stop=HIGH]
	// After falling edge (start bit), sample data bits at bit-boundary intervals.
	// NES software writes $4016 once per bit period to set OUT0.
	bool _txLineState = true;          // Current OUT0 line state
	bool _txActive = false;            // Currently receiving a frame?
	uint64_t _txStartCycle = 0;        // NES cycle of falling edge (start bit)
	uint8_t _txNextBitIndex = 0;       // Next data bit to sample (0-7)
	uint8_t _txShiftReg = 0;           // Assembled byte
	uint64_t _txNextSampleCycle = 0;   // NES cycle for next bit sample

	// ---- RX state: stream bits to NES via $4017 D0 reads ----
	// Async serial frame presented on the line:
	//   [start=HIGH(inverted)] [D0..D7(inverted)] [stop=LOW(inverted)]
	//   Total bits per frame: 1 + 8 + stopBits = 10 or 11
	// Bit index 0 = start bit, 1-8 = data, 9+ = stop bits
	deque<uint8_t> _rxByteQueue;       // Bytes waiting to be sent to NES
	uint8_t _rxShiftReg = 0;           // Current byte (inverted) being shifted out
	uint8_t _rxTotalBits = 0;          // Total bits in current frame (1+8+stopBits)
	uint8_t _rxBitIndex = 0;           // Current bit position (0=start, 1-8=data, 9+=stop)
	uint64_t _rxFrameStartCycle = 0;   // When current RX frame started
	bool _rxActive = false;            // Currently presenting a frame?

	// Pointer to the serial port bridge (set externally)
	SerialPortBridge* _bridge = nullptr;

protected:
	void Serialize(Serializer& s) override
	{
		BaseControlDevice::Serialize(s);
		SV(_baudRate);
		SV(_stopBits);
		SV(_bitCycles);
		SV(_txLineState);
		SV(_txActive);
		SV(_txStartCycle);
		SV(_txNextBitIndex);
		SV(_txShiftReg);
		SV(_txNextSampleCycle);
		SV(_rxShiftReg);
		SV(_rxTotalBits);
		SV(_rxBitIndex);
		SV(_rxFrameStartCycle);
		SV(_rxActive);
	}

	string GetKeyNames() override { return ""; }
	void InternalSetStateFromInput() override {}

	uint64_t GetNesCycles()
	{
		return ((NesConsole*)_emu->GetConsole().get())->GetMasterClock();
	}

	uint32_t GetCpuClockRate()
	{
		return NesConstants::GetClockRate(((NesConsole*)_emu->GetConsole().get())->GetRegion());
	}

	void UpdateBitCycles()
	{
		if(_baudRate == 0) _baudRate = 9600;
		_bitCycles = GetCpuClockRate() / _baudRate;
	}

	// Process TX: sample data bits based on timing since start bit
	void ProcessTx(uint64_t now, bool lineState)
	{
		if(!_txActive) return;

		// Catch up: sample any bits whose sample cycle has passed
		while(_txNextBitIndex < 8 && now >= _txNextSampleCycle) {
			// Sample the line at this point. 
			// The line state between writes represents the bit value.
			// At the sample cycle, the line was in the state set by the
			// most recent write before that cycle. Since we're called on
			// each write, the _txLineState at entry was active until this write.
			if(lineState) {
				_txShiftReg |= (1 << _txNextBitIndex);
			}
			_txNextBitIndex++;
			_txNextSampleCycle += _bitCycles;
		}

		if(_txNextBitIndex >= 8) {
			// All 8 data bits collected — frame complete
			if(_bridge) {
				_bridge->SendByte(_txShiftReg);
			}
			_txActive = false;
		}
	}

	// Start a new RX frame for a byte from the host serial port
	void StartRxFrame(uint8_t byte)
	{
		// Real hardware: RS232 -> TTL -> inverting buffer -> NES D0
		// Present the inverted signal: idle=LOW, start=HIGH, data inverted
		_rxShiftReg = ~byte;
		_rxBitIndex = 0;
		_rxTotalBits = 1 + 8 + _stopBits;  // start + 8 data + stop
		_rxFrameStartCycle = GetNesCycles();
		_rxActive = true;
	}

public:
	NesSerialAdapter(Emulator* emu, uint8_t port, KeyMappingSet keyMappings)
		: BaseControlDevice(emu, ControllerType::NesSerialAdapter, port, keyMappings)
	{
		UpdateBitCycles();
	}

	void SetBridge(SerialPortBridge* bridge) { _bridge = bridge; }
	void SetBaudRate(uint32_t baud) { _baudRate = baud; UpdateBitCycles(); }
	void SetStopBits(uint8_t bits) { _stopBits = bits; }

	// $4016 write — OUT0 (bit 0) is the TX serial line
	void WriteRam(uint16_t addr, uint8_t value) override
	{
		if(addr != 0x4016) return;

		bool newLineState = (value & 0x01) != 0;
		uint64_t now = GetNesCycles();

		// Process any pending TX bits BEFORE updating line state
		// (the bit value is what was on the line before this write)
		if(_txActive) {
			ProcessTx(now, _txLineState);
		}

		// Detect falling edge = start bit
		if(_txLineState && !newLineState && !_txActive) {
			_txActive = true;
			_txStartCycle = now;
			_txNextBitIndex = 0;
			_txShiftReg = 0;
			// First data bit sample at: startCycle + 1.5 * bitCycles
			// (middle of first data bit, after start bit)
			_txNextSampleCycle = now + _bitCycles + (_bitCycles >> 1);
		}

		_txLineState = newLineState;
	}

	// $4017 read — D0 (bit 0) is the RX serial line
	uint8_t ReadRam(uint16_t addr) override
	{
		if(addr != 0x4017) return 0;
		if(!IsCurrentPort(addr)) return 0;

		uint64_t now = GetNesCycles();

		// Drain bridge RX queue into our byte queue
		if(_bridge) {
			uint8_t buf[64];
			int n = _bridge->DrainRxBytes(buf, sizeof(buf));
			for(int i = 0; i < n; i++) {
				_rxByteQueue.push_back(buf[i]);
			}
		}

		// If idle and there's a byte waiting, start a new RX frame
		if(!_rxActive && !_rxByteQueue.empty()) {
			StartRxFrame(_rxByteQueue.front());
			_rxByteQueue.pop_front();
		}

		// Present current bit based on timing
		if(_rxActive) {
			uint64_t elapsed = now - _rxFrameStartCycle;
			uint8_t currentBit = (uint8_t)(elapsed / _bitCycles);

			if(currentBit >= _rxTotalBits) {
				// Frame complete — go idle
				_rxActive = false;
			} else {
				_rxBitIndex = currentBit;
				if(_rxBitIndex == 0) {
					// Start bit — inverted = HIGH
					return 0x01;
				} else if(_rxBitIndex <= 8) {
					// Data bits (already inverted in _rxShiftReg)
					return (_rxShiftReg >> (_rxBitIndex - 1)) & 0x01;
				} else {
					// Stop bit — inverted = LOW
					return 0x00;
				}
			}
		}

		// Idle — inverted = LOW
		return 0x00;
	}

	// Called by the serial bridge to inject received bytes
	void QueueRxByte(uint8_t byte)
	{
		_rxByteQueue.push_back(byte);
	}
};