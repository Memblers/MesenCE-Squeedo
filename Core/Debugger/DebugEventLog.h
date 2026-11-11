#pragma once
#include "pch.h"
#include "Shared/MemoryType.h"
#include "Debugger/DebugTypes.h"

class Emulator;

enum class DebugEventPolicy {
	Ignore = 0,
	Report = 1,
	Break = 2
};

enum class DebugEventKind {
	BreakEvent = 0,
	Crash = 1,
	Hang = 2,
	LuaError = 3,
	Message = 4
};

//Plain-old-data struct shared with the UI (mirrored in UI/Interop/DebugApi.cs)
struct DebugEventRecord
{
	uint64_t Sequence;
	uint64_t FrameCount;
	int32_t EventType;
	int32_t Source;
	int32_t CpuType;
	uint32_t Pc;
	uint32_t MemAddress;
	int32_t MemValue;
	uint32_t Count;
	char Message[256];
};

struct MemoryWriteRecord
{
	uint64_t FrameCount;
	uint32_t Pc;
	uint32_t Address;
	int32_t Value;
	int32_t CpuType;
	int32_t MemType;
};

struct BankSwitchRecord
{
	uint64_t Sequence;
	uint64_t FrameCount;
	uint32_t Pc;
	int32_t OldOffset;
	int32_t NewOffset;
	uint16_t StartAddr;
	uint16_t EndAddr;
	int16_t PageNumber;
	uint8_t IsChr;
	uint8_t MemType;
};

//Script info returned to the UI (mirrored in UI/Interop/DebugApi.cs)
struct ScriptInfoResult
{
	int32_t ScriptId;
	uint8_t InitDone;
	uint8_t HasError;
	char Name[128];
	char LastError[512];
};

/*
	Ring-buffer event log used by the MCP server so that agents can observe
	what the user sees on screen (crashes, break-on events, Lua errors, OSD messages).

	Also provides:
	- per-BreakSource event policies (Ignore/Report/Break)
	- a memory write tracker ("what wrote this address?")
	- a bankswitch log for NES mappers
*/
class DebugEventLog
{
public:
	static constexpr int32_t EventRingSize = 4096;
	static constexpr int32_t WriteRingSize = 256;
	static constexpr int32_t BankRingSize = 512;
	static constexpr int32_t MaxPolicyEntries = 64;

	//Storm throttle: max full event captures per (source) per frame. Beyond this, only a
	//lock-free counter is bumped - a garbage-execution storm must not do per-event capture work.
	static constexpr int32_t MaxCapturedEventsPerSourcePerFrame = 32;

	static void SetEnabled(Emulator* emu, bool enabled);
	static bool IsEnabled();

	static void RecordBreakEvent(CpuType cpuType, BreakSource source, uint32_t pc, uint32_t memAddr = 0, int32_t memValue = 0);

	//Shared "Break On..." handling: records the event when a policy/checkbox wants it and
	//returns true when execution should stop (checkbox set or Break policy).
	static bool HandleBreakOn(CpuType cpuType, BreakSource source, bool breakCheckboxOn, uint32_t pc, int32_t value);
	static void RecordLuaErrorEvent(const string& scriptName, const string& error);
	static void RecordMessageEvent(const string& title, const string& message);

	static uint32_t GetEvents(DebugEventRecord* outRecords, uint32_t maxCount, uint64_t cursor, uint64_t* outNextCursor);

	//Cumulative per-source counts of events that were throttled (not recorded) since the last Reset.
	//outCounts is indexed by BreakSource value; maxCount entries are filled.
	static void GetThrottledEventCounts(uint64_t* outCounts, uint32_t maxCount);

	static void SetPolicy(BreakSource source, DebugEventPolicy policy);
	static DebugEventPolicy GetPolicy(BreakSource source);
	static bool IsFatalSource(BreakSource source);

	static bool IsCrashed();
	static void ClearCrash();
	static void Reset();

	//Memory write tracking ("what wrote this address?")
	static void ArmWriteWatch(uint32_t startAddr, uint32_t endAddr, MemoryType memType);
	static void DisarmWriteWatch();
	static bool IsWriteWatchArmed();
	static void RecordMemoryWrite(CpuType cpuType, uint32_t pc, uint32_t addr, int32_t value);
	static uint32_t GetRecentWrites(MemoryWriteRecord* outRecords, uint32_t maxCount);
	static void GetWriteWatchRange(uint32_t& startAddr, uint32_t& endAddr, MemoryType& memType);

	//Bankswitch log
	static void RecordBankSwitch(uint32_t pc, bool isChr, int16_t pageNumber, uint16_t startAddr, uint16_t endAddr, int32_t oldOffset, int32_t newOffset, uint8_t memType);
	static uint32_t GetBankLog(BankSwitchRecord* outRecords, uint32_t maxCount, uint64_t cursor, uint64_t* outNextCursor);
	static void ClearBankLog();

private:
	static bool _enabled;
	static Emulator* _emu;

	static uint64_t GetFrameCount();
	static bool ThrottleAllowsCapture(int32_t sourceIdx);
	static void PushEvent(DebugEventKind type, int32_t source, int32_t cpuType, uint32_t pc, uint32_t memAddr, int32_t memValue, const char* message);
};
