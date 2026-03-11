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

#include <imgui_impl_sdl3.h>

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

static std::deque<int> key_buffer;
// Event queue
std::queue<DebuggerInputEvent> debugger_event_queue = {};

int DBGUI_GetKey()
{
	// First check key buffer
	if (!key_buffer.empty()) {
		int key = key_buffer.front();
		key_buffer.pop_front();
		return key;
	}

	// Then check event queue
	while (!debugger_event_queue.empty()) {
		DebuggerInputEvent event = debugger_event_queue.front();
		debugger_event_queue.pop();

		// Process ImGui events
		ImGui_ImplSDL3_ProcessEvent(&event.ev);

		// Convert SDL events to key codes
		if (event.ev.type == SDL_EVENT_KEY_DOWN) {
			SDL_Keycode key = event.ev.key.key;
			SDL_Keymod mod = event.ev.key.mod;

			// Map SDL keys to our key constants
			switch (key) {
			case SDLK_UP: return (mod & SDL_KMOD_SHIFT ? DBGUI_KEY_SUP : DBGUI_KEY_UP);
			case SDLK_DOWN: return (mod & SDL_KMOD_SHIFT ? DBGUI_KEY_SDOWN : DBGUI_KEY_DOWN);
			case SDLK_LEFT: return DBGUI_KEY_LEFT;
			case SDLK_RIGHT: return DBGUI_KEY_RIGHT;
			case SDLK_PAGEUP: return (mod & SDL_KMOD_SHIFT ? DBGUI_KEY_SPREVIOUS : DBGUI_KEY_PPAGE);
			case SDLK_PAGEDOWN: return (mod & SDL_KMOD_SHIFT ? DBGUI_KEY_SNEXT : DBGUI_KEY_NPAGE);
			case SDLK_HOME: return (mod & SDL_KMOD_SHIFT ? DBGUI_KEY_SHOME : DBGUI_KEY_HOME);
			case SDLK_END: return (mod & SDL_KMOD_SHIFT ? DBGUI_KEY_SEND : DBGUI_KEY_END);
			case SDLK_BACKSPACE: return DBGUI_KEY_BACKSPACE;
			case SDLK_DELETE: return DBGUI_KEY_DC;
			case SDLK_INSERT: return DBGUI_KEY_IC;
			case SDLK_RETURN: return '\n';
			case SDLK_ESCAPE: return 27;
			case SDLK_TAB: return (mod & SDL_KMOD_SHIFT ? DBGUI_KEY_BTAB : '\t');
			case SDLK_F1: return DBGUI_KEY_F(1);
			case SDLK_F2: return DBGUI_KEY_F(2);
			case SDLK_F3: return DBGUI_KEY_F(3);
			case SDLK_F4: return DBGUI_KEY_F(4);
			case SDLK_F5: return DBGUI_KEY_F(5);
			case SDLK_F6: return DBGUI_KEY_F(6);
			case SDLK_F7: return DBGUI_KEY_F(7);
			case SDLK_F8: return DBGUI_KEY_F(8);
			case SDLK_F9: return DBGUI_KEY_F(9);
			case SDLK_F10: return DBGUI_KEY_F(10);
			case SDLK_F11: return DBGUI_KEY_F(11);
			case SDLK_F12: return DBGUI_KEY_F(12);
			default:
				// Handle printable characters
				if (key >= SDLK_SPACE && key <= SDLK_Z) {
					char c = static_cast<char>(key);
					if (mod & SDL_KMOD_SHIFT) {
						c = toupper(c);
					}
					return c;
				}
				if (key >= SDLK_0 && key <= SDLK_9) {
					return static_cast<int>(key);
				}
				break;
			}
#if defined(WIN32) && PDCURSES
			switch (key) {
			case PADENTER: key = 0x0A; break;
			case PADSLASH: key = '/'; break;
			case PADSTAR: key = '*'; break;
			case PADMINUS: key = '-'; break;
			case PADPLUS: key = '+'; break;
			case PADSTOP: key = KEY_DC; break;
			case PAD0: key = KEY_IC; break;
			case KEY_A1: key = KEY_HOME; break;
			case KEY_A2: key = KEY_UP; break;
			case KEY_A3: key = KEY_PPAGE; break;
			case KEY_B1: key = KEY_LEFT; break;
			case KEY_B3: key = KEY_RIGHT; break;
			case KEY_C1: key = KEY_END; break;
			case KEY_C2: key = KEY_DOWN; break;
			case KEY_C3: key = KEY_NPAGE; break;
			case ALT_C:
				if (ungetch('C') != ERR) {
					key = 27;
				}
				break;
			case ALT_D:
				if (ungetch('D') != ERR) {
					key = 27;
				}
				break;
			case ALT_E:
				if (ungetch('E') != ERR) {
					key = 27;
				}
				break;
			case ALT_X:
				if (ungetch('X') != ERR) {
					key = 27;
				}
				break;
			case ALT_B:
				if (ungetch('B') != ERR) {
					key = 27;
				}
				break;
			case ALT_S:
				if (ungetch('S') != ERR) {
					key = 27;
				}
				break;
			}
#endif
		}
		else if (event.ev.type == SDL_EVENT_TEXT_INPUT) {
			if (!event.text.empty()) {
				return static_cast<unsigned char>(event.text[0]);
			}
		}
	}

	return KEY_NONE;
}

