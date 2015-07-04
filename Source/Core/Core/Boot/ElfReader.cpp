// Copyright 2014 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <string>
#include <zlib.h>

#include "Common/CommonFuncs.h"
#include "Common/CommonTypes.h"
#include "Common/MsgHandler.h"

#include "Core/Boot/ElfReader.h"
#include "Core/Debugger/Debugger_SymbolMap.h"
#include "Core/HW/Memmap.h"
#include "Core/PowerPC/PPCSymbolDB.h"

// Wii U RPL files have import/export sections with an address above 0xc0000000
// They're not currently loaded by this reader
static const u32 RPL_VIRTUAL_SECTION_ADDR = 0xc0000000;
// RPLs are linked to this address by default
static const u32 RPL_DEFAULT_BASE = 0x2000000;

static const u32 RPL_SHT_EXPORT = SHT_LOUSER + 1;
static const u32 RPL_SHT_IMPORT = SHT_LOUSER + 2;

struct RPLExport {
	u32 address;
	u32 name_index;
};

static void bswap(u32 &w) {w = Common::swap32(w);}
static void bswap(u16 &w) {w = Common::swap16(w);}

static void byteswapHeader(Elf32_Ehdr &ELF_H)
{
	bswap(ELF_H.e_type);
	bswap(ELF_H.e_machine);
	bswap(ELF_H.e_ehsize);
	bswap(ELF_H.e_phentsize);
	bswap(ELF_H.e_phnum);
	bswap(ELF_H.e_shentsize);
	bswap(ELF_H.e_shnum);
	bswap(ELF_H.e_shstrndx);
	bswap(ELF_H.e_version);
	bswap(ELF_H.e_entry);
	bswap(ELF_H.e_phoff);
	bswap(ELF_H.e_shoff);
	bswap(ELF_H.e_flags);
}

static void byteswapSegment(Elf32_Phdr &sec)
{
	bswap(sec.p_align);
	bswap(sec.p_filesz);
	bswap(sec.p_flags);
	bswap(sec.p_memsz);
	bswap(sec.p_offset);
	bswap(sec.p_paddr);
	bswap(sec.p_vaddr);
	bswap(sec.p_type);
}

static void byteswapSection(Elf32_Shdr &sec)
{
	bswap(sec.sh_addr);
	bswap(sec.sh_addralign);
	bswap(sec.sh_entsize);
	bswap(sec.sh_flags);
	bswap(sec.sh_info);
	bswap(sec.sh_link);
	bswap(sec.sh_name);
	bswap(sec.sh_offset);
	bswap(sec.sh_size);
	bswap(sec.sh_type);

	NOTICE_LOG(BOOT, "Sections: \n"
		"! flags       ! address    ! offset     ! size       ! data0      ! data1      ! align ! data3      !");
	NOTICE_LOG(BOOT, "\n| 0x%08x  | 0x%08x | 0x%08x | %10u | 0x%08x | 0x%08x | %5u | 0x%08x",
			sec.sh_flags, sec.sh_addr, sec.sh_offset, sec.sh_size, sec.sh_link, sec.sh_info,
			sec.sh_addralign, sec.sh_entsize);
}

