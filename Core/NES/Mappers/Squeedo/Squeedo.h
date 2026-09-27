#pragma once
#include "pch.h"
#include "NES/BaseMapper.h"
#include "NES/Mappers/Squeedo/Pic18Cpu.h"
#include "NES/Mappers/Squeedo/Pic18Peripherals.h"
#include "NES/Mappers/Squeedo/Pic18Types.h"
#include "NES/Mappers/Squeedo/IntelHexParser.h"
#include "NES/Mappers/Homebrew/FlashSST39SF040.h"
#include "NES/NesCpu.h"
#include "NES/NesTypes.h"
#include "Shared/Emulator.h"
#include "Shared/BatteryManager.h"
#include "Shared/RomInfo.h"
#include "Shared/MessageManager.h"
#include "Utilities/VirtualFile.h"
#include "Utilities/HexUtilities.h"
#include "Utilities/FolderUtilities.h"

class Squeedo : public BaseMapper
{
private:
	// PIC18 coprocessor
	Pic18CpuState _picState = {};
	unique_ptr<Pic18Cpu> _picCpu;
	unique_ptr<Pic18Peripherals> _picPeripherals;

	// Flash ROM (SST39SF040, 512KB)
	unique_ptr<FlashSST39SF040> _flash;
	vector<uint8_t> _orgPrgRom;

	// PIC catch-up timing
	// PIC runs at 40MHz (10 MIPS), NES at ~1.789 MHz
	// Ratio: ~5.59 PIC cycles per NES cycle
	int _picCycleAccumulator = 0;
	static constexpr int PIC_CYCLE_NUM = 559;
	static constexpr int PIC_CYCLE_DEN = 100;

	// IRQ state
	bool _irqActive = false;

	// Cached GPIO state for detecting changes (init to impossible values so first ApplyGpioBanking always fires)
	uint8_t _lastPortA = 0xFF;  // PRG bank bits (RA0-RA3)
	uint8_t _lastPortB = 0xFF;  // SRAM bank bits (RB6-RB7)
	uint8_t _lastPortC = 0xFF;  // CHR bank bits (RC1-RC2)

protected:
	uint16_t GetPrgPageSize() override { return 0x4000; }  // 16KB pages (2 per 32KB bank)
	uint16_t GetChrPageSize() override { return 0x2000; }  // 8KB pages
	uint32_t GetSaveRamSize() override { return 0x8000; }  // 32KB (4 × 8KB pages)
	uint32_t GetSaveRamPageSize() override { return 0x2000; }
	uint32_t GetWorkRamSize() override { return 0; }
	uint32_t GetChrRamSize() override { return 0x8000; }   // 32KB (4 × 8KB pages)
	uint16_t RegisterStartAddress() override { return 0x5000; }
	uint16_t RegisterEndAddress() override { return 0x5FFF; }
	bool AllowRegisterRead() override { return true; }
	bool EnableCpuClockHook() override { return true; }
	uint32_t GetNametableCount() override { return 4; }  // 4-screen

	void InitMapper() override
	{
		// No-op — all initialization done in InitMapper(RomData&)
	}

	void InitMapper(RomData& romData) override
	{
		SetMirroringType(MirroringType::FourScreens);

		// Flash chip emulation
		_flash.reset(new FlashSST39SF040(_prgRom, _prgSize));
		_orgPrgRom = vector<uint8_t>(_prgRom, _prgRom + _prgSize);
		ApplySaveData();

		// Initialize PIC18 CPU
		_picCpu.reset(new Pic18Cpu(_picState));
		_picPeripherals.reset(new Pic18Peripherals(_picState, _console));
		_picCpu->SetPeripherals(_picPeripherals.get());
		_picPeripherals->SetCpu(_picCpu.get());

		// IRQ callback: PIC RA5 → NES /IRQ
		_picPeripherals->SetIrqCallback([this](bool active) {
			SetIrq(active);
		});

		// Load PIC firmware from companion .hex file
		LoadPicFirmware(romData);

		// Reset PIC
		_picCpu->Reset();

		// Register PIC18 memory types for the debugger
		// PIC18F4620 program memory is 64KB (32K words × 2 bytes).
		// Data memory is 4096 bytes (16 banks × 256).
		static constexpr uint32_t Pic18PrgSize = 0x10000;  // 64KB
		static constexpr uint32_t Pic18DataSize = 0x1000;  // 4096 bytes
		// Pic18Memory is the address space the disassembler uses (GetCpuMemoryType),
		// backed by the full program memory image. Pic18ProgramRom/Pic18DataRam are
		// the distinct memory types shown in the memory viewer.
		_emu->RegisterMemory(MemoryType::Pic18Memory, _picState.Program, Pic18PrgSize);
		_emu->RegisterMemory(MemoryType::Pic18ProgramRom, _picState.Program, Pic18PrgSize);
		_emu->RegisterMemory(MemoryType::Pic18DataRam, _picState.Data, Pic18DataSize);

		// Apply initial banking from PIC GPIO state (all zero after reset)
		ApplyGpioBanking();
	}

