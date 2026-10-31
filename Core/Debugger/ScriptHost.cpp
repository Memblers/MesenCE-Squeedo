#include "pch.h"
#include "Debugger/ScriptHost.h"
#include "Debugger/ScriptingContext.h"
#include "Debugger/ScriptingContext.h"
#include "Shared/EventType.h"
#include "Shared/MemoryOperationType.h"

ScriptHost::ScriptHost(int scriptId)
{
	_scriptId = scriptId;
}

int ScriptHost::GetScriptId()
{
	return _scriptId;
}

string ScriptHost::GetLog()
{
	shared_ptr<ScriptingContext> context = _context.lock();
	return context ? context->GetLog() : "";
}

string ScriptHost::GetScriptName()
{
	shared_ptr<ScriptingContext> context = _context.lock();
	return context ? context->GetScriptName() : "";
}

bool ScriptHost::IsInitDone()
{
	shared_ptr<ScriptingContext> context = _context.lock();
	return context && context->CheckInitDone();
}

bool ScriptHost::HasError()
{
	shared_ptr<ScriptingContext> context = _context.lock();
	return context && context->HasError();
}

string ScriptHost::GetLastError()
{
	shared_ptr<ScriptingContext> context = _context.lock();
	return context ? context->GetLastError() : "";
}

bool ScriptHost::LoadScript(string scriptName, string path, string scriptContent, Debugger* debugger)
{
	_context.reset(new ScriptingContext(debugger));
	if(!_context->LoadScript(scriptName, path, scriptContent, debugger)) {
		return false;
	}
	return true;
}

void ScriptHost::ProcessEvent(EventType eventType, CpuType cpuType)
{
	_context->CallEventCallback(eventType, cpuType);
}
