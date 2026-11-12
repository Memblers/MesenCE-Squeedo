#include "pch.h"
#include <atomic>
#include "Debugger/DebugEventLog.h"
#include "Debugger/DebugUtilities.h"
#include "Debugger/ITraceLogger.h"
#include "Shared/Emulator.h"
#include "Shared/MessageManager.h"
#include "Utilities/SimpleLock.h"

bool DebugEventLog::_enabled = false;
Emulator* DebugEventLog::_emu = nullptr;

static SimpleLock s_lock;
static DebugEventRecord s_events[DebugEventLog::EventRingSize];
static uint64_t s_eventCount = 0;

static MemoryWriteRecord s_writes[DebugEventLog::WriteRingSize];
static uint64_t s_writeCount = 0;

static BankSwitchRecord s_banks[DebugEventLog::BankRingSize];
static uint64_t s_bankCount = 0;

static std::atomic<int32_t> s_policies[DebugEventLog::MaxPolicyEntries] = {};
static bool s_crashed = false;
static uint64_t s_crashFrame = 0;
static uint32_t s_crashPc = 0;

//Per-source storm throttle state (lock-free - this runs on the emulation thread for every hit)
struct ThrottleState
{
	std::atomic<uint64_t> Frame { 0 };
	std::atomic<uint32_t> Hits { 0 };
	std::atomic<uint64_t> Throttled { 0 };
};
static ThrottleState s_throttle[DebugEventLog::MaxPolicyEntries];

static bool s_writeWatchArmed = false;
static uint32_t s_writeStart = 0;
static uint32_t s_writeEnd = 0;
static MemoryType s_writeMemType = MemoryType::NesMemory;

static void CopyString(char* dest, size_t destSize, const string& src)
{
	size_t len = std::min<size_t>(src.size(), destSize - 1);
	memcpy(dest, src.c_str(), len);
	dest[len] = 0;
}

void DebugEventLog::SetEnabled(Emulator* emu, bool enabled)
{
	auto lock = s_lock.AcquireSafe();
	_emu = emu;
	_enabled = enabled;

	//While the MCP event log is active, every trace logger keeps its always-on crash tail
	//(last 1024 instructions per CPU) so crash reports are self-diagnosing with zero setup.
	//Regular UI-only sessions keep this off (zero overhead).
	ITraceLogger::TailCaptureEnabled = enabled;

	if(!enabled) {
		s_crashed = false;
	}
}

bool DebugEventLog::IsEnabled()
{
	return _enabled;
}

uint64_t DebugEventLog::GetFrameCount()
{
	return _emu ? _emu->GetFrameCount() : 0;
}

bool DebugEventLog::ThrottleAllowsCapture(int32_t sourceIdx)
{
	if(sourceIdx < 0 || sourceIdx >= MaxPolicyEntries) {
		return true;
	}

	ThrottleState& t = s_throttle[sourceIdx];
	uint64_t frame = GetFrameCount();
	uint64_t window = t.Frame.load(std::memory_order_relaxed);
	if(frame != window) {
		//New frame - restart the capture window (best effort; a race here only skews the limit slightly)
		t.Frame.store(frame, std::memory_order_relaxed);
		t.Hits.store(0, std::memory_order_relaxed);
	}

	if(t.Hits.fetch_add(1, std::memory_order_relaxed) < MaxCapturedEventsPerSourcePerFrame) {
		return true;
	}

	//Storm: skip the capture, just bump the aggregate counter
	t.Throttled.fetch_add(1, std::memory_order_relaxed);
	return false;
}

void DebugEventLog::GetThrottledEventCounts(uint64_t* outCounts, uint32_t maxCount)
{
	uint32_t count = std::min<uint32_t>(maxCount, MaxPolicyEntries);
	for(uint32_t i = 0; i < count; i++) {
		outCounts[i] = s_throttle[i].Throttled.load(std::memory_order_relaxed);
	}
}

bool DebugEventLog::IsFatalSource(BreakSource source)
{
	switch(source) {
		case BreakSource::NesBreakOnCpuCrash:
		case BreakSource::GbInvalidOpCode:
		case BreakSource::GbaInvalidOpCode:
		case BreakSource::BreakOnUndefinedOpCode:
			return true;

		default:
			return false;
	}
}