	void ApplySaveData()
	{
		if(_console->GetNesConfig().DisableFlashSaves) return;
		LoadRomPatch(_orgPrgRom);
	}

	void SaveBattery() override
	{
		if(_console->GetNesConfig().DisableFlashSaves) return;
		SaveRom(_orgPrgRom);
	}

	void LoadPicFirmware(RomData& romData)
	{
		// Try to find companion .hex file
		// 1. Same directory as the ROM
		// 2. Firmware folder
		string romPath = romData.Info.Filename;
		if(romPath.empty()) {
			romPath = _romInfo.Filename;
		}

		vector<string> searchPaths;

		// Try same directory as ROM
		if(!romPath.empty()) {
			size_t dotPos = romPath.rfind('.');
			if(dotPos != string::npos) {
				searchPaths.push_back(romPath.substr(0, dotPos) + ".hex");
			}
		}

		// Try firmware folder
		string firmwareFolder = FolderUtilities::GetFirmwareFolder();
		string romName = _romInfo.RomName;
		if(!romName.empty()) {
			searchPaths.push_back(FolderUtilities::CombinePath(firmwareFolder, romName + ".hex"));
		}

		for(string& hexPath : searchPaths) {
			VirtualFile hexFile(hexPath);
			if(hexFile.IsValid()) {
				vector<uint8_t> hexData;
				hexFile.ReadFile(hexData);
				if(!hexData.empty()) {
					bool ok = IntelHexParser::Parse(hexData, _picState.Program, sizeof(_picState.Program), 0);
					// Report the load result so the memory viewer can be correlated
					MessageManager::Log("[Squeedo] Loaded PIC firmware: " + hexPath + (ok ? "" : " (parse error)"));
					return;
				}
			}
		}
		MessageManager::Log("[Squeedo] PIC firmware (.hex) not found for " + _romInfo.RomName);
	}

	void SetIrq(bool active)
	{
		if(active && !_irqActive) {
			_console->GetCpu()->SetIrqSource(IRQSource::External);
		} else if(!active && _irqActive) {
			_console->GetCpu()->ClearIrqSource(IRQSource::External);
		}
		_irqActive = active;
	}

	void ApplyGpioBanking()
	{
		// PRG bank: PIC PortA bits 0-3 → SST39SF040 A15-A18
		// 4 bits → 16 × 32KB = 512KB
		// With 16KB pages, each 32KB bank = 2 consecutive pages
		uint8_t portA = _picState.Data[Pic18Sfr::PORTA & 0xFFF];
		if(portA != _lastPortA) {
			_lastPortA = portA;
			SelectPrgPage2x(0, (portA & 0x0F) * 2);
		}

		// SRAM bank: PIC PortB bits 6-7 → 62256 A13-A14
		// 2 bits → 4 × 8KB = 32KB
		uint8_t portB = _picState.Data[Pic18Sfr::PORTB & 0xFFF];
		if(portB != _lastPortB) {
			_lastPortB = portB;
			uint32_t offset = ((portB >> 6) & 0x03) * 0x2000;
			SetCpuMemoryMapping(0x6000, 0x7FFF, PrgMemoryType::SaveRam, offset, MemoryAccessType::ReadWrite);
		}

		// CHR bank: PIC PortC bits 1-2 → 62256 A13-A14 (gated for pattern tables)
		// 2 bits → 4 × 8KB = 32KB
		uint8_t portC = _picState.Data[Pic18Sfr::PORTC & 0xFFF];
		if(portC != _lastPortC) {
			_lastPortC = portC;
			SelectChrPage(0, ((portC >> 1) & 0x03), ChrMemoryType::ChrRam);
		}
	}

