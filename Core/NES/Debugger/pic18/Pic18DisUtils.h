#pragma once
#include "pch.h"

class LabelManager;
struct EffectiveAddressInfo;

class Pic18DisUtils
{
public:
	static void GetDisassembly(string& out, uint16_t opcode, uint32_t pc, uint8_t* byteCode = nullptr, LabelManager* labelManager = nullptr);
	static int GetInstructionSize(uint16_t opcode);
	static bool IsJumpToSub(uint16_t opcode);
	static bool IsReturnInstruction(uint16_t opcode);
	static bool IsUnconditionalJump(uint16_t opcode);
	static bool IsConditionalJump(uint16_t opcode);

	//Resolves indirect operands (INDF/POSTINC/POSTDEC/PREINC/PLUSW) to their target
	//address using the given register values. Returns an empty info for operands that
	//aren't indirect. w/bsr/fsr are passed explicitly so the caller can supply either
	//the full Pic18CpuState (disassembler) or the lightweight Pic18TraceState (trace log).
	static EffectiveAddressInfo GetEffectiveAddress(uint16_t opcode, uint8_t w, uint8_t bsr, const uint16_t* fsr);
};