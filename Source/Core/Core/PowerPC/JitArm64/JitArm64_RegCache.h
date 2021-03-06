// copyright 2014 dolphin emulator project
// licensed under gplv2
// refer to the license.txt file included.

#pragma once

#include <vector>

#include "Common/Arm64Emitter.h"
#include "Core/PowerPC/Gekko.h"
#include "Core/PowerPC/PPCAnalyst.h"

// Dedicated host registers
// X29 = ppcState pointer
using namespace Arm64Gen;

enum RegType
{
	REG_NOTLOADED = 0,
	REG_REG, // Reg type is register
	REG_IMM, // Reg is really a IMM
	REG_AWAY, // Reg is away
};
enum RegLocation
{
	REG_LOW = 0,
	REG_HIGH,
};

enum FlushMode
{
	// Flushes all registers, no exceptions
	FLUSH_ALL = 0,
	// Flushes registers in a conditional branch
	// Doesn't wipe the state of the registers from the cache
	FLUSH_MAINTAIN_STATE,
	// Flushes only the required registers for an interpreter call
	FLUSH_INTERPRETER,
};

class OpArg
{
public:
	OpArg()
	{
		m_type = REG_NOTLOADED;
		m_reg = INVALID_REG;
		m_value = 0;
	}

	RegType GetType()
	{
		return m_type;
	}

	ARM64Reg GetReg()
	{
		return m_reg;
	}
	ARM64Reg GetAwayReg()
	{
		return m_away_reg;
	}
	RegLocation GetAwayLocation()
	{
		return m_away_location;
	}
	u32 GetImm()
	{
		return m_value;
	}
	void LoadToReg(ARM64Reg reg)
	{
		m_type = REG_REG;
		m_reg = reg;
	}
	void LoadToAway(ARM64Reg reg, RegLocation location)
	{
		m_type = REG_AWAY;
		m_reg = INVALID_REG;
		m_away_reg = reg;
		m_away_location = location;
	}
	void LoadAwayToReg(ARM64Reg reg)
	{
		// We are still an away type
		// We just are also in another register
		m_reg = reg;
	}
	void LoadToImm(u32 imm)
	{
		m_type = REG_IMM;
		m_value = imm;
	}
	void Flush()
	{
		m_type = REG_NOTLOADED;
	}

private:
	// For REG_REG
	RegType m_type; // store type
	ARM64Reg m_reg; // host register we are in

	// For REG_AWAY
	// Host register that we are away in
	// This is a 64bit register
	ARM64Reg m_away_reg;
	RegLocation m_away_location;

	// For REG_IMM
	u32 m_value; // IMM value
};

class HostReg
{
public:
	HostReg() : m_reg(INVALID_REG), m_locked(false) {}
	HostReg(ARM64Reg reg) : m_reg(reg), m_locked(false) {}
	bool IsLocked(void) { return m_locked; }
	void Lock(void) { m_locked = true; }
	void Unlock(void) { m_locked = false; }
	ARM64Reg GetReg(void) { return m_reg; }

	bool operator==(const ARM64Reg& reg)
	{
		return reg == m_reg;
	}

private:
	ARM64Reg m_reg;
	bool m_locked;
};

class Arm64RegCache
{
public:
	Arm64RegCache(void) : m_emit(nullptr), m_reg_stats(nullptr) {};
	virtual ~Arm64RegCache() {};

	void Init(ARM64XEmitter *emitter);

	virtual void Start(PPCAnalyst::BlockRegStats &stats) {}

	// Flushes the register cache in different ways depending on the mode
	virtual void Flush(FlushMode mode, PPCAnalyst::CodeOp* op) = 0;

	// Returns a guest register inside of a host register
	// Will dump an immediate to the host register as well
	virtual ARM64Reg R(u32 reg) = 0;

	// Returns a temporary register for use
	// Requires unlocking after done
	ARM64Reg GetReg(void);

	// Locks a register so a cache cannot use it
	// Useful for function calls
	template<typename T = ARM64Reg, typename... Args>
	void Lock(Args... args)
	{
		for (T reg : {args...})
		{
			LockRegister(reg);
		}
	}

	// Unlocks a locked register
	// Unlocks registers locked with both GetReg and LockRegister
	template<typename T = ARM64Reg, typename... Args>
	void Unlock(Args... args)
	{
		for (T reg : {args...})
		{
			UnlockRegister(reg);
		}
	}

protected:
	// Get the order of the host registers
	virtual void GetAllocationOrder(void) = 0;

	// Lock a register
	void LockRegister(ARM64Reg host_reg);

	// Unlock a register
	void UnlockRegister(ARM64Reg host_reg);

	// Code emitter
	ARM64XEmitter *m_emit;

	// Host side registers that hold the host registers in order of use
	std::vector<HostReg> m_host_registers;

	// Register stats for the current block
	PPCAnalyst::BlockRegStats *m_reg_stats;
};

class Arm64GPRCache : public Arm64RegCache
{
public:
	~Arm64GPRCache() {}

	void Start(PPCAnalyst::BlockRegStats &stats);

	// Flushes the register cache in different ways depending on the mode
	void Flush(FlushMode mode, PPCAnalyst::CodeOp* op = nullptr);

	// Returns a guest register inside of a host register
	// Will dump an immediate to the host register as well
	ARM64Reg R(u32 preg);

	// Set a register to an immediate
	void SetImmediate(u32 reg, u32 imm) { m_guest_registers[reg].LoadToImm(imm); }

	// Returns if a register is set as an immediate
	bool IsImm(u32 reg) { return m_guest_registers[reg].GetType() == REG_IMM; }

	// Gets the immediate that a register is set to
	u32 GetImm(u32 reg) { return m_guest_registers[reg].GetImm(); }

protected:
	// Get the order of the host registers
	void GetAllocationOrder(void);

	// Our guest GPRs
	// PowerPC has 32 GPRs
	OpArg m_guest_registers[32];

private:
	bool IsCalleeSaved(ARM64Reg reg);
};

class Arm64FPRCache : public Arm64RegCache
{
public:
	~Arm64FPRCache() {}
	// Flushes the register cache in different ways depending on the mode
	void Flush(FlushMode mode, PPCAnalyst::CodeOp* op = nullptr);

	// Returns a guest register inside of a host register
	// Will dump an immediate to the host register as well
	ARM64Reg R(u32 preg);

protected:
	// Get the order of the host registers
	void GetAllocationOrder(void);

	// Our guest FPRs
	// Gekko has 32 paired registers(32x2)
	OpArg m_guest_registers[32][2];
};
