// SPDX-FileCopyrightText:  2002-2025 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_DEBUGGER_H
#define DOSBOX_DEBUGGER_H

#include "dosbox.h"

#if C_DEBUGGER
#include "config/config.h"
#include "debugtypes.h"
#include "hardware/memory.h"

void DEBUG_AddConfigSection( const ConfigPtr &conf );
void DEBUG_Init( );
void DEBUG_Destroy( );

void DEBUG_Close( );
void DBGUI_DrawScreen( );
void DEBUG_Enable( bool pressed );
void DEBUG_CheckExecuteBreakpoint( const ADDRESS_PAIR & );
bool DEBUG_ExitNormalLoop( void );
Bitu DEBUG_EnableDebugger( );

bool DEBUG_Breakpoint( );
bool DEBUG_IntBreakpoint( uint8_t intNum );

extern void DEBUG_NewInstruction( );

void LOG_StartUp( );
void LOG_Init( );
void LOG_Destroy( );

extern Bitu cycle_count;
extern Bitu debugCallback;
#endif // C_DEBUGGER

#if C_DEBUGGER && C_HEAVY_DEBUGGER
bool DEBUG_HeavyIsBreakpoint( );
void DEBUG_HeavyWriteLogInstruction( );

template <typename T>
void DEBUG_UpdateMemoryReadBreakpoints( const PhysPt addr );
#else
template <typename T>
constexpr void DEBUG_UpdateMemoryReadBreakpoints( const PhysPt ) {
	// no-op
}
#endif // C_DEBUGGER && C_HEAVY_DEBUGGER

#endif // DOSBOX_DEBUGGER_H
