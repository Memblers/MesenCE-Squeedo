#pragma once
#include "pch.h"
#include "Debugger/BaseTraceLogger.h"
#include "Utilities/HexUtilities.h"

class Pic18Debugger;

//Lightweight trace-log state.
//IMPORTANT: BaseTraceLogger allocates an array of ExecutionLogSize (30000) entries
//of this type and memset()s it. Pic18CpuState embeds a 2MB Program[] array, which
//would make that allocation ~60GB and hang the debugger for over a minute. Only
//cache the registers we actually display in the trace row.
struct Pic18TraceState
{
	uint32_t PC = 0;
	uint8_t W = 0;
	uint8_t STATUS = 0;
	uint8_t BSR = 0;
};

class Pic18TraceLogger : public BaseTraceLogger<Pic18TraceLogger, Pic18TraceState>
{
protected:
	RowDataType GetFormatTagType(string& tag) override
	{
		if(tag == "A") return RowDataType::A;
		if(tag == "S") return RowDataType::PS;
		if(tag == "B") return RowDataType::B;
		if(tag == "SP") return RowDataType::SP;
		return RowDataType::Text;
	}

public:
	uint32_t GetProgramCounter(Pic18TraceState& state)
	{
		return state.PC * 2;
	}

	uint8_t GetStackPointer(Pic18TraceState& state)
	{
		return 0;
	}

	uint64_t GetCycleCount(Pic18TraceState& state)
	{
		return 0;
	}

	void GetTraceRow(string& output, Pic18TraceState& cpuState, TraceLogPpuState& ppuState, DisassemblyInfo& disassemblyInfo)
	{
		output += "PC:";
		output += HexUtilities::ToHex((uint16_t)cpuState.PC);
		output += " W:";
		output += HexUtilities::ToHex(cpuState.W);
		output += " S:";
		output += HexUtilities::ToHex(cpuState.STATUS);
		output += " B:";
		output += HexUtilities::ToHex(cpuState.BSR);
	}

	void LogPpuState() {}
	TraceLogPpuState GetPpuState() { return {}; }

public:
	Pic18TraceLogger(Debugger* debugger, IDebugger* cpuDebugger)
		: BaseTraceLogger(debugger, cpuDebugger, CpuType::Pic18)
	{
	}
};