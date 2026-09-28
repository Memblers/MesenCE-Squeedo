#pragma once
#include "pch.h"
#include "Shared/SerialPort.h"
#include "Utilities/SimpleLock.h"
#include "Shared/MessageManager.h"
#include "Utilities/HexUtilities.h"

class Pic18Peripherals;

// Bridges a host serial port to the PIC18 UART inside the Squeedo mapper.
//
// Threading model:
//   - Emulation thread: calls SendByte() when PIC writes TXREG,
//                       calls PollReceivedBytes() each NES cycle to feed RX
//   - Background thread: reads from serial port → RX queue,
//                        drains TX queue → writes to serial port
class SerialPortBridge
{
private:
	SerialPort _port;
	std::thread _thread;
	std::atomic<bool> _running = false;

	// TX queue: emulation thread → serial port
	deque<uint8_t> _txQueue;
	SimpleLock _txLock;

	// RX queue: serial port → emulation thread
	deque<uint8_t> _rxQueue;
	SimpleLock _rxLock;

	// Error reporting (log once, not spammy)
	std::atomic<bool> _portError = false;

	// Diagnostic counters
	std::atomic<uint64_t> _txBytes { 0 };
	std::atomic<uint64_t> _rxBytes { 0 };
	std::chrono::steady_clock::time_point _lastStatusTime;

	void ThreadFunc()
	{
		uint8_t readBuf[256];
		uint8_t txBuf[256];

		while(_running.load(std::memory_order_relaxed)) {
			// Drain TX queue → write to serial port
			int txCount = 0;
			{
				LockHandler lock = _txLock.AcquireSafe();
				while(!_txQueue.empty() && txCount < (int)sizeof(txBuf)) {
					txBuf[txCount++] = _txQueue.front();
					_txQueue.pop_front();
				}
			}
			if(txCount > 0) {
				int written = _port.Write(txBuf, txCount);
				// If write fails, port may have disconnected
				if(written == 0 && txCount > 0) {
					_portError.store(true, std::memory_order_relaxed);
					break;
				}
			}

			// Read from serial port → RX queue
			// Use a short timeout so we loop frequently for TX
			int n = _port.Read(readBuf, sizeof(readBuf), 10);
			if(n > 0) {
				LockHandler lock = _rxLock.AcquireSafe();
				for(int i = 0; i < n; i++) {
					_rxQueue.push_back(readBuf[i]);
				}
				_rxBytes.fetch_add(n, std::memory_order_relaxed);
			}
		}
	}

public:
	SerialPortBridge() = default;
	~SerialPortBridge() { Stop(); }

	// Non-copyable
	SerialPortBridge(const SerialPortBridge&) = delete;
	SerialPortBridge& operator=(const SerialPortBridge&) = delete;

	bool Start(const string& portName, uint32_t baudRate)
	{
		Stop();

		if(!_port.Open(portName, baudRate)) {
			MessageManager::Log("[Squeedo] Failed to open serial port: " + portName);
			return false;
		}

		_running.store(true, std::memory_order_relaxed);
		_portError.store(false, std::memory_order_relaxed);
		_txBytes.store(0, std::memory_order_relaxed);
		_rxBytes.store(0, std::memory_order_relaxed);
		_lastStatusTime = std::chrono::steady_clock::now();
		_thread = std::thread(&SerialPortBridge::ThreadFunc, this);

		MessageManager::Log("[Squeedo] Serial port opened: " + portName + " @ " + std::to_string(baudRate));
		return true;
	}

	void Stop()
	{
		_running.store(false, std::memory_order_relaxed);
		if(_thread.joinable()) {
			_thread.join();
		}
		_port.Close();

		// Drain queues
		{
			LockHandler lock = _txLock.AcquireSafe();
			_txQueue.clear();
		}
		{
			LockHandler lock = _rxLock.AcquireSafe();
			_rxQueue.clear();
		}
	}

	bool IsRunning() const { return _running.load(std::memory_order_relaxed) && _port.IsOpen(); }
	bool HasError() const { return _portError.load(std::memory_order_relaxed); }

	// Called by emulation thread when PIC writes TXREG
	void SendByte(uint8_t byte)
	{
		if(!IsRunning()) return;
		LockHandler lock = _txLock.AcquireSafe();
		_txQueue.push_back(byte);
		_txBytes.fetch_add(1, std::memory_order_relaxed);

		// Log first 20 bytes sent
		uint64_t n = _txBytes.load(std::memory_order_relaxed);
		if(n <= 20) {
			MessageManager::Log("[Squeedo] TX byte #" + std::to_string(n) + ": 0x" + HexUtilities::ToHex(byte) + " ('" + (char)(byte >= 0x20 && byte < 0x7F ? byte : '.') + "')");
		}
	}

	// Called by emulation thread each NES cycle to drain RX queue into PIC
	// Returns number of bytes fed to the PIC
	int PollReceivedBytes(Pic18Peripherals* peripherals);

	// Called by emulation thread to periodically log status
	void PollStatusLog()
	{
		auto now = std::chrono::steady_clock::now();
		if(std::chrono::duration_cast<std::chrono::seconds>(now - _lastStatusTime).count() >= 5) {
			_lastStatusTime = now;
			uint64_t tx = _txBytes.load(std::memory_order_relaxed);
			uint64_t rx = _rxBytes.load(std::memory_order_relaxed);
			bool err = _portError.load(std::memory_order_relaxed);
			MessageManager::Log("[Squeedo] Serial status: TX=" + std::to_string(tx) + " RX=" + std::to_string(rx) + (err ? " ERROR" : " OK"));
		}
	}
};