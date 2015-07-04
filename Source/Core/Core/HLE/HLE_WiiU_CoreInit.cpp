#include <string>
#include <unordered_map>

#include "Common/CommonTypes.h"
#include "Common/StringUtil.h"

#include "Core/HLE/HLE.h"
#include "Core/HLE/HLE_OS.h"
#include "Core/HLE/HLE_WiiU_CoreInit.h"
#include "Core/HW/Memmap.h"
#include "Core/PowerPC/PowerPC.h"

namespace HLE_WiiU_CoreInit
{

u32 next_tid = 1;
std::unordered_map<u32, OSThread> threads;

void COS_Report()
{
	std::string ReportMessage;
	HLE_OS::GetStringVA(ReportMessage, 4);
	NPC = LR;

	//PanicAlert("(%08x->%08x) %s", LR, PC, ReportMessage.c_str());
	NOTICE_LOG(OSREPORT, "%08x->%08x| %s", LR, PC, ReportMessage.c_str());
}

// bool (*OSCreateThread)(void *thread, void *entry, int argc, void *args, uint32_t stack, uint32_t stack_size, int32_t priority, uint16_t attr); 
void OSCreateThread()
{
	OSThread thread;
	thread.nativePtr = GPR(3);
	thread.entry = GPR(4);
	thread.argc = GPR(5);
	thread.argv = GPR(6);
	thread.stack = GPR(7);
	thread.stack_size = GPR(8);
	thread.priority = GPR(9);
	thread.attr = GPR(10);
	thread.DumpAttributes();
	GPR(3) = thread.tid = next_tid++;
	threads[thread.nativePtr] = thread;
	NPC = LR;
}

// int32_t(*OSResumeThread)(void *thread);
void OSResumeThread()
{
	u32 thread = GPR(3);
	WARN_LOG(BOOT, "OSResumeThread thread=%x", thread);
	NPC = LR;
}

// void OSSetThreadName(void *thread, const char* name);
void OSSetThreadName()
{
	u32 thread = GPR(3);
	u32 nameRaw = GPR(4);
	const char* name = (const char*) Memory::GetPointer(nameRaw);
	WARN_LOG(BOOT, "OSSetThreadName %s", name);
	NPC = LR;
}

void OSJoinThread()
{
	u32 thread = GPR(3);
	WARN_LOG(BOOT, "OSJoinThread %x", thread);
	NPC = LR;
}

static u32 heapPtr;
static void HeapAllocStubImpl(u32 size, u32 align)
{
	WARN_LOG(BOOT, "Heap alloc: %x %x", size, align);
	u32 alignRem = heapPtr % align;
	u32 alignAdd = (alignRem == 0 ? 0 : align - alignRem);
	u32 retval = heapPtr + alignAdd;
	heapPtr += size + alignAdd;
	GPR(3) = retval;
	if (retval > 0x9a000000) {
		PanicAlert("Running out of memory in heap!");
	}
	NPC = LR;
}
void HeapAllocStub()
{
	u32 size = GPR(4);
	u32 align = GPR(5);
	HeapAllocStubImpl(size, align);
}

void HeapAllocStubWithImplicitHeap()
{
	u32 size = GPR(3);
	u32 align = GPR(4);
	HeapAllocStubImpl(size, align);
}

void HeapFreeStub()
{
	WARN_LOG(BOOT, "Freeing to heap: %x %x %x", GPR(3), GPR(4), GPR(5));
	NPC = LR;
}

void OSGetMemBound()
{
	Memory::Write_U32(0x1230, GPR(4));
	Memory::Write_U32(0x2340, GPR(5));
	NPC = LR;
}

static int hardcodedMessageIndex = 0;
struct Message {
	u32 unknown;
	u32 data0;
	u32 data1;
	u32 data2;
};
static Message hardcodedMessages[] = {
		{ 0, 0xfacef000, 0, 0 },
		{ 0, 0xfacebacc, 0, 0 },
		{ 0, 0xfacef000, 0, 0 },
		{ 0, 0xfacef000, 0, 0 },
		{ 0, 0xfacef000, 0, 0 },
		{ 0, 0xd1e0d1e0, 0, 0 },
};

void OSReceiveMessage()
{
	WARN_LOG(BOOT, "OSReceiveMessage(%x, %x, %x)", GPR(3), GPR(4), GPR(5));
	u32 msgPointer = GPR(4);
	Message& msg = hardcodedMessages[hardcodedMessageIndex++];
	Memory::Write_U32(msg.unknown, msgPointer);
	Memory::Write_U32(msg.data0, msgPointer + 4);
	Memory::Write_U32(msg.data1, msgPointer + 8);
	Memory::Write_U32(msg.data2, msgPointer + 12);
	GPR(3) = 1; // has message
	NPC = LR;
}

void OSGetCallArgs()
{
	u32 argcPointer = GPR(3);
	if (argcPointer != 0)
		Memory::Write_U32(2, argcPointer);
	u32 argvPointer = GPR(4);
	if (argvPointer != 0)
		memcpy(Memory::GetPointer(argvPointer), "TEST4", 5);
	NPC = LR;
}

void OSGetForegroundBucket()
{
	GPR(3) = 0x800dead0;
	NPC = LR;
}

void exit()
{
	PowerPC::Pause();
	NPC = LR;
}

void OSThread::DumpAttributes()
{
	WARN_LOG(BOOT, "OSThread nativePtr=%x entry=%x argc=%x argv=%x stack=%x stack_size=%x priority=%x attr=%x",
		nativePtr, entry, argc, argv, stack, stack_size, priority, (u32) attr);
}

void DumpArgsAndReturn()
{
	WARN_LOG(BOOT, "calling %s: r3=%x r4=%x r5=%x r6=%x r7=%x r8=%x r9=%x",
		HLE::GetFunctionNameByIndex(HLE::GetFunctionIndex(PC)),
		GPR(3), GPR(4), GPR(5), GPR(6), GPR(7), GPR(8), GPR(9));
	NPC = LR;
}

void OSContext::Save()
{
	memcpy(this, &PowerPC::ppcState, sizeof(OSContext));
}

void OSContext::Restore()
{
	// some stuff shouldn't be restored directly
	u32 old_pc = PC;
	u32 old_npc = NPC;
	int old_downcount = PowerPC::ppcState.downcount;
	memcpy(&PowerPC::ppcState, this, sizeof(OSContext));
	PC = old_pc;
	NPC = pc;
	PowerPC::ppcState.downcount = old_downcount;
}

void Scheduler::Reschedule()
{
}

void Reset()
{
	heapPtr = 0x83000000;
	hardcodedMessageIndex = 0;
}

};