// SPDX-FileCopyrightText:  2021-2026 The DOSBox Staging Team
// SPDX-FileCopyrightText:  2002-2021 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dosbox.h"

#if C_DEBUGGER

#include <fstream>

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

extern void DrawCode(void);
extern void DrawConsole(void);

extern int32_t DEBUG_Run(int32_t, bool);

extern uint32_t GetAddress(uint16_t, uint32_t);
extern bool ParseCommand(char*);

extern bool exitLoop;

DBGBlock dbg          = {};
extern bool debugging;

extern SCodeViewData codeViewData;

uint16_t dataSeg[NUM_WIN_DATA] = {0, 0, 0, 0};
uint32_t dataOfs[NUM_WIN_DATA] = {0, 0, 0, 0};

extern bool showPrintable;

static void ClearInputLine(void)
{
	codeViewData.inputStr[0] = 0;
	codeViewData.inputPos    = 0;
}

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
		DrawCode();
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

uint32_t DEBUG_CheckKeys(void)
{
	Bits ret       = 0;
	bool numberrun = false;
	bool skipDraw  = false;
	MEVENT mEvent;
	nc_getmouse(&mEvent);

	int key = getch();

	if (key >= '1' && key <= '5' && safe_strlen(codeViewData.inputStr) == 0) {
		const int32_t v[] = {5, 500, 1000, 5000, 10000};

		ret = DEBUG_Run(v[key - '1'], true);

		/* Setup variables so we end up at the proper ret processing */
		numberrun = true;

		// Don't redraw the screen if it's going to get redrawn
		// immediately afterwards, to avoid resetting oldregs.
		if (ret == static_cast<Bits>(debugCallback)) {
			skipDraw = true;
		}
		key = -1;
	}

	if (key > 0 || numberrun) {
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
		switch (ascii_to_upper(key)) {
		case 27: // escape (a bit slow): Clears line. and processes alt
		         // commands.
			key = getch();
			if (key < 0) { // Purely escape Clear line
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
				{
					uint32_t bytes = (dbg.rows_data[dbg.active_win_data])
					              << 3;
					if (dataOfs[dbg.active_win_data] > bytes) {
						dataOfs[dbg.active_win_data] -= bytes;
					} else {
						dataOfs[dbg.active_win_data] = 0u;
					}
				}
				break;
			default: break;
			}
			break;
		case KEY_PPAGE: 
			if (dataOfs[dbg.active_win_data] > 16) {
				dataOfs[dbg.active_win_data] -= 16;
			} else {
				dataOfs[dbg.active_win_data] = 0;
			}
			break;
		case KEY_SPREVIOUS:
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
		case KEY_NPAGE: dataOfs[dbg.active_win_data] += 16; break;
		case KEY_SNEXT:
			dataOfs[dbg.active_win_data] +=
			        dbg.rows_data[dbg.active_win_data] * 16;
			break;
		case CTL_PGUP:
			if (dbg.rows_output > 2) {
				--dbg.rows_output;
				dbg.win_out->_begy += 1;
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
				}
			}
			break;
		case CTL_PGDN:
			if (dbg.rows_data[0U] > 1) {
				if (wresize(dbg.win_data[0U],
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
				}
			}
			break;
		case CTL_UP:
			if (dbg.rows_output > 2) {
				--dbg.rows_output;
				dbg.win_out->_begy += 1;
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
				}
			}
			break;
		case CTL_DOWN:
			if (dbg.rows_code > 1) {
				if (wresize(dbg.win_code,
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
				}
			}
			break;
		case CTL_TAB:
			if (++dbg.active_win_data >= NUM_WIN_DATA) {
				dbg.active_win_data = 0U;
			}
			DEBUG_RefreshLayout();
			break;
		case KEY_BTAB:
			if (dbg.active_win_data-- == 0U) {
				dbg.active_win_data = NUM_WIN_DATA - 1;
			}
			DEBUG_RefreshLayout();
			break;

		case KEY_DOWN: // down
			if (codeViewData.cursorPos < dbg.rows_code - 1) {
				if( SP->key_modifiers & PDC_KEY_MODIFIER_SHIFT )
					codeViewData.useEIP = codeViewData.useEIPlast;
				else
					codeViewData.cursorPos++;
			} else {
				codeViewData.useEIP += codeViewData.firstInstSize;
			}
			indexEIParray = MAXSIZE_EIPARRAY;
			break;
		case KEY_SDOWN: // shift + down
			if (codeViewData.cursorPos < dbg.rows_code - 1) {
				codeViewData.cursorPos = dbg.rows_code - 1;
			} else {
				codeViewData.useEIP = codeViewData.useEIPlast;
			}
			indexEIParray = MAXSIZE_EIPARRAY;
			break;
		case KEY_UP: // up
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
		case KEY_SUP: // shift + up
			if (codeViewData.cursorPos == 0 && key == KEY_SUP) {
				if (UseExistingEIP(dbg.rows_code)) {
					break;
				}
				PopulateEIParray();
				UseExistingEIP(dbg.rows_code);
			} else {
				codeViewData.cursorPos = 0;
			}
			break;
		case KEY_HOME: // Home: scroll log page up
			DEBUG_RefreshPage(-1);
			break;
		case KEY_SHOME: // shift + Home: scroll log page up one page
			DEBUG_RefreshPage(-dbg.rows_output + 1);
			break;
		case KEY_END: // End: scroll log page down
			DEBUG_RefreshPage(1);
			break;
		case KEY_SEND: // shift + End: scroll log page down one page
			DEBUG_RefreshPage(dbg.rows_output - 1);
			break;
		case KEY_IC: // Insert: toggle insert/overwrite
			codeViewData.ovrMode = !codeViewData.ovrMode;
			break;
		case KEY_LEFT: // move to the left in command line
			if (codeViewData.inputPos > 0) {
				codeViewData.inputPos--;
			}
			break;
		case KEY_RIGHT: // move to the right in command line
			if (codeViewData.inputStr[codeViewData.inputPos]) {
				codeViewData.inputPos++;
			}
			break;
		case KEY_F(6): // previous command (f1-f4 generate rubbish at my
		               // place)
		case KEY_F(3): // previous command
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
		case KEY_F(7): // next command (f1-f4 generate rubbish at my place)
		case KEY_F(4): // next command
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
		case KEY_F(5): // Run Program
			debugging = false;
			DrawConsole(); // update console window to show "running" status

			ret      = DEBUG_Run(1, false);
			skipDraw = true; // don't update screen after this instruction
			break;
		case KEY_F(8): // Toggle printable characters
			showPrintable = !showPrintable;
			break;
		case KEY_F(9): // Set/Remove Breakpoint
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
		case KEY_F(10): // Step over inst
			if (StepOver()) {
				ret      = DEBUG_Run(1, false);
				skipDraw = true;
				break;
			}
			// If we aren't stepping over something, do a normal step.
			[[fallthrough]];
		case KEY_F(11): // trace into
			exitLoop = false;
			ret      = DEBUG_Run(1, true);
			break;
		case 0x0A: // Parse typed Command
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
		case KEY_BACKSPACE: // backspace (linux)
		case 0x7f: // backspace in some terminal emulators (linux)
		case 0x08: // delete
			if (codeViewData.inputPos == 0) {
				break;
			}
			codeViewData.inputPos--;
			[[fallthrough]];
		case KEY_DC: // delete character
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
		case KEY_CLOSE:
			DEBUG_Close();
			skipDraw = true;
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
			} else if (key == killchar()) {
				ClearInputLine();
			}
			break;
		}
		if (KEY_MOUSE == key) {
			auto changes = SP->mouse_status.changes;
			if (changes & (BUTTON1_RELEASED | PDC_MOUSE_WHEEL_UP |
			               PDC_MOUSE_WHEEL_DOWN)) {
				for (auto i = 0U; i < NUM_WIN_DATA; ++i) {
					if (wenclose(dbg.win_data[i],
					             mEvent.y,
					             mEvent.x)) {
						if (changes & BUTTON1_RELEASED) {
							if (dbg.active_win_data != i) {
								dbg.active_win_data = i;
								DEBUG_RefreshLayout();
							}
						} else if (changes &
						           PDC_MOUSE_WHEEL_UP) {
							if (dataOfs[i] > 96) {
								dataOfs[i] -= 96;
							} else {
								dataOfs[i] = 0;
							}
						} else {
							dataOfs[i] += 96;
						}
					}
				}
			}
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
		if (!skipDraw) {
			DEBUG_DrawScreen();
		}
	}
	return ret;
}
#endif // C_DEBUGGER