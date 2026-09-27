#pragma once
#include "pch.h"

class IntelHexParser
{
public:
	// Parse Intel HEX file and load into target buffer
	// Returns true on success, false on parse error
	// targetAddr: base address offset to subtract from HEX addresses
	static bool Parse(const vector<uint8_t>& hexData, uint8_t* target, uint32_t targetSize, uint32_t targetAddr = 0)
	{
		string line;
		uint32_t extAddr = 0;  // Extended address (upper 16 bits)

		size_t pos = 0;
		while(pos < hexData.size()) {
			line.clear();
			while(pos < hexData.size() && hexData[pos] != '\n' && hexData[pos] != '\r') {
				line += (char)hexData[pos++];
			}
			while(pos < hexData.size() && (hexData[pos] == '\n' || hexData[pos] == '\r')) {
				pos++;
			}

			if(line.empty()) continue;
			if(line[0] != ':') return false;

			uint8_t byteCount = HexToByte(line.substr(1, 2));
			uint16_t address = (HexToByte(line.substr(3, 2)) << 8) | HexToByte(line.substr(5, 2));
			uint8_t recordType = HexToByte(line.substr(7, 2));

			if(recordType == 0x01) break;  // EOF

			if(recordType == 0x02) {
				// Extended Segment Address
				extAddr = (uint32_t)HexToByte(line.substr(9, 2)) << 12;
				extAddr |= (uint32_t)HexToByte(line.substr(11, 2)) << 4;
				continue;
			}

			if(recordType == 0x04) {
				// Extended Linear Address
				extAddr = (uint32_t)HexToByte(line.substr(9, 2)) << 24;
				extAddr |= (uint32_t)HexToByte(line.substr(11, 2)) << 16;
				continue;
			}

			if(recordType == 0x00) {
				// Data record
				// PIC18 INHX32 HEX addresses are byte addresses (address 0x0000 ==
				// first instruction word's first byte). Store bytes verbatim at that
				// byte offset; the CPU fetches with `ReadProgram(PC * 2)`.
				uint32_t fullAddr = extAddr + address - targetAddr;
				for(uint8_t i = 0; i < byteCount; i++) {
					uint8_t dataByte = HexToByte(line.substr(9 + i * 2, 2));
					if(fullAddr + i < targetSize) {
						target[fullAddr + i] = dataByte;
					}
				}
			}
		}
		return true;
	}

private:
	static uint8_t HexToByte(const string& hex)
	{
		uint8_t val = 0;
		for(char c : hex) {
			val <<= 4;
			if(c >= '0' && c <= '9') val |= (c - '0');
			else if(c >= 'A' && c <= 'F') val |= (c - 'A' + 10);
			else if(c >= 'a' && c <= 'f') val |= (c - 'a' + 10);
		}
		return val;
	}
};