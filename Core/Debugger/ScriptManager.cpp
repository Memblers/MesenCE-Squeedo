#include "pch.h"
#include "Lua/lua.hpp"
#include "Debugger/ScriptManager.h"
#include "Debugger/ScriptHost.h"
#include "Debugger/DebugBreakHelper.h"
#include "Debugger/Debugger.h"
#include "Shared/Emulator.h"
#include "Shared/Video/DebugHud.h"
#include "Shared/MemoryOperationType.h"

ScriptManager::ScriptManager(Debugger* debugger)
{
	_debugger = debugger;
	_hasScript = false;
	_nextScriptId = 1;
}

ScriptManager::~ScriptManager()
{
	_debugger->GetEmulator()->GetDebugHud()->ClearScreen();
	_debugger->GetEmulator()->GetScriptHud()->ClearScreen();
}

int ScriptManager::LoadScript(string name, string path, string content, int32_t scriptId)
{
	DebugBreakHelper helper(_debugger);
	auto lock = _scriptLock.AcquireSafe();

	if(scriptId < 0) {
		unique_ptr<ScriptHost> script(new ScriptHost(_nextScriptId++));
		script->LoadScript(name, path, content, _debugger);
		scriptId = script->GetScriptId();
		_scripts.push_back(std::move(script));
		_hasScript = true;
		return scriptId;
	} else {
		auto result = std::find_if(_scripts.begin(), _scripts.end(), [=](unique_ptr<ScriptHost>& script) {
			return script->GetScriptId() == scriptId;
		});
		if(result != _scripts.end()) {
			//Send a ScriptEnded event before reloading the code
			(*result)->ProcessEvent(EventType::ScriptEnded, _debugger->GetMainCpuType());

			(*result)->LoadScript(name, path, content, _debugger);
			RefreshMemoryCallbackFlags();
			return scriptId;
		}
	}

	return -1;
}

void ScriptManager::RemoveScript(int32_t scriptId)
{
	DebugBreakHelper helper(_debugger);
	auto lock = _scriptLock.AcquireSafe();
	auto predicate = [=](const unique_ptr<ScriptHost>& script) {
		if(script->GetScriptId() == scriptId) {
			//Send a ScriptEnded event before unloading the script
			script->ProcessEvent(EventType::ScriptEnded, _debugger->GetMainCpuType());
			_debugger->GetEmulator()->GetDebugHud()->ClearScreen();
			_debugger->GetEmulator()->GetScriptHud()->ClearScreen();
			return true;
		}
		return false;
	};

	_scripts.erase(std::remove_if(_scripts.begin(), _scripts.end(), predicate), _scripts.end());

	RefreshMemoryCallbackFlags();

	_hasScript = _scripts.size() > 0;
}

void ScriptManager::RefreshMemoryCallbackFlags()
{
	_isPpuMemoryCallbackEnabled = false;
	_isCpuMemoryCallbackEnabled = false;
	for(unique_ptr<ScriptHost>& script : _scripts) {
		script->RefreshMemoryCallbackFlags();
	}
}

string ScriptManager::GetScriptLog(int32_t scriptId)
{
	auto lock = _scriptLock.AcquireSafe();
	for(unique_ptr<ScriptHost>& script : _scripts) {
		if(script->GetScriptId() == scriptId) {
			return script->GetLog();
		}
	}
	return "";
}

uint32_t ScriptManager::GetScriptIds(int32_t* outIds, uint32_t maxCount)
{
	auto lock = _scriptLock.AcquireSafe();
	uint32_t count = 0;
	for(unique_ptr<ScriptHost>& script : _scripts) {
		if(count >= maxCount) {
			break;
		}
		outIds[count++] = script->GetScriptId();
	}
	return count;
}

bool ScriptManager::GetScriptInfo(int32_t scriptId, string& name, bool& initDone, bool& hasError, string& lastError)
{
	auto lock = _scriptLock.AcquireSafe();
	for(unique_ptr<ScriptHost>& script : _scripts) {
		if(script->GetScriptId() == scriptId) {
			name = script->GetScriptName();
			initDone = script->IsInitDone();
			hasError = script->HasError();
			lastError = script->GetLastError();
			return true;
		}
	}
	return false;
}

bool ScriptManager::ValidateScript(const string& scriptName, const string& content, string& errorOut)
{
	//Compile-only check in a throwaway Lua state - the script is never executed
	lua_State* lua = luaL_newstate();
	int result = luaL_loadbufferx(lua, content.c_str(), content.size(), ("@" + scriptName).c_str(), nullptr);
	bool valid = result == 0;
	if(!valid) {
		const char* msg = lua_tostring(lua, -1);
		errorOut = msg ? msg : "unknown Lua error";
	}
	lua_close(lua);
	return valid;
}

void ScriptManager::ProcessEvent(EventType type, CpuType cpuType)
{
	for(unique_ptr<ScriptHost>& script : _scripts) {
		script->ProcessEvent(type, cpuType);
	}
}
