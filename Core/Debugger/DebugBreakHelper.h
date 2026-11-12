#pragma once
#include "pch.h"
#include "Debugger/Debugger.h"
#include "Shared/Emulator.h"

class DebugBreakHelper
{
private:
	Debugger* _debugger;
	bool _needBreak = false;

public:
	DebugBreakHelper(Debugger* debugger, bool breakBetweenInstructions = false)
	{
		_debugger = debugger;

		_needBreak = !debugger->GetEmulator()->IsEmulationThread();

		if(_needBreak) {
			//Only attempt to break if this is done in a thread other than the main emulation thread (and the debugger is active)
			//Never wait forever: if the emulation thread cannot stop (hung/crashed emu thread, deadlocked
			//notification, etc.), an unbounded tight spin here froze the UI thread (blank trace/debug
			//windows) and the MCP thread - only a full restart recovered. After the timeout the call
			//proceeds without synchronization (data may be slightly torn, but nothing freezes).
			constexpr int maxWaitMs = 2000;
			int elapsedMs = 0;

			while(true) {
				debugger->BreakRequest(false);

				bool stopped = true;
				while(!debugger->IsExecutionStopped()) {
					std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(1));
					if(++elapsedMs >= maxWaitMs) {
						stopped = false;
						break;
					}
				}

				if(!stopped) {
					//Timed out - release the break request and continue unsynchronized
					debugger->BreakRequest(true);
					_needBreak = false;
					return;
				}

				if(breakBetweenInstructions) {
					if(debugger->GetDebuggerFeatures(debugger->GetMainCpuType()).ChangeProgramCounter) {
						//Execution stopped in-between 2 main cpu instructions, leave loop
						break;
					} else {
						//Execution stopped, but in the middle of an instruction, step forward
						//to the next instruction and try again
						debugger->Step(debugger->GetMainCpuType(), 1, StepType::Step, BreakSource::InternalOperation);
						debugger->BreakRequest(true);
						elapsedMs += 15;
						std::this_thread::sleep_for(std::chrono::duration<int, std::milli>(15));
					}
				} else {
					//Execution stopped, leave loop
					break;
				}
			}
		}
	}

	~DebugBreakHelper()
	{
		if(_needBreak) {
			_debugger->BreakRequest(true);
		}
	}
};