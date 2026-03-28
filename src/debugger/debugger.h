// SPDX-FileCopyrightText:  2002-2025 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_DEBUGGER_H
#define DOSBOX_DEBUGGER_H

#include "dosbox.h"

#if C_DEBUGGER

#include "config/config.h"
#include "hardware/memory.h"

void DEBUG_AddConfigSection(const ConfigPtr& conf);
void DEBUG_Init();
void DEBUG_Destroy();

void DEBUG_Close();
void DBGUI_DrawScreen();
void DEBUG_Enable(bool pressed);
void DEBUG_CheckExecuteBreakpoint(uint16_t seg, uint32_t off);
bool DEBUG_ExitNormalLoop(void);
Bitu DEBUG_EnableDebugger();

bool DEBUG_Breakpoint();
bool DEBUG_IntBreakpoint(uint8_t intNum);

void LOG_StartUp();
void LOG_Init();
void LOG_Destroy();

extern Bitu cycle_count;
extern Bitu debugCallback;

/********************/
/* DebugVar   stuff */
/********************/
class CDebugVar {
public:
	CDebugVar(const char* vname, PhysPt address);

	char* GetName(void)
	{
		return name;
	}
	PhysPt GetAdr(void)
	{
		return adr;
	}
	void SetValue(bool has, uint16_t val)
	{
		hasvalue = has;
		value    = val;
	}
	uint16_t GetValue(void)
	{
		return value;
	}
	bool HasValue(void)
	{
		return hasvalue;
	}

private:
	const PhysPt adr = 0;
	char name[16]    = {};
	bool hasvalue    = false;
	uint16_t value   = 0;

public:
	static void InsertVariable(char* name, PhysPt adr);
	static CDebugVar* FindVar(PhysPt adr);
	static void DeleteAll();
	static bool SaveVars(char* name);
	static bool LoadVars(char* name);
};
#endif // C_DEBUGGER

#if C_DEBUGGER && C_HEAVY_DEBUGGER
bool DEBUG_HeavyIsBreakpoint();
void DEBUG_HeavyWriteLogInstruction();

template <typename T>
void DEBUG_UpdateMemoryReadBreakpoints(const PhysPt addr);
#else
template <typename T>
constexpr void DEBUG_UpdateMemoryReadBreakpoints(const PhysPt)
{
	// no-op
}
#endif // C_DEBUGGER && C_HEAVY_DEBUGGER

#endif // DOSBOX_DEBUGGER_H
