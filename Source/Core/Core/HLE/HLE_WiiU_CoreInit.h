// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include "Common/CommonTypes.h"

namespace HLE_WiiU_CoreInit
{
	class OSContext
	{
	public:
		// this is just a micro version of PowerPC state - members must match; we just directly memcpy the two structs
		u32 gpr[32];    // General purpose registers. r1 = stack pointer.

		u32 pc;     // program counter
		u32 npc;

		// Optimized CR implementation. Instead of storing CR in its PowerPC format
		// (4 bit value, SO/EQ/LT/GT), we store instead a 64 bit value for each of
		// the 8 CR register parts. This 64 bit value follows this format:
		//   - SO iff. bit 61 is set
		//   - EQ iff. lower 32 bits == 0
		//   - GT iff. (s64)cr_val > 0
		//   - LT iff. bit 62 is set
		//
		// This has the interesting property that sign-extending the result of an
		// operation from 32 to 64 bits results in a 64 bit value that works as a
		// CR value. Checking each part of CR is also fast, as it is equivalent to
		// testing one bit or the low 32 bit part of a register. And CR can still
		// be manipulated bit by bit fairly easily.
		u64 cr_val[8];

		u32 msr;    // machine specific register
		u32 fpscr;  // floating point flags/status bits

		// Exception management.
		volatile u32 Exceptions;

		// Downcount for determining when we need to do timing
		// This isn't quite the right location for it, but it is here to accelerate the ARM JIT
		// This variable should be inside of the CoreTiming namespace if we wanted to be correct.
		int downcount;

		// XER, reformatted into byte fields for easier access.
		u8 xer_ca;
		u8 xer_so_ov; // format: (SO << 1) | OV
		// The Broadway CPU implements bits 16-23 of the XER register... even though it doesn't support lscbx
		u16 xer_stringctrl;

#if _M_X86_64
		// This member exists for the purpose of an assertion in x86 JitBase.cpp
		// that its offset <= 0x100.  To minimize code size on x86, we want as much
		// useful stuff in the one-byte offset range as possible - which is why ps
		// is sitting down here.  It currently doesn't make a difference on other
		// supported architectures.
		std::tuple<> above_fits_in_first_0x100;
#endif

		// The paired singles are strange : PS0 is stored in the full 64 bits of each FPR
		// but ps calculations are only done in 32-bit precision, and PS1 is only 32 bits.
		// Since we want to use SIMD, SSE2 is the only viable alternative - 2x double.
		GC_ALIGNED16(u64 ps[32][2]);

		u32 sr[16];  // Segment registers.

		// special purpose registers - controls quantizers, DMA, and lots of other misc extensions.
		// also for power management, but we don't care about that.
		u32 spr[1024];

		void Save();
		void Restore();
	};

	class OSThread
	{
	public:
		u32 nativePtr;
		u32 entry;
		u32 argc;
		u32 argv;
		u32 stack;
		u32 stack_size;
		u32 priority;
		u16 attr;
		std::string name;

		u32 tid;
		OSContext threadContext;
		void DumpAttributes();
	};

	class Scheduler {
	public:
		void Reschedule();
	};

	extern u32 next_tid;

	void COS_Report();
	void OSCreateThread();
	void OSResumeThread();
	void OSJoinThread();
	void OSSetThreadName();
	void HeapAllocStub();
	void HeapAllocStubWithImplicitHeap();
	void HeapFreeStub();
	void OSGetMemBound();
	void OSReceiveMessage();
	void OSGetCallArgs();
	void OSGetForegroundBucket();
	void exit();
	void DumpArgsAndReturn();

	void Reset();
};