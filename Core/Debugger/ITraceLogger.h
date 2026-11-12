#pragma once
#include "pch.h"
#include "Debugger/DebugTypes.h"

struct TraceRow
{
	uint32_t ProgramCounter;
	CpuType Type;
	uint8_t ByteCode[8];
	uint8_t ByteCodeSize;
	uint32_t LogSize;
	char LogOutput[500];
};

struct TraceLoggerOptions
{
	bool Enabled;
	bool IndentCode;
	bool UseLabels;
	char Condition[1000];
	char Format[1000];
};

class ITraceLogger
{
protected:
	bool _enabled = false;

public:
	static uint64_t NextRowId;

	//Always-on crash tail: every trace logger keeps a small ring of the last TailLogSize
	//instructions, recorded even when full tracing is off, so crash reports are
	//self-diagnosing with zero setup (see BaseTraceLogger::AddTailRow).
	static uint64_t NextTailId;
	static constexpr uint32_t TailLogSize = 1024;

	//True while the MCP server is active (set by DebugEventLog::SetEnabled).
	//When false (regular UI-only session), the tail costs nothing.
	static bool TailCaptureEnabled;

	virtual int64_t GetRowId(uint32_t offset) = 0;
	virtual void GetExecutionTrace(TraceRow& row, uint32_t offset) = 0;
	virtual void Clear() = 0;
	virtual void SetOptions(TraceLoggerOptions options) = 0;

	//Tail ring access (always readable, no enable step required)
	virtual int64_t GetTailRowId(uint32_t offset) = 0;
	virtual void GetTailExecutionTrace(TraceRow& row, uint32_t offset) = 0;

	__forceinline bool IsEnabled() { return _enabled; }

	//True when an instruction should be fed to the logger: full tracing enabled,
	//or the MCP-driven always-on tail is active.
	__forceinline bool ShouldCapture() { return _enabled || TailCaptureEnabled; }
};