void DebugEventLog::PushEvent(DebugEventKind type, int32_t source, int32_t cpuType, uint32_t pc, uint32_t memAddr, int32_t memValue, const char* message)
{
	auto lock = s_lock.AcquireSafe();
	if(!_enabled) {
		return;
	}

	DebugEventRecord& rec = s_events[s_eventCount % EventRingSize];
	memset(&rec, 0, sizeof(rec));
	rec.Sequence = s_eventCount + 1;
	rec.FrameCount = GetFrameCount();
	rec.EventType = (int32_t)type;
	rec.Source = source;
	rec.CpuType = cpuType;
	rec.Pc = pc;
	rec.MemAddress = memAddr;
	rec.MemValue = memValue;
	rec.Count = 1;
	if(message) {
		CopyString(rec.Message, sizeof(rec.Message), message);
	}
	s_eventCount++;
}

void DebugEventLog::RecordBreakEvent(CpuType cpuType, BreakSource source, uint32_t pc, uint32_t memAddr, int32_t memValue)
{
	bool fatal = IsFatalSource(source);
	if(fatal) {
		auto lock = s_lock.AcquireSafe();
		if(s_crashed) {
			//Same crash episode - don't flood the log
			return;
		}
		s_crashed = true;
		s_crashFrame = GetFrameCount();
		s_crashPc = pc;
		lock.Release();

		PushEvent(DebugEventKind::Crash, (int32_t)source, (int32_t)cpuType, pc, memAddr, memValue, nullptr);
		return;
	}

	//Storm throttle: cap full captures per source per frame. A garbage-execution storm
	//(e.g. running through PPU mirrors) hits thousands of times per frame - beyond the cap
	//we only bump a lock-free counter instead of capturing a record.
	if(!ThrottleAllowsCapture((int32_t)source)) {
		return;
	}

	PushEvent(DebugEventKind::BreakEvent, (int32_t)source, (int32_t)cpuType, pc, memAddr, memValue, nullptr);
}

bool DebugEventLog::HandleBreakOn(CpuType cpuType, BreakSource source, bool breakCheckboxOn, uint32_t pc, int32_t value)
{
	DebugEventPolicy policy = GetPolicy(source);
	if(breakCheckboxOn || policy != DebugEventPolicy::Ignore) {
		RecordBreakEvent(cpuType, source, pc, pc, value);
	}
	return breakCheckboxOn || policy == DebugEventPolicy::Break;
}

void DebugEventLog::RecordLuaErrorEvent(const string& scriptName, const string& error)
{
	//Lua error messages already start with "<scriptName>:<line>:" - don't prefix the name again
	PushEvent(DebugEventKind::LuaError, -1, -1, 0, 0, 0, error.c_str());
}

void DebugEventLog::RecordMessageEvent(const string& title, const string& message)
{
	PushEvent(DebugEventKind::Message, -1, -1, 0, 0, 0, (title + ": " + message).c_str());
}

uint32_t DebugEventLog::GetEvents(DebugEventRecord* outRecords, uint32_t maxCount, uint64_t cursor, uint64_t* outNextCursor)
{
	auto lock = s_lock.AcquireSafe();

	uint64_t total = s_eventCount;
	uint64_t firstAvailable = total > EventRingSize ? total - EventRingSize + 1 : 1;

	//Skip records that were pushed out of the ring buffer
	uint64_t next = cursor + 1 < firstAvailable ? firstAvailable - 1 : cursor;

	uint32_t count = 0;
	for(uint64_t seq = next + 1; seq <= total && count < maxCount; seq++) {
		outRecords[count++] = s_events[(seq - 1) % EventRingSize];
	}

	*outNextCursor = next + count;
	return count;
}

void DebugEventLog::SetPolicy(BreakSource source, DebugEventPolicy policy)
{
	int32_t idx = (int32_t)source;
	if(idx >= 0 && idx < MaxPolicyEntries) {
		s_policies[idx].store((int32_t)policy, std::memory_order_relaxed);
	}
}

DebugEventPolicy DebugEventLog::GetPolicy(BreakSource source)
{
	//Lock-free read - this runs on every instruction for the opcode checks
	int32_t idx = (int32_t)source;
	if(idx >= 0 && idx < MaxPolicyEntries) {
		return (DebugEventPolicy)s_policies[idx].load(std::memory_order_relaxed);
	}
	return DebugEventPolicy::Ignore;
}

bool DebugEventLog::IsCrashed()
{
	auto lock = s_lock.AcquireSafe();
	return s_crashed;
}

void DebugEventLog::ClearCrash()
{
	auto lock = s_lock.AcquireSafe();
	s_crashed = false;
}

