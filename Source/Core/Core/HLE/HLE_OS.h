// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include "Common/CommonTypes.h"

namespace HLE_OS
{
	void HLE_GeneralDebugPrint();
	void HLE_write_console();
	void HLE_OSPanic();
	void GetStringVA(std::string& _rOutBuffer, u32 strReg);
}
