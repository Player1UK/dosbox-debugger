// SPDX-FileCopyrightText:  2021-2026 The DOSBox Staging Team
// SPDX-FileCopyrightText:  2002-2021 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dosbox.h"

#if C_DEBUGGER

#include "cbreakpoint.h"
#include "cpu/paging.h"
#include "debugger.h"
#include "debugger_inc.h"
#include "gui/common.h"
#include "gui/mapper.h"
#include "hardware/input/keyboard.h"
#include "hardware/pic.h"
#include "hardware/timer.h"
#include "shell/shell.h"

#include <imgui.h>

extern uint32_t DEBUG_CheckKeys(void);
extern void SetCodeWinStart();

DBGBlock dbg   = {};
bool debugging = false;
bool exitLoop  = false;

Bitu DEBUG_Loop(void)
{
	// TODO Disable sound
	GFX_PollAndHandleEvents();

	// Start a new ImGui frame
	DBGUI_NewFrame();

	// Draw debugger windows
	DEBUG_DrawScreen();

	// Render ImGui
	DBGUI_Render();

	// Interrupt started ? - then skip it
	uint16_t oldCS  = SegValue(cs);
	uint32_t oldEIP = reg_eip;
	PIC_runIRQs();
	Delay(1);
	if ((oldCS != SegValue(cs)) || (oldEIP != reg_eip)) {
		CBreakpoint::AddBreakpoint(oldCS, oldEIP, true);
		CBreakpoint::ActivateBreakpointsExceptAt(SegPhys(cs) + reg_eip);
		debugging = false;
		DOSBOX_SetNormalLoop();
		return 0;
	}
	return DEBUG_CheckKeys();
}

void DEBUG_Enable(bool pressed)
{
	if (!pressed) {
		return;
	}

	// Maybe construct the debugger's UI
	static bool was_ui_started = false;
	if (!was_ui_started) {
		DBGUI_StartUp();
		was_ui_started = DBGUI_IsInitialized();
	} else {
		//SDL_ShowWindow(pdc_window);
	}

	// The debugger is run in release mode so cannot use asserts
	if (!was_ui_started) {
		LOG_ERR("DEBUG: Failed to start up the debug window");
		return;
	}

	// Defocus the graphical UI and bring the debugger UI into focus
	GFX_LosingFocus();
	SDL_RaiseWindow(dbg.win_main);
	SetCodeWinStart();

	// Maybe show help for the first time in the debugger
	static bool was_help_shown = false;
	if (!was_help_shown) {
		DEBUG_ShowMsg("           TYPE ? or HELP (+ENTER) TO GET AN OVERVIEW OF ALL COMMANDS           \n");
		was_help_shown = true;
	}

	// Start the debugging loops
	debugging = true;
	DOSBOX_SetLoop(&DEBUG_Loop);

	KEYBOARD_ClrBuffer();
}

void DEBUG_Close() {
	//SDL_HideWindow(pdc_window);
	debugging = false;
	DOSBOX_SetNormalLoop();
}

class DEBUG;
DEBUG* pDebugcom = nullptr;

// DEBUG.COM stuff
class DEBUG final : public Program {
public:
	DEBUG() : active(false)
	{
		pDebugcom = this;
	}

	~DEBUG() override
	{
		pDebugcom = nullptr;
	}

	bool IsActive() const
	{
		return active;
	}

	void Run() override
	{
		if (cmd->FindExist("/NOMOUSE", false)) {
			real_writed(0, 0x33 << 2, 0);
			return;
		}

		uint16_t commandNr = 1;
		if (!cmd->FindCommand(commandNr++, temp_line)) {
			return;
		}
		// Get filename
		char filename[128];
		safe_strcpy(filename, temp_line.c_str());
		// Setup commandline
		char args[256 + 1];
		args[0]    = 0;
		bool found = cmd->FindCommand(commandNr++, temp_line);
		while (found) {
			if (safe_strlen(args) + temp_line.length() + 1 > 256) {
				break;
			}
			strcat(args, temp_line.c_str());
			found = cmd->FindCommand(commandNr++, temp_line);
			if (found) {
				strcat(args, " ");
			}
		}
		// Start new shell and execute prog
		active = true;
		// Save cpu state....
		uint16_t oldcs  = SegValue(cs);
		uint32_t oldeip = reg_eip;
		uint16_t oldss  = SegValue(ss);
		uint32_t oldesp = reg_esp;

		// Start shell
		DOS_Shell shell;
		if (!shell.ExecuteProgram(filename, args)) {
			WriteOut(MSG_Get("PROGRAM_EXECUTABLE_MISSING"), filename);
		}

		// set old reg values
		SegSet16(ss, oldss);
		reg_esp = oldesp;
		SegSet16(cs, oldcs);
		reg_eip = oldeip;
	}

private:
	bool active;
};

void DEBUG_CheckExecuteBreakpoint(uint16_t seg, uint32_t off)
{
	if (pDebugcom && pDebugcom->IsActive()) {
		CBreakpoint::AddBreakpoint(seg, off, true);
		CBreakpoint::ActivateBreakpointsExceptAt(SegPhys(cs) + reg_eip);
		pDebugcom = nullptr;
	}
}

Bitu DEBUG_EnableDebugger()
{
	exitLoop = true;
	DEBUG_Enable(true);
	CPU_Cycles = CPU_CycleLeft = 0;
	return 0;
}

extern SCodeViewData codeViewData;

Bitu debugCallback;

void DEBUG_Init()
{
	// Add some keyhandlers
	MAPPER_AddHandler(DEBUG_Enable, SDL_SCANCODE_PAUSE, MMOD2, "debugger", "Debugger");

	// Reset code overview and input line
	codeViewData = {};

	// setup debug.com
	PROGRAMS_MakeFile("DEBUG.COM", ProgramCreate<DEBUG>);
	PROGRAMS_MakeFile("DBXDEBUG.COM", ProgramCreate<DEBUG>);

	// Setup callback
	debugCallback = CALLBACK_Allocate();

	CALLBACK_Setup(debugCallback, DEBUG_EnableDebugger, CB_RETF, "debugger");
}

void DEBUG_Destroy()
{
	CBreakpoint::DeleteAll();
	CDebugVar::DeleteAll();

	DBGUI_Shutdown();
}

void DEBUG_AddConfigSection(const ConfigPtr& conf)
{
	assert(conf);

	// TODO the [debug] section has no settings, so what's the point?
	conf->AddSection("debug");
}

bool DEBUG_ExitLoop(void)
{
	if (exitLoop) {
		exitLoop = false;
		return true;
	}
	return false;
}
#endif // C_DEBUGGER