void DBGUI_UngetKey(int key)
{
	key_buffer.push_front(key);
}

bool DBGUI_HasKey()
{
	return !key_buffer.empty() || !debugger_event_queue.empty();
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
	if (quickexit) {
		SetCodeWinStart();
	}
	else {
		// ensure all breakpoints are activated
		CBreakpoint::ActivateBreakpoints();

		const auto graphics_window = GFX_GetWindow();
		SDL_RaiseWindow(graphics_window);

		DOSBOX_SetNormalLoop();
	}
	return ret;
}

uint32_t DEBUG_CheckKeys(void)
{
	Bits ret       = 0;
	bool numberrun = false;
	int key        = DBGUI_GetKey();

	if (key >= '1' && key <= '5' && safe_strlen(codeViewData.inputStr) == 0) {
		const int32_t v[] = {5, 500, 1000, 5000, 10000};

		ret = DEBUG_Run(v[key - '1'], true);

		/* Setup variables so we end up at the proper ret processing */
		numberrun = true;
		key       = KEY_NONE;
	}

	if (key != KEY_NONE || numberrun) {
		switch (ascii_to_upper(key)) {
		case 27: // escape (a bit slow): Clears line. and processes alt
		         // commands.
			key = DBGUI_GetKey();
			if (key == KEY_NONE) { // Purely escape Clear line
				ClearInputLine();
				break;
			}

			switch (ascii_to_upper(key)) {
			case 'C': // ALT - C: CS:IP
				dataSeg[dbg.active_win_data] = SegValue(cs);
				if (cpu.pmode && !(reg_flags & FLAG_VM)) {
					dataOfs[dbg.active_win_data] = reg_eip;
				} else {
					dataOfs[dbg.active_win_data] = reg_ip;
				}
				break;
			case 'D': // ALT - D: DS:SI
				dataSeg[dbg.active_win_data] = SegValue(ds);
				if (cpu.pmode && !(reg_flags & FLAG_VM)) {
					dataOfs[dbg.active_win_data] = reg_esi;
				} else {
					dataOfs[dbg.active_win_data] = reg_si;
				}
				break;
			case 'E': // ALT - E: es:di
				dataSeg[dbg.active_win_data] = SegValue(es);
				if (cpu.pmode && !(reg_flags & FLAG_VM)) {
					dataOfs[dbg.active_win_data] = reg_edi;
				} else {
					dataOfs[dbg.active_win_data] = reg_di;
				}
				break;
			case 'X': // ALT - X: ds:dx
				dataSeg[dbg.active_win_data] = SegValue(ds);
				if (cpu.pmode && !(reg_flags & FLAG_VM)) {
					dataOfs[dbg.active_win_data] = reg_edx;
				} else {
					dataOfs[dbg.active_win_data] = reg_dx;
				}
				break;
			case 'B': // ALT -B: es:bx
				dataSeg[dbg.active_win_data] = SegValue(es);
				if (cpu.pmode && !(reg_flags & FLAG_VM)) {
					dataOfs[dbg.active_win_data] = reg_ebx;
				} else {
					dataOfs[dbg.active_win_data] = reg_bx;
				}
				break;
			case 'S': // ALT - S: ss:sp
				dataSeg[dbg.active_win_data] = SegValue(ss);
				if (cpu.pmode && !(reg_flags & FLAG_VM)) {
					dataOfs[dbg.active_win_data] = reg_esp;
				} else {
					dataOfs[dbg.active_win_data] = reg_sp;
				}
				break;
			default: break;
			}
			break;
		case DBGUI_KEY_PPAGE:
			if (dataOfs[dbg.active_win_data] > 16) {
				dataOfs[dbg.active_win_data] -= 16;
			} else {
				dataOfs[dbg.active_win_data] = 0;
			}
			break;
		case DBGUI_KEY_SPREVIOUS:
			if (dataOfs[dbg.active_win_data] >
			    static_cast<uint32_t>(
			            dbg.rows_data[dbg.active_win_data] * 16)) {
				dataOfs[dbg.active_win_data] -=
				        dbg.rows_data[dbg.active_win_data] *
				                                16;
			} else {
				dataOfs[dbg.active_win_data] = 0;
			}
			break;
		case DBGUI_KEY_NPAGE: dataOfs[dbg.active_win_data] += 16; break;
		case DBGUI_KEY_SNEXT:
			dataOfs[dbg.active_win_data] +=
			        dbg.rows_data[dbg.active_win_data] * 16;
			break;
		case DBGUI_CTL_PGUP:
			if (dbg.rows_output > 2) {
				--dbg.rows_output;
				/*dbg.win_out->_begy += 1;
				if (wresize(dbg.win_out,
				            dbg.rows_output - 1,
				            dbg.colums) != ERR) {
					wmoveoffset(dbg.win_con, 1, 0);
					wmoveoffset(dbg.win_var, 1, 0);
					if (wresize(dbg.win_data[0U],
					            dbg.rows_data[0U] + 1,
					            dbg.colums) != ERR) {
						++dbg.rows_data[0U];
					}
					DEBUG_RefreshLayout();
				}*/
			}
			break;
		case DBGUI_CTL_PGDN:
			if (dbg.rows_data[0U] > 1) {
				/*if (wresize(dbg.win_data[0U],
				            dbg.rows_data[0U] - 1,
				            dbg.colums) != ERR) {
					--dbg.rows_data[0U];
					wmoveoffset(dbg.win_var, -1, 0);
					wmoveoffset(dbg.win_con, -1, 0);
					dbg.win_out->_begy -= 1;
					if (wresize(dbg.win_out,
					            dbg.rows_output + 1,
					            dbg.colums) != ERR) {
						++dbg.rows_output;
					}
					DEBUG_RefreshLayout();
				}*/
			}
			break;
		case DBGUI_CTL_UP:
			if (dbg.rows_output > 2) {
				--dbg.rows_output;
				/*dbg.win_out->_begy += 1;
				if (wresize(dbg.win_out,
				            dbg.rows_output - 1,
				            dbg.colums) != ERR) {
					wmoveoffset(dbg.win_con, 1, 0);
					wmoveoffset(dbg.win_var, 1, 0);
					wmoveoffset(dbg.win_data[0U], 1, 0);
					wmoveoffset(dbg.win_reg, 1, 0);
					if (wresize(dbg.win_code,
					            dbg.rows_code + 1,
					            dbg.colums) != ERR) {
						++dbg.rows_code;
					}
					DEBUG_RefreshLayout();
				}*/
			}
			break;
		case DBGUI_CTL_DOWN:
			if (dbg.rows_code > 1) {
				/*if (wresize(dbg.win_code,
				            dbg.rows_code - 1,
				            dbg.colums) !=
				    ERR) {
					--dbg.rows_code;
					wmoveoffset(dbg.win_reg, -1, 0);
					wmoveoffset(dbg.win_data[0U], -1, 0);
					wmoveoffset(dbg.win_var, -1, 0);
					wmoveoffset(dbg.win_con, -1, 0);
					dbg.win_out->_begy -= 1;
					if (wresize(dbg.win_out,
					            dbg.rows_output + 1,
					            dbg.colums) != ERR) {
						++dbg.rows_output;
					}
					DEBUG_RefreshLayout();
				}*/
			}
			break;
		case DBGUI_CTL_TAB:
			if (++dbg.active_win_data >= NUM_WIN_DATA) {
				dbg.active_win_data = 0U;
			}
			break;
		case DBGUI_KEY_BTAB:
			if (dbg.active_win_data-- == 0U) {
				dbg.active_win_data = NUM_WIN_DATA - 1;
			}
			break;

		case DBGUI_KEY_DOWN: // down
			if (codeViewData.cursorPos < dbg.rows_code - 2) {
				codeViewData.cursorPos++;
			} else {
				codeViewData.useEIP += codeViewData.firstInstSize;
			}
			indexEIParray = MAXSIZE_EIPARRAY;
			break;
		case DBGUI_KEY_SDOWN: // shift + down
			if (codeViewData.cursorPos < dbg.rows_code - 2) {
				codeViewData.cursorPos = dbg.rows_code - 2;
			} else {
				codeViewData.useEIP = codeViewData.useEIPlast;
			}
			indexEIParray = MAXSIZE_EIPARRAY;
			break;
		case DBGUI_KEY_UP: // up
			if (codeViewData.cursorPos > 0) {
				codeViewData.cursorPos--;
			} else if (codeViewData.useEIP) {
				if (UseExistingEIP(1U)) {
					break;
				}
				PopulateEIParray();
				UseExistingEIP(1U);
			}
			break;
		case DBGUI_KEY_SUP: // shift + up
			if (codeViewData.cursorPos == 0 && key == DBGUI_KEY_SUP) {
				if (UseExistingEIP(dbg.rows_code)) {
					break;
				}
				PopulateEIParray();
				UseExistingEIP(dbg.rows_code);
			} else {
				codeViewData.cursorPos = 0;
			}
			break;
		case DBGUI_KEY_HOME: // Home: scroll log page up
			DEBUG_RefreshPage(-1);
			break;
		case DBGUI_KEY_SHOME: // shift + Home: scroll log page up one page
			DEBUG_RefreshPage(-dbg.rows_output + 1);
			break;
		case DBGUI_KEY_END: // End: scroll log page down
			DEBUG_RefreshPage(1);
			break;
		case DBGUI_KEY_SEND: // shift + End: scroll log page down one page
			DEBUG_RefreshPage(dbg.rows_output - 1);
			break;
		case DBGUI_KEY_IC: // Insert: toggle insert/overwrite
			codeViewData.ovrMode = !codeViewData.ovrMode;
			break;
		case DBGUI_KEY_LEFT: // move to the left in command line
			if (codeViewData.inputPos > 0) {
				codeViewData.inputPos--;
			}
			break;
		case DBGUI_KEY_RIGHT: // move to the right in command line
			if (codeViewData.inputStr[codeViewData.inputPos]) {
				codeViewData.inputPos++;
			}
			break;
		case DBGUI_KEY_F(6): // previous command (f1-f4 generate rubbish
		                     // at my place)
		case DBGUI_KEY_F(3): // previous command
			if (histBuffPos == histBuff.begin()) {
				break;
			}
			if (histBuffPos == histBuff.end()) {
				// copy inputStr to suspInputStr so we can
				// restore it
				safe_strcpy(codeViewData.suspInputStr,
				            codeViewData.inputStr);
			}
			safe_strcpy(codeViewData.inputStr, (--histBuffPos)->c_str());
			codeViewData.inputPos = safe_strlen(codeViewData.inputStr);
			break;
		case DBGUI_KEY_F(7): // next command (f1-f4 generate rubbish at
		                     // my place)
		case DBGUI_KEY_F(4): // next command
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
		case DBGUI_KEY_F(5): // Run Program
			debugging = false;
			// Redraw screen to show "(Running)" before entering normal loop
			DBGUI_NewFrame();
			DEBUG_DrawScreen();
			DBGUI_Render();
			ret = DEBUG_Run(1, false);
			break;
		case DBGUI_KEY_F(8): // Toggle printable characters
			showPrintable = !showPrintable;
			break;
		case DBGUI_KEY_F(9): // Set/Remove Breakpoint
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
		case DBGUI_KEY_F(10): // Step over inst
			if (StepOver()) {
				ret = DEBUG_Run(1, false);
				break;
			}
			// If we aren't stepping over something, do a normal step.
			[[fallthrough]];
		case DBGUI_KEY_F(11): // trace into
			exitLoop = false;
			ret      = DEBUG_Run(1, true);
			break;
		case '\n': // Parse typed Command
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
		case DBGUI_KEY_BACKSPACE: // backspace
		case 0x7f: // backspace in some terminal emulators (linux)
		case 0x08: // delete
			if (codeViewData.inputPos == 0) {
				break;
			}
			codeViewData.inputPos--;
			[[fallthrough]];
		case DBGUI_KEY_DC: // delete character
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
			break;
		case DBGUI_KEY_CLOSE:
			DEBUG_Close();
			break;
		default:
			if ((key >= 32) && (key < 127)) {
				if ((codeViewData.inputPos < 0) ||
				    (codeViewData.inputPos >= MAXCMDLEN)) {
					break;
				}
				codeViewData.inputStr[MAXCMDLEN] = '\0';
				if (codeViewData.inputStr[codeViewData.inputPos] == 0) {
					codeViewData.inputStr[codeViewData.inputPos++] =
					        char(key);
					codeViewData.inputStr[codeViewData.inputPos] = '\0';
				} else if (!codeViewData.ovrMode) {
					auto len = (int)safe_strlen(
					        codeViewData.inputStr);
					if (len < MAXCMDLEN) {
						for (len++;
						     len > codeViewData.inputPos;
						     len--) {
							codeViewData.inputStr[len] =
							        codeViewData.inputStr[len - 1];
						}
						codeViewData
						        .inputStr[codeViewData.inputPos++] =
						        char(key);
					}
				} else {
					codeViewData.inputStr[codeViewData.inputPos++] =
					        char(key);
				}
			}
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
		// Drawing is now handled by the main loop
	}
	return ret;
}
#endif // C_DEBUGGER