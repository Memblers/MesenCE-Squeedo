#include "pch.h"
#include "NES/Debugger/pic18/Pic18DisUtils.h"
#include "NES/Mappers/Squeedo/Pic18Types.h"
#include "Debugger/DisassemblyInfo.h"
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
		if((opcode & 0xFFF0) == 0x0100) { out = "MOVLB $"; out += HexUtilities::ToHex((uint8_t)(opcode & 0x0F)); return; }
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
		// Byte-oriented: MULWF (0x02xx-0x03xx), DECF (0x04xx-0x07xx)
		// (ADDWFC is at 0x20xx, handled in case 0x2)
		if(opc8 >= 0x02 && opc8 <= 0x07) {
			if(opc8 <= 0x03) {
				out = "MULWF "; out += fmtAddr(f); return;
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
		const char* names[] = { "IORWF","ANDWF","XORWF","COMF" };
		fmtByte(names[(opcode >> 10) & 0x03]);
		return;
	}

	// ----------------------------------------------------------------
	// 0x2: ADDWF, INCF, DECFSZ (byte-oriented)
	// ----------------------------------------------------------------
	case 0x2: {
		const char* names[] = { "ADDWFC","ADDWF","INCF","DECFSZ" };
		fmtByte(names[(opcode >> 10) & 0x03]);
		return;
	}

	// ----------------------------------------------------------------
	// 0x3: RLCF, RRCF, SWAPF, INCFSZ (byte-oriented)
	// ----------------------------------------------------------------
	case 0x3: {
		const char* names[] = { "RRCF","RLCF","SWAPF","INCFSZ" };
		fmtByte(names[(opcode >> 10) & 0x03]);
		return;
	}

	// ----------------------------------------------------------------
	// 0x4: RLNCF, RRNCF, INFSNZ, DCFSNZ (byte-oriented)
	// ----------------------------------------------------------------
	case 0x4: {
		const char* names[] = { "RRNCF","RLNCF","INFSNZ","DCFSNZ" };
		fmtByte(names[(opcode >> 10) & 0x03]);
		return;
	}

	// ----------------------------------------------------------------
	// 0x5: MOVF, SUBFWB, SUBWFB (byte-oriented)
	// ----------------------------------------------------------------
	case 0x5: {
		const char* names[] = { "MOVF","SUBFWB","SUBWFB","SUBWF" };
		fmtByte(names[(opcode >> 10) & 0x03]);
		return;
	}

	// ----------------------------------------------------------------
	// 0x6: CPFSLT, CPFSEQ, CPFSGT, TSTFSZ, SETF, CLRF, NEGF, MOVWF
	// ----------------------------------------------------------------
	case 0x6: {
		const char* names[] = { "CPFSLT","CPFSEQ","CPFSGT","TSTFSZ","SETF","CLRF","NEGF","MOVWF" };
		uint8_t sub = (opcode >> 9) & 0x07;
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
		//rel is a signed word offset relative to the next instruction (PC+1 in
		//word addressing) - convert to a byte target: pc + 2 + rel*2
		uint32_t target = pc + 2 + (int32_t)rel * 2;
		if(sub == 0x1B) {
			out = "RCALL $"; out += HexUtilities::ToHex((uint16_t)target);
		} else {
			out = "BRA $"; out += HexUtilities::ToHex((uint16_t)target);
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
			const char* cond[] = { "BZ","BNZ","BC","BNC","BOV","BNOV","BN","BNN" };
			//rel is a signed word offset relative to the next instruction (PC+1 in
			//word addressing) - convert to a byte target: pc + 2 + rel*2
			uint32_t target = pc + 2 + (int32_t)rel * 2;
			out = cond[opc8 & 7]; out += " $"; out += HexUtilities::ToHex((uint16_t)target);
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
	// CPFSLT (0x60xx), CPFSEQ (0x62xx), CPFSGT (0x64xx), TSTFSZ (0x66xx)
	// These are skip-based — treat as conditional flow
	if((opcode & 0xFC00) == 0x2C00) return true; // DECFSZ
	if((opcode & 0xFC00) == 0x3C00) return true; // INCFSZ
	if((opcode & 0xFC00) == 0x4C00) return true; // DCFSNZ
	if((opcode & 0xFC00) == 0x4800) return true; // INFSNZ
	if((opcode & 0xFE00) == 0x6000) return true; // CPFSLT
	if((opcode & 0xFE00) == 0x6200) return true; // CPFSEQ
	if((opcode & 0xFE00) == 0x6400) return true; // CPFSGT
	if((opcode & 0xFE00) == 0x6600) return true; // TSTFSZ
	return false;
}

EffectiveAddressInfo Pic18DisUtils::GetEffectiveAddress(uint16_t opcode, uint8_t w, uint8_t bsr, const uint16_t* fsr)
{
	uint8_t hi = opcode >> 12;
	bool hasOperand;
	if(hi == 0x0) {
		//Only MULWF (0x02-0x03) and DECF (0x04-0x07) carry an f operand here
		//(0x00 = specials/MOVLB, 0x08-0x0F = literal ops)
		hasOperand = ((opcode >> 8) & 0xFF) >= 0x02 && ((opcode >> 8) & 0xFF) <= 0x07;
	} else {
		//Byte-oriented (0x1-0x6) and bit-oriented (0x7-0xB) instructions
		hasOperand = hi >= 0x1 && hi <= 0xB;
	}
	if(!hasOperand) {
		return {};
	}

	uint8_t f = opcode & 0xFF;
	//a=0 means access bank (matches Pic18Cpu::ResolveAddr's !a handling)
	uint16_t addr = ((opcode >> 8) & 1) ? (uint16_t)(((uint16_t)bsr << 8) | f) : (uint16_t)(f < 0x80 ? f : (0xF00 | f));

	//Indirect operands resolve through the FSRs
	uint16_t target = 0;
	switch(addr) {
		case Pic18Sfr::INDF0: case Pic18Sfr::POSTINC0: case Pic18Sfr::POSTDEC0: target = fsr[0] & 0xFFF; break;
		case Pic18Sfr::PREINC0: target = (uint16_t)((fsr[0] + 1) & 0xFFF); break;
		case Pic18Sfr::PLUSW0: target = (uint16_t)((fsr[0] + (int8_t)w) & 0xFFF); break;
		case Pic18Sfr::INDF1: case Pic18Sfr::POSTINC1: case Pic18Sfr::POSTDEC1: target = fsr[1] & 0xFFF; break;
		case Pic18Sfr::PREINC1: target = (uint16_t)((fsr[1] + 1) & 0xFFF); break;
		case Pic18Sfr::PLUSW1: target = (uint16_t)((fsr[1] + (int8_t)w) & 0xFFF); break;
		case Pic18Sfr::INDF2: case Pic18Sfr::POSTINC2: case Pic18Sfr::POSTDEC2: target = fsr[2] & 0xFFF; break;
		case Pic18Sfr::PREINC2: target = (uint16_t)((fsr[2] + 1) & 0xFFF); break;
		case Pic18Sfr::PLUSW2: target = (uint16_t)((fsr[2] + (int8_t)w) & 0xFFF); break;
		default: return {};
	}

	return EffectiveAddressInfo(target, 1, true, target >= Pic18Sfr::SfrBase ? MemoryType::Pic18SfrRam : MemoryType::Pic18DataRam);
}