ElfReader::ElfReader(void *ptr)
{
	base = (char*)ptr;
	base32 = (u32 *)ptr;
	header = (Elf32_Ehdr*)ptr;
	byteswapHeader(*header);
	m_number_of_sections = header->e_shnum;
	NOTICE_LOG(BOOT, "e_ident     0x%02x %02x %02x %02x   %02x %02x %02x %02x    %02x %02x %02x %02x   %02x %02x %02x %02x\n",
		header->e_ident[0], header->e_ident[1], header->e_ident[2], header->e_ident[3],
		header->e_ident[4], header->e_ident[5], header->e_ident[6], header->e_ident[7],
		header->e_ident[8], header->e_ident[9], header->e_ident[10], header->e_ident[11],
		header->e_ident[12], header->e_ident[13], header->e_ident[14], header->e_ident[15]);
	NOTICE_LOG(BOOT, "e_type      0x%04x [%s]", header->e_type, (header->e_type == 0xfe01) ? "Cafe RPL" : "UNKNOWN");
	NOTICE_LOG(BOOT, "e_machine   0x%04x [%s]", header->e_machine, (header->e_machine == 0x0014) ? "PowerPC" : "UNKNOWN");
	NOTICE_LOG(BOOT, "e_version   0x%08x", header->e_version);
	NOTICE_LOG(BOOT, "e_entry     0x%08x", header->e_entry);
	NOTICE_LOG(BOOT, "e_phoff     0x%08x", header->e_phoff);
	NOTICE_LOG(BOOT, "e_shoff     0x%08x", header->e_shoff);
	NOTICE_LOG(BOOT, "e_flags     0x%08x", header->e_flags);
	NOTICE_LOG(BOOT, "e_ehsize    0x%04x (%u)", header->e_ehsize, header->e_ehsize);
	NOTICE_LOG(BOOT, "e_phentsize 0x%04x (%u)", header->e_phentsize, header->e_phentsize);
	NOTICE_LOG(BOOT, "e_phnum     0x%04x (%u)", header->e_phnum, header->e_phnum);
	NOTICE_LOG(BOOT, "e_shentsize 0x%04x (%u)", header->e_shentsize, header->e_shentsize);
	NOTICE_LOG(BOOT, "e_shnum     0x%04x (%u)", header->e_shnum, header->e_shnum);
	NOTICE_LOG(BOOT, "e_shstrndx  0x%04x (%u)", header->e_shstrndx, header->e_shstrndx);

	// Wii U uses code name Cafe. RPX and RPL files have 0xCAFE at offset 7. But Wii U ELF files have 0x0000.
	is_rpx = (header->e_ident[7] == 0xCA) && (header->e_ident[8] == 0xFE);
	if (is_rpx)
	{
		decompressed = new u8 *[m_number_of_sections];
		memset(decompressed, 0, sizeof(u8*)*m_number_of_sections);
	}
	else
	{
		decompressed = nullptr;
	}

	segments = (Elf32_Phdr *)(base + header->e_phoff);
	sections = (Elf32_Shdr *)(base + header->e_shoff);

	for (int i = 0; i < GetNumSegments(); i++)
	{
		byteswapSegment(segments[i]);
	}

	for (int i = 0; i < GetNumSections(); i++)
	{
		byteswapSection(sections[i]);
	}
	entryPoint = header->e_entry;

	for (u32 i = 0; i < m_number_of_sections; ++i)
	{
		NOTICE_LOG(BOOT, "Section %d is named: '%s'", i, GetSectionName(i));
	}
}

ElfReader::~ElfReader() {
	if (decompressed)
	{
		for (u32 i = 0; i < m_number_of_sections; ++i)
		{
			delete[] decompressed[i];
		}
		delete [] decompressed;
	}
}


const char *ElfReader::GetSectionName(int section) const
{
	int nameOffset = sections[section].sh_name;
	if (nameOffset == 0)
		return nullptr;
	char *ptr = (char*)GetSectionDataPtr(header->e_shstrndx);

	if (ptr)
		return ptr + nameOffset;
	else
		return nullptr;
}

const u8 *ElfReader::GetSectionDataPtr(int section) const
{
	if (section <= 0 || section >= header->e_shnum || sections[section].sh_type == SHT_NOBITS || sections[section].sh_size == 0)
		return nullptr;
	// Wii U RPX and RPL files have some compressed sections. decompressed is nullptr for a normal uncompressed elf.
	if (sections[section].sh_flags & SHF_DEFLATED && decompressed)
	{
		if (!decompressed[section])
		{
			// first four bytes of section is the inflated size
			u32 inflated_size = Common::swap32( *((u32 *)GetPtr(sections[section].sh_offset)) );
			decompressed[section] = new u8[inflated_size];
			z_stream s;
			memset(&s, 0, sizeof(s));

			s.zalloc = Z_NULL;
			s.zfree = Z_NULL;
			s.opaque = Z_NULL;

			int ret = inflateInit(&s);
			if (ret != Z_OK)
				ERROR_LOG(BOOT, "Couldn't decompress .rpx section %d because inflateInit returned %d", section, ret);

			s.avail_in = sections[section].sh_size;
			s.next_in = (Bytef *)GetPtr(sections[section].sh_offset + 4);
			s.avail_out = inflated_size;
			s.next_out = decompressed[section];

			ret = inflate(&s, Z_FINISH);
			if (ret != Z_OK && ret != Z_STREAM_END)
				ERROR_LOG(BOOT, "Couldn't decompress .rpx section %d because inflate returned %d", ret);

			inflateEnd(&s);
		}
		return decompressed[section];
	}
	else
	{
		return GetPtr(sections[section].sh_offset);
	}
}

