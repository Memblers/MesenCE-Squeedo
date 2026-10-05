#include "pch.h"
#include "Debugger/ExpressionEvaluator.h"
#include "NES/Mappers/Squeedo/Pic18Types.h"
#include "NES/Debugger/pic18/Pic18Debugger.h"

unordered_map<string, int64_t>& ExpressionEvaluator::GetPic18Tokens()
{
	static unordered_map<string, int64_t> supportedTokens = {
		{ "pc", EvalValues::RegPC },
		{ "w", EvalValues::RegA },
		{ "bsr", EvalValues::RegB },
		{ "status", EvalValues::RegPS },
		{ "stkptr", EvalValues::RegSP },
		{ "fsr0l", EvalValues::RegX },
		{ "fsr0h", EvalValues::RegY },
		{ "fsr1l", EvalValues::RegD },
		{ "fsr1h", EvalValues::RegE },
		{ "tblptr", EvalValues::R0 },
		{ "tablat", EvalValues::RegM },
		{ "pclath", EvalValues::RegH },
		{ "pclatu", EvalValues::RegL },
		{ "z", EvalValues::RegPS_Zero },
		{ "c", EvalValues::RegPS_Carry },
		{ "n", EvalValues::RegPS_Negative },
		{ "ov", EvalValues::RegPS_Overflow },
		{ "dc", EvalValues::RegPS_Decimal },
	};

	return supportedTokens;
}

int64_t ExpressionEvaluator::GetPic18TokenValue(int64_t token, EvalResultType& resultType)
{
	Pic18CpuState& s = (Pic18CpuState&)((Pic18Debugger*)_cpuDebugger)->GetState();
	switch(token) {
		//PC is returned as a byte address (word PC * 2) to match GetProgramCounter,
		//breakpoints, disassembly and the mesen_step output.
		case EvalValues::RegPC: return s.PC * 2;
		case EvalValues::RegA: return s.W;
		case EvalValues::RegB: return s.BSR;
		case EvalValues::RegPS: return s.STATUS;
		case EvalValues::RegSP: return s.STKPTR;
		case EvalValues::RegX: return s.FSR[0] & 0xFF;
		case EvalValues::RegY: return (s.FSR[0] >> 8) & 0xFF;
		case EvalValues::RegD: return s.FSR[1] & 0xFF;
		case EvalValues::RegE: return (s.FSR[1] >> 8) & 0xFF;
		case EvalValues::R0: return s.TBLPTR;
		case EvalValues::RegM: return s.TABLAT;
		case EvalValues::RegH: return s.PCLATH;
		case EvalValues::RegL: return s.PCLATU;
		case EvalValues::RegPS_Zero: return ReturnBool(s.STATUS & Pic18StatusBits::Z, resultType);
		case EvalValues::RegPS_Carry: return ReturnBool(s.STATUS & Pic18StatusBits::C, resultType);
		case EvalValues::RegPS_Negative: return ReturnBool(s.STATUS & Pic18StatusBits::N, resultType);
		case EvalValues::RegPS_Overflow: return ReturnBool(s.STATUS & Pic18StatusBits::OV, resultType);
		case EvalValues::RegPS_Decimal: return ReturnBool(s.STATUS & Pic18StatusBits::DC, resultType);
	}
	return 0;
}