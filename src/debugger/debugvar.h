// SPDX-FileCopyrightText:  2002-2025 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_DEBUGVAR_H
#define DOSBOX_DEBUGVAR_H

#include "dosbox.h"

#if C_DEBUGGER
#include "hardware/memory.h"

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

#endif // DOSBOX_DEBUGVAR_H
