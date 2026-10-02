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

	public partial class McpJsonContext : JsonSerializerContext { }
}
