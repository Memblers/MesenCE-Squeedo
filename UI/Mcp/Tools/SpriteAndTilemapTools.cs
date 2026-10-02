using Mesen.Interop;
using ModelContextProtocol;
using ModelContextProtocol.Server;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

namespace Mesen.Mcp.Tools
{
	[McpServerToolType]
	public class SpriteAndTilemapTools
	{
		[McpServerTool(Name = "mesen_get_sprite_list", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("Get the list of all sprites currently displayed by the PPU. Returns position, tile, palette, size, mirroring, and priority for each sprite.")]
		public static string GetSpriteList(
			[Description("CPU type: Nes, Snes, Gameboy, Gba, Pce, Sms, Ws")] string cpuType)
		{
			McpToolHelper.EnsureDebuggerReady();
			CpuType cpu = McpToolHelper.ParseCpuType(cpuType);

			BaseState ppuState = DebugApi.GetPpuState(cpu);
			BaseState ppuToolsState = DebugApi.GetPpuToolsState(cpu);

			GetSpritePreviewOptions options = new GetSpritePreviewOptions() {
				Background = SpriteBackground.Transparent
			};

			DebugSpritePreviewInfo previewInfo = DebugApi.GetSpritePreviewInfo(cpu, options, ppuState, ppuToolsState);

			// Get VRAM and sprite RAM
			MemoryType vramType = cpu.GetVramMemoryType();
			byte[] vram = DebugApi.GetMemoryState(vramType);
			MemoryType vramExtType = cpu.GetVramMemoryType(getExtendedRam: true);
			if(vramType != vramExtType) {
				byte[] extVram = DebugApi.GetMemoryState(vramExtType);
				byte[] combined = new byte[vram.Length + extVram.Length];
				Array.Copy(vram, combined, vram.Length);
				Array.Copy(extVram, 0, combined, vram.Length, extVram.Length);
				vram = combined;
			}

			MemoryType spriteRamType = cpu.GetSpriteRamMemoryType();
			byte[] spriteRam;
			if(spriteRamType == MemoryType.None) {
				spriteRam = Array.Empty<byte>();
			} else {
				spriteRam = DebugApi.GetMemoryState(spriteRamType);
				MemoryType spriteRamExtType = cpu.GetSpriteRamMemoryType(getExtendedRam: true);
				if(spriteRamType != spriteRamExtType) {
					byte[] extSpriteRam = DebugApi.GetMemoryState(spriteRamExtType);
					byte[] combined = new byte[spriteRam.Length + extSpriteRam.Length];
					Array.Copy(spriteRam, combined, spriteRam.Length);
					Array.Copy(extSpriteRam, 0, combined, spriteRam.Length, extSpriteRam.Length);
					spriteRam = combined;
				}
			}

			UInt32[] palette = DebugApi.GetPaletteInfo(cpu).GetRgbPalette();

			DebugSpriteInfo[] spriteList = Array.Empty<DebugSpriteInfo>();
			UInt32[] spritePreviews = Array.Empty<UInt32>();

			// Allocate screen preview buffer (C++ writes to it even if we don't need it)
			int screenPixels = (int)previewInfo.Width * (int)previewInfo.Height;
			UInt32[] screenBuffer = new UInt32[Math.Max(screenPixels, 1)];
			GCHandle screenHandle = GCHandle.Alloc(screenBuffer, GCHandleType.Pinned);
			try {
				DebugApi.GetSpriteList(ref spriteList, ref spritePreviews, cpu, options, ppuState, ppuToolsState, vram, spriteRam, palette, screenHandle.AddrOfPinnedObject());
			} finally {
				screenHandle.Free();
			}

			int bpp = spriteList.Length > 0 ? spriteList[0].Bpp : 0;
			string format = spriteList.Length > 0 ? spriteList[0].Format.ToString() : "";

			StringBuilder sb = new();
			sb.Append(previewInfo.Width).Append('x').Append(previewInfo.Height)
				.Append(" bpp=").Append(bpp)
				.Append(" format=").AppendLine(format);
			sb.AppendLine("#\tX\tY\tWxH\tTile\tTileAddr\tPal\tPalAddr\tPri\tHF\tVF\tVis");

			foreach(DebugSpriteInfo sprite in spriteList) {
				string vis = sprite.Visibility.ToString();
				sb.Append(sprite.SpriteIndex).Append('\t')
					.Append(sprite.X).Append('\t')
					.Append(sprite.Y).Append('\t')
					.Append(sprite.Width).Append('x').Append(sprite.Height).Append('\t')
					.Append('$').Append(sprite.TileIndex.ToString("X2")).Append('\t')
					.Append('$').Append(sprite.TileAddress.ToString("X4")).Append('\t')
					.Append(sprite.Palette).Append('\t')
					.Append('$').Append(sprite.PaletteAddress.ToString("X4")).Append('\t')
					.Append(sprite.Priority).Append('\t')
					.Append(sprite.HorizontalMirror == NullableBoolean.True ? 'H' : '-').Append('\t')
					.Append(sprite.VerticalMirror == NullableBoolean.True ? 'V' : '-').Append('\t')
					.AppendLine(vis != "Visible" ? vis : "-");
			}
			return sb.ToString();
		}

		[McpServerTool(Name = "mesen_get_tilemap_info", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("Get tilemap information: dimensions, scroll position, tile size, and per-tile details. Use layer parameter for systems with multiple BG layers (SNES, GBA).")]
		public static string GetTilemapInfo(
			[Description("CPU type: Nes, Snes, Gameboy, Gba, Pce, Sms, Ws")] string cpuType,
			[Description("BG layer index (0-3, default 0). Used for SNES/GBA which have multiple layers.")] int layer = 0)
		{
			McpToolHelper.EnsureDebuggerReady();
			CpuType cpu = McpToolHelper.ParseCpuType(cpuType);

			BaseState ppuState = DebugApi.GetPpuState(cpu);
			BaseState ppuToolsState = DebugApi.GetPpuToolsState(cpu);

			GetTilemapOptions options = new GetTilemapOptions() {
				Layer = (byte)layer
			};

			FrameInfo tilemapSize = DebugApi.GetTilemapSize(cpu, options, ppuState);

			MemoryType vramType = cpu.GetVramMemoryType();
			byte[] vram = DebugApi.GetMemoryState(vramType);
			MemoryType vramExtType = cpu.GetVramMemoryType(getExtendedRam: true);
			if(vramType != vramExtType) {
				byte[] extVram = DebugApi.GetMemoryState(vramExtType);
				byte[] combined = new byte[vram.Length + extVram.Length];
				Array.Copy(vram, combined, vram.Length);
				Array.Copy(extVram, 0, combined, vram.Length, extVram.Length);
				vram = combined;
			}

			UInt32[] palette = DebugApi.GetPaletteInfo(cpu).GetRgbPalette();

			// Allocate output buffer (C++ writes to it even if we don't use the pixels)
			int tilemapPixels = (int)tilemapSize.Width * (int)tilemapSize.Height;
			UInt32[] tilemapBuffer = new UInt32[Math.Max(tilemapPixels, 1)];
			GCHandle tilemapHandle = GCHandle.Alloc(tilemapBuffer, GCHandleType.Pinned);
			DebugTilemapInfo tilemapInfo;
			try {
				tilemapInfo = DebugApi.GetTilemap(cpu, options, ppuState, ppuToolsState, vram, palette, tilemapHandle.AddrOfPinnedObject());
			} finally {
				tilemapHandle.Free();
			}

			StringBuilder sb = new();
			sb.Append("Layer ").Append(layer).Append(' ')
				.Append(tilemapSize.Width).Append('x').Append(tilemapSize.Height)
				.Append(" tile=").Append(tilemapInfo.TileWidth).Append('x').Append(tilemapInfo.TileHeight)
				.Append(" bpp=").Append(tilemapInfo.Bpp)
				.Append(" format=").Append(tilemapInfo.Format)
				.Append(" mirror=").AppendLine(tilemapInfo.Mirroring.ToString());
			sb.Append("Scroll: (").Append(tilemapInfo.ScrollX).Append(',').Append(tilemapInfo.ScrollY)
				.Append(") / ").Append(tilemapInfo.ScrollWidth).Append('x').Append(tilemapInfo.ScrollHeight)
				.Append(" Grid: ").Append(tilemapInfo.RowCount).Append('x').Append(tilemapInfo.ColumnCount).AppendLine();
			sb.Append("Tilemap: $").Append(tilemapInfo.TilemapAddress.ToString("X4"))
				.Append(" Tileset: $").Append(tilemapInfo.TilesetAddress.ToString("X4"))
				.Append(" Priority: ").Append(tilemapInfo.Priority);
			return sb.ToString();
		}

		[McpServerTool(Name = "mesen_get_tilemap_tile_info", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("Get detailed information about a specific tile in the tilemap by its pixel coordinates.")]
		public static string GetTilemapTileInfo(
			[Description("X pixel coordinate in the tilemap")] int x,
			[Description("Y pixel coordinate in the tilemap")] int y,
			[Description("CPU type: Nes, Snes, Gameboy, Gba, Pce, Sms, Ws")] string cpuType,
			[Description("BG layer index (0-3, default 0)")] int layer = 0)
		{
			McpToolHelper.EnsureDebuggerReady();
			CpuType cpu = McpToolHelper.ParseCpuType(cpuType);

			BaseState ppuState = DebugApi.GetPpuState(cpu);
			BaseState ppuToolsState = DebugApi.GetPpuToolsState(cpu);

			MemoryType vramType = cpu.GetVramMemoryType();
			byte[] vram = DebugApi.GetMemoryState(vramType);
			MemoryType vramExtType = cpu.GetVramMemoryType(getExtendedRam: true);
			if(vramType != vramExtType) {
				byte[] extVram = DebugApi.GetMemoryState(vramExtType);
				byte[] combined = new byte[vram.Length + extVram.Length];
				Array.Copy(vram, combined, vram.Length);
				Array.Copy(extVram, 0, combined, vram.Length, extVram.Length);
				vram = combined;
			}

			GetTilemapOptions options = new GetTilemapOptions() {
				Layer = (byte)layer
			};

			DebugTilemapTileInfo? tileInfo = DebugApi.GetTilemapTileInfo((uint)x, (uint)y, cpu, options, vram, ppuState, ppuToolsState);
			if(tileInfo == null) {
				throw new McpException($"No tile at ({x}, {y})");
			}

			DebugTilemapTileInfo tile = tileInfo.Value;
			StringBuilder sb = new();
			sb.Append("Tile (").Append(tile.Row).Append(',').Append(tile.Column).Append(") ")
				.Append(tile.Width).Append('x').Append(tile.Height)
				.Append(" index=").Append(tile.TileIndex)
				.Append(" map=$").Append(tile.TileMapAddress.ToString("X4"))
				.Append(" tile=$").AppendLine(tile.TileAddress.ToString("X4"));
			sb.Append("Palette: ").Append(tile.PaletteIndex)
				.Append(" ($").Append(tile.PaletteAddress.ToString("X4")).Append(')')
				.Append(" HFlip=").Append(tile.HorizontalMirroring == NullableBoolean.True)
				.Append(" VFlip=").Append(tile.VerticalMirroring == NullableBoolean.True)
				.Append(" Priority=").Append(tile.HighPriority == NullableBoolean.True);
			return sb.ToString();
		}
	}
}
