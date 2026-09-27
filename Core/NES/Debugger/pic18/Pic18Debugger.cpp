#include "pch.h"
#pragma warning(disable: 5205)  // ITraceLogger non-virtual destructor
#include "NES/Debugger/pic18/Pic18Debugger.h"
#include "NES/Debugger/pic18/Pic18DisUtils.h"
#include "NES/Debugger/pic18/Pic18TraceLogger.h"
#include "NES/Mappers/Squeedo/Pic18Cpu.h"
#include "NES/Mappers/Squeedo/Pic18Types.h"
#include "NES/Mappers/Squeedo/Squeedo.h"
#include "NES/NesConsole.h"
#include "Debugger/Debugger.h"
#include "Debugger/CallstackManager.h"
#include "Debugger/BreakpointManager.h"
#include "Debugger/Disassembler.h"
#include "Debugger/DisassemblyInfo.h"
#include "NES/Debugger/pic18/Pic18DisUtils.h"
#include "Shared/Emulator.h"

Pic18Debugger::Pic18Debugger(Debugger* debugger) : IDebugger(debugger->GetEmulator())
{
	_debugger = debugger;
	_console = (NesConsole*)debugger->GetConsole();
	_disassembler = debugger->GetDisassembler();

	Squeedo* squeedo = dynamic_cast<Squeedo*>(_console->GetMapper());
	if(squeedo) {
		_cpu = squeedo->GetPicCpu();
	}

	_traceLogger.reset(new Pic18TraceLogger(debugger, this));
	_callstackManager.reset(new CallstackManager(debugger, this));
	_breakpointManager.reset(new BreakpointManager(debugger, this, CpuType::Pic18, debugger->GetEventManager(CpuType::Pic18)));
	_step.reset(new StepRequest());
}

Pic18Debugger::~Pic18Debugger()
{
}

void Pic18Debugger::Init()
{
	if(!_cpu) return;

	// Pre-populate the disassembler cache with the full PIC18 firmware
	// so the debugger shows disassembly immediately when paused.
	// PIC18F4620 program memory is 64KB (0x10000 bytes = 32K words).
	static constexpr uint32_t Pic18PrgSize = 0x10000;
	for(uint32_t addr = 0; addr < Pic18PrgSize; ) {
		AddressInfo addrInfo = { (int32_t)addr, MemoryType::Pic18ProgramRom };
		int opSize = _disassembler->BuildCache(addrInfo, 0, CpuType::Pic18);
		if(opSize <= 0) {
			// Unknown instruction — skip 2 bytes (1 word)
			addr += 2;
		} else {
			addr += opSize;
		}
	}
}

void Pic18Debugger::Reset()
{
	_step.reset(new StepRequest());
	_callstackManager->Clear();
}

void Pic18Debugger::ProcessInstruction()
{
	if(!_cpu) return;

	Pic18CpuState& state = _cpu->GetState();
	uint32_t pc = state.PC;
	uint16_t opcode = ((uint16_t)_cpu->ReadProgram(pc * 2 + 1) << 8) | _cpu->ReadProgram(pc * 2);

	// Callstack tracking: check PREVIOUS instruction
	uint32_t byteAddr = pc * 2;
	AddressInfo destAddr = { (int32_t)byteAddr, MemoryType::Pic18ProgramRom };
	if(Pic18DisUtils::IsJumpToSub(_prevOpCode)) {
		// CALL/RCALL — push to callstack
		uint32_t prevByteAddr = _prevPc * 2;
		uint32_t opSize = Pic18DisUtils::GetInstructionSize(_prevOpCode);
		uint32_t returnPc = _prevPc + (opSize / 2);
		uint32_t returnByteAddr = returnPc * 2;
		AddressInfo srcAddress = { (int32_t)prevByteAddr, MemoryType::Pic18ProgramRom };
		AddressInfo retAddress = { (int32_t)returnByteAddr, MemoryType::Pic18ProgramRom };
		_callstackManager->Push(srcAddress, prevByteAddr, destAddr, byteAddr, retAddress, returnByteAddr, _prevStackPointer, StackFrameFlags::None);
	} else if(Pic18DisUtils::IsReturnInstruction(_prevOpCode)) {
		// RETURN/RETFIE/RETLW — pop from callstack
		_callstackManager->Pop(destAddr, byteAddr, state.STKPTR);
		if(_step->BreakAddress == (int32_t)byteAddr && _step->BreakStackPointer == (int64_t)state.STKPTR) {
			_step->Break(BreakSource::CpuStep);
		}
	}

	// Save for next iteration
	_prevOpCode = opcode;
	_prevPc = pc;
	_prevStackPointer = state.STKPTR;

	AddressInfo addrInfo = { (int32_t)byteAddr, MemoryType::Pic18ProgramRom };
	MemoryOperationInfo operation(byteAddr, opcode, MemoryOperationType::ExecOpCode, MemoryType::Pic18ProgramRom);

	InstructionProgress.LastMemOperation = operation;
	InstructionProgress.StartCycle = state.CycleCount;

	_disassembler->BuildCache(addrInfo, 0, CpuType::Pic18);

	_step->ProcessCpuExec();

	_debugger->ProcessBreakConditions(CpuType::Pic18, *_step.get(), _breakpointManager.get(), operation, addrInfo);
}

void Pic18Debugger::ProcessRead(uint32_t addr, uint8_t value, MemoryOperationType opType)
{
}

void Pic18Debugger::ProcessWrite(uint32_t addr, uint8_t value, MemoryOperationType opType)
{
}

void Pic18Debugger::Step(int32_t stepCount, StepType type)
{
	_step.reset(new StepRequest(type));
	_step->StepCount = stepCount;
}

void Pic18Debugger::Run()
{
	_step.reset(new StepRequest());
}

uint32_t Pic18Debugger::GetProgramCounter(bool getInstPc)
{
	if(!_cpu) return 0;
	// state.PC is a 21-bit word address; the debugger address space is byte-based (2 bytes per word)
	return _cpu->GetState().PC * 2;
}

void Pic18Debugger::SetProgramCounter(uint32_t addr, bool updateDebuggerOnly)
{
	if(!_cpu) return;
	if(updateDebuggerOnly) {
		// Only update debugger's view
	} else {
		_cpu->GetState().PC = addr / 2;
	}
}

BaseState& Pic18Debugger::GetState()
{
	if(!_cpu) {
		throw std::runtime_error("PIC18 CPU not available");
	}
	return _cpu->GetState();
}

uint64_t Pic18Debugger::GetCpuCycleCount(bool forProfiler)
{
	if(!_cpu) return 0;
	return _cpu->GetState().CycleCount;
}

DebuggerFeatures Pic18Debugger::GetSupportedFeatures()
{
	DebuggerFeatures features = {};
	features.ChangeProgramCounter = true;
	return features;
}

ISerializable* Pic18Debugger::GetSerializableCpu()
{
	return nullptr;  // PIC state is serialized as part of the mapper
}