void DebugEventLog::Reset()
{
	auto lock = s_lock.AcquireSafe();
	s_eventCount = 0;
	s_writeCount = 0;
	s_bankCount = 0;
	s_crashed = false;
	s_crashFrame = 0;
	s_crashPc = 0;
	for(ThrottleState& t : s_throttle) {
		t.Frame.store(0, std::memory_order_relaxed);
		t.Hits.store(0, std::memory_order_relaxed);
		t.Throttled.store(0, std::memory_order_relaxed);
	}
	//Policies and write watch are kept - they are configuration, not state
}

void DebugEventLog::ArmWriteWatch(uint32_t startAddr, uint32_t endAddr, MemoryType memType)
{
	auto lock = s_lock.AcquireSafe();
	s_writeStart = startAddr;
	s_writeEnd = endAddr;
	s_writeMemType = memType;
	s_writeWatchArmed = true;
	s_writeCount = 0;
}

void DebugEventLog::DisarmWriteWatch()
{
	auto lock = s_lock.AcquireSafe();
	s_writeWatchArmed = false;
}

bool DebugEventLog::IsWriteWatchArmed()
{
	return s_writeWatchArmed;
}

void DebugEventLog::GetWriteWatchRange(uint32_t& startAddr, uint32_t& endAddr, MemoryType& memType)
{
	auto lock = s_lock.AcquireSafe();
	startAddr = s_writeStart;
	endAddr = s_writeEnd;
	memType = s_writeMemType;
}

void DebugEventLog::RecordMemoryWrite(CpuType cpuType, uint32_t pc, uint32_t addr, int32_t value)
{
	if(!s_writeWatchArmed) {
		return;
	}

	auto lock = s_lock.AcquireSafe();
	if(!s_writeWatchArmed || addr < s_writeStart || addr > s_writeEnd) {
		return;
	}

	MemoryType memType = DebugUtilities::GetCpuMemoryType(cpuType);
	if(memType != s_writeMemType) {
		return;
	}

	MemoryWriteRecord& rec = s_writes[s_writeCount % WriteRingSize];
	rec.FrameCount = GetFrameCount();
	rec.Pc = pc;
	rec.Address = addr;
	rec.Value = value;
	rec.CpuType = (int32_t)cpuType;
	rec.MemType = (int32_t)memType;
	s_writeCount++;
}

uint32_t DebugEventLog::GetRecentWrites(MemoryWriteRecord* outRecords, uint32_t maxCount)
{
	auto lock = s_lock.AcquireSafe();

	uint64_t total = s_writeCount;
	uint64_t firstAvailable = total > WriteRingSize ? total - WriteRingSize + 1 : 1;
	uint64_t start = firstAvailable > 1 ? firstAvailable - 1 : 0;

	uint32_t count = 0;
	for(uint64_t i = start; i < total && count < maxCount; i++) {
		outRecords[count++] = s_writes[i % WriteRingSize];
	}
	return count;
}

void DebugEventLog::RecordBankSwitch(uint32_t pc, bool isChr, int16_t pageNumber, uint16_t startAddr, uint16_t endAddr, int32_t oldOffset, int32_t newOffset, uint8_t memType)
{
	if(!_enabled) {
		return;
	}

	auto lock = s_lock.AcquireSafe();
	BankSwitchRecord& rec = s_banks[s_bankCount % BankRingSize];
	memset(&rec, 0, sizeof(rec));
	rec.Sequence = s_bankCount + 1;
	rec.FrameCount = GetFrameCount();
	rec.Pc = pc;
	rec.OldOffset = oldOffset;
	rec.NewOffset = newOffset;
	rec.StartAddr = startAddr;
	rec.EndAddr = endAddr;
	rec.PageNumber = pageNumber;
	rec.IsChr = isChr ? 1 : 0;
	rec.MemType = memType;
	s_bankCount++;
}

uint32_t DebugEventLog::GetBankLog(BankSwitchRecord* outRecords, uint32_t maxCount, uint64_t cursor, uint64_t* outNextCursor)
{
	auto lock = s_lock.AcquireSafe();

	uint64_t total = s_bankCount;
	uint64_t firstAvailable = total > BankRingSize ? total - BankRingSize + 1 : 1;
	uint64_t next = cursor + 1 < firstAvailable ? firstAvailable - 1 : cursor;

	uint32_t count = 0;
	for(uint64_t seq = next + 1; seq <= total && count < maxCount; seq++) {
		outRecords[count++] = s_banks[(seq - 1) % BankRingSize];
	}

	*outNextCursor = next + count;
	return count;
}

void DebugEventLog::ClearBankLog()
{
	auto lock = s_lock.AcquireSafe();
	s_bankCount = 0;
}
