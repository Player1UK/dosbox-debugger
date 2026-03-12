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
#include "hardware/pic.h"
#include "shell/shell.h"

extern uint32_t GetAddress(uint16_t, uint32_t);
extern bool ParseCommand(char*);
extern void SetCodeWinStart();

extern bool exitLoop;

extern DBGBlock dbg;
extern bool debugging;

extern SCodeViewData codeViewData;

extern bool showPrintable;

uint16_t dataSeg[NUM_WIN_DATA] = { 0 };//, 0, 0, 0 };
uint32_t dataOfs[NUM_WIN_DATA] = { 0 };//, 0, 0, 0 };

bool skipFirstInstruction = false;

// History stuff
#define MAX_HIST_BUFFER 50
static std::list<std::string> histBuff = {};
static auto histBuffPos                = histBuff.end();

const uint32_t MAXSIZE_EIPARRAY = 150U;
uint32_t indexEIParray          = 0;
uint32_t EIParray[MAXSIZE_EIPARRAY];

static bool StepOver()
{
	exitLoop     = false;
	PhysPt start = GetAddress(SegValue(cs), reg_eip);
	char dline[200];
	Bitu size;
	size = DasmI386(dline, start, reg_eip, cpu.code.big);

	if (strstr(dline, "call") || strstr(dline, "int") ||
	    strstr(dline, "loop") || strstr(dline, "rep")) {
		// Don't add a temporary breakpoint if there's already one here
		if (!CBreakpoint::FindPhysBreakpoint(SegValue(cs), reg_eip + size, true)) {
			CBreakpoint::AddBreakpoint(SegValue(cs), reg_eip + size, true);
		}
		debugging = false;
		return true;
	}
	return false;
}

static void PopulateEIParray()
{
	PhysPt start                   = GetAddress(codeViewData.useCS, 0);
	Bitu size                      = 0;
	indexEIParray                  = 0;
	EIParray[MAXSIZE_EIPARRAY - 1] = -1;
	for (uint32_t newEIP = 0; newEIP < codeViewData.useEIP;
	     newEIP += size, start += size) {
		EIParray[indexEIParray] = newEIP;
		if (++indexEIParray > MAXSIZE_EIPARRAY) {
			indexEIParray = 0U;
		}
		char dline[200];
		size = DasmI386(dline, start, newEIP, cpu.code.big);
	}
}

static bool UseExistingEIP(uint32_t gap)
{
	if (indexEIParray >= MAXSIZE_EIPARRAY) {
		return false;
	}
	auto indexEIParray_original = indexEIParray;
	if (indexEIParray >= gap) {
		indexEIParray -= gap;
	} else if (EIParray[MAXSIZE_EIPARRAY - 1] < EIParray[0]) {
		indexEIParray = MAXSIZE_EIPARRAY - (gap - indexEIParray);
	} else {
		indexEIParray = 0U;
	}
	if (indexEIParray != indexEIParray_original && EIParray[indexEIParray] < codeViewData.useEIP) {
		codeViewData.useEIP = EIParray[indexEIParray];
		return true;
	}
	return false;
}

static void ClearInputLine(void)
{
	codeViewData.inputStr[0] = 0;
	codeViewData.inputPos    = 0;
}

static int32_t DEBUG_Run(int32_t amount, bool quickexit)
{
	skipFirstInstruction = true;
	CPU_CycleLeft += CPU_Cycles - amount;
	CPU_Cycles = amount;
	int32_t ret = (*cpudecoder)();
	dbg.update_win[WIN_CODE] = true;
	dbg.update_win[WIN_REG] = true;
	if (quickexit) {
		SetCodeWinStart();
	} else {
		// ensure all breakpoints are activated
		CBreakpoint::ActivateBreakpoints();

		const auto graphics_window = GFX_GetWindow();
		SDL_RaiseWindow(graphics_window);

		DOSBOX_SetNormalLoop();
	}
	return ret;
}

