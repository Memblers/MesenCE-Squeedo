#pragma once
#include "pch.h"

class LabelManager;

class Pic18DisUtils
{
public:
	static void GetDisassembly(string& out, uint16_t opcode, uint32_t pc, uint8_t* byteCode = nullptr, LabelManager* labelManager = nullptr);
	static int GetInstructionSize(uint16_t opcode);
	static bool IsJumpToSub(uint16_t opcode);
	static bool IsReturnInstruction(uint16_t opcode);
	static bool IsUnconditionalJump(uint16_t opcode);
	static bool IsConditionalJump(uint16_t opcode);
};