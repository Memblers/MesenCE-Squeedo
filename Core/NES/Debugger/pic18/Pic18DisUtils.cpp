#include "pch.h"
#include "NES/Debugger/pic18/Pic18DisUtils.h"
#include "NES/Mappers/Squeedo/Pic18Types.h"
#include "Utilities/HexUtilities.h"
#include "Debugger/LabelManager.h"
#include "Shared/MemoryType.h"

int Pic18DisUtils::GetInstructionSize(uint16_t opcode)
{
	if((opcode & 0xF000) == 0xC000) return 4;  // MOVFF (1100 ffff ffff ffff)
	if((opcode & 0xFF00) == 0xEE00) return 4;  // LFSR
	if((opcode & 0xFE00) == 0xEC00) return 4;  // CALL
	if((opcode & 0xFF00) == 0xEF00) return 4;  // GOTO
	return 2;
}

void Pic18DisUtils::GetDisassembly(string& out, uint16_t opcode, uint32_t pc, uint8_t* byteCode, LabelManager* labelManager)
{
	uint8_t f = opcode & 0xFF;
	uint8_t opc8 = (opcode >> 8) & 0xFF;

	// Helper to read 2nd word of a 4-byte instruction (little-endian)
	auto getSecondWord = [&]() -> uint16_t {
		if(byteCode) return ((uint16_t)byteCode[3] << 8) | byteCode[2];
		return 0;
	};

	// PIC18F4620 SFR name lookup (access bank: f >= 0x80 → SFR at 0xF00|f)
	static const std::unordered_map<uint16_t, const char*>& sfrNames = Pic18SfrNameMap();
	auto fmtAddr = [&](uint8_t reg) -> string {
		uint16_t fullAddr = reg < 0x80 ? reg : (uint16_t)(0xF00 | reg);
		auto it = sfrNames.find(fullAddr);
		if(it != sfrNames.end()) return it->second;
		// Also try label manager for user-defined labels
		if(labelManager) {
			AddressInfo addrInfo = { (int32_t)fullAddr, MemoryType::Pic18DataRam };
			string label = labelManager->GetLabel(addrInfo);
			if(!label.empty()) return label;
		}
		return "$" + HexUtilities::ToHex(reg);
	};

	// Resolve a 12-bit address (for MOVFF src/dst)
	auto fmtAddr12 = [&](uint16_t addr) -> string {
		auto it = sfrNames.find(addr);
		if(it != sfrNames.end()) return it->second;
		if(labelManager) {
			AddressInfo addrInfo = { (int32_t)addr, MemoryType::Pic18DataRam };
			string label = labelManager->GetLabel(addrInfo);
			if(!label.empty()) return label;
		}
		return "$" + HexUtilities::ToHex(addr);
	};

	auto fmtByte = [&](const char* mn) {
		bool d = (opcode >> 9) & 1;  // bit 9 = destination (0=W, 1=f)
		out = mn;
		out += " ";
		out += fmtAddr(f);
		out += d ? ",f" : ",w";
	};

	auto fmtLit = [&](const char* mn) {
		out = mn;
		out += " $";
		out += HexUtilities::ToHex(f);
	};

	auto fmtBit = [&](const char* mn) {
		uint8_t b = (opcode >> 9) & 0x07;
		out = mn;
		out += " ";
		out += fmtAddr(f);
		out += ",";
		out += std::to_string(b);
	};

	switch(opcode >> 12) {

	// ----------------------------------------------------------------
	// 0x0: Specials, MOVFF, literal ops, ADDWFC/MULWF/DECF
	// ----------------------------------------------------------------
	case 0x0: {
		if(opcode == 0x0000) { out = "NOP"; return; }
		if(opcode == 0x0003) { out = "SLEEP"; return; }
		if(opcode == 0x0004) { out = "CLRWDT"; return; }
		if(opcode == 0x0005) { out = "PUSH"; return; }
		if(opcode == 0x0006) { out = "POP"; return; }
		if(opcode == 0x0007) { out = "DAW"; return; }
		if(opcode == 0x00FF) { out = "RESET"; return; }
		if((opcode & 0xFFFE) == 0x0010) { out = "RETFIE"; return; }
		if((opcode & 0xFFFE) == 0x0012) { out = "RETURN"; return; }
		if((opcode & 0xFFF8) == 0x0008) {
			const char* tbl[] = { "TBLRD*","TBLRD*+","TBLRD*-","TBLRD+*","TBLWT*","TBLWT*+","TBLWT*-","TBLWT+*" };
			out = tbl[opcode & 7]; return;
		}
		if((opcode & 0xFFF0) == 0x0010) { out = "MOVLB $"; out += HexUtilities::ToHex((uint8_t)(opcode & 0x0F)); return; }
		// Literal ops (0x08xx-0x0Fxx)
		switch(opc8) {
		case 0x0E: fmtLit("MOVLW"); return;
		case 0x0F: fmtLit("ADDLW"); return;
		case 0x08: fmtLit("SUBLW"); return;
		case 0x09: fmtLit("IORLW"); return;
		case 0x0B: fmtLit("ANDLW"); return;
		case 0x0A: fmtLit("XORLW"); return;
		case 0x0C: fmtLit("RETLW"); return;
		case 0x0D: fmtLit("MULLW"); return;
		}
		// Byte-oriented: ADDWFC (0x00xx-0x01xx), MULWF (0x02xx-0x03xx), DECF (0x04xx-0x07xx)
		if(opc8 <= 0x07) {
			if(opc8 <= 0x03 && ((opcode >> 9) & 1)) {
			out = "MULWF "; out += fmtAddr(f); return;
			} else if(opc8 <= 0x03) {
				fmtByte("ADDWFC"); return;
			} else {
				fmtByte("DECF"); return;
			}
		}
		out = "???"; return;
	}

	// ----------------------------------------------------------------
	// 0x1: IORWF, ANDWF, XORWF, COMF (byte-oriented)
	// ----------------------------------------------------------------
	case 0x1: {
		const char* names[] = { "IORWF","ADDWF","ANDWF","COMF" };
		fmtByte(names[(opcode >> 10) & 0x03]);
		return;
	}

	// ----------------------------------------------------------------
	// 0x2: ADDWF, INCF, DECFSZ (byte-oriented)
	// ----------------------------------------------------------------
	case 0x2: {
		const char* names[] = { "NOP","ADDWF","INCF","DECFSZ" };
		uint8_t sub = (opcode >> 10) & 0x03;
		if(sub == 0) { out = "???"; return; }
		fmtByte(names[sub]);
		return;
	}

	// ----------------------------------------------------------------
	// 0x3: RLCF, RRCF, SWAPF, INCFSZ (byte-oriented)
	// ----------------------------------------------------------------
	case 0x3: {
		const char* names[] = { "RLCF","RRCF","SWAPF","INCFSZ" };
		fmtByte(names[(opcode >> 10) & 0x03]);
		return;
	}

	// ----------------------------------------------------------------
	// 0x4: RLNCF, RRNCF, INFSNZ, DCFSNZ (byte-oriented)
	// ----------------------------------------------------------------
	case 0x4: {
		const char* names[] = { "RLNCF","RRNCF","INFSNZ","DCFSNZ" };
		fmtByte(names[(opcode >> 10) & 0x03]);
		return;
	}

	// ----------------------------------------------------------------
	// 0x5: MOVF, SUBFWB, SUBWFB (byte-oriented)
	// ----------------------------------------------------------------
	case 0x5: {
		const char* names[] = { "MOVF","SUBFWB","SUBWFB","???" };
		uint8_t sub = (opcode >> 10) & 0x03;
		if(sub == 3) { out = "???"; return; }
		fmtByte(names[sub]);
		return;
	}

	// ----------------------------------------------------------------
	// 0x6: SETF, CPFSEQ, TSTFSZ, CPFSGT, CPFSLT, CLRF, NEGF, MOVWF
	// ----------------------------------------------------------------
	case 0x6: {
		const char* names[] = { "SETF","CPFSEQ","TSTFSZ","CPFSGT","CPFSLT","CLRF","NEGF","MOVWF" };
		uint8_t sub = (opcode >> 9) & 0x07;
		if(sub == 0 || sub == 5) {
			// SETF, CLRF: no d bit, just file,a
			out = names[sub];
			out += " ";
			out += fmtAddr(f);
			return;
		}
		// Others also no d bit (CPFSEQ, TSTFSZ, CPFSGT, CPFSLT, NEGF, MOVWF)
		out = names[sub];
		out += " ";
		out += fmtAddr(f);
		return;
	}

	// ----------------------------------------------------------------
	// 0x7: BTG (bit-oriented)
	// ----------------------------------------------------------------
	case 0x7: {
		fmtBit("BTG");
		return;
	}

	// ----------------------------------------------------------------
	// 0x8: BSF (bit-oriented)
	// ----------------------------------------------------------------
	case 0x8: {
		fmtBit("BSF");
		return;
	}

	// ----------------------------------------------------------------
	// 0x9: BCF (bit-oriented)
	// ----------------------------------------------------------------
	case 0x9: {
		fmtBit("BCF");
		return;
	}

	// ----------------------------------------------------------------
	// 0xA: BTFSS (bit-oriented)
	// ----------------------------------------------------------------
	case 0xA: {
		fmtBit("BTFSS");
		return;
	}

	// ----------------------------------------------------------------
	// 0xB: BTFSC (bit-oriented)
	// ----------------------------------------------------------------
	case 0xB: {
		fmtBit("BTFSC");
		return;
	}

	// ----------------------------------------------------------------
	// 0xC: MOVFF (1100 ffff ffff ffff / 1111 ffff ffff ffff)
	// ----------------------------------------------------------------
	case 0xC: {
		uint16_t src = opcode & 0xFFF;
		uint16_t dst = getSecondWord() & 0xFFF;
		out = "MOVFF ";
		out += fmtAddr12(src);
		out += ",";
		out += fmtAddr12(dst);
		return;
	}

	// ----------------------------------------------------------------
	// 0xD: BRA / RCALL
	// ----------------------------------------------------------------
	case 0xD: {
		int16_t rel = opcode & 0x7FF;
		if(rel & 0x400) rel |= ~0x7FF;
		uint8_t sub = (opcode >> 11) & 0x1F;
		if(sub == 0x1B) {
			out = "RCALL $"; out += HexUtilities::ToHex((uint16_t)(pc + rel));
		} else {
			out = "BRA $"; out += HexUtilities::ToHex((uint16_t)(pc + rel));
		}
		return;
	}

	// ----------------------------------------------------------------
	// 0xE: CALL, GOTO, LFSR, conditional branches
	// ----------------------------------------------------------------
	case 0xE: {
		if((opc8 & 0xFE) == 0xEC) {
			uint16_t w2 = getSecondWord();
			uint32_t target = (((uint32_t)(w2 & 0xFFF) << 8) | (opcode & 0xFF)) * 2;
			out = "CALL $"; out += HexUtilities::ToHex((uint16_t)target);
			return;
		}
		if(opc8 == 0xEF) {
			uint16_t w2 = getSecondWord();
			uint32_t target = (((uint32_t)(w2 & 0xFFF) << 8) | (opcode & 0xFF)) * 2;
			out = "GOTO $"; out += HexUtilities::ToHex((uint16_t)target);
			return;
		}
		if(opc8 == 0xEE) {
			uint8_t fsr = (opcode >> 4) & 0x03;
			uint16_t lit = ((opcode & 0x0F) << 8) | (getSecondWord() & 0xFF);
			out = "LFSR "; out += std::to_string(fsr);
			out += ",$"; out += HexUtilities::ToHex(lit);
			return;
		}
		if((opc8 & 0xF8) == 0xE0) {
			int8_t rel = (int8_t)(opcode & 0xFF);
			const char* cond[] = { "BC","BN","BOV","BZ","BNC","BNN","BNOV","BNZ" };
			out = cond[opc8 & 7]; out += " $"; out += HexUtilities::ToHex((uint16_t)(pc + rel));
			return;
		}
		out = "???"; return;
	}

	// ----------------------------------------------------------------
	// 0xF: Literal operations (0xFxxx encoding)
	// ----------------------------------------------------------------
	case 0xF: {
		if(opcode == 0xF000) { out = "NOP"; return; }
		switch(opc8) {
		case 0xFE: fmtLit("MOVLW"); return;
		case 0xFF: fmtLit("ADDLW"); return;
		case 0xF8: fmtLit("SUBLW"); return;
		case 0xF9: fmtLit("IORLW"); return;
		case 0xFB: fmtLit("ANDLW"); return;
		case 0xFA: fmtLit("XORLW"); return;
		case 0xFC: fmtLit("RETLW"); return;
		case 0xFD: fmtLit("MULLW"); return;
		}
		out = "???"; return;
	}

	}
	out = "???";
}

