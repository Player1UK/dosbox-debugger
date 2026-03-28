// SPDX-FileCopyrightText:  2002-2025 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_DEBUG_H
#define DOSBOX_DEBUG_H

#include "dosbox.h"

#if C_DEBUGGER

#include "dos/programs.h"

class DEBUG final : public Program {
public:
	DEBUG( );
	~DEBUG( ) override;

	bool IsActive( ) const;
	void Run( ) override;

	char filename[128] = "";
private:
	bool active;
};
extern DEBUG *pDebugcom;

#endif // C_DEBUGGER

#endif // DOSBOX_DEBUG_H