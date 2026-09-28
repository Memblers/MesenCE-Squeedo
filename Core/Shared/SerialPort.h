#pragma once
#include "pch.h"

// Minimal cross-platform serial port class for host ↔ emulated UART bridge.
// Supports Windows (COM ports), Linux (/dev/ttyS*, /dev/ttyUSB*), and macOS (/dev/cu.*).
//
// Implementation is in SerialPort.cpp to keep platform headers out of the PCH.
class SerialPort
{
private:
	void* _handle = nullptr;  // Opaque platform handle (HANDLE on Windows, fd on Unix)

public:
	SerialPort();
	~SerialPort();

	// Non-copyable
	SerialPort(const SerialPort&) = delete;
	SerialPort& operator=(const SerialPort&) = delete;

	bool IsOpen() const;
	bool Open(const string& portName, uint32_t baudRate);
	void Close();

	// Write bytes to the serial port. Returns number of bytes written.
	int Write(const uint8_t* data, int len);

	// Read bytes from the serial port. Returns number of bytes read (0 on timeout).
	// timeoutMs: 0 = non-blocking poll, >0 = wait up to N ms
	int Read(uint8_t* buffer, int maxLen, uint32_t timeoutMs = 100);
};