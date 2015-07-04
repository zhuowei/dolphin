// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"

#include "Core/Boot/Boot.h"
#include "Core/Boot/ElfReader.h"
#include "Core/Debugger/Debugger_SymbolMap.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/HLE_WiiU_CoreInit.h"
#include "Core/HW/Memmap.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/PowerPC/PPCSymbolDB.h"

bool CBoot::IsElfWii(const std::string& filename)
{
	/* We already check if filename existed before we called this function, so
	   there is no need for another check, just read the file right away */

	const u64 filesize = File::GetSize(filename);
	std::vector<u8> mem((size_t)filesize);

	{
	File::IOFile f(filename, "rb");
	f.ReadBytes(mem.data(), (size_t)filesize);
	}

	// Use the same method as the DOL loader uses: search for mfspr from HID4,
	// which should only be used in Wii ELFs.
	//
	// Likely to have some false positives/negatives, patches implementing a
	// better heuristic are welcome.

	u32 HID4_pattern = 0x7c13fba6;
	u32 HID4_mask = 0xfc1fffff;
	ElfReader reader(mem.data());

	// WiiU is not a Wii
	if (reader.is_rpx)
	{
		return false;
	}
	else
	{
		for (int i = 0; i < reader.GetNumSections(); ++i)
		{
			if (reader.IsCodeSection(i))
			{
				for (unsigned int j = 0; j < reader.GetSectionSize(i) / sizeof (u32); ++j)
				{
					u32 word = Common::swap32(((u32*)reader.GetSectionDataPtr(i))[j]);
					if ((word & HID4_mask) == HID4_pattern)
					{
						return true;
					}
				}
			}
		}
	}

	return false;
}

// Thanks to Tom
// returns 1 iff str ends with suffix 
int str_ends_with(const char * str, const char * suffix)
{
	if (str == NULL || suffix == NULL)
		return 0;

	size_t str_len = strlen(str);
	size_t suffix_len = strlen(suffix);

	if (suffix_len > str_len)
		return 0;

	return 0 == strncmp(str + str_len - suffix_len, suffix, suffix_len);
}

bool CBoot::IsElfWiiU(const std::string& filename)
{
	/* We already check if filename existed before we called this function, so
	there is no need for another check, just read the file right away */

	const u64 filesize = File::GetSize(filename);
	u8 *const mem = new u8[(size_t)filesize];

	{
		File::IOFile f(filename, "rb");
		f.ReadBytes(mem, (size_t)filesize);
	}

	ElfReader reader(mem);
	bool isWiiU = false;

	if (reader.is_rpx)
	{
		isWiiU = true;
	}
	else
	{
		for (int i = 0; i < reader.GetNumSections(); ++i)
		{
			if (str_ends_with(reader.GetSectionName(i), ".rpl"))
			{
				isWiiU = true;
				break;
			}
		}
	}
	delete[] mem;
	return isWiiU;
}

static u32 rpx_load_address;

static ElfReader* BootOneRPX(const std::string& name, std::vector<std::string>& ld_library_path,
	std::map<std::string, std::unique_ptr<ElfReader> >& readers, ElfReader::RPLExportsMap& exports)
{
	auto readerFind = readers.find(name);
	if (readerFind != readers.end())
		return readers.at(name).get();

	std::string filename;
	bool foundFile = false;
	for each (std::string path in ld_library_path)
	{
		if (File::Exists(path + DIR_SEP + name))
		{
			foundFile = true;
			filename = path + DIR_SEP + name;
			break;
		}
	}
	if (!foundFile)
	{
		ERROR_LOG(BOOT, "Unable to boot RPX: missing %s", name.c_str());
		return nullptr;
	}
	DEBUG_LOG(BOOT, "Loading %s", filename.c_str());
	const u64 filesize = File::GetSize(filename);
	std::vector<u8> mem((size_t)filesize);

	{
		File::IOFile f(filename, "rb");
		f.ReadBytes(mem.data(), (size_t)filesize);
	}

	std::unique_ptr<ElfReader> reader(new ElfReader(mem.data()));

	for each (std::string name in reader->GetDependencies())
	{
		BootOneRPX(name, ld_library_path, readers, exports);
	}
	WARN_LOG(BOOT, "Loading %s at address %x", name.c_str(), rpx_load_address);
	reader->LoadInto(rpx_load_address);
	reader->Relocate(exports);
	reader->LoadSymbols();

	reader->LoadExports(name, exports);

	rpx_load_address += reader->GetLoadedLength();
	//if (name == "coreinit.rpl")
	//	PC = reader->GetEntryPoint(); // Coreinit needs to be initialized first
	readers[name] = std::move(reader);
	return readers.at(name).get();
}