// PowerPC page sizes
static u32 PPC_PAGE_SIZE = 4096;
static u32 PPC_PAGE_MASK = ~(PPC_PAGE_SIZE - 1);

static inline u32 ppc_page_begin(u32 p)
{
	return p & PPC_PAGE_MASK;
}

static inline u32 ppc_page_end(u32 p)
{
	return ppc_page_begin(p + (PPC_PAGE_SIZE - 1));
}

bool ElfReader::LoadInto(u32 vaddr)
{
	DEBUG_LOG(MASTER_LOG,"String section: %i", header->e_shstrndx);

//	sectionOffsets = new u32[GetNumSections()];
	sectionAddrs = new u32[GetNumSections()];

	// Should we relocate? (if it's a library not an executable)
	// all Wii U RPLs and RPXes are relocateable (and marked as ET_DYN)
	bRelocate = (header->e_type != ET_EXEC);

	if (bRelocate)
	{
		DEBUG_LOG(MASTER_LOG,"Relocatable module");
		entryPoint += vaddr;
	}
	else
	{
		DEBUG_LOG(MASTER_LOG,"Prerelocated executable");
	}

	// Note... Wii U RPX files have no segments, only sections
	INFO_LOG(MASTER_LOG,"%i segments:", header->e_phnum);

	// First pass : Get the bits into RAM
	u32 segmentVAddr[32];

	u32 baseAddress = bRelocate?vaddr:0;
	for (int i = 0; i < header->e_phnum; i++)
	{
		Elf32_Phdr *p = segments + i;

		INFO_LOG(MASTER_LOG, "Type: %i Vaddr: %08x Filesz: %i Memsz: %i ", p->p_type, p->p_vaddr, p->p_filesz, p->p_memsz);

		if (p->p_type == PT_LOAD)
		{
			segmentVAddr[i] = baseAddress + p->p_vaddr;
			u32 writeAddr = segmentVAddr[i];

			const u8 *src = GetSegmentPtr(i);
			u8 *dst = Memory::GetPointer(writeAddr);
			u32 srcSize = p->p_filesz;
			u32 dstSize = p->p_memsz;
			u32 *s = (u32*)src;
			u32 *d = (u32*)dst;
			for (int j = 0; j < (int)(srcSize + 3) / 4; j++)
			{
				*d++ = /*_byteswap_ulong*/(*s++);
			}
			if (srcSize < dstSize)
			{
				//memset(dst + srcSize, 0, dstSize-srcSize); //zero out bss
			}
			INFO_LOG(MASTER_LOG,"Loadable Segment Copied to %08x, size %08x", writeAddr, p->p_memsz);
		}
	}
	
	INFO_LOG(MASTER_LOG,"%i sections:", header->e_shnum);

	u32 lastSuggestedAddress = 0xffffffff;
	u32 lastLength = 0;
	u32 lastWriteAddress = baseAddress;

	u32 minAddr = 0xffffffff, maxAddr = 0;

	for (int i=0; i<GetNumSections(); i++)
	{
		Elf32_Shdr *s = &sections[i];
		if ((s->sh_addr & SHF_ALLOC) && lastSuggestedAddress == 0xffffffff)
			lastSuggestedAddress = s->sh_addr;
		const char *name = GetSectionName(i);

		u32 writeAddr = lastWriteAddress + lastLength;
		//if (lastSuggestedAddress + lastLength != s->sh_addr) // not contiguous
		if (((lastSuggestedAddress + lastLength) & 0xf0000000) != (s->sh_addr & 0xf0000000))
			writeAddr = ppc_page_end(writeAddr);
		else
			writeAddr += (s->sh_addr - (lastSuggestedAddress + lastLength));
		// sectionOffsets[i] = writeAddr - vaddr;
		sectionAddrs[i] = writeAddr;

		if (s->sh_flags & SHF_ALLOC)
		{
			INFO_LOG(MASTER_LOG,"Data Section found: %s     Sitting at %08x, size %08x", name, writeAddr, s->sh_size);
			const u8 *src = GetSectionDataPtr(i);
			u8 *dst = Memory::GetPointer(writeAddr);
			u32 srcSize = GetSectionSize(i);
			u32 dstSize = GetSectionSize(i);
			if (s->sh_type == SHT_NOBITS)
				srcSize = 0;
			for (u32 num = 0; num < srcSize; ++num)
				Memory::Write_U8(src[num], writeAddr + num);
			// zero out bss
			for (u32 num = srcSize; num < dstSize; ++num)
				Memory::Write_U8(0, writeAddr + num);

			// for length calculations
			if (minAddr > writeAddr)
				minAddr = writeAddr;
			if (maxAddr < writeAddr + dstSize)
				maxAddr = writeAddr + dstSize;

			if (header->e_entry >= s->sh_addr && header->e_entry < s->sh_addr + dstSize)
				entryPoint = (header->e_entry - s->sh_addr) + writeAddr;

			lastWriteAddress = writeAddr;
			lastLength = dstSize;
			lastSuggestedAddress = s->sh_addr;

		}
		else
		{
			INFO_LOG(MASTER_LOG,"NonData Section found: %s     Ignoring (size=%08x) (flags=%08x)", name, s->sh_size, s->sh_flags);
		}
	}
	loaded_length = ppc_page_end(maxAddr - minAddr);

	INFO_LOG(MASTER_LOG,"Done loading.");
	return true;
}

