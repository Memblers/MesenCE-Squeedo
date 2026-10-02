using Mesen.Interop;
using ModelContextProtocol;
using ModelContextProtocol.Server;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Text;

namespace Mesen.Mcp.Tools
{
	[McpServerToolType]
	public class TextSearchTools
	{
		[McpServerTool(Name = "mesen_relative_search", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("Perform a relative search in memory. Finds text by matching the *differences* between consecutive bytes against the search string, regardless of encoding. This is the standard ROM hacking technique to discover unknown character encodings. Returns candidate addresses and the inferred base value for the first character.")]
		public static string RelativeSearch(
			[Description("Text string to search for (e.g. 'LINK' or 'DRAGON'). Use only letters from the same case (all uppercase or all lowercase) for best results.")] string searchText,
			[Description("Memory type to search in (call mesen_list_memory_types first to get valid values for the current ROM)")] string memoryType,
			[Description("Start address (default 0)")] string? startAddress = null,
			[Description("End address (default: end of memory)")] string? endAddress = null,
			[Description("Maximum results (default 50)")] int maxResults = 50)
		{
			McpToolHelper.EnsureDebuggerReady();

			if(searchText.Length < 3) {
				throw new McpException("Search text must be at least 3 characters for meaningful results");
			}

			MemoryType memType = McpToolHelper.ParseMemoryType(memoryType);

			Int32 memSize = DebugApi.GetMemorySize(memType);
			uint start = 0;
			uint end = (uint)(memSize - 1);
			if(startAddress != null) McpToolHelper.TryParseAddress(startAddress, out start);
			if(endAddress != null) McpToolHelper.TryParseAddress(endAddress, out end);
			end = Math.Min(end, (uint)(memSize - 1));

			// Compute difference signature
			int[] signature = new int[searchText.Length - 1];
			for(int i = 0; i < signature.Length; i++) {
				signature[i] = searchText[i + 1] - searchText[i];
			}

			byte[] memory = DebugApi.GetMemoryValues(memType, start, end);

			int matchCount = 0;
			Dictionary<string, string>? sampleTable = null;
			StringBuilder matchLines = new();

			for(int offset = 0; offset <= memory.Length - searchText.Length && matchCount < maxResults; offset++) {
				bool found = true;
				for(int i = 0; i < signature.Length; i++) {
					int diff = (int)memory[offset + i + 1] - (int)memory[offset + i];
					if(diff != signature[i]) {
						found = false;
						break;
					}
				}

				if(found) {
					byte firstByte = memory[offset];
					int baseOffset = firstByte - (int)searchText[0];

					// Build inferred table only for the first match
					if(sampleTable == null) {
						sampleTable = new();
						for(int i = 0; i < searchText.Length; i++) {
							sampleTable["$" + memory[offset + i].ToString("X2")] = searchText[i].ToString();
						}
					}

					matchLines.Append('$').Append((start + offset).ToString("X4"))
						.Append(" first=$").Append(firstByte.ToString("X2"))
						.Append(" offset=").AppendLine(baseOffset.ToString());
					matchCount++;
				}
			}

			string sig = string.Join(", ", signature.Select(d => (d >= 0 ? "+" : "") + d));
			StringBuilder result = new();
			result.Append("Search: '").Append(searchText).Append("'  Signature: ").Append(sig)
				.Append("  Matches: ").AppendLine(matchCount.ToString());
			result.Append(matchLines);

			if(sampleTable != null) {
				result.Append("Table: ");
				bool first = true;
				foreach(var kv in sampleTable) {
					if(!first) result.Append(' ');
					result.Append(kv.Key).Append('=').Append(kv.Value);
					first = false;
				}
				result.AppendLine();
			}

			string tip = matchCount > 20
				? "Too many results. Use a longer search string (6+ chars) or specify a narrower address range."
				: matchCount == 0
					? "No matches. Try: 1) different case (all UPPER or all lower), 2) the text may use DTE/MTE compression, 3) try a different memory type."
					: "";
			if(!string.IsNullOrEmpty(tip)) {
				result.Append(tip);
			}

			return result.ToString();
		}

		[McpServerTool(Name = "mesen_load_tbl", ReadOnly = false, Destructive = false, OpenWorld = false),
		 Description("Load a TBL (table) file that maps ROM byte values to characters. Standard ROM hacking format: each line is 'HH=C' where HH is hex byte(s) and C is the character. Supports multi-byte entries and DTE (one byte = multiple chars).")]
		public static string LoadTbl(
			[Description("Absolute path to the .tbl file, OR the raw TBL content as a string (one mapping per line, e.g. '00=A\\n01=B\\n...')")] string tblPathOrContent)
		{
			string tblContent;
			bool isFile = false;

			if(File.Exists(tblPathOrContent)) {
				tblContent = File.ReadAllText(tblPathOrContent);
				isFile = true;
			} else if(tblPathOrContent.Contains('=')) {
				tblContent = tblPathOrContent;
			} else {
				throw new McpException("File not found and content does not look like a TBL table: " + tblPathOrContent);
			}

			Dictionary<string, string> byteToChar;
			Dictionary<string, string> charToByte;
			int maxKeyLength;
			List<string> errors;

			ParseTbl(tblContent, out byteToChar, out charToByte, out maxKeyLength, out errors);

			if(byteToChar.Count == 0) {
				throw new McpException("No valid entries found in TBL");
			}

			// Atomic swap of immutable snapshot
			_tblState = new TblState(byteToChar, charToByte, maxKeyLength);

			string source = isFile ? tblPathOrContent : "(inline content)";
			string result = "TBL loaded: " + source + " (" + byteToChar.Count + " entries, max key " + maxKeyLength + " bytes)";
			if(errors.Count > 0) {
				result += "\nWarnings:\n" + string.Join("\n", errors);
			}
			return result;
		}

		[McpServerTool(Name = "mesen_search_text", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("Search memory for a text string using the currently loaded TBL (character table). The TBL must be loaded first with mesen_load_tbl. Encodes the search text using the TBL mapping and searches for the resulting byte pattern.")]
		public static string SearchText(
			[Description("Text to search for (will be encoded using the loaded TBL)")] string text,
			[Description("Memory type to search in (call mesen_list_memory_types first to get valid values for the current ROM)")] string memoryType,
			[Description("Start address (default 0)")] string? startAddress = null,
			[Description("End address (default: end of memory)")] string? endAddress = null,
			[Description("Maximum results (default 50)")] int maxResults = 50)
		{
			McpToolHelper.EnsureDebuggerReady();

			TblState? tbl = _tblState;
			if(tbl == null || tbl.CharToByte.Count == 0) {
				throw new McpException("No TBL loaded. Call mesen_load_tbl first.");
			}

			MemoryType memType = McpToolHelper.ParseMemoryType(memoryType);

			// Encode the search text to bytes using the TBL
			List<byte> encodedBytes = new();
			List<string> unmapped = new();
			int i = 0;
			while(i < text.Length) {
				bool mapped = false;
				// Try longest match first (for multi-char -> single-byte DTE entries)
				for(int len = Math.Min(text.Length - i, 10); len >= 1; len--) {
					string substr = text.Substring(i, len);
					if(tbl.CharToByte.TryGetValue(substr, out string? hexBytes)) {
						// Parse the hex bytes
						for(int b = 0; b < hexBytes.Length; b += 2) {
							encodedBytes.Add(byte.Parse(hexBytes.Substring(b, 2), System.Globalization.NumberStyles.HexNumber));
						}
						i += len;
						mapped = true;
						break;
					}
				}
				if(!mapped) {
					unmapped.Add(text[i].ToString());
					i++;
				}
			}

			if(encodedBytes.Count == 0) {
				throw new McpException("Could not encode any characters. Check your TBL.");
			}

			if(unmapped.Count > 0) {
				throw new McpException("Some characters could not be mapped with the current TBL: " + string.Join(", ", unmapped.Distinct()));
			}

			// Now search for the byte pattern in memory
			Int32 memSize = DebugApi.GetMemorySize(memType);
			uint start = 0;
			uint end = (uint)(memSize - 1);
			if(startAddress != null) McpToolHelper.TryParseAddress(startAddress, out start);
			if(endAddress != null) McpToolHelper.TryParseAddress(endAddress, out end);
			end = Math.Min(end, (uint)(memSize - 1));

			byte[] memory = DebugApi.GetMemoryValues(memType, start, end);
			byte[] pattern = encodedBytes.ToArray();

			List<string> matchAddresses = new();
			for(int offset = 0; offset <= memory.Length - pattern.Length && matchAddresses.Count < maxResults; offset++) {
				bool found = true;
				for(int j = 0; j < pattern.Length; j++) {
					if(memory[offset + j] != pattern[j]) {
						found = false;
						break;
					}
				}
				if(found) {
					matchAddresses.Add("$" + (start + offset).ToString("X4"));
				}
			}

			string encodedPattern = string.Join(" ", pattern.Select(b => b.ToString("X2")));
			if(matchAddresses.Count == 0) {
				return "'" + text + "' encoded as " + encodedPattern + ". No matches.";
			}
			return "'" + text + "' encoded as " + encodedPattern + ". " + matchAddresses.Count + " matches: " + string.Join(" ", matchAddresses);
		}

		[McpServerTool(Name = "mesen_decode_text", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("Decode a region of memory as text using the currently loaded TBL. Reads bytes and converts them to characters using the table mapping. Useful to read game text once you have the correct TBL.")]
		public static string DecodeText(
			[Description("Start address (decimal or 0x/$ hex)")] string address,
			[Description("Number of bytes to decode (max 4096)")] int length,
			[Description("Memory type (call mesen_list_memory_types first to get valid values for the current ROM)")] string memoryType,
			[Description("Optional hex byte value that marks end-of-string (e.g. 'FF' or '00'). Decoding stops at this byte.")] string? endMarker = null)
		{
			McpToolHelper.EnsureDebuggerReady();

			TblState? tbl = _tblState;
			if(tbl == null || tbl.ByteToChar.Count == 0) {
				throw new McpException("No TBL loaded. Call mesen_load_tbl first.");
			}

			uint addr = McpToolHelper.ParseAddress(address);
			MemoryType memType = McpToolHelper.ParseMemoryType(memoryType);

			length = Math.Min(length, 4096);
			Int32 memSize = DebugApi.GetMemorySize(memType);
			if(addr >= memSize) {
				throw new McpException("Address out of range");
			}
			length = (int)Math.Min(length, memSize - addr);

			byte? endByte = null;
			if(endMarker != null) {
				if(!McpToolHelper.TryParseValue(endMarker, out byte eb)) {
					throw new McpException("Invalid endMarker value: " + endMarker + ". Use hex (e.g. 'FF', '0x00', '$00') or decimal.");
				}
				endByte = eb;
			}

			byte[] data = DebugApi.GetMemoryValues(memType, addr, (uint)(addr + length - 1));

			StringBuilder decoded = new();
			int bytesConsumed = 0;

			int pos = 0;
			while(pos < data.Length) {
				if(endByte.HasValue && data[pos] == endByte.Value) {
					decoded.Append("<END>");
					bytesConsumed = pos + 1;
					break;
				}

				bool mapped = false;
				// Try longest byte sequence first
				for(int keyLen = tbl.MaxKeyLength; keyLen >= 1; keyLen--) {
					if(pos + keyLen > data.Length) continue;

					StringBuilder keyBuilder = new();
					for(int k = 0; k < keyLen; k++) {
						keyBuilder.Append(data[pos + k].ToString("X2"));
					}
					string key = keyBuilder.ToString().ToUpperInvariant();

					if(tbl.ByteToChar.TryGetValue(key, out string? character)) {
						decoded.Append(character);
						pos += keyLen;
						mapped = true;
						break;
					}
				}

				if(!mapped) {
					decoded.Append($"[${data[pos]:X2}]");
					pos++;
				}

				bytesConsumed = pos;
			}

			return bytesConsumed + " bytes: " + decoded;
		}

		[McpServerTool(Name = "mesen_get_tbl_info", ReadOnly = true, Destructive = false, Idempotent = true, OpenWorld = false),
		 Description("Get info about the currently loaded TBL (character table). Shows all mappings.")]
		public static string GetTblInfo()
		{
			TblState? tbl = _tblState;
			if(tbl == null || tbl.ByteToChar.Count == 0) {
				throw new McpException("No TBL loaded");
			}

			string mappings = string.Join(",", tbl.ByteToChar.Select(kv => kv.Key + "=" + kv.Value));
			return "TBL: " + tbl.ByteToChar.Count + " entries (max key " + tbl.MaxKeyLength + " bytes)\n" + mappings;
		}

		// --- TBL state (immutable snapshot swapped atomically) ---
		private sealed class TblState
		{
			public readonly Dictionary<string, string> ByteToChar;
			public readonly Dictionary<string, string> CharToByte;
			public readonly int MaxKeyLength;

			public TblState(Dictionary<string, string> byteToChar, Dictionary<string, string> charToByte, int maxKeyLength)
			{
				ByteToChar = byteToChar;
				CharToByte = charToByte;
				MaxKeyLength = maxKeyLength;
			}
		}

		private static volatile TblState? _tblState;

		private static void ParseTbl(string content, out Dictionary<string, string> byteToChar, out Dictionary<string, string> charToByte, out int maxKeyLength, out List<string> errors)
		{
			byteToChar = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
			charToByte = new Dictionary<string, string>();
			errors = new List<string>();
			maxKeyLength = 1;

			string[] lines = content.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries);
			foreach(string rawLine in lines) {
				string line = rawLine.Trim();

				// Skip comments and empty lines
				if(line.Length == 0 || line.StartsWith("//") || line.StartsWith("#") || line.StartsWith(";")) {
					continue;
				}

				// Handle special prefixes used by some tools (/, *, $)
				if(line.Length > 0 && (line[0] == '/' || line[0] == '*' || line[0] == '$')) {
					line = line.Substring(1);
				}

				int eqIdx = line.IndexOf('=');
				if(eqIdx < 1) {
					errors.Add("Skipped invalid line: " + rawLine);
					continue;
				}

				string hexPart = line.Substring(0, eqIdx).Trim().Replace(" ", "").ToUpperInvariant();
				string charPart = line.Substring(eqIdx + 1);

				// Validate hex
				if(hexPart.Length == 0 || hexPart.Length % 2 != 0) {
					errors.Add("Invalid hex in line: " + rawLine);
					continue;
				}

				bool validHex = true;
				for(int i = 0; i < hexPart.Length; i++) {
					if(!Uri.IsHexDigit(hexPart[i])) {
						validHex = false;
						break;
					}
				}
				if(!validHex) {
					errors.Add("Invalid hex in line: " + rawLine);
					continue;
				}

				int keyByteLen = hexPart.Length / 2;
				if(keyByteLen > maxKeyLength) {
					maxKeyLength = keyByteLen;
				}

				byteToChar[hexPart] = charPart;
				if(charPart.Length > 0 && !charToByte.ContainsKey(charPart)) {
					charToByte[charPart] = hexPart;
				}
			}
		}
	}
}
