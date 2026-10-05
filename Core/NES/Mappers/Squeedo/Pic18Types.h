#pragma once
#include "pch.h"
#include "Shared/BaseState.h"
#include "Utilities/ISerializable.h"
#include "Utilities/Serializer.h"

// PIC18F4620 Special Function Register addresses in data memory space
namespace Pic18Sfr
{
	constexpr uint16_t TOSU = 0xFFF;
	constexpr uint16_t TOSH = 0xFFE;
	constexpr uint16_t TOSL = 0xFFD;
	constexpr uint16_t STKPTR = 0xFFC;
	constexpr uint16_t PCLATU = 0xFFB;
	constexpr uint16_t PCLATH = 0xFFA;
	constexpr uint16_t PCL = 0xFF9;
	constexpr uint16_t TBLPTRU = 0xFF8;
	constexpr uint16_t TBLPTRH = 0xFF7;
	constexpr uint16_t TBLPTRL = 0xFF6;
	constexpr uint16_t TABLAT = 0xFF5;
	constexpr uint16_t PRODH = 0xFF4;
	constexpr uint16_t PRODL = 0xFF3;
	constexpr uint16_t INTCON = 0xFF2;
	constexpr uint16_t INTCON2 = 0xFF1;
	constexpr uint16_t INTCON3 = 0xFF0;
	constexpr uint16_t INDF0 = 0xFEF;
	constexpr uint16_t POSTINC0 = 0xFEE;
	constexpr uint16_t POSTDEC0 = 0xFED;
	constexpr uint16_t PREINC0 = 0xFEC;
	constexpr uint16_t PLUSW0 = 0xFEB;
	constexpr uint16_t FSR0H = 0xFEA;
	constexpr uint16_t FSR0L = 0xFE9;
	constexpr uint16_t WREG = 0xFE8;
	constexpr uint16_t INDF1 = 0xFE7;
	constexpr uint16_t POSTINC1 = 0xFE6;
	constexpr uint16_t POSTDEC1 = 0xFE5;
	constexpr uint16_t PREINC1 = 0xFE4;
	constexpr uint16_t PLUSW1 = 0xFE3;
	constexpr uint16_t FSR1H = 0xFE2;
	constexpr uint16_t FSR1L = 0xFE1;
	constexpr uint16_t BSR = 0xFE0;
	constexpr uint16_t INDF2 = 0xFDF;
	constexpr uint16_t POSTINC2 = 0xFDE;
	constexpr uint16_t POSTDEC2 = 0xFDD;
	constexpr uint16_t PREINC2 = 0xFDC;
	constexpr uint16_t PLUSW2 = 0xFDB;
	constexpr uint16_t FSR2H = 0xFDA;
	constexpr uint16_t FSR2L = 0xFD9;
	constexpr uint16_t STATUS = 0xFD8;
	constexpr uint16_t TMR0H = 0xFD7;
	constexpr uint16_t TMR0L = 0xFD6;
	constexpr uint16_t T0CON = 0xFD5;
	constexpr uint16_t OSCCON = 0xFD3;
	constexpr uint16_t HLVDCON = 0xD2;
	constexpr uint16_t WDTCON = 0xFD1;
	constexpr uint16_t RCON = 0xFD0;
	constexpr uint16_t TMR1H = 0xFCF;
	constexpr uint16_t TMR1L = 0xFCE;
	constexpr uint16_t T1CON = 0xFCD;
	constexpr uint16_t TMR2 = 0xFCC;
	constexpr uint16_t PR2 = 0xFCB;
	constexpr uint16_t T2CON = 0xFCA;
	constexpr uint16_t SSPBUF = 0xFC9;
	constexpr uint16_t SSPADD = 0xFC8;
	constexpr uint16_t SSPSTAT = 0xFC7;
	constexpr uint16_t SSPCON1 = 0xFC6;
	constexpr uint16_t SSPCON2 = 0xFC5;
	constexpr uint16_t ADRESH = 0xFC4;
	constexpr uint16_t ADCON0 = 0xFC2;
	constexpr uint16_t ADCON1 = 0xFC1;
	constexpr uint16_t CCPR1H = 0xFBF;
	constexpr uint16_t CCPR1L = 0xFBE;
	constexpr uint16_t CCP1CON = 0xFBD;
	constexpr uint16_t CCPR2H = 0xFBC;
	constexpr uint16_t CCPR2L = 0xFBB;
	constexpr uint16_t CCP2CON = 0xFBA;
	constexpr uint16_t PWM1CON = 0xFB7;
	constexpr uint16_t ECCP1AS = 0xFB6;
	constexpr uint16_t CVRCON = 0xFB5;
	constexpr uint16_t CMCON = 0xFB4;
	constexpr uint16_t TMR3H = 0xFB3;
	constexpr uint16_t TMR3L = 0xFB2;
	constexpr uint16_t T3CON = 0xFB1;
	constexpr uint16_t SPBRG = 0xFAF;
	constexpr uint16_t RCREG = 0xFAE;
	constexpr uint16_t TXREG = 0xFAD;
	constexpr uint16_t TXSTA = 0xFAC;
	constexpr uint16_t RCSTA = 0xFAB;
	constexpr uint16_t EEADR = 0xFA9;
	constexpr uint16_t EEDATA = 0xFA8;
	constexpr uint16_t EECON2 = 0xFA7;
	constexpr uint16_t EECON1 = 0xFA6;
	constexpr uint16_t IPR2 = 0xFA2;
	constexpr uint16_t PIR2 = 0xFA1;
	constexpr uint16_t PIE2 = 0xFA0;
	constexpr uint16_t IPR1 = 0xF9F;
	constexpr uint16_t PIR1 = 0xF9E;
	constexpr uint16_t PIE1 = 0xF9D;
	constexpr uint16_t OSCTUNE = 0xF9B;
	constexpr uint16_t TRISE = 0xF96;
	constexpr uint16_t TRISD = 0xF95;
	constexpr uint16_t TRISC = 0xF94;
	constexpr uint16_t TRISB = 0xF93;
	constexpr uint16_t TRISA = 0xF92;
	constexpr uint16_t LATE = 0xF8D;
	constexpr uint16_t LATD = 0xF8C;
	constexpr uint16_t LATC = 0xF8B;
	constexpr uint16_t LATB = 0xF8A;
	constexpr uint16_t LATA = 0xF89;
	constexpr uint16_t PORTE = 0xF84;
	constexpr uint16_t PORTD = 0xF83;
	constexpr uint16_t PORTC = 0xF82;
	constexpr uint16_t PORTB = 0xF81;
	constexpr uint16_t PORTA = 0xF80;