bool Pic18DisUtils::IsJumpToSub(uint16_t opcode)
{
	// CALL: 1110 110s kkkk kkkk (4-byte, opc8 0xEC or 0xED)
	if((opcode & 0xFE00) == 0xEC00) return true;
	// RCALL: 1101 1xxx xxxx xxxx (subop == 0x1B)
	if((opcode >> 11) == 0x1B) return true;
	return false;
}

bool Pic18DisUtils::IsReturnInstruction(uint16_t opcode)
{
	// RETURN: 0000 0000 0001 001x
	if((opcode & 0xFFFE) == 0x0012) return true;
	// RETFIE: 0000 0000 0001 000x
	if((opcode & 0xFFFE) == 0x0010) return true;
	// RETLW: 0000 1100 kkkk kkkk (0x0xxx encoding)
	if((opcode & 0xFF00) == 0x0C00) return true;
	// RETLW: 1111 1100 kkkk kkkk (0xFxxx encoding)
	if((opcode & 0xFF00) == 0xFC00) return true;
	return false;
}

bool Pic18DisUtils::IsUnconditionalJump(uint16_t opcode)
{
	// GOTO: 1110 1111 xxxx xxxx (4-byte)
	if((opcode & 0xFF00) == 0xEF00) return true;
	if((opcode >> 11) == 0x1A) return true;  // BRA: 1101 0xxx xxxx xxxx
	if((opcode >> 11) == 0x1B) return true;  // RCALL: 1101 1xxx xxxx xxxx
	return false;
}

