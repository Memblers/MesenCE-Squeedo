using Mesen.Interop;
using ModelContextProtocol;
using System.Collections.Generic;

namespace Mesen.Mcp.Consoles
{
	public static class ConsoleHandlerFactory
	{
		private static readonly NesHandler _nes = new();
		private static readonly SnesHandler _snes = new();
		private static readonly SpcHandler _spc = new();
		private static readonly GbHandler _gb = new();
		private static readonly GbaHandler _gba = new();
		private static readonly PceHandler _pce = new();
		private static readonly SmsHandler _sms = new();
		private static readonly WsHandler _ws = new();

		private static readonly Dictionary<CpuType, IConsoleHandler> _byCpu = new() {
			[CpuType.Nes] = _nes,
			[CpuType.Snes] = _snes,
			[CpuType.Sa1] = _snes,
			[CpuType.Spc] = _spc,
			[CpuType.Gameboy] = _gb,
			[CpuType.Gba] = _gba,
			[CpuType.Pce] = _pce,
			[CpuType.Sms] = _sms,
			[CpuType.Ws] = _ws
		};

		private static readonly Dictionary<ConsoleType, IConsoleHandler> _byConsole = new() {
			[ConsoleType.Nes] = _nes,
			[ConsoleType.Snes] = _snes,
			[ConsoleType.Gameboy] = _gb,
			[ConsoleType.Gba] = _gba,
			[ConsoleType.PcEngine] = _pce,
			[ConsoleType.Sms] = _sms,
			[ConsoleType.Ws] = _ws
		};

		public static IConsoleHandler GetHandler(CpuType cpu)
		{
			if(_byCpu.TryGetValue(cpu, out IConsoleHandler? handler)) {
				return handler;
			}
			throw new McpException("No console handler for CPU type: " + cpu);
		}

		public static IConsoleHandler GetHandler(ConsoleType console)
		{
			if(_byConsole.TryGetValue(console, out IConsoleHandler? handler)) {
				return handler;
			}
			throw new McpException("No console handler for console type: " + console);
		}
	}
}