	// Threshold: addresses >= SfrBase must go through ReadSfr/WriteSfr;
	// everything below is GPR and can be accessed as a plain Data[] read/write.
	constexpr uint16_t SfrBase = 0xF80;
}

// STATUS register bits
namespace Pic18StatusBits
{
	constexpr uint8_t C = 0x01;   // Carry
	constexpr uint8_t DC = 0x02;  // Digit Carry
	constexpr uint8_t Z = 0x04;   // Zero
	constexpr uint8_t OV = 0x08;  // Overflow
	constexpr uint8_t N = 0x10;   // Negative
}

// INTCON register bits (bit masks)
namespace Pic18IntconBits
{
	constexpr uint8_t RBIF = 0x01;     // bit 0
	constexpr uint8_t INT0IF = 0x02;   // bit 1
	constexpr uint8_t TMR0IF = 0x04;   // bit 2
	constexpr uint8_t RBIE = 0x08;     // bit 3
	constexpr uint8_t INT0IE = 0x10;   // bit 4
	constexpr uint8_t TMR0IE = 0x20;   // bit 5
	constexpr uint8_t PEIE = 0x40;     // bit 6
	constexpr uint8_t GIE = 0x80;      // bit 7

	// INTCON2 bits
	constexpr uint8_t RBIP = 0x01;     // bit 0
	constexpr uint8_t TMR0IP = 0x04;   // bit 2
	constexpr uint8_t INTEDG2 = 0x10;  // bit 4
	constexpr uint8_t INTEDG1 = 0x20;  // bit 5
	constexpr uint8_t INTEDG0 = 0x40;  // bit 6
	constexpr uint8_t NOT_RBPU = 0x80; // bit 7

