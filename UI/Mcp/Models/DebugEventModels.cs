using System.Collections.Generic;

namespace Mesen.Mcp.Models
{
	public class DebugEventGroupDto
	{
		public string Kind { get; set; } = "";
		public string? Source { get; set; }
		public string? Pc { get; set; }
		public string? Cpu { get; set; }
		public ulong Count { get; set; }
		public ulong NewCount { get; set; }
		public ulong FirstFrame { get; set; }
		public ulong LastFrame { get; set; }
		public double RatePerFrame { get; set; }
		public bool Spike { get; set; }
		public bool Expected { get; set; }
		public string? Note { get; set; }
		public string? Message { get; set; }
	}

	public class CrashInfoDto
	{
		public string? Source { get; set; }
		public string? Pc { get; set; }
		public string? Cpu { get; set; }
		public ulong Frame { get; set; }
		public string? Message { get; set; }
	}

	public class HangInfoDto
	{
		public ulong Frame { get; set; }
		public long StalledMs { get; set; }
	}

	public class ThrottledEventDto
	{
		public string Source { get; set; } = "";
		public ulong Count { get; set; }
	}

	public class DebugEventsReportDto
	{
		public string State { get; set; } = "running";
		public ulong Frame { get; set; }
		public double Fps { get; set; }
		public string? BreakReason { get; set; }
		public CrashInfoDto? Crash { get; set; }
		public HangInfoDto? Hang { get; set; }
		public ulong DroppedRecords { get; set; }
		public bool WriteWatchArmed { get; set; }
		public List<DebugEventGroupDto> Events { get; set; } = new();
		public List<ThrottledEventDto> Throttled { get; set; } = new();
		public List<string> Alerts { get; set; } = new();
	}

	public class CrashReportDto
	{
		public bool Crashed { get; set; }
		public CrashInfoDto? Crash { get; set; }
		public string? CpuState { get; set; }
		public List<string> Callstack { get; set; } = new();
		public List<string> TraceTail { get; set; } = new();
		public List<DebugEventGroupDto> RecentEvents { get; set; } = new();
		public string? Advice { get; set; }
	}

	public class LuaScriptInfoDto
	{
		public int Id { get; set; }
		public string Name { get; set; } = "";
		public bool InitDone { get; set; }
		public bool HasError { get; set; }
		public string? LastError { get; set; }
	}

	public class LuaStatusReportDto
	{
		public List<LuaScriptInfoDto> Scripts { get; set; } = new();
	}

	public class MemoryWriteDto
	{
		public ulong Frame { get; set; }
		public string? Pc { get; set; }
		public string? Address { get; set; }
		public int Value { get; set; }
		public string? Cpu { get; set; }
	}

	public class MemoryWriteReportDto
	{
		public bool Armed { get; set; }
		public string? WatchRange { get; set; }
		public List<MemoryWriteDto> Writes { get; set; } = new();
	}

	public class BankSwitchDto
	{
		public ulong Sequence { get; set; }
		public ulong Frame { get; set; }
		public string? Pc { get; set; }
		public string? Range { get; set; }
		public long OldOffset { get; set; }
		public long NewOffset { get; set; }
		public bool IsChr { get; set; }
		public int MemType { get; set; }
	}

	public class BankLogReportDto
	{
		public ulong Total { get; set; }
		public List<BankSwitchDto> Changes { get; set; } = new();
	}
}
