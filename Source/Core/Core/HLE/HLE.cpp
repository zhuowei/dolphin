// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "Common/CommonTypes.h"

#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/Debugger/Debugger_SymbolMap.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/HLE_Misc.h"
#include "Core/HLE/HLE_OS.h"
#include "Core/HLE/HLE_WiiU_CoreInit.h"
#include "Core/HW/Memmap.h"
#include "Core/IPC_HLE/WII_IPC_HLE_Device_es.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/PowerPC/PPCSymbolDB.h"


namespace HLE
{

using namespace PowerPC;

typedef void (*TPatchFunction)();

enum
{
	HLE_RETURNTYPE_BLR = 0,
	HLE_RETURNTYPE_RFI = 1,
};

struct SPatch
{
	char m_szPatchName[128];
	TPatchFunction PatchFunction;
	int type;
	int flags;
};

static const SPatch OSPatches[] =
{
	{ "FAKE_TO_SKIP_0",       HLE_Misc::UnimplementedFunction, HLE_HOOK_REPLACE, HLE_TYPE_GENERIC },

	{ "PanicAlert",           HLE_Misc::HLEPanicAlert,         HLE_HOOK_REPLACE, HLE_TYPE_DEBUG },

	// Name doesn't matter, installed in CBoot::BootUp()
	{ "HBReload",             HLE_Misc::HBReload,              HLE_HOOK_REPLACE, HLE_TYPE_GENERIC },

	// Debug/OS Support
	{ "OSPanic",              HLE_OS::HLE_OSPanic,             HLE_HOOK_REPLACE, HLE_TYPE_DEBUG },

	{ "OSReport",             HLE_OS::HLE_GeneralDebugPrint,   HLE_HOOK_REPLACE, HLE_TYPE_DEBUG },
	{ "DEBUGPrint",           HLE_OS::HLE_GeneralDebugPrint,   HLE_HOOK_REPLACE, HLE_TYPE_DEBUG },
	{ "WUD_DEBUGPrint",       HLE_OS::HLE_GeneralDebugPrint,   HLE_HOOK_REPLACE, HLE_TYPE_DEBUG },
	{ "vprintf",              HLE_OS::HLE_GeneralDebugPrint,   HLE_HOOK_REPLACE, HLE_TYPE_DEBUG },
	{ "printf",               HLE_OS::HLE_GeneralDebugPrint,   HLE_HOOK_REPLACE, HLE_TYPE_DEBUG },
	{ "puts",                 HLE_OS::HLE_GeneralDebugPrint,   HLE_HOOK_REPLACE, HLE_TYPE_DEBUG }, // gcc-optimized printf?
	{ "___blank(char *,...)", HLE_OS::HLE_GeneralDebugPrint,   HLE_HOOK_REPLACE, HLE_TYPE_DEBUG }, // used for early init things (normally)
	{ "___blank",             HLE_OS::HLE_GeneralDebugPrint,   HLE_HOOK_REPLACE, HLE_TYPE_DEBUG },
	{ "__write_console",      HLE_OS::HLE_write_console,       HLE_HOOK_REPLACE, HLE_TYPE_DEBUG }, // used by sysmenu (+more?)
	{ "GeckoCodehandler",     HLE_Misc::HLEGeckoCodehandler,   HLE_HOOK_START,   HLE_TYPE_GENERIC },
	// Wii U
	{ "COSError",			  HLE_WiiU_CoreInit::COS_Report,   HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "COSWarn",              HLE_WiiU_CoreInit::COS_Report,   HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "COSInfo",              HLE_WiiU_CoreInit::COS_Report,   HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "COSVerbose",           HLE_WiiU_CoreInit::COS_Report,   HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "OSCreateThread", HLE_WiiU_CoreInit::OSCreateThread, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "OSResumeThread", HLE_WiiU_CoreInit::OSResumeThread, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "OSSetThreadName", HLE_WiiU_CoreInit::OSSetThreadName, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "OSJoinThread", HLE_WiiU_CoreInit::OSJoinThread, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "OSYieldThread", HLE_Misc::UnimplementedFunction, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "OSRunThread", HLE_WiiU_CoreInit::DumpArgsAndReturn, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "MEMCreateExpHeapEx", HLE_Misc::UnimplementedFunction, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "MEMCreateUnitHeapEx", HLE_Misc::UnimplementedFunction, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "MEMAllocFromExpHeapEx", HLE_WiiU_CoreInit::HeapAllocStub, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "MEMAllocFromUnitHeapEx", HLE_WiiU_CoreInit::HeapAllocStub, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "MEMAllocFromFrmHeapEx", HLE_WiiU_CoreInit::HeapAllocStub, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "MEMFreeToExpHeap", HLE_WiiU_CoreInit::HeapFreeStub, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "MEMFreeToUnitHeap", HLE_WiiU_CoreInit::HeapFreeStub, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "MEMFreeToFrmHeap", HLE_WiiU_CoreInit::HeapFreeStub, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "FakeMEMAllocFromDefaultHeapEx", HLE_WiiU_CoreInit::HeapAllocStubWithImplicitHeap, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "FakeMEMFreeToDefaultHeap", HLE_WiiU_CoreInit::HeapFreeStub, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	//{ "FakeMEMAllocFromDefaultHeap", HLE_WiiU_CoreInit::HeapAllocStubWithImplicitHeap, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "OSGetMemBound", HLE_WiiU_CoreInit::OSGetMemBound, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "OSSendAppSwitchRequest", HLE_WiiU_CoreInit::DumpArgsAndReturn, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "OSReceiveMessage", HLE_WiiU_CoreInit::OSReceiveMessage, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "OSGetCallArgs", HLE_WiiU_CoreInit::OSGetCallArgs, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "exit", HLE_WiiU_CoreInit::exit, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "OSSignalEventAll", HLE_WiiU_CoreInit::DumpArgsAndReturn, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "OSReleaseForeground", HLE_WiiU_CoreInit::DumpArgsAndReturn, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "OSGetForegroundBucket", HLE_WiiU_CoreInit::OSGetForegroundBucket, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "OSInitRendezvous", HLE_Misc::UnimplementedFunction, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "OSWaitRendezvous", HLE_Misc::UnimplementedFunction, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	// WiiU GX2
	{ "GX2Init", HLE_WiiU_CoreInit::DumpArgsAndReturn, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "GX2Shutdown", HLE_WiiU_CoreInit::DumpArgsAndReturn, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "GX2Invalidate", HLE_WiiU_CoreInit::DumpArgsAndReturn, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "GX2SetupContextStateEx", HLE_WiiU_CoreInit::DumpArgsAndReturn, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "GX2SetContextState", HLE_WiiU_CoreInit::DumpArgsAndReturn, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "GX2SetScissor", HLE_WiiU_CoreInit::DumpArgsAndReturn, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "GX2SetViewport", HLE_WiiU_CoreInit::DumpArgsAndReturn, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "GX2SetShaderModeEx", HLE_WiiU_CoreInit::DumpArgsAndReturn, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "GX2SetDepthStencilControl", HLE_WiiU_CoreInit::DumpArgsAndReturn, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "GX2SetStencilMask", HLE_WiiU_CoreInit::DumpArgsAndReturn, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "GX2SetColorControl", HLE_WiiU_CoreInit::DumpArgsAndReturn, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "GX2SetBlendControl", HLE_WiiU_CoreInit::DumpArgsAndReturn, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "GX2SetBlendConstantColor", HLE_WiiU_CoreInit::DumpArgsAndReturn, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "GX2SetAlphaTest", HLE_WiiU_CoreInit::DumpArgsAndReturn, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "GX2SetTargetChannelMasks", HLE_WiiU_CoreInit::DumpArgsAndReturn, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "GX2SetPolygonControl", HLE_WiiU_CoreInit::DumpArgsAndReturn, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "GX2ClearColor", HLE_WiiU_CoreInit::DumpArgsAndReturn, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
	{ "GX2ClearDepthStencilEx", HLE_WiiU_CoreInit::DumpArgsAndReturn, HLE_HOOK_REPLACE, HLE_TYPE_WIIU },
};

static const SPatch OSBreakPoints[] =
{
	{ "FAKE_TO_SKIP_0", HLE_Misc::UnimplementedFunction },
};

void Patch(u32 addr, const char *hle_func_name)
{
	for (u32 i = 0; i < sizeof(OSPatches) / sizeof(SPatch); i++)
	{
		if (!strcmp(OSPatches[i].m_szPatchName, hle_func_name))
		{
			orig_instruction[addr] = i;
			return;
		}
	}
}

void PatchFunctions()
{
	orig_instruction.clear();
	for (u32 i = 0; i < sizeof(OSPatches) / sizeof(SPatch); i++)
	{
		Symbol *symbol = g_symbolDB.GetSymbolFromName(OSPatches[i].m_szPatchName);
		if (symbol)
		{
			for (u32 addr = symbol->address; addr < symbol->address + symbol->size; addr += 4)
			{
				orig_instruction[addr] = i;
			}
			INFO_LOG(OSHLE, "Patching %s %08x", OSPatches[i].m_szPatchName, symbol->address);
		}
	}

	if (SConfig::GetInstance().m_LocalCoreStartupParameter.bEnableDebugging)
	{
		for (size_t i = 1; i < sizeof(OSBreakPoints) / sizeof(SPatch); i++)
		{
			Symbol *symbol = g_symbolDB.GetSymbolFromName(OSPatches[i].m_szPatchName);
			if (symbol)
			{
				PowerPC::breakpoints.Add(symbol->address, false);
				INFO_LOG(OSHLE, "Adding BP to %s %08x", OSBreakPoints[i].m_szPatchName, symbol->address);
			}
		}
	}

	// CBreakPoints::AddBreakPoint(0x8000D3D0, false);
}

void Execute(u32 _CurrentPC, u32 _Instruction)
{
	unsigned int FunctionIndex = _Instruction & 0xFFFFF;
	if ((FunctionIndex > 0) && (FunctionIndex < (sizeof(OSPatches) / sizeof(SPatch))))
	{
		OSPatches[FunctionIndex].PatchFunction();
	}
	else
	{
		PanicAlert("HLE system tried to call an undefined HLE function %i.", FunctionIndex);
	}

	// _dbg_assert_msg_(HLE,NPC == LR, "Broken HLE function (doesn't set NPC)", OSPatches[pos].m_szPatchName);
}

u32 GetFunctionIndex(u32 addr)
{
	std::map<u32, u32>::const_iterator iter = orig_instruction.find(addr);
	return (iter != orig_instruction.end()) ?  iter->second : 0;
}

int GetFunctionTypeByIndex(u32 index)
{
	return OSPatches[index].type;
}

int GetFunctionFlagsByIndex(u32 index)
{
	return OSPatches[index].flags;
}

const char* GetFunctionNameByIndex(u32 index)
{
	return OSPatches[index].m_szPatchName;
}

bool IsEnabled(int flags)
{
	if (flags == HLE::HLE_TYPE_DEBUG && !SConfig::GetInstance().m_LocalCoreStartupParameter.bEnableDebugging && PowerPC::GetMode() != MODE_INTERPRETER)
		return false;

	return true;
}

u32 UnPatch(const std::string& patchName)
{
	Symbol* symbol = g_symbolDB.GetSymbolFromName(patchName);

	if (symbol)
	{
		for (u32 addr = symbol->address; addr < symbol->address + symbol->size; addr += 4)
		{
			orig_instruction[addr] = 0;
			PowerPC::ppcState.iCache.Invalidate(addr);
		}
		return symbol->address;
	}

	return 0;
}

}  // end of namespace HLE
