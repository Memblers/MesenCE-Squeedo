using Avalonia.Threading;
using Mesen.Debugger;
using Mesen.Debugger.Labels;
using Mesen.Debugger.Utilities;
using Mesen.Interop;
using Mesen.Mcp.Tools;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;
using ModelContextProtocol.AspNetCore;
using ModelContextProtocol.Protocol;
using ModelContextProtocol.Server;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using System;
using System.Threading.Tasks;

namespace Mesen.Mcp
{
	public class MesenMcpServer
	{
		private static readonly MesenMcpServer _instance = new();
		public static MesenMcpServer Instance => _instance;

		private WebApplication? _app;
		private volatile bool _isRunning;
		private ILogger? _logger;

		public bool IsRunning => _isRunning;

		private MesenMcpServer() { }

		public string? LastError { get; private set; }

		public async Task StartAsync(int port)
		{
			if(_isRunning) {
				return;
			}

			try {
				WebApplicationBuilder builder = WebApplication.CreateSlimBuilder(new WebApplicationOptions {
					Args = Array.Empty<string>()
				});

				builder.Logging.ClearProviders();
				builder.Logging.AddConsole(options => {
					options.LogToStandardErrorThreshold = LogLevel.Trace;
				});

				builder.WebHost.UseUrls($"http://localhost:{port}");

				builder.Services
					.AddMcpServer(options => {
						options.ServerInfo = new() {
							Name = "Mesen Emulator",
							Version = Interop.EmuApi.GetMesenVersion().ToString(3)
						};
					})
					.WithHttpTransport(options => {
						// Dual-era endpoint: 2026-07-28+ clients (per-request _meta, server/discover) are served
						// statelessly, while clients using the legacy initialize handshake (2025-11-25 and earlier)
						// keep a full Mcp-Session-Id session on the same /mcp endpoint.
						options.SessionMode = HttpServerSessionMode.StatefulForInitializeClients;
					})
					.WithTools<DebugExecutionTools>()
					.WithTools<DebugEventTools>()
					.WithTools<DisassemblyTools>()
					.WithTools<EmulatorTools>()
					.WithTools<EmulatorConfigTools>()
					.WithTools<HistoryTools>()
					.WithTools<InputTools>()
					.WithTools<MemoryTools>()
					.WithTools<RomHackingTools>()
					.WithTools<SpriteAndTilemapTools>()
					.WithTools<TextSearchTools>()
					.WithTools<TraceTools>()
					.WithPrompts<MesenMcpPrompts>()
					.WithResources<MesenMcpResources>();

				//Append an "unseen events" footer to every tool response so agents cannot
				//miss crashes, Lua errors or rate spikes even when a call "succeeded".
				builder.Services.Configure<McpServerOptions>(options => {
					options.Filters.Request.CallToolFilters.Add(next => async (request, cancellationToken) => {
						CallToolResult result = await next(request, cancellationToken);
						return DebugEventAggregator.AppendFooter(request.Params?.Name, result);
					});
				});

				_app = builder.Build();

				// Reject cross-origin requests from non-local pages to prevent DNS rebinding attacks (required by the Streamable HTTP transport)
				_app.Use(async (context, next) => {
					string? origin = context.Request.Headers.Origin;
					if(!string.IsNullOrEmpty(origin) && !IsLocalOrigin(origin)) {
						context.Response.StatusCode = StatusCodes.Status403Forbidden;
						return;
					}
					await next(context);
				});

				_app.MapMcp("/mcp");

				_logger = _app.Services.GetService<ILoggerFactory>()?.CreateLogger<MesenMcpServer>();

				await _app.StartAsync();
				_isRunning = true;
				LastError = null;
				_logger?.LogInformation("MCP server started on port {Port}", port);

				// Auto-initialize debugger and enable tracing for the main CPU
				InitializeDebuggerAndTracing();
			} catch(Exception ex) {
				LastError = ex.ToString();
				_isRunning = false;
				_logger?.LogError(ex, "Failed to start MCP server");
			}
		}

		private static bool IsLocalOrigin(string origin)
		{
			if(!Uri.TryCreate(origin, UriKind.Absolute, out Uri? uri)) {
				return false;
			}
			return uri.IsLoopback;
		}

		/// <summary>
		/// Called when a ROM is loaded so we can re-initialize the debugger and tracing,
		/// and load the debug workspace (symbol/label files like <rom>.mlb) so MCP tools
		/// can resolve labels without requiring a debug window to be opened first.
		/// </summary>
		public void OnRomLoaded(bool isPowerCycle)
		{
			if(!_isRunning) {
				return;
			}

			DebugEventAggregator.OnRomLoaded();
			InitializeDebuggerAndTracing();

			if(isPowerCycle) {
				//On power cycle, the core debugger is recreated - re-push labels and
				//breakpoints without reloading the workspace from disk
				Dispatcher.UIThread.Post(() => {
					BreakpointManager.SetBreakpoints();
					LabelManager.RefreshLabels(false);
				});
			} else {
				Dispatcher.UIThread.Post(() => DebugWorkspaceManager.Load());
			}
		}

		private void InitializeDebuggerAndTracing()
		{
			try {
				DebugEventAggregator.EnsureInitialized();
				if(EmuApi.IsRunning()) {
					DebugApi.InitializeDebugger();
					McpToolHelper.MarkDebuggerInitialized();
					McpToolHelper.EnableDefaultTracing();
				}
			} catch(Exception ex) {
				_logger?.LogWarning(ex, "Failed to auto-initialize debugger");
			}
		}

		public async Task StopAsync()
		{
			if(!_isRunning || _app == null) {
				return;
			}

			try {
				await _app.StopAsync();
				await _app.DisposeAsync();
				_logger?.LogInformation("MCP server stopped");
			} catch {
				// Ignore shutdown errors
			}
			_app = null;
			_isRunning = false;
			_logger = null;
		}
	}
}