int ElfReader::GetSectionSize(SectionID section) const
{
	if (section <= 0 || section >= header->e_shnum)
		return 0;
	if (sections[section].sh_flags & SHF_DEFLATED)
		return Common::swap32(*((u32 *)GetPtr(sections[section].sh_offset)));
	return sections[section].sh_size;
}

SectionID ElfReader::GetSectionByName(const char *name, int firstSection) const
{
	for (int i = firstSection; i < header->e_shnum; i++)
	{
		const char *secname = GetSectionName(i);

		if (secname != nullptr && strcmp(name, secname) == 0)
			return i;
	}
	return -1;
}

bool ElfReader::LoadSymbols()
{
	bool hasSymbols = false;
	SectionID sec = GetSectionByName(".symtab");
	if (sec != -1)
	{
		int stringSection = sections[sec].sh_link;
		const char *stringBase = (const char *)GetSectionDataPtr(stringSection);

		//We have a symbol table!
		Elf32_Sym *symtab = (Elf32_Sym *)(GetSectionDataPtr(sec));
		int numSymbols = GetSectionSize(sec) / sizeof(Elf32_Sym);
		for (int sym = 0; sym < numSymbols; sym++)
		{
			int size = Common::swap32(symtab[sym].st_size);
			if (size == 0)
				continue;

			// int bind = symtab[sym].st_info >> 4;
			int type = symtab[sym].st_info & 0xF;
			int sectionIndex = Common::swap16(symtab[sym].st_shndx);
			int value = Common::swap32(symtab[sym].st_value);
			if (is_rpx && ((u32)value) >= RPL_VIRTUAL_SECTION_ADDR)
				continue;
			const char *name = stringBase + Common::swap32(symtab[sym].st_name);
			if (bRelocate)
				value = (value - sections[sectionIndex].sh_addr) + sectionAddrs[sectionIndex];

			int symtype = Symbol::SYMBOL_DATA;
			switch (type)
			{
			case STT_OBJECT:
				symtype = Symbol::SYMBOL_DATA; break;
			case STT_FUNC:
				symtype = Symbol::SYMBOL_FUNCTION; break;
			default:
				continue;
			}
			g_symbolDB.AddKnownSymbol(value, size, name, symtype);
			hasSymbols = true;
		}
	}
	g_symbolDB.Index();
	return hasSymbols;
}



