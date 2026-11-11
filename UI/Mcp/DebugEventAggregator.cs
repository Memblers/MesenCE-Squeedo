using Mesen.Config;
using Mesen.Interop;
using Mesen.Mcp.Consoles;
using Mesen.Mcp.Models;
using Mesen.Mcp.Tools;
using ModelContextProtocol;
using ModelContextProtocol.Protocol;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace Mesen.Mcp
{
	public enum EventSeverity
	{
		Info,
		Suspicious,
		Fatal
	}

	/// <summary>
	/// Drains the core-side debug event log, aggregates events per (kind, source, PC),
	/// tracks rates/spikes, crash/hang state and break reasons, and builds the
	/// "unseen events" footer appended to every MCP tool response.
	/// </summary>
	public static class DebugEventAggregator
	{
		private class EventGroup
		{
			public DebugEventKind Kind;
			public BreakSource Source = BreakSource.Unspecified;
			public uint Pc;
			public int Cpu;
			public string Message = "";
			public ulong Count;
			public ulong NewCount;
			public ulong FirstFrame;
			public ulong LastFrame;
			public double RatePerFrame;
			public bool Spike;
			public bool SpikeAnnounced;

			//rate tracking (60-frame windows)
			public ulong WindowStartFrame;
			public ulong WindowCount;
			public double PrevWindowRate;
			public string Key => Kind + "|" + (int)Source + "|" + Pc;
		}

		//Per-source aggregation used to batch storm alerts (one summary per source per drain)
		private class EventStormStats
		{
			public ulong Total;
			public ulong NewSites;
			public uint MinPc;
			public uint MaxPc;
			public ulong Throttled;
			public bool Spike;
			public double SpikeRate;
		}

		//Minimal marker queued from the emulation thread, applied later on the MCP thread
		private class PendingNotification
		{
			public ConsoleNotificationType Type;
			public BreakSource Source = BreakSource.Unspecified;
			public CpuType Cpu;
		}

		private static readonly object _lock = new();
		private static bool _initialized;
		private static NotificationListener? _listener;

		//Notifications arrive on the emulation thread (synchronous native callback). They are
		//queued here with no locks and no core calls, and applied on the MCP thread in Drain()
		//via ProcessPendingNotifications() - inline capture on the emulation thread is what
		//used to starve the debugger/palette windows during an event storm.
		private const int MaxPendingNotifications = 256;
		private static readonly ConcurrentQueue<PendingNotification> _pendingNotifications = new();
		private static int _pendingNotificationCount;
		private static bool _notificationSyncNeeded;

		//Last-seen cumulative per-source throttle counters (core-side storm cap)
		private static readonly ulong[] _lastThrottleCounts = new ulong[64];

		private static ulong _eventCursor;
		private static ulong _bankCursor;
		private static ulong _droppedRecords;

		private static readonly Dictionary<string, EventGroup> _groups = new();
		private static readonly List<EventGroup> _order = new();
		private static readonly List<string> _pendingAlerts = new();
		private static int _suppressedAlerts;

		//crash state
		private static bool _crashed;
		private static CrashInfoDto? _crashInfo;

		//hang watchdog
		private static uint _lastFrame;
		private static long _lastFrameTick;
		private static long? _hangSinceTick;
		private static uint _hangFrame;

		//break/pause state
		private static bool _breakActive;
		private static string? _breakReason;
		private static bool _userPaused;

		//expected (legit) sites
		private static readonly Dictionary<string, string> _expectedSites = new();
		private static bool _writeWatchArmed;

		private static readonly BreakSource[] FatalTier = {
			BreakSource.NesBreakOnCpuCrash, BreakSource.GbInvalidOpCode, BreakSource.GbaInvalidOpCode, BreakSource.BreakOnUndefinedOpCode
		};

		private static readonly BreakSource[] SuspiciousTier = {
			BreakSource.BreakOnBrk, BreakSource.BreakOnUnofficialOpCode, BreakSource.BreakOnUnstableOpCode,
			BreakSource.NesBusConflict, BreakSource.NesBreakOnPpuScrollGlitch, BreakSource.GbOamCorruption,
			BreakSource.BreakOnUninitMemoryRead, BreakSource.NesDmaInputRead, BreakSource.NesBreakOnDecayedOamRead,
			BreakSource.NesBreakOnExtOutputMode, BreakSource.NesInvalidVramAccess, BreakSource.NesInvalidOamWrite,
			BreakSource.GbInvalidOamAccess, BreakSource.GbInvalidVramAccess, BreakSource.GbDisableLcdOutsideVblank,
			BreakSource.GbNopLoad, BreakSource.SmsNopLoad, BreakSource.GbaNopLoad,
			BreakSource.BreakOnCop, BreakSource.BreakOnWdm, BreakSource.BreakOnStp,
			BreakSource.PceBreakOnInvalidVramAddress, BreakSource.SnesInvalidPpuAccess, BreakSource.SnesReadDuringAutoJoy,
			BreakSource.GbaUnalignedMemoryAccess
		};

		public static void EnsureInitialized()
		{
			lock(_lock) {
				if(_initialized) {
					return;
				}
				_initialized = true;

				try {
					DebugApi.SetDebugEventsEnabled(true);
					ApplyDefaultPolicies();
				} catch { }

				try {
					_listener = new NotificationListener();
					_listener.OnNotification += OnNotification;
				} catch {
					_listener = null;
				}
			}
		}

		private static void ApplyDefaultPolicies()
		{
			foreach(BreakSource src in FatalTier) {
				DebugApi.SetBreakEventPolicy((int)src, (int)DebugEventPolicy.Break);
			}
			//Suspicious sources (BRK, unofficial opcodes, invalid VRAM, ...) fire constantly in
			//normal games and storm during garbage execution - default them to Ignore. Recording
			//them is opt-in via mesen_set_event_policy (records are storm-throttled in the core).
			foreach(BreakSource src in SuspiciousTier) {
				DebugApi.SetBreakEventPolicy((int)src, (int)DebugEventPolicy.Ignore);
			}
		}

		public static void OnRomLoaded()
		{
			lock(_lock) {
				try { DebugApi.ResetDebugEvents(); } catch { }
				_eventCursor = 0;
				_bankCursor = 0;
				_droppedRecords = 0;
				Array.Clear(_lastThrottleCounts, 0, _lastThrottleCounts.Length);
				_groups.Clear();
				_order.Clear();
				_pendingAlerts.Clear();
				_expectedSites.Clear();
				_crashed = false;
				_crashInfo = null;
				_hangSinceTick = null;
				_breakActive = false;
				_breakReason = null;
				_lastFrameTick = 0;
			}
		}

		//Runs on the emulation thread (synchronous native callback). This path must stay cheap:
		//no core calls, no shared locks - just queue a minimal marker. All state changes
		//(break reason, PC lookup, ...) are applied later on the MCP thread in
		//ProcessPendingNotifications(), called from Drain() while the emu is free-running.
		private static void OnNotification(NotificationEventArgs e)
		{
			bool critical = e.NotificationType == ConsoleNotificationType.GameLoaded || e.NotificationType == ConsoleNotificationType.GameReset;
			if(!critical && Interlocked.CompareExchange(ref _pendingNotificationCount, 0, 0) >= MaxPendingNotifications) {
				//Break storm - drop and force a state resync on the next drain
				_notificationSyncNeeded = true;
				return;
			}

			PendingNotification n = new() { Type = e.NotificationType };
			if(e.NotificationType == ConsoleNotificationType.CodeBreak) {
				try {
					BreakEvent evt = Marshal.PtrToStructure<BreakEvent>(e.Parameter);
					n.Source = evt.Source;
					n.Cpu = evt.SourceCpu;
				} catch { }
			}

			Interlocked.Increment(ref _pendingNotificationCount);
			_pendingNotifications.Enqueue(n);
		}

		//Applies queued notifications on the MCP thread. Also recovers the break state when
		//notifications had to be dropped during a storm.
		private static void ProcessPendingNotifications()
		{
			while(_pendingNotifications.TryDequeue(out PendingNotification? n)) {
				Interlocked.Decrement(ref _pendingNotificationCount);
				switch(n.Type) {
					case ConsoleNotificationType.CodeBreak: {
						if(n.Source == BreakSource.Unspecified) {
							_breakActive = true;
							_breakReason = "break";
							break;
						}
						uint pc = 0;
						try {
							//Safe to call now - this runs on the MCP thread, not the emulation thread
							pc = DebugApi.GetProgramCounter(n.Cpu, true);
						} catch { }
						_breakActive = true;
						_breakReason = n.Source + " at " + FormatPc(pc) + " (frame " + SafeGetFrame() + ")";
						break;
					}

					case ConsoleNotificationType.DebuggerResumed:
					case ConsoleNotificationType.GameResumed:
						_breakActive = false;
						_breakReason = null;
						_userPaused = false;
						break;

					case ConsoleNotificationType.GamePaused:
						_userPaused = true;
						break;

					case ConsoleNotificationType.GameLoaded:
						OnRomLoaded();
						break;

					case ConsoleNotificationType.GameReset:
						_crashed = false;
						_crashInfo = null;
						_hangSinceTick = null;
						break;
				}
			}

			if(_notificationSyncNeeded) {
				_notificationSyncNeeded = false;
				try {
					if(!DebugApi.IsExecutionStopped()) {
						_breakActive = false;
						_breakReason = null;
					}
				} catch { }
			}
		}

		//Folds the core-side storm throttle counters (events that were counted but not captured)
		//into the per-source batch tracker so the summary alert reports them.
		private static void RefreshThrottleCounts(Dictionary<BreakSource, EventStormStats> storms)
		{
			try {
				ulong[] counts = DebugApi.GetThrottledEventCounts();
				for(int i = 0; i < counts.Length && i < _lastThrottleCounts.Length; i++) {
					ulong delta = counts[i] >= _lastThrottleCounts[i] ? counts[i] - _lastThrottleCounts[i] : counts[i];
					_lastThrottleCounts[i] = counts[i];
					if(delta > 0) {
						EventStormStats stats = GetStormStats(storms, (BreakSource)i);
						stats.Throttled += delta;
						stats.Total += delta;
					}
				}
			} catch { }
		}

		private static EventStormStats GetStormStats(Dictionary<BreakSource, EventStormStats> storms, BreakSource source)
		{
			if(!storms.TryGetValue(source, out EventStormStats? stats)) {
				stats = new EventStormStats();
				storms[source] = stats;
			}
			return stats;
		}

		private static ulong SafeGetFrame()
		{
			try {
				return EmuApi.IsRunning() ? EmuApi.GetTimingInfo(EmuApi.GetRomInfo().ConsoleType.GetMainCpuType()).FrameCount : 0;
			} catch {
				return 0;
			}
		}

		private static string FormatPc(uint pc) => "$" + pc.ToString("X4");

		//Drains new records from the core and merges them into the aggregate groups.
		//Also runs the hang watchdog. Returns the number of newly seen records.
		private static int Drain()
		{
			//Apply queued emulator-thread notifications first (GameLoaded resets the cursor
			//below), then fold the core-side throttle counters into the batch tracker.
			Dictionary<BreakSource, EventStormStats> storms = new();
			ProcessPendingNotifications();
			RefreshThrottleCounts(storms);

			int newRecords = 0;
			try {
				ulong nextCursor;
				DebugEventRecord[] records = DebugApi.PollDebugEvents(_eventCursor, out nextCursor);

				foreach(DebugEventRecord rec in records) {
					if(newRecords == 0 && _eventCursor > 0 && rec.Sequence > _eventCursor + 1) {
						_droppedRecords += rec.Sequence - _eventCursor - 1;
					}
					newRecords++;

					DebugEventKind kind = (DebugEventKind)rec.EventType;
					string key = kind + "|" + rec.Source + "|" + rec.Pc;
					if(!_groups.TryGetValue(key, out EventGroup? group)) {
						group = new EventGroup {
							Kind = kind,
							Source = (BreakSource)rec.Source,
							Pc = rec.Pc,
							Cpu = rec.CpuType,
							FirstFrame = rec.FrameCount
						};
						_groups[key] = group;
						_order.Add(group);
					}

					group.Count++;
					group.NewCount++;
					group.LastFrame = rec.FrameCount;
					string msg = rec.GetMessage();
					string prevMsg = group.Message;
					if(!string.IsNullOrEmpty(msg)) {
						group.Message = msg;
					}

					UpdateRate(group, rec.FrameCount);

					bool expected = _expectedSites.ContainsKey(group.Key);
					switch(kind) {
						case DebugEventKind.Crash:
							if(!_crashed) {
								_crashed = true;
								_crashInfo = new CrashInfoDto {
									Source = group.Source.ToString(),
									Pc = FormatPc(rec.Pc),
									Cpu = ((CpuType)rec.CpuType).ToString(),
									Frame = rec.FrameCount,
									Message = msg
								};
								AddAlert("[mesen] ⚠ CRASHED at " + FormatPc(rec.Pc) + " (frame " + rec.FrameCount + ", " + group.Source + ") - call mesen_get_crash_report");
							}
							break;

						case DebugEventKind.LuaError:
							if(group.NewCount <= 1 || msg != prevMsg) {
								AddAlert("[mesen] ⚠ LuaError " + Truncate(msg, 120));
							}
							break;

						case DebugEventKind.BreakEvent:
							if(!expected) {
								//Batch per source - one summary alert per source per drain instead
								//of one per PC (a garbage storm hits dozens of distinct PCs per frame)
								EventStormStats stats = GetStormStats(storms, group.Source);
								stats.Total++;
								if(group.NewCount == 1) {
									stats.NewSites++;
									if(stats.NewSites == 1) {
										stats.MinPc = rec.Pc;
										stats.MaxPc = rec.Pc;
									} else {
										stats.MinPc = Math.Min(stats.MinPc, rec.Pc);
										stats.MaxPc = Math.Max(stats.MaxPc, rec.Pc);
									}
								}
								if(group.Spike && !group.SpikeAnnounced) {
									group.SpikeAnnounced = true;
									stats.Spike = true;
									stats.SpikeRate = Math.Max(stats.SpikeRate, group.RatePerFrame);
								}
							}
							break;

						case DebugEventKind.Message:
							//OSD messages: aggregated silently, surfaced via mesen_get_events
							break;
					}
				}

				if(records.Length > 0) {
					_eventCursor = nextCursor;
				}
			} catch { }

			//Batched storm alerts: at most one summary (plus one spike warning) per source per drain
			foreach(KeyValuePair<BreakSource, EventStormStats> kv in storms) {
				EventStormStats s = kv.Value;
				string summary = "[mesen] " + kv.Key + " x" + s.Total;
				if(s.NewSites > 0) {
					summary += ", " + s.NewSites + " new site(s) " + FormatPc(s.MinPc) + "-" + FormatPc(s.MaxPc);
				}
				if(s.Throttled > 0) {
					summary += " (+" + s.Throttled + " throttled by storm cap)";
				}
				AddAlert(summary);
				if(s.Spike) {
					AddAlert("[mesen] ⚠ rate spike: " + kv.Key + " (" + s.SpikeRate.ToString("0.##") + "/frame) - possible crash loop");
				}
			}

			PollHang();
			return newRecords;
		}

		private static void AddAlert(string alert)
		{
			//Cap the pending list - a crash loop must not flood the footer
			if(_pendingAlerts.Count >= 20) {
				_suppressedAlerts++;
				return;
			}
			_pendingAlerts.Add(alert);
		}

		private static void UpdateRate(EventGroup group, ulong frame)
		{
			if(group.WindowCount == 0) {
				group.WindowStartFrame = frame;
			}
			group.WindowCount++;

			if(frame > group.WindowStartFrame && frame - group.WindowStartFrame >= 60) {
				double rate = group.WindowCount / (double)(frame - group.WindowStartFrame);
				if(group.PrevWindowRate > 0 && rate > group.PrevWindowRate * 4 && rate >= 0.5) {
					group.Spike = true;
					group.SpikeAnnounced = false;
				} else if(group.PrevWindowRate > 0 && rate <= group.PrevWindowRate * 2) {
					group.Spike = false;
					group.SpikeAnnounced = false;
				}
				group.PrevWindowRate = rate;
				group.WindowStartFrame = frame;
				group.WindowCount = 0;
			}

			group.RatePerFrame = group.PrevWindowRate;
		}

		private static void PollHang()
		{
			try {
				if(!EmuApi.IsRunning()) {
					_hangSinceTick = null;
					return;
				}

				uint frame = EmuApi.GetTimingInfo(EmuApi.GetRomInfo().ConsoleType.GetMainCpuType()).FrameCount;
				long now = Stopwatch.GetTimestamp();

				if(frame != _lastFrame) {
					_lastFrame = frame;
					_lastFrameTick = now;
					_hangSinceTick = null;
					return;
				}

				if(_userPaused || _breakActive || _lastFrameTick == 0) {
					return;
				}

				long stalledMs = (now - _lastFrameTick) * 1000 / Stopwatch.Frequency;
				if(stalledMs > 5000) {
					if(_hangSinceTick == null) {
						_hangSinceTick = _lastFrameTick;
						_hangFrame = frame;
						AddAlert("[mesen] ⚠ HUNG: frame count stalled at " + frame + " for " + (stalledMs / 1000) + "s (crash loop or infinite wait?)");
					}
				}
			} catch { }
		}

		private static string Truncate(string s, int max)
		{
			return s.Length <= max ? s : s.Substring(0, max) + "...";
		}

		private static string? BuildStateString()
		{
			if(_crashed) {
				return "crashed";
			}
			if(_hangSinceTick != null) {
				return "hung";
			}
			if(_breakActive) {
				return "paused (break)";
			}
			if(_userPaused) {
				return "paused (user)";
			}
			return "running";
		}

		/// <summary>
		/// Appends a compact alert footer to a tool result so agents cannot miss crashes/errors.
		/// Returns the result unchanged when there is nothing new to report.
		/// </summary>
		public static CallToolResult AppendFooter(string? toolName, CallToolResult result)
		{
			try {
				string? footer = BuildFooter(toolName);
				if(footer == null) {
					return result;
				}

				List<ContentBlock> content = result.Content == null ? new List<ContentBlock>() : new List<ContentBlock>(result.Content);
				content.Add(new TextContentBlock { Text = footer });
				return new CallToolResult {
					Content = content,
					IsError = result.IsError,
					StructuredContent = result.StructuredContent,
					Meta = result.Meta
				};
			} catch {
				return result;
			}
		}

		private static string? BuildFooter(string? toolName)
		{
			lock(_lock) {
				if(!_initialized) {
					return null;
				}

				Drain();

				bool isEventsTool = toolName == "mesen_get_events";
				List<string> lines = new();

				//Persistent state lines are always shown while active
				if(_crashed && _crashInfo != null) {
					lines.Add("[mesen] ⚠ CRASHED at " + _crashInfo.Pc + " (frame " + _crashInfo.Frame + ") - call mesen_get_crash_report");
				}
				if(_hangSinceTick != null) {
					long stalledMs = (Stopwatch.GetTimestamp() - _hangSinceTick.Value) * 1000 / Stopwatch.Frequency;
					lines.Add("[mesen] ⚠ HUNG: frame " + _hangFrame + " stalled for " + (stalledMs / 1000) + "s");
				}
				if(_breakActive && _breakReason != null) {
					lines.Add("[mesen] PAUSED: " + _breakReason);
				}

				if(isEventsTool) {
					//mesen_get_events already shows everything - just consume the alerts
					_pendingAlerts.Clear();
					ResetNewCounts();
				} else {
					foreach(string alert in _pendingAlerts) {
						if(lines.Count >= 5) {
							break;
						}
						lines.Add(alert);
					}
					if(_suppressedAlerts > 0) {
						lines.Add("[mesen] ... " + _suppressedAlerts + " more alerts suppressed (call mesen_get_events)");
					}
					_pendingAlerts.Clear();
					_suppressedAlerts = 0;
					ResetNewCounts();
				}

				if(lines.Count == 0) {
					return null;
				}
				return "\n---\n" + string.Join("\n", lines);
			}
		}

		private static void ResetNewCounts()
		{
			foreach(EventGroup group in _order) {
				group.NewCount = 0;
			}
		}

		public static string GetEventsReport(bool includeMessages)
		{
			lock(_lock) {
				if(!_initialized) {
					EnsureInitialized();
				}
				Drain();

				DebugEventsReportDto report = new() {
					State = BuildStateString() ?? "unknown",
					Frame = SafeGetFrame(),
					Fps = 0,
					BreakReason = _breakReason,
					Crash = _crashInfo,
					DroppedRecords = _droppedRecords,
					WriteWatchArmed = IsWriteWatchArmedSafe()
				};

				try {
					if(EmuApi.IsRunning()) {
						report.Fps = Math.Round(EmuApi.GetTimingInfo(EmuApi.GetRomInfo().ConsoleType.GetMainCpuType()).Fps, 2);
					}
				} catch { }

				if(_hangSinceTick != null) {
					report.Hang = new HangInfoDto {
						Frame = _hangFrame,
						StalledMs = (Stopwatch.GetTimestamp() - _hangSinceTick.Value) * 1000 / Stopwatch.Frequency
					};
				}

				try {
					//Events skipped by the core-side storm cap (cumulative since ROM load/reset)
					ulong[] throttled = DebugApi.GetThrottledEventCounts();
					for(int i = 0; i < throttled.Length; i++) {
						if(throttled[i] > 0) {
							report.Throttled.Add(new ThrottledEventDto { Source = ((BreakSource)i).ToString(), Count = throttled[i] });
						}
					}
				} catch { }

				foreach(EventGroup group in _order) {
					if(!includeMessages && group.Kind == DebugEventKind.Message) {
						continue;
					}
					report.Events.Add(ToDto(group));
				}

				report.Alerts.AddRange(_pendingAlerts);
				if(_suppressedAlerts > 0) {
					report.Alerts.Add("... " + _suppressedAlerts + " more alerts suppressed");
				}
				_pendingAlerts.Clear();
				_suppressedAlerts = 0;
				ResetNewCounts();

				return McpToolHelper.Serialize(report);
			}
		}

		private static DebugEventGroupDto ToDto(EventGroup group)
		{
			return new DebugEventGroupDto {
				Kind = group.Kind.ToString(),
				Source = group.Source == BreakSource.Unspecified ? null : group.Source.ToString(),
				Pc = group.Pc == 0 ? null : FormatPc(group.Pc),
				Cpu = group.Cpu < 0 ? null : ((CpuType)group.Cpu).ToString(),
				Count = group.Count,
				NewCount = group.NewCount,
				FirstFrame = group.FirstFrame,
				LastFrame = group.LastFrame,
				RatePerFrame = Math.Round(group.RatePerFrame, 4),
				Spike = group.Spike,
				Expected = _expectedSites.ContainsKey(group.Key),
				Note = _expectedSites.TryGetValue(group.Key, out string? note) ? note : null,
				Message = string.IsNullOrEmpty(group.Message) ? null : group.Message
			};
		}

		public static string GetCrashReport()
		{
			lock(_lock) {
				if(!_initialized) {
					EnsureInitialized();
				}
				Drain();

				CrashReportDto report = new() {
					Crashed = _crashed,
					Crash = _crashInfo
				};

				if(_crashed && _crashInfo != null) {
					try {
						CpuType cpu = EmuApi.GetRomInfo().ConsoleType.GetMainCpuType();
						uint pc = DebugApi.GetProgramCounter(cpu, true);
						report.CpuState = "PC=" + FormatPc(pc) + " " + FormatRegisters(cpu);

						StackFrameInfo[] callstack = DebugApi.GetCallstack(cpu);
						foreach(StackFrameInfo frame in callstack) {
							report.Callstack.Add("$" + frame.Source.ToString("X4") + " -> $" + frame.Target.ToString("X4") + " ret=$" + frame.Return.ToString("X4"));
						}

						//Trace tail (chronological: oldest -> newest), the instructions leading up to the crash
						TraceRow[] rows = DebugApi.GetExecutionTrace(0, 50);
						for(int i = rows.Length - 1; i >= 0; i--) {
							report.TraceTail.Add("$" + rows[i].ProgramCounter.ToString("X4") + ": " + rows[i].GetOutput());
						}

						report.Advice = "Inspect the crash PC, callstack and trace tail above (the trace tail is only populated when tracing was enabled beforehand via mesen_set_trace_options - tracing is off by default). Use mesen_get_last_trace for more trace rows, mesen_get_state for full registers, mesen_read_memory to inspect RAM, and mesen_set_event_policy to tune which events break execution.";
					} catch(Exception ex) {
						report.Advice = "Failed to collect crash state: " + ex.Message;
					}
				} else {
					report.Advice = "No crash recorded since ROM load. Crash events are always recorded (fatal tier). If the game froze without a crash event, check mesen_get_events for 'hung' state.";
				}

				foreach(EventGroup group in _order) {
					if(group.Kind == DebugEventKind.Crash || group.Kind == DebugEventKind.BreakEvent) {
						report.RecentEvents.Add(ToDto(group));
					}
				}

				return McpToolHelper.Serialize(report);
			}
		}

		private static string FormatRegisters(CpuType cpu)
		{
			try {
				IConsoleHandler handler = ConsoleHandlerFactory.GetHandler(cpu);
				Dictionary<string, string>? regs = handler.GetRegisters(cpu);
				if(regs == null) {
					return "";
				}
				StringBuilder sb = new();
				foreach(var kv in regs) {
					sb.Append(kv.Key).Append('=').Append(kv.Value).Append(' ');
				}
				return sb.ToString().TrimEnd();
			} catch {
				return "";
			}
		}

		public static string GetStatusSummary()
		{
			lock(_lock) {
				if(!_initialized) {
					EnsureInitialized();
				}
				Drain();

				StringBuilder sb = new();
				sb.Append("state=").Append(BuildStateString());
				if(_breakReason != null) {
					sb.Append(" reason=\"").Append(_breakReason).Append('"');
				}
				if(_crashInfo != null) {
					sb.Append(" crashAt=").Append(_crashInfo.Pc).Append(" crashFrame=").Append(_crashInfo.Frame);
				}
				if(_hangSinceTick != null) {
					sb.Append(" hungAtFrame=").Append(_hangFrame);
				}
				return sb.ToString();
			}
		}

		//--- policies and expected sites ---

		public static void SetPolicy(BreakSource source, DebugEventPolicy policy)
		{
			DebugApi.SetBreakEventPolicy((int)source, (int)policy);
		}

		public static DebugEventPolicy GetPolicy(BreakSource source)
		{
			return (DebugEventPolicy)DebugApi.GetBreakEventPolicy((int)source);
		}

		public static void SetPolicyForTier(string tier, DebugEventPolicy policy)
		{
			switch(tier.ToLowerInvariant()) {
				case "fatal":
					foreach(BreakSource src in FatalTier) SetPolicy(src, policy);
					break;
				case "suspicious":
					foreach(BreakSource src in SuspiciousTier) SetPolicy(src, policy);
					break;
				case "*":
				case "all":
					foreach(BreakSource src in FatalTier) SetPolicy(src, policy);
					foreach(BreakSource src in SuspiciousTier) SetPolicy(src, policy);
					break;
				default:
					throw new McpException("Unknown tier: " + tier + ". Use 'fatal', 'suspicious' or 'all'.");
			}
		}

		public static bool IsFatalTier(BreakSource source) => FatalTier.Contains(source);
		public static bool IsSuspiciousTier(BreakSource source) => SuspiciousTier.Contains(source);

		public static string GetPolicyListing()
		{
			StringBuilder sb = new();
			sb.AppendLine("Event policies (ignore = silent, report = record + notify, break = pause emulation):");
			sb.AppendLine("-- fatal tier --");
			foreach(BreakSource src in FatalTier) {
				sb.AppendLine("  " + src + ": " + GetPolicy(src));
			}
			sb.AppendLine("-- suspicious tier --");
			foreach(BreakSource src in SuspiciousTier) {
				sb.AppendLine("  " + src + ": " + GetPolicy(src));
			}
			return sb.ToString();
		}

		public static void MarkExpected(BreakSource source, uint pc, bool expected, string? note)
		{
			lock(_lock) {
				string key = (DebugEventKind.BreakEvent + "|" + (int)source + "|" + pc);
				if(expected) {
					_expectedSites[key] = note ?? "";
				} else {
					_expectedSites.Remove(key);
				}
				//Also remove any spike flag so the agent's decision sticks
				if(_groups.TryGetValue(key, out EventGroup? group)) {
					group.Spike = false;
					group.SpikeAnnounced = expected;
				}
			}
		}

		//--- memory write watch ---

		public static void ArmWriteWatch(uint start, uint end, MemoryType memType)
		{
			DebugApi.ArmMemoryWriteWatch(start, end, memType);
			_writeWatchArmed = true;
		}

		public static void DisarmWriteWatch()
		{
			DebugApi.DisarmMemoryWriteWatch();
			_writeWatchArmed = false;
		}

		private static bool IsWriteWatchArmedSafe()
		{
			return _writeWatchArmed;
		}

		public static string GetMemoryWritersReport(int count)
		{
			try {
				MemoryWriteRecord[] records = DebugApi.GetMemoryWriteLog((uint)Math.Min(count, 256));
				MemoryWriteReportDto report = new() { Armed = true };
				foreach(MemoryWriteRecord rec in records) {
					report.Writes.Add(new MemoryWriteDto {
						Frame = rec.FrameCount,
						Pc = FormatPc(rec.Pc),
						Address = FormatPc(rec.Address),
						Value = rec.Value,
						Cpu = ((CpuType)rec.CpuType).ToString()
					});
				}
				return McpToolHelper.Serialize(report);
			} catch(Exception ex) {
				throw new McpException("Failed to read memory write log: " + ex.Message);
			}
		}

		//--- bankswitch log ---

		public static string GetBankLogReport(int count, bool sinceLast)
		{
			try {
				ulong cursor = sinceLast ? _bankCursor : 0;
				ulong next;
				BankSwitchRecord[] records = DebugApi.GetBankSwitchLog(cursor, out next, (uint)Math.Min(count, 512));
				if(sinceLast) {
					lock(_lock) {
						_bankCursor = next;
					}
				}

				BankLogReportDto report = new() { Total = next };
				foreach(BankSwitchRecord rec in records) {
					report.Changes.Add(new BankSwitchDto {
						Sequence = rec.Sequence,
						Frame = rec.FrameCount,
						Pc = rec.Pc == 0 ? null : FormatPc(rec.Pc),
						Range = FormatPc(rec.StartAddr) + "-" + FormatPc(rec.EndAddr),
						OldOffset = rec.OldOffset,
						NewOffset = rec.NewOffset,
						IsChr = rec.IsChr != 0,
						MemType = rec.MemType
					});
				}
				return McpToolHelper.Serialize(report);
			} catch(Exception ex) {
				throw new McpException("Failed to read bankswitch log: " + ex.Message);
			}
		}
	}
}
