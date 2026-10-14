#pragma once
#include "pch.h"
#include "Debugger/BaseTraceLogger.h"
#include "NES/Debugger/pic18/Pic18DisUtils.h"
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
	uint8_t STKPTR = 0;
	uint16_t FSR[3] = {};
	int64_t CycleCount = 0;
};

class Pic18TraceLogger : public BaseTraceLogger<Pic18TraceLogger, Pic18TraceState>
{
protected:
	RowDataType GetFormatTagType(string& tag) override
	{
		//W/BSR/STKPTR/STATUS are the natural PIC18 names (WREG also accepted).
		//A/B/S/SP are kept as aliases so existing custom formats keep working.
		if(tag == "W" || tag == "WREG" || tag == "A") return RowDataType::A;
		if(tag == "STATUS" || tag == "S") return RowDataType::PS;
		if(tag == "BSR" || tag == "B") return RowDataType::B;
		if(tag == "STKPTR" || tag == "SP") return RowDataType::SP;
		return RowDataType::Text;
	}

public:
	uint32_t GetProgramCounter(Pic18TraceState& state)
	{
		return state.PC * 2;
	}

	uint8_t GetStackPointer(Pic18TraceState& state)
	{
		//STKPTR is a call-depth counter (0 = empty, grows on CALL). Invert it so
		//the "indent code" option indents deeper levels, like on CPUs whose stack
		//pointer counts down from $FF (the base class uses $FF - SP for indentation).
		return (uint8_t)(0xFF - state.STKPTR);
	}

	uint64_t GetCycleCount(Pic18TraceState& state)
	{
		return (uint64_t)state.CycleCount;
	}

	void GetTraceRow(string& output, Pic18TraceState& cpuState, TraceLogPpuState& ppuState, DisassemblyInfo& disassemblyInfo)
	{
		//STATUS bits 4..0: N, OV, Z, DC, C
		constexpr char activeStatusLetters[5] = { 'N', 'O', 'Z', 'D', 'C' };
		constexpr char inactiveStatusLetters[5] = { 'n', 'o', 'z', 'd', 'c' };

		uint8_t byteCode[8] = {};
		disassemblyInfo.GetByteCode(byteCode);
		uint16_t opcode = (uint16_t)(byteCode[0] | (byteCode[1] << 8));

		for(RowPart& rowPart : _rowParts) {
			switch(rowPart.DataType) {
				case RowDataType::A: WriteIntValue(output, cpuState.W, rowPart); break;
				case RowDataType::B: WriteIntValue(output, cpuState.BSR, rowPart); break;
				case RowDataType::SP: WriteIntValue(output, cpuState.STKPTR, rowPart); break;
				case RowDataType::PS: GetStatusFlag(activeStatusLetters, inactiveStatusLetters, output, cpuState.STATUS, rowPart, 5); break;

				//Indirect operands (INDF/POSTINC/...) resolve through the FSRs captured at
				//trace time - handled here (instead of the shared helpers) because the
				//trace state is the lightweight Pic18TraceState, not a full Pic18CpuState.
				case RowDataType::EffectiveAddress: {
					EffectiveAddressInfo ea = Pic18DisUtils::GetEffectiveAddress(opcode, cpuState.W, cpuState.BSR, cpuState.FSR);
					if(ea.ShowAddress && ea.Address >= 0) {
						if(_options.UseLabels) {
							AddressInfo addr { (int32_t)ea.Address, ea.Type };
							string label = _labelManager->GetLabel(addr);
							if(!label.empty()) {
								if(label.size() > 2 && label[label.size() - 1] == '0' && label[label.size() - 2] == '+') {
									label = label.substr(0, label.size() - 2);
								}
								WriteStringValue(output, " [" + label + "]", rowPart);
								break;
							}
						}
						WriteStringValue(output, " [$" + DebugUtilities::AddressToHex(CpuType::Pic18, ea.Address) + "]", rowPart);
					}
					break;
				}

				case RowDataType::MemoryValue: {
					EffectiveAddressInfo ea = Pic18DisUtils::GetEffectiveAddress(opcode, cpuState.W, cpuState.BSR, cpuState.FSR);
					if(ea.Address >= 0 && ea.ValueSize > 0) {
						uint16_t value = (uint16_t)disassemblyInfo.GetMemoryValue(ea, _memoryDumper, ea.Type);
						if(rowPart.DisplayInHex) {
							output += "= $";
							WriteIntValue(output, (uint8_t)value, rowPart);
						} else {
							output += "= ";
						}
					}
					break;
				}

				default: ProcessSharedTag(rowPart, output, cpuState, ppuState, disassemblyInfo); break;
			}
		}
	}

	void LogPpuState() {}
	TraceLogPpuState GetPpuState() { return {}; }

public:
	Pic18TraceLogger(Debugger* debugger, IDebugger* cpuDebugger)
		: BaseTraceLogger(debugger, cpuDebugger, CpuType::Pic18)
	{
	}
};