bool ElfReader::Relocate(RPLExportsMap& exports)
{
	bool success = true;
	SectionID sec = GetSectionByName(".symtab");
	if (sec == -1)
	{
		return false;
	}
	int stringSection = sections[sec].sh_link;
	const char *stringBase = (const char *)GetSectionDataPtr(stringSection);

	//We have a symbol table!
	Elf32_Sym *symtab = (Elf32_Sym *)(GetSectionDataPtr(sec));

	for (int i = 0; i < GetNumSections(); i++)
	{
		Elf32_Shdr *s = &sections[i];
		const char *name = GetSectionName(i);
		if (s->sh_type == SHT_REL)
		{
			PanicAlert("Failed to relocate ELF: SHT_REL sections are not handled");
			continue;
		}
		else if (s->sh_type == SHT_RELA)
		{
			int numRels = GetSectionSize(i) / s->sh_entsize;
			int relSectionIndex = s->sh_info;

			Elf32_Rela* relaSection = (Elf32_Rela*)GetSectionDataPtr(i);
			for (int i = 0; i < numRels; i++)
			{
				Elf32_Rela* rela = relaSection + i;
				uint32_t offset = Common::swap32(rela->r_offset);
				uint32_t info = Common::swap32(rela->r_info);
				uint32_t addend = Common::swap32(rela->r_addend);

				uint32_t symIndex = ELF32_R_SYM(info);
				uint32_t relocType = ELF32_R_TYPE(info);
				Elf32_Sym* symbol = symtab + symIndex;
				const char *symbolName = stringBase + Common::swap32(symbol->st_name);
				DEBUG_LOG(BOOT, "Relocation: offset=%x, addend=%x, sym=%s, relocType=%d", offset, addend, symbolName, relocType);

				u32 symValue = Common::swap32(symbol->st_value);
				u32 symSectionIndex = Common::swap16(symbol->st_shndx);
				u32 symAddr = (symValue - sections[symSectionIndex].sh_addr) + sectionAddrs[symSectionIndex];
				if (symValue >= RPL_VIRTUAL_SECTION_ADDR) // import
				{
					if (sections[symSectionIndex].sh_type != RPL_SHT_IMPORT)
						PanicAlert("relocations for symbols above 0xc0000000 is only supported for import sections");

					const char* libname_raw = ((const char*)GetSectionDataPtr(symSectionIndex)) + 8;
					std::string libname = libname_raw;
					if (libname.find(".rpl") == std::string::npos)
						libname += ".rpl";
					// TODO: actually resolve based on lib name and import table
					auto& themap = exports.map[libname];
					auto findResult = themap.find(symbolName);
					if (findResult == themap.end())
					{
						ERROR_LOG(BOOT, "Failed to resolve symbol %s", symbolName);
						symValue = 0xdeadbeef; // FIXME: properly handle this error
						success = false;
					}
					else
					{
						symAddr = findResult->second;
						if (libname == "coreinit.rpl")
						{
							exports.usedCoreInit.insert(symbolName);
						}
					}
				}
				symAddr += addend;

				u32 writeAddr = (offset - sections[relSectionIndex].sh_addr) + sectionAddrs[relSectionIndex];

				if (addend == 0x24028 && !strcmp(symbolName, "$TEXT")) {
					WARN_LOG(BOOT, "Debugger pls symAddr=%x symValue=%x offset=%x writeAddr=%x addend=%x %x %x %x %x %x %x\n",
						symAddr, symValue, offset, writeAddr, addend,
						symSectionIndex, sections[symSectionIndex].sh_addr, sectionAddrs[symSectionIndex],
						relSectionIndex, sections[relSectionIndex].sh_addr, sectionAddrs[relSectionIndex]);
				}
				if (!strcmp(symbolName, "$UNDEF"))
					symAddr = 0;

				switch (relocType)
				{
				case R_PPC_ADDR32:
					Memory::Write_U32(symAddr, writeAddr);
					break;
				case R_PPC_ADDR16_LO:
					Memory::Write_U16(symAddr & 0xffff, writeAddr);
					break;
				case R_PPC_ADDR16_HI:
					Memory::Write_U16((symAddr >> 16) & 0xffff, writeAddr);
					break;
				case R_PPC_ADDR16_HA:
					Memory::Write_U16((((symAddr >> 16) + ((symAddr & 0x8000) ? 1 : 0)) & 0xFFFF), writeAddr);
					break;
				case R_PPC_REL24:
					printf("symaddr %x offset %x sub %x writeaddr sub %x", symAddr, offset, symAddr - offset, symAddr - writeAddr);
					Memory::Write_U32((Memory::Read_U32(writeAddr) & 0xfc000003) | (((symAddr - writeAddr) >> 2) & 0xffffff) << 2, writeAddr);
					break;
				case R_PPC_EMB_SDA21: {
					u32 instr = Memory::Read_U32(writeAddr) & 0xffe00000; // top 6 bits of instruction + top 5 bits for destination register
					u32 reg;
					const char* sectname = GetSectionName(symSectionIndex);
					if (!strcmp(sectname, ".sdata") || !strcmp(sectname, ".sbss"))
						reg = 13;
					else if (!strcmp(sectname, ".sdata2") || !strcmp(sectname, ".sbss2"))
						reg = 2;
					else if (!strcmp(sectname, ".sdata0") || !strcmp(sectname, ".sbss0"))
						reg = 0;
					else
					{
						PanicAlert("Invalid relocation for EMB_SDA21: symbol in invalid section %s", sectname);
						break;
					}
					u32 off = symAddr & 0xffff;
					Memory::Write_U32(instr | (reg << 16) | off, writeAddr);
					break;
				}
				default:
					// I don't think any Wii U RPX executable uses relocations other than the above types
					PanicAlert("Failed to relocate ELF: unsupported relocation type %d", relocType);
					success = false;
					break;
				}
			}
		}
	}
	return success;
}

