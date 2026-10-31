using Mesen.Config;
using Mesen.Interop;
using Mesen.Mcp.Consoles;
using Mesen.Mcp.Models;
using Mesen.Mcp.Tools;
using ModelContextProtocol;
using ModelContextProtocol.Protocol;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;

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

		private static readonly object _lock = new();
		private static bool _initialized;
		private static NotificationListener? _listener;

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
			foreach(BreakSource src in SuspiciousTier) {
				DebugApi.SetBreakEventPolicy((int)src, (int)DebugEventPolicy.Report);
			}
		}

		public static void OnRomLoaded()
		{
			lock(_lock) {
				try { DebugApi.ResetDebugEvents(); } catch { }
				_eventCursor = 0;
				_bankCursor = 0;
				_droppedRecords = 0;
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

		private static void OnNotification(NotificationEventArgs e)
		{
			lock(_lock) {
				switch(e.NotificationType) {
					case ConsoleNotificationType.CodeBreak: {
						try {
							BreakEvent evt = Marshal.PtrToStructure<BreakEvent>(e.Parameter);
							uint pc = DebugApi.GetProgramCounter(evt.SourceCpu, true);
							_breakActive = true;
							_breakReason = evt.Source + " at $" + pc.ToString("X4") + " (frame " + SafeGetFrame() + ")";
						} catch {
							_breakActive = true;
							_breakReason = "break";
						}
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
							if(group.NewCount == 1 && !expected) {
								AddAlert("[mesen] new event: " + group.Source + " at " + FormatPc(rec.Pc) + (string.IsNullOrEmpty(msg) ? "" : " (" + Truncate(msg, 80) + ")"));
							}
							if(group.Spike && !group.SpikeAnnounced && !expected) {
								group.SpikeAnnounced = true;
								AddAlert("[mesen] ⚠ rate spike: " + group.Source + " at " + FormatPc(rec.Pc) + " (" + group.RatePerFrame.ToString("0.##") + "/frame) - possible crash loop");
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

						report.Advice = "Inspect the crash PC, callstack and trace tail above. Use mesen_get_last_trace for more trace rows, mesen_get_state for full registers, mesen_read_memory to inspect RAM, and mesen_set_event_policy to tune which events break execution.";
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