bool CBoot::Boot_ELF(const std::string& filename)
{
	const u64 filesize = File::GetSize(filename);
	std::vector<u8> mem((size_t)filesize);

	{
	File::IOFile f(filename, "rb");
	f.ReadBytes(mem.data(), (size_t)filesize);
	}

	ElfReader reader(mem.data());
	reader.LoadInto(0x80000000);
	if (!reader.LoadSymbols())
	{
		if (LoadMapFromFilename())
			HLE::PatchFunctions();
	}
	else
	{
		HLE::PatchFunctions();
	}

	PC = reader.GetEntryPoint();

	return true;
}

bool CBoot::Boot_RPX(const std::string& filename)
{
	size_t lastSlash = filename.rfind(DIR_SEP);
	if (lastSlash == std::string::npos)
		lastSlash = filename.rfind("\\"); // Windows
	std::string dirName, name;
	if (lastSlash == std::string::npos)
	{
		dirName = "";
		name = filename;
	}
	else
	{
		dirName = filename.substr(0, lastSlash);
		name = filename.substr(lastSlash + 1);
	}
	std::map<std::string, std::unique_ptr<ElfReader> > readers;
	ElfReader::RPLExportsMap exports;
	// FIXME: remove hardcoded path
	std::vector<std::string> ld_library_path = { dirName, "P:/docs/wiiu/titles/000500101000400A/11464/rpl" };
	rpx_load_address = 0x80100000;
	ElfReader* reader = BootOneRPX(name, ld_library_path, readers, exports);
	if (reader == nullptr)
		return false;
	//g_symbolDB.AddKnownSymbol(exports.map["gx2.rpl"]["GX2Init"], 0x4, "GX2Init", Symbol::SYMBOL_FUNCTION);
	//g_symbolDB.AddKnownSymbol(exports.map["coreinit.rpl"]["exit"], 0x4, "exit", Symbol::SYMBOL_FUNCTION);
	g_symbolDB.AddKnownSymbol(0x80001000, 0x4, "FakeMEMAllocFromDefaultHeapEx", Symbol::SYMBOL_FUNCTION);
	g_symbolDB.AddKnownSymbol(0x80001010, 0x4, "FakeMEMFreeToDefaultHeap", Symbol::SYMBOL_FUNCTION);
	HLE::PatchFunctions();
	WARN_LOG(BOOT, "CoreInit used:");
	for each (std::string s in exports.usedCoreInit)
	{
		WARN_LOG(BOOT, "%s", s.c_str());
	}

	GPR(1) = 0x83000000; // setup stack
	rSPR(1007) = 1; // main core
	PC = reader->GetEntryPoint();
	Memory::Write_U32(0x80001000, exports.map["coreinit.rpl"]["MEMAllocFromDefaultHeapEx"]);
	Memory::Write_U32(0x80001000 - 4, exports.map["coreinit.rpl"]["MEMAllocFromDefaultHeap"]);
	Memory::Write_U32(0x80001010, exports.map["coreinit.rpl"]["MEMFreeToDefaultHeap"]);
	Memory::Write_U32(0x38800001, 0x80001000 - 4);
	Memory::Write_U32(0x4e800020, 0x80001000);
	Memory::Write_U32(0x4e800020, 0x80001010);
	//PowerPC::debug_interface.SetBreakpoint(0x80001000);
	//PowerPC::debug_interface.SetBreakpoint(0x80001010);
	((UReg_MSR&)MSR).FP = 1; // Enable floating point
	HLE_WiiU_CoreInit::Reset();
	return true;

}