bool Pic18DisUtils::IsConditionalJump(uint16_t opcode)
{
	// Conditional branches: 1110 000n nkkk kkkk (BC, BN, BOV, BZ, BNC, BNN, BNOV, BNZ)
	if((opcode & 0xF800) == 0xE000) return true;
	// BTFSC: 1011 bbba ffff ffff
	if((opcode & 0xF000) == 0xB000) return true;
	// BTFSS: 1010 bbba ffff ffff
	if((opcode & 0xF000) == 0xA000) return true;
	// Skip instructions (byte-oriented, skip next if condition):
	// DECFSZ (0x2Cxx), INCFSZ (0x3Cxx), DCFSNZ (0x4Cxx), INFSNZ (0x48xx)
	// CPFSEQ (0x62xx), CPFSGT (0x66xx), CPFSLT (0x68xx), TSTFSZ (0x64xx)
	// These are skip-based — treat as conditional flow
	if((opcode & 0xFC00) == 0x2C00) return true; // DECFSZ
	if((opcode & 0xFC00) == 0x3C00) return true; // INCFSZ
	if((opcode & 0xFC00) == 0x4C00) return true; // DCFSNZ
	if((opcode & 0xFC00) == 0x4800) return true; // INFSNZ
	if((opcode & 0xFE00) == 0x6200) return true; // CPFSEQ
	if((opcode & 0xFE00) == 0x6600) return true; // CPFSGT
	if((opcode & 0xFE00) == 0x6800) return true; // CPFSLT
	if((opcode & 0xFE00) == 0x6400) return true; // TSTFSZ
	return false;
}