uint32_t DEBUG_ProcessKey( SDL_KeyboardEvent key )
{
	Bits ret = 0;

	switch( key.key ) {
	case SDLK_C: // ALT - C: CS:IP
		if( !( key.mod & SDL_KMOD_ALT ) )
			break;
		dataSeg[dbg.active_win_data] = SegValue(cs);
		if (cpu.pmode && !(reg_flags & FLAG_VM)) {
			dataOfs[dbg.active_win_data] = reg_eip;
		} else {
			dataOfs[dbg.active_win_data] = reg_ip;
		}
		dbg.update_win[WIN_DATA] = true;
		break;
	case SDLK_D: // ALT - D: DS:SI
		if( !( key.mod & SDL_KMOD_ALT ) )
			break;
		dataSeg[dbg.active_win_data] = SegValue(ds);
		if (cpu.pmode && !(reg_flags & FLAG_VM)) {
			dataOfs[dbg.active_win_data] = reg_esi;
		} else {
			dataOfs[dbg.active_win_data] = reg_si;
		}
		dbg.update_win[WIN_DATA] = true;
		break;
	case SDLK_E: // ALT - E: es:di
		if( !( key.mod & SDL_KMOD_ALT ) )
			break;
		dataSeg[dbg.active_win_data] = SegValue(es);
		if (cpu.pmode && !(reg_flags & FLAG_VM)) {
			dataOfs[dbg.active_win_data] = reg_edi;
		} else {
			dataOfs[dbg.active_win_data] = reg_di;
		}
		dbg.update_win[WIN_DATA] = true;
		break;
	case SDLK_X: // ALT - X: ds:dx
		if( !( key.mod & SDL_KMOD_ALT ) )
			break;
		dataSeg[dbg.active_win_data] = SegValue(ds);
		if (cpu.pmode && !(reg_flags & FLAG_VM)) {
			dataOfs[dbg.active_win_data] = reg_edx;
		} else {
			dataOfs[dbg.active_win_data] = reg_dx;
		}
		dbg.update_win[WIN_DATA] = true;
		break;
	case SDLK_B: // ALT -B: es:bx
		if( !( key.mod & SDL_KMOD_ALT ) )
			break;
		dataSeg[dbg.active_win_data] = SegValue(es);
		if (cpu.pmode && !(reg_flags & FLAG_VM)) {
			dataOfs[dbg.active_win_data] = reg_ebx;
		} else {
			dataOfs[dbg.active_win_data] = reg_bx;
		}
		dbg.update_win[WIN_DATA] = true;
		break;
	case SDLK_S: // ALT - S: ss:sp
		if( !( key.mod & SDL_KMOD_ALT ) )
			break;
		dataSeg[dbg.active_win_data] = SegValue(ss);
		if (cpu.pmode && !(reg_flags & FLAG_VM)) {
			dataOfs[dbg.active_win_data] = reg_esp;
		} else {
			dataOfs[dbg.active_win_data] = reg_sp;
		}
		dbg.update_win[WIN_DATA] = true;
		break;
	case SDLK_PAGEUP:
		if( key.mod & SDL_KMOD_SHIFT ) {
			if( dataOfs[dbg.active_win_data] >
				static_cast<uint32_t>(
					dbg.rows_data[dbg.active_win_data] * 16 ) ) {
				dataOfs[dbg.active_win_data] -=
					dbg.rows_data[dbg.active_win_data] *
					16;
			} else {
				dataOfs[dbg.active_win_data] = 0;
			}
		} else if( key.mod & SDL_KMOD_CTRL ) {
			if( dbg.rows_output > 2 ) {
				--dbg.rows_output;
			}
		} else {
			if( dataOfs[dbg.active_win_data] > 16 ) {
				dataOfs[dbg.active_win_data] -= 16;
			} else {
				dataOfs[dbg.active_win_data] = 0;
			}
		}
		dbg.update_win[WIN_DATA] = true;
		break;
	case SDLK_PAGEDOWN:
		if( key.mod & SDL_KMOD_SHIFT ) {
			dataOfs[dbg.active_win_data] +=
				dbg.rows_data[dbg.active_win_data] * 16;
		} else if( key.mod & SDL_KMOD_CTRL ) {
			if( dbg.rows_data[0U] > 1 ) {
				++dbg.rows_output;
			}
		} else
			dataOfs[dbg.active_win_data] += 16;
		dbg.update_win[WIN_DATA] = true;
		break;
	case SDLK_UP:
		if( key.mod & SDL_KMOD_SHIFT ) {
			if( codeViewData.cursorPos == 0 ) {
				if( UseExistingEIP( dbg.rows_code ) ) {
					break;
				}
				PopulateEIParray( );
				UseExistingEIP( dbg.rows_code );
			} else {
				codeViewData.cursorPos = 0;
			}
		} else if( key.mod & SDL_KMOD_CTRL ) {
			if( dbg.rows_output > 2 ) {
				--dbg.rows_output;
				++dbg.rows_code;
			}
		} else {
			if( codeViewData.cursorPos > 0 ) {
				codeViewData.cursorPos--;
			} else if( codeViewData.useEIP ) {
				if( UseExistingEIP( 1U ) ) {
					break;
				}
				PopulateEIParray( );
				UseExistingEIP( 1U );
			}
		}
		dbg.update_win[WIN_CODE] = true;
		break;
	case SDLK_DOWN:
		if( key.mod & SDL_KMOD_SHIFT ) {
			if( codeViewData.cursorPos < dbg.rows_code - 1 ) {
				codeViewData.cursorPos = dbg.rows_code - 1;
			} else {
				codeViewData.useEIP = codeViewData.useEIPlast;
			}
			indexEIParray = MAXSIZE_EIPARRAY;
		} else if( key.mod & SDL_KMOD_CTRL ) {
			if( dbg.rows_code > 1 ) {
				--dbg.rows_code;
				++dbg.rows_output;
			}
		} else {
			if( codeViewData.cursorPos < dbg.rows_code - 1 ) {
				codeViewData.cursorPos++;
			} else {
				codeViewData.useEIP += codeViewData.firstInstSize;
			}
			indexEIParray = MAXSIZE_EIPARRAY;
		}
		dbg.update_win[WIN_CODE] = true;
		break;
	case SDLK_TAB:
		if( key.mod & SDL_KMOD_SHIFT ) {
			if( dbg.active_win_data-- == 0U ) {
				dbg.active_win_data = NUM_WIN_DATA - 1;
			}
		} else {
			if( ++dbg.active_win_data >= NUM_WIN_DATA ) {
				dbg.active_win_data = 0U;
			}
		}
		break;
	case SDLK_HOME: // Home: scroll log page up
		if( key.mod & SDL_KMOD_SHIFT )
			DEBUG_RefreshPage( -dbg.rows_output + 1 );
		else
			DEBUG_RefreshPage(-1);
		break;
	case SDLK_END: // End: scroll log page down
		if( key.mod & SDL_KMOD_SHIFT )
			DEBUG_RefreshPage( dbg.rows_output - 1 );
		else
			DEBUG_RefreshPage(1);
		break;
	case SDLK_INSERT: // Insert: toggle insert/overwrite
		codeViewData.ovrMode = !codeViewData.ovrMode;
		break;
	/*case SDLK_LEFT: // move to the left in command line
		if (codeViewData.inputPos > 0) {
			codeViewData.inputPos--;
		}
		break;
	case SDLK_RIGHT: // move to the right in command line
		if (codeViewData.inputStr[codeViewData.inputPos]) {
			codeViewData.inputPos++;
		}
		break;*/
	case SDLK_F6: // previous command (f1-f4 generate rubbish at my place)
	case SDLK_F3: // previous command
		if (histBuffPos == histBuff.begin()) {
			break;
		}
		if (histBuffPos == histBuff.end()) {
			// copy inputStr to suspInputStr so we can restore it
			safe_strcpy(codeViewData.suspInputStr,
				        codeViewData.inputStr);
		}
		safe_strcpy(codeViewData.inputStr, (--histBuffPos)->c_str());
		codeViewData.inputPos = safe_strlen(codeViewData.inputStr);
		break;
	case SDLK_F7: // next command (f1-f4 generate rubbish at my place)
	case SDLK_F4: // next command
		if (histBuffPos == histBuff.end()) {
			break;
		}
		if (++histBuffPos != histBuff.end()) {
			safe_strcpy(codeViewData.inputStr,
				        histBuffPos->c_str());
		} else {
			// copy suspInputStr back into inputStr
			safe_strcpy(codeViewData.inputStr,
				        codeViewData.suspInputStr);
		}
		codeViewData.inputPos = safe_strlen(codeViewData.inputStr);
		break;
	case SDLK_F5: // Run Program
		debugging = false;
		// Redraw screen to show "(Running)" before entering normal loop
		DBGUI_NewFrame();
		DEBUG_DrawScreen();
		DBGUI_Render();
		ret = DEBUG_Run(1, false);
		break;
	case SDLK_F8: // Toggle printable characters
		showPrintable = !showPrintable;
		break;
	case SDLK_F9: // Set/Remove Breakpoint
		if (CBreakpoint::IsBreakpoint(codeViewData.cursorSeg,
			                            codeViewData.cursorOfs)) {
			if (CBreakpoint::DeleteBreakpoint(codeViewData.cursorSeg,
				                                codeViewData.cursorOfs)) {
				DEBUG_ShowMsg("DEBUG: Breakpoint deletion success.\n");
			} else {
				DEBUG_ShowMsg("DEBUG: Failed to delete breakpoint.\n");
			}
		} else {
			CBreakpoint::AddBreakpoint(codeViewData.cursorSeg,
				                        codeViewData.cursorOfs,
				                        false);
			DEBUG_ShowMsg("DEBUG: Set breakpoint at %04X:%04X\n",
				            codeViewData.cursorSeg,
				            codeViewData.cursorOfs);
		}
		break;
	case SDLK_F10: // Step over inst
		if (StepOver()) {
			ret = DEBUG_Run(1, false);
			break;
		}
		// If we aren't stepping over something, do a normal step.
		[[fallthrough]];
	case SDLK_F11: // trace into
		exitLoop = false;
		ret      = DEBUG_Run(1, true);
		break;
	case SDLK_RETURN: // Parse typed Command
		codeViewData.inputStr[MAXCMDLEN] = '\0';
		if (ParseCommand(codeViewData.inputStr)) {
			char* cmd = ltrim(codeViewData.inputStr);
			if (histBuff.empty() || *--histBuff.end() != cmd) {
				histBuff.emplace_back(cmd);
			}
			if (histBuff.size() > MAX_HIST_BUFFER) {
				histBuff.pop_front();
			}
			histBuffPos = histBuff.end();
			ClearInputLine();
		} else {
			codeViewData.inputPos = safe_strlen(
				    codeViewData.inputStr);
		}
		break;
	/*case SDLK_BACKSPACE: // backspace
	case 0x7f: // backspace in some terminal emulators (linux)
	case 0x08: // delete
		if (codeViewData.inputPos == 0) {
			break;
		}
		codeViewData.inputPos--;
		[[fallthrough]];
	case SDLK_DELETE: // delete character
		if ((codeViewData.inputPos < 0) ||
			(codeViewData.inputPos >= MAXCMDLEN)) {
			break;
		}
		if (codeViewData.inputStr[codeViewData.inputPos] != 0) {
			codeViewData.inputStr[MAXCMDLEN] = '\0';
			for (char* p =
				            &codeViewData.inputStr[codeViewData.inputPos];
				    (*p = *(p + 1));
				    p++) {
			}
		}
		break;*/
	default:
		break;
	}
	if (ret < 0) {
		return ret;
	}
	if (ret > 0) {
		if (ret >= CB_MAX) {
			ret = 0;
		} else {
			ret = (*Callback_Handlers[ret])();
		}
		if (ret) {
			exitLoop   = true;
			CPU_Cycles = CPU_CycleLeft = 0;
			return ret;
		}
	}
	ret = 0;
	return ret;
}
#endif // C_DEBUGGER