	// INTCON3 bits
	constexpr uint8_t INT1IF = 0x01;   // bit 0
	constexpr uint8_t INT2IF = 0x02;   // bit 1
	constexpr uint8_t INT1IE = 0x08;   // bit 3
	constexpr uint8_t INT2IE = 0x10;   // bit 4
	constexpr uint8_t INT1IP = 0x40;   // bit 6
	constexpr uint8_t INT2IP = 0x80;   // bit 7
}

// RCON register bits (bit masks)
namespace Pic18RconBits
{
	constexpr uint8_t BOR = 0x01;    // bit 0
	constexpr uint8_t POR = 0x02;    // bit 1
	constexpr uint8_t PD = 0x04;     // bit 2
	constexpr uint8_t TO = 0x08;     // bit 3
	constexpr uint8_t RI = 0x10;     // bit 4
	constexpr uint8_t IPEN = 0x80;   // bit 7
}

// STKPTR bits
namespace Pic18StkptrBits
{
	constexpr uint8_t STKPTR_MASK = 0x1F;
	constexpr uint8_t STKFUL = 0x07;
	constexpr uint8_t STKUNF = 0x06;
}

// T0CON bits
namespace Pic18T0conBits
{
	constexpr uint8_t TMR0ON = 0x07;
	constexpr uint8_t T08BIT = 0x06;
	constexpr uint8_t T0CS = 0x05;
	constexpr uint8_t T0SE = 0x04;
	constexpr uint8_t PSA = 0x03;
	constexpr uint8_t T0PS_MASK = 0x07;  // bits 0-2
}

// T1CON bits
namespace Pic18T1conBits
{
	constexpr uint8_t TMR1ON = 0x00;
	constexpr uint8_t TMR1CS = 0x01;
	constexpr uint8_t T1SYNC = 0x02;
	constexpr uint8_t T1OSCEN = 0x03;
	constexpr uint8_t T1CKPS_MASK = 0x30;  // bits 4-5
}

// T3CON bits
namespace Pic18T3conBits
{
	constexpr uint8_t TMR3ON = 0x00;
	constexpr uint8_t TMR3CS = 0x01;
	constexpr uint8_t T3SYNC = 0x02;
	constexpr uint8_t T3CCP1 = 0x03;
	constexpr uint8_t T3CKPS_MASK = 0x30;  // bits 4-5
	constexpr uint8_t T3CCP2 = 0x06;
}

// PIE1/PIR1/IPR1 bits (bit masks)
namespace Pic18Pir1Bits
{
	constexpr uint8_t TMR1IF = 0x01; // bit 0
	constexpr uint8_t TMR2IF = 0x02; // bit 1
	constexpr uint8_t CCP1IF = 0x04; // bit 2
	constexpr uint8_t SSPIF = 0x08;  // bit 3
	constexpr uint8_t TXIF = 0x10;   // bit 4
	constexpr uint8_t RCIF = 0x20;   // bit 5
	constexpr uint8_t ADIF = 0x40;   // bit 6
	constexpr uint8_t PSPIF = 0x80;  // bit 7
}

// PIE2/PIR2/IPR2 bits (bit masks)
namespace Pic18Pir2Bits
{
	constexpr uint8_t CCP2IF = 0x01;  // bit 0
	constexpr uint8_t TMR3IF = 0x02;  // bit 1
	constexpr uint8_t HLVDIF = 0x04;  // bit 2
	constexpr uint8_t BCLIF = 0x08;   // bit 3
	constexpr uint8_t EEIF = 0x10;    // bit 4
	constexpr uint8_t USBIF = 0x20;   // bit 5
	constexpr uint8_t CMIF = 0x40;    // bit 6
	constexpr uint8_t OSCFIF = 0x80;  // bit 7
}