	void Reset(bool softReset) override
	{
		if(!softReset) {
			// Hard reset (power cycle): reset the PIC18
			_picCpu->Reset();
		}
		// Soft reset (NES reset button): PIC does NOT reset on real hardware
	}

	void OnAfterResetPowerOn() override
	{
		// Re-arm banking from the freshly-reset PIC GPIO state
		_lastPortA = _lastPortB = _lastPortC = 0xFF;
		ApplyGpioBanking();
	}

	void ProcessCpuClock() override
	{
		BaseProcessCpuClock();

		// Accumulate PIC cycles: ~5.59 PIC cycles per NES cycle
		_picCycleAccumulator += PIC_CYCLE_NUM;
		while(_picCycleAccumulator >= PIC_CYCLE_DEN) {
			_picCycleAccumulator -= PIC_CYCLE_DEN;

			_picPeripherals->ClockTimers(1);
			_emu->ProcessInstruction<CpuType::Pic18>();
			int picCycles = _picCpu->ExecuteInstruction();
			if(picCycles > 1) {
				_picPeripherals->ClockTimers(picCycles - 1);
			}
		}

		// Check if PIC GPIO changed → apply banking
		if(_picPeripherals->CheckAndClearGpioDirty()) {
			ApplyGpioBanking();
		}
	}

	// Transparent PSP bridge — the mapper does NOT interpret register meanings.
	// NES reads return whatever the PIC has pre-loaded on PortD.
	// NES writes are delivered to the PIC via PSP interrupt.
	uint8_t ReadRegister(uint16_t addr) override
	{
		return _picPeripherals->NesRead(addr & 0x1F);
	}

	void WriteRegister(uint16_t addr, uint8_t value) override
	{
		_picPeripherals->NesWrite(addr & 0x1F, value);
	}

	void Serialize(Serializer& s) override
	{
		BaseMapper::Serialize(s);

		SV(_picState.PC);
		SV(_picState.W);
		SV(_picState.BSR);
		SV(_picState.STATUS);
		SV(_picState.INTCON);
		SV(_picState.STKPTR);
		SV(_picState.RCON);
		SVArray(_picState.Stack, 31);
		SVArray(_picState.FSR, 3);
		SV(_picState.TBLPTR);
		SV(_picState.TABLAT);
		SV(_picState.PRODH);
		SV(_picState.PRODL);
		SV(_picState.PCLATU);
		SV(_picState.PCLATH);
		SV(_picState.CycleCount);
		SV(_picState.PspIbf);
		SV(_picState.PspObf);
		SV(_picState.PspPortDOutput);
		SV(_picState.InterruptPending);
		SVArray(_picState.Data, 4096);

		SV(_picPeripherals);
		SV(_irqActive);
		SV(_picCycleAccumulator);
		SV(_lastPortA);
		SV(_lastPortB);
		SV(_lastPortC);

		if(!s.IsSaving()) {
			ApplyGpioBanking();
		}
	}

	vector<MapperStateEntry> GetMapperStateEntries() override
	{
		vector<MapperStateEntry> entries;
		entries.push_back(MapperStateEntry("PRG", HexUtilities::ToHex((uint8_t)(_picState.Data[Pic18Sfr::PORTA & 0xFFF] & 0x0F))));
		entries.push_back(MapperStateEntry("SRAM", HexUtilities::ToHex((uint8_t)((_picState.Data[Pic18Sfr::PORTB & 0xFFF] >> 6) & 0x03))));
		entries.push_back(MapperStateEntry("CHR", HexUtilities::ToHex((uint8_t)((_picState.Data[Pic18Sfr::PORTC & 0xFFF] >> 1) & 0x03))));
		entries.push_back(MapperStateEntry("PIC PC", HexUtilities::ToHex((uint16_t)_picState.PC)));
		entries.push_back(MapperStateEntry("PIC W", HexUtilities::ToHex(_picState.W)));
		entries.push_back(MapperStateEntry("PIC S", HexUtilities::ToHex(_picState.STATUS)));
		entries.push_back(MapperStateEntry("PIC B", HexUtilities::ToHex(_picState.BSR)));
		return entries;
	}

public:
	vector<CpuType> GetSecondaryCpuTypes() override { return { CpuType::Pic18 }; }

	Pic18Cpu* GetPicCpu() { return _picCpu.get(); }
	Pic18CpuState& GetPicState() { return _picState; }
};