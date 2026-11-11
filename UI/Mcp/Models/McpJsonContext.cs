using System.Text.Json.Serialization;

namespace Mesen.Mcp.Models
{
	[JsonSourceGenerationOptions(
		PropertyNamingPolicy = JsonKnownNamingPolicy.CamelCase,
		UseStringEnumConverter = true,
		DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull
	)]

	// PPU state (used by console handlers)
	[JsonSerializable(typeof(NesPpuStateResponse))]
	[JsonSerializable(typeof(SnesPpuStateResponse))]
	[JsonSerializable(typeof(GbPpuStateResponse))]
	[JsonSerializable(typeof(GbaPpuStateResponse))]
	[JsonSerializable(typeof(SmsPpuStateResponse))]
	[JsonSerializable(typeof(PcePpuStateResponse))]
	[JsonSerializable(typeof(WsPpuStateResponse))]

	// ROM headers (used by console handlers)
	[JsonSerializable(typeof(NesRomHeaderResponse))]
	[JsonSerializable(typeof(SnesRomHeaderResponse))]

	// Debug event observability (used by DebugEventTools/DebugEventAggregator)
	[JsonSerializable(typeof(DebugEventGroupDto))]
	[JsonSerializable(typeof(ThrottledEventDto))]
	[JsonSerializable(typeof(CrashInfoDto))]
	[JsonSerializable(typeof(HangInfoDto))]
	[JsonSerializable(typeof(DebugEventsReportDto))]
	[JsonSerializable(typeof(CrashReportDto))]
	[JsonSerializable(typeof(LuaScriptInfoDto))]
	[JsonSerializable(typeof(LuaStatusReportDto))]
	[JsonSerializable(typeof(MemoryWriteDto))]
	[JsonSerializable(typeof(MemoryWriteReportDto))]
	[JsonSerializable(typeof(BankSwitchDto))]
	[JsonSerializable(typeof(BankLogReportDto))]

	public partial class McpJsonContext : JsonSerializerContext { }
}