std::vector<std::string> ElfReader::GetDependencies()
{
	std::vector<std::string> vec;
	if (!is_rpx) return vec;
	for (int i = 0; i < header->e_shnum; i++)
	{
		if (sections[i].sh_type == RPL_SHT_IMPORT)
		{
			const char* name = ((const char*) GetSectionDataPtr(i)) + 8;
			std::string libname = name;
			if (libname.find(".rpl") == std::string::npos)
				libname += ".rpl";
			if (std::find(vec.begin(), vec.end(), libname) == vec.end())
				vec.emplace_back(libname);
		}
	}
	return vec;
}

bool ElfReader::LoadExports(std::string const& libraryname, RPLExportsMap& exportsMap)
{
	// must be loaded and relocated already before running loadExports
	static_assert(sizeof(RPLExport) == 8, "RPLExport size should be 8");
	for (int i = 0; i < header->e_shnum; i++)
	{
		if (sections[i].sh_type == RPL_SHT_EXPORT)
		{
			u32* sectionPtr = (u32*) Memory::GetPointer(sectionAddrs[i]);
			u32 numExports = Common::swap32(sectionPtr[0]);
			RPLExport* exports = (RPLExport*)(sectionPtr + 2);
			const char* nametable = (const char*)(exports + numExports + 1);
			for (u32 i = 0; i < numExports; i++)
			{
				const char* name = ((const char*) sectionPtr) + Common::swap32(exports[i].name_index);
				u32 address = Common::swap32(exports[i].address);
				exportsMap.AddExport(libraryname, name, address);
				if (g_symbolDB.GetSymbolFromName(name) == nullptr)
					g_symbolDB.AddKnownSymbol(address, 4, name, Symbol::SYMBOL_FUNCTION); // I know it's inefficient: Haters gotta hate.
			}
		}
	}
	return true;
}