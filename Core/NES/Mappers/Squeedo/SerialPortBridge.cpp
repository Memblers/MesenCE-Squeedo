#include "pch.h"
#include "NES/Mappers/Squeedo/SerialPortBridge.h"
#include "NES/Mappers/Squeedo/Pic18Peripherals.h"
#include "Utilities/HexUtilities.h"

int SerialPortBridge::PollReceivedBytes(Pic18Peripherals* peripherals)
{
	if(!peripherals) return 0;

	int count = 0;
	LockHandler lock = _rxLock.AcquireSafe();
	while(!_rxQueue.empty()) {
		uint8_t byte = _rxQueue.front();
		_rxQueue.pop_front();
		peripherals->UartReceiveByte(byte);
		count++;

		// Log first 20 bytes received
		uint64_t n = _rxBytes.load(std::memory_order_relaxed);
		if(n <= 20) {
			MessageManager::Log("[Squeedo] RX byte #" + std::to_string(n) + ": 0x" + HexUtilities::ToHex(byte) + " ('" + (char)(byte >= 0x20 && byte < 0x7F ? byte : '.') + "')");
		}
	}
	return count;
}