// RCSTA bits
namespace Pic18RcstaBits
{
	constexpr uint8_t RX9D = 0x00;
	constexpr uint8_t OERR = 0x01;
	constexpr uint8_t FERR = 0x02;
	constexpr uint8_t ADDEN = 0x03;
	constexpr uint8_t CREN = 0x04;
	constexpr uint8_t SREN = 0x05;
	constexpr uint8_t RX9 = 0x06;
	constexpr uint8_t SPEN = 0x07;
}

// TXSTA bits
namespace Pic18TxstaBits
{
	constexpr uint8_t TX9D = 0x00;
	constexpr uint8_t TRMT = 0x01;
	constexpr uint8_t BRGH = 0x02;
	constexpr uint8_t SENDB = 0x03;
	constexpr uint8_t SYNC = 0x04;
	constexpr uint8_t TXEN = 0x05;
	constexpr uint8_t TX9 = 0x06;
	constexpr uint8_t CSRC = 0x07;
}

struct Pic18CpuState : public BaseState
{
	// Core registers
	uint32_t PC = 0x800;          // Program counter (word address, reset vector = 0x800)
	uint8_t W = 0;                // Working register
	uint8_t BSR = 0;              // Bank Select Register
	uint8_t STATUS = 0;           // Status register
	uint8_t INTCON = 0;           // Interrupt control
	uint8_t STKPTR = 0;           // Stack pointer
	uint8_t _pad0 = 0;

	// Stack (31 entries, 21-bit each stored as 32-bit)
	uint32_t Stack[31] = {};

	// FSR registers (12-bit file select registers)
	uint16_t FSR[3] = {};

	// Table pointer and latch
	uint32_t TBLPTR = 0;          // 21-bit table pointer
	uint8_t TABLAT = 0;           // Table latch

	// Multiply result
	uint8_t PRODH = 0;
	uint8_t PRODL = 0;

	// PCL holding registers
	uint8_t PCLATU = 0;
	uint8_t PCLATH = 0;

	// Fast register stack: WREG/STATUS/BSR are auto-saved on interrupt entry
	// and restored by RETFIE with the fast-return bit (RETFIE f)
	uint8_t WREG_S = 0;
	uint8_t STATUS_S = 0;
	uint8_t BSR_S = 0;

	// RCON (interrupt priority enable)
	uint8_t RCON = 0;

	// Data memory: 4096 bytes (16 banks × 256 bytes)
	// Access Bank Low: 0x000-0x07F (GPR, Bank 0 first half)
	// Access Bank High: SFRs at 0xF60-0xFDF, GPR mirror at 0xF80-0xFFF
	// Normal addressing: BSR:addr → data[BSR*256 + (addr & 0xFF)]
	uint8_t Data[4096] = {};

	// Program memory: 2MB (1M words × 2 bytes)
	// Organized as bytes for table read/write compatibility
	uint8_t Program[0x200000] = {};

	// Timer latches (for reading TMRxH without race condition)
	uint8_t TMR0ReadLatch = 0;
	uint8_t TMR1ReadLatch = 0;
	uint8_t TMR3ReadLatch = 0;

	// PSP state
	uint8_t PspPortDOutput = 0;   // Data pre-loaded by PIC for NES reads
	bool PspIbf = false;          // Input Buffer Full (NES wrote data; cleared when PIC reads PORTD)
	bool PspObf = false;          // Output Buffer Full (PIC pre-loaded data; cleared when NES reads)
	bool PspIbov = false;         // Input Buffer Overflow (NES wrote while IBF still set; cleared in software)

	// Interrupt state
	bool InterruptPending = false;

	// Internal cycle counter for timing
	int64_t CycleCount = 0;
};