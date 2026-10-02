using Mesen.Interop;
using ModelContextProtocol;
using ModelContextProtocol.Server;
using System;
using System.Collections.Generic;
using System.ComponentModel;

namespace Mesen.Mcp.Tools
{
	[McpServerToolType]
	public class InputTools
	{
		[McpServerTool(Name = "mesen_input_override", ReadOnly = false, Destructive = false, OpenWorld = false),
		 Description("Override controller input for automated gameplay. Use 'list' to get available ports or 'set' to override button states.")]
		public static string InputOverride(
			[Description("Action: 'list' (get available ports) or 'set' (override input)")] string action,
			[Description("Controller port (0-7, for 'set' action)")] int port = 0,
			[Description("Comma-separated buttons for 'set': A,B,X,Y,L,R,Up,Down,Left,Right,Select,Start. Empty to release all.")] string buttons = "")
		{
			McpToolHelper.EnsureDebuggerReady();

			switch(action.ToLowerInvariant()) {
				case "list":
					List<int> indexes = DebugApi.GetAvailableInputOverrides();
					return "Ports: " + string.Join(", ", indexes);

				case "set":
					if(port < 0 || port > 7) {
						throw new McpException("Port must be 0-7.");
					}

					DebugControllerState state = new();
					string[] buttonList = buttons.Split(',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);
					foreach(string btn in buttonList) {
						switch(btn.ToLowerInvariant()) {
							case "a": state.A = true; break;
							case "b": state.B = true; break;
							case "x": state.X = true; break;
							case "y": state.Y = true; break;
							case "l": state.L = true; break;
							case "r": state.R = true; break;
							case "u": state.U = true; break;
							case "d": state.D = true; break;
							case "up": state.Up = true; break;
							case "down": state.Down = true; break;
							case "left": state.Left = true; break;
							case "right": state.Right = true; break;
							case "select": state.Select = true; break;
							case "start": state.Start = true; break;
						}
					}

					DebugApi.SetInputOverrides((UInt32)port, state);
					return "Port " + port + ": " + (string.IsNullOrEmpty(buttons) ? "(released)" : buttons);

				default:
					throw new McpException("Invalid action: " + action + ". Use 'list' or 'set'.");
			}
		}

	}
}
