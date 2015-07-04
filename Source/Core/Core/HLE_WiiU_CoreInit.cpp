#include "Core/HLE/HLE.h"
#include "Core/HLE/HLE_WiiU_CoreInit.h"

namespace HLE_WiiU_CoreInit {
//#define H(name) {##name, HLE_#name, HLE_HOOK_REPLACE, HLE_TYPE_GENERIC}
#define H(name)
	static const SPatch CoreInitPatches[] = {
		// libc basics
		H(memcpy),
		H(memmove),
		H(memset),
		H(exit),
		// OS logging functions
		H(COSWarn),
		H(COSError),
		//H(OSPanic),
		H(OSLogReport),
		H(OSConsoleWrite),
		H(OSVReport),
		//H(OSReport),
		// OS get info functions
	}
}