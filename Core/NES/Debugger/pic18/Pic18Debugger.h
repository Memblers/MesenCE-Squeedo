#pragma once
#include "pch.h"
#include "Debugger/IDebugger.h"
#include "NES/Mappers/Squeedo/Pic18Types.h"

class NesConsole;
class Pic18Cpu;
struct Pic18CpuState;
class Debugger;
class Squeedo;
class Disassembler;

class Pic18Debugger final : public IDebugger
{
private:
	Debugger* _debugger = nullptr;
	NesConsole* _console = nullptr;
	Pic18Cpu* _cpu = nullptr;
	Disassembler* _disassembler = nullptr;

	unique_ptr<BreakpointManager> _breakpointManager;
	unique_ptr<CallstackManager> _callstackManager;
	unique_ptr<ITraceLogger> _traceLogger;

	uint16_t _prevOpCode = 0;
	uint32_t _prevPc = 0;
	uint32_t _prevStackPointer = 0;

	//True while the emulation thread is inside ProcessInstruction() (i.e. the fetch hook for
	//the instruction at state.PC has run but the instruction has not executed yet). When the
	//emulation is stopped anywhere else (e.g. at a main CPU boundary), the pending instruction's
	//fetch hook has NOT fired yet. StepRequest counts are consumed at fetch, so Step() needs
	//one extra step count in that case - see GetEffectiveStepCount().
	std::atomic<bool> _inProcessInstruction = false;

	int32_t GetEffectiveStepCount(int32_t stepCount);

public:
	Pic18Debugger(Debugger* debugger);
	~Pic18Debugger();

	void Init() override;
	void Reset() override;

	void ProcessInstruction();
	void ProcessRead(uint32_t addr, uint8_t value, MemoryOperationType opType);
	void ProcessWrite(uint32_t addr, uint8_t value, MemoryOperationType opType);

	void Step(int32_t stepCount, StepType type) override;
	void Run() override;

	uint32_t GetProgramCounter(bool getInstPc) override;
	void SetProgramCounter(uint32_t addr, bool updateDebuggerOnly = false) override;
	BreakpointManager* GetBreakpointManager() override { return _breakpointManager.get(); }
	CallstackManager* GetCallstackManager() override { return _callstackManager.get(); }
	IAssembler* GetAssembler() override { return nullptr; }
	BaseEventManager* GetEventManager() override { return nullptr; }
	ITraceLogger* GetTraceLogger() override { return _traceLogger.get(); }
	BaseState& GetState() override;
	uint64_t GetCpuCycleCount(bool forProfiler = false) override;
	DebuggerFeatures GetSupportedFeatures() override;
	ISerializable* GetSerializableCpu() override;
};