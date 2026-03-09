// SPDX-FileCopyrightText:  2002-2025 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dosbox.h"

#if C_DEBUGGER
#include "cbreakpoint.h"
#include "cpu/cpu.h"
#include "cpu/paging.h"
#include "debugger_inc.h"
#include "utils/string_utils.h"

#if !PDCURSES
#error SYSTEM CURSES INCLUDED, SHOULD BE PDCURSES
#endif

int old_cursor_state;
Bitu cycle_count = 0;

extern DBGBlock dbg;
extern bool debugging;

extern FILE* debuglog;

#define MAX_LOG_BUFFER 500
static std::list<std::string> logBuff              = {};
static std::list<std::string>::iterator logBuffPos = logBuff.end();

bool showExtend;
bool showPrintable = true;

char curSelectorName[3] = {0, 0, 0};

void DEBUG_ShowMsg(const char* format, ...)
{
	// Quit early if the window hasn't been created yet
	if (!dbg.win_out) {
		return;
	}

	char buf[512];
	va_list msg;
	va_start(msg, format);
	vsprintf(buf, format, msg);
	va_end(msg);

	buf[sizeof(buf) - 1] = '\0';

	/* Add newline if not present */
	size_t len = safe_strlen(buf);
	if (buf[len - 1] != '\n' && len + 1 < sizeof(buf)) {
		strcat(buf, "\n");
	}

	if (debuglog) {
		fprintf(debuglog, "%s", buf);
		fflush(debuglog);
	}

	if (logBuffPos != logBuff.end()) {
		logBuffPos = logBuff.end();
		DEBUG_RefreshPage(0);
		//		mvwprintw(dbg.win_out,dbg.win_out->_maxy-1, 0, "");
	}
	logBuff.emplace_back(buf);
	if (logBuff.size() > MAX_LOG_BUFFER) {
		logBuff.pop_front();
	}

	logBuffPos = logBuff.end();
	wprintw(dbg.win_out, "%s", buf);
	wrefresh(dbg.win_out);
}

void DEBUG_RefreshPage(int scroll)
{
	// Quit early if the window hasn't been created yet
	if (!dbg.win_out) {
		return;
	}

	if (scroll < 0) {
		while (scroll && logBuffPos != logBuff.begin()) {
			--logBuffPos;
			++scroll;
		}
	} else {
		while (scroll && logBuffPos != logBuff.end()) {
			++logBuffPos;
			--scroll;
		}
	}

	std::list<std::string>::iterator i = logBuffPos;
	int maxy, maxx;
	getmaxyx(dbg.win_out, maxy, maxx);
	int rem_lines = maxy;
	if (rem_lines == -1) {
		return;
	}

	wclear(dbg.win_out);

	while (rem_lines > 0 && i != logBuff.begin()) {
		--i;
		for (std::string::size_type posf = 0, posl;
		     (posl = (*i).find('\n', posf)) != std::string::npos;
		     posf = posl + 1) {
			rem_lines -= (int)((posl - posf) / maxx) + 1; // len=(posl+1)-posf-1
		}
		/* Const cast is needed for pdcurses which has no const char in
		 * mvwprintw (bug maybe) */
		mvwprintw(dbg.win_out, rem_lines - 1, 0, const_cast<char*>((*i).c_str()));
	}
	mvwprintw(dbg.win_out, maxy - 1, 0, "");
	wrefresh(dbg.win_out);
}

/********************/
/*   Draw windows   */
/********************/

extern char* AnalyzeInstruction(char*, bool);
extern uint32_t GetAddress(uint16_t, uint32_t);
extern bool GetDescriptorInfo(char*, char*, char*);
extern SCodeViewData codeViewData;

static void SetColor(Bitu test)
{
	if (test) {
		if (has_colors()) {
			wattrset(dbg.win_reg, COLOR_PAIR(PAIR_BYELLOW_BLACK));
		}
	} else {
		if (has_colors()) {
			wattrset(dbg.win_reg, 0);
		}
	}
}

void DrawCode(void)
{
	bool saveSel;
	uint32_t disEIP = codeViewData.useEIP;
	PhysPt start    = GetAddress(codeViewData.useCS, codeViewData.useEIP);
	char dline[200];
	Bitu size;
	Bitu c;
	static char line20[21] = "                    ";

	for (int i = 0, iMid = dbg.rows_code * 0.95; i < dbg.rows_code; ++i) {
		saveSel = false;
		if (has_colors()) {
			if ((codeViewData.useCS == SegValue(cs)) &&
			    (disEIP == reg_eip)) {
				wattrset(dbg.win_code, COLOR_PAIR(PAIR_GREEN_BLACK));
				if (codeViewData.cursorPos == -1) {
					codeViewData.cursorPos = i; // Set Cursor
				}
				if (i == codeViewData.cursorPos) {
					codeViewData.cursorSeg = SegValue(cs);
					codeViewData.cursorOfs = disEIP;
				}
				saveSel = (i == codeViewData.cursorPos);
			} else if (i == codeViewData.cursorPos) {
				wattrset(dbg.win_code, COLOR_PAIR(PAIR_BLACK_GREY));
				codeViewData.cursorSeg = codeViewData.useCS;
				codeViewData.cursorOfs = disEIP;
				saveSel                = true;
			} else if (CBreakpoint::IsBreakpoint(codeViewData.useCS,
			                                     disEIP)) {
				wattrset(dbg.win_code, COLOR_PAIR(PAIR_GREY_RED));
			} else {
				wattrset(dbg.win_code, 0);
			}
		}

		Bitu drawsize = size = DasmI386(dline, start, disEIP, cpu.code.big);
		if (disEIP < codeViewData.goodEIP &&
		    disEIP + size > codeViewData.goodEIP) {
			size = drawsize = codeViewData.goodEIP - disEIP;
		}
		bool toolarge = false;
		mvwprintw(dbg.win_code, i, 0, "%04X:%04X  ", codeViewData.useCS, disEIP);

		if (drawsize > check_cast<uint32_t>(dbg.rows_code)) {
			toolarge = true;
			drawsize = dbg.rows_code - 1;
		}
		for (c = 0; c < drawsize; c++) {
			uint8_t value;
			if (mem_readb_checked(start + c, &value)) {
				value = 0;
			}
			wprintw(dbg.win_code, "%02X", value);
		}
		if (toolarge) {
			waddstr(dbg.win_code, "..");
			drawsize++;
		}
		// Spacepad up to 20 characters
		if (drawsize && (drawsize < check_cast<uint32_t>(dbg.rows_code))) {
			line20[20 - drawsize * 2] = 0;
			waddstr(dbg.win_code, line20);
			line20[20 - drawsize * 2] = ' ';
		} else {
			waddstr(dbg.win_code, line20);
		}

		char empty_res[] = {0};
		char* res        = empty_res;
		if (showExtend) {
			res = AnalyzeInstruction(dline, saveSel);
		}
		// Spacepad it up to 28 characters
		size_t dline_len = safe_strlen(dline);
		if (dline_len < 28) {
			memset(dline + dline_len, ' ', 28 - dline_len);
		}
		dline[28] = 0;
		waddstr(dbg.win_code, dline);
		// Spacepad it up to 20 characters
		size_t res_len = strlen(res);
		if (res_len && (res_len < 21)) {
			waddstr(dbg.win_code, res);
			line20[20 - res_len] = 0;
			waddstr(dbg.win_code, line20);
			line20[20 - res_len] = ' ';
		} else {
			waddstr(dbg.win_code, line20);
		}

		start += size;
		disEIP += size;

		if (i == 0) {
			codeViewData.firstInstSize = size;
		}
		if (i == iMid) {
			codeViewData.useEIPmid = disEIP;
		}
	}
	codeViewData.useEIPlast = disEIP;

	wattrset(dbg.win_code, 0);
	wrefresh(dbg.win_code);
}

void DrawConsole(void)
{
	if (!debugging) {
		if (has_colors()) {
			wattrset(dbg.win_con, COLOR_PAIR(PAIR_GREEN_BLACK));
		}
		mvwprintw(dbg.win_con, 0, 0, "%s", "(Running)");
		wclrtoeol(dbg.win_con);
	} else {
		// TODO long lines
		char* dispPtr = codeViewData.inputStr;
		char* curPtr  = &codeViewData.inputStr[codeViewData.inputPos];
		mvwprintw(dbg.win_con,
		          0,
		          0,
		          "%c-> %s%c",
		          (codeViewData.ovrMode ? 'O' : 'I'),
		          dispPtr,
		          (*curPtr ? ' ' : '_'));
		wclrtoeol(dbg.win_con); // not correct in pdcurses if full line
		mvwchgat(dbg.win_con, 0, 0, 3, 0, PAIR_BLACK_GREY, nullptr);
		if (*curPtr) {
			mvwchgat(dbg.win_con,
			         0,
			         (curPtr - dispPtr + 4),
			         1,
			         0,
			         PAIR_BLACK_GREY,
			         nullptr);
		}
	}
	wattrset(dbg.win_con, 0);
	wrefresh(dbg.win_con);
}

static void DrawData()
{
	uint8_t ch;
	uint32_t add, address;
	bool f16bit = false;
	for (auto dw = 0U; dw < NUM_WIN_DATA; ++dw) {
		add = dataOfs[dw];
		/* Data win */
		for (auto y = 0; y < dbg.rows_data[dw]; ++y) {
			// Address
			if (add < 0x10000) {
				mvwprintw(dbg.win_data[dw],
				          y,
				          0,
				          "%04X:%04X  ",
				          dataSeg[dw],
				          add);
			} else {
				mvwprintw(dbg.win_data[dw],
				          y,
				          0,
				          "%04X:%08X ",
				          dataSeg[dw],
				          add);
				f16bit = true;
			}
			for (int x = 0; x < 16; x++) {
				address = GetAddress(dataSeg[dw], add);
				if (mem_readb_checked(address, &ch)) {
					ch = 0;
				}
				mvwprintw(dbg.win_data[dw],
				          y,
				          3 * x + (f16bit ? 13 : (10 + (x >> 2))),
				          " %02X ",
				          ch);
				if (showPrintable) {
					if (ch < 32 ||
					    !isprint(*reinterpret_cast<unsigned char*>(
					            &ch))) {
						ch = '.';
					}
					mvwaddch(dbg.win_data[dw], y, 63 + x, ch);
				} else {
#if PDCURSES
					mvwaddrawch(dbg.win_data[dw], y, 63 + x, ch);
#else
					if (ch < 32) {
						ch = '.';
					}
					mvwaddch(dbg.win_data[dw], y, 63 + x, ch);
#endif
				}
				++add;
			}
		}
		wrefresh(dbg.win_data[dw]);
	}
}

static struct {
	uint32_t eax = 0;
	uint32_t ebx = 0;
	uint32_t ecx = 0;
	uint32_t edx = 0;
	uint32_t esi = 0;
	uint32_t edi = 0;
	uint32_t ebp = 0;
	uint32_t esp = 0;
	uint32_t eip = 0;
} oldregs = {};

static auto oldcpucpl     = cpu.cpl;
static auto oldflags      = cpu_regs.flags;
static Segment oldsegs[6] = {};

static void DrawRegisters(void)
{
	/* Main Registers */
	SetColor(reg_eax != oldregs.eax);
	oldregs.eax = reg_eax;
	mvwprintw(dbg.win_reg, 0, 4, "%04X %04X", (reg_eax >> 16) & 0xFFFF, reg_eax & 0xFFFF);
	SetColor(reg_ebx != oldregs.ebx);
	oldregs.ebx = reg_ebx;
	mvwprintw(dbg.win_reg, 1, 4, "%04X %04X", (reg_ebx >> 16) & 0xFFFF, reg_ebx & 0xFFFF);
	SetColor(reg_ecx != oldregs.ecx);
	oldregs.ecx = reg_ecx;
	mvwprintw(dbg.win_reg, 2, 4, "%04X %04X", (reg_ecx >> 16) & 0xFFFF, reg_ecx & 0xFFFF);
	SetColor(reg_edx != oldregs.edx);
	oldregs.edx = reg_edx;
	mvwprintw(dbg.win_reg, 3, 4, "%04X %04X", (reg_edx >> 16) & 0xFFFF, reg_edx & 0xFFFF);

	SetColor(reg_esi != oldregs.esi);
	oldregs.esi = reg_esi;
	mvwprintw(dbg.win_reg, 0, 20, "%04X %04X", (reg_esi >> 16) & 0xFFFF, reg_esi & 0xFFFF);
	SetColor(reg_edi != oldregs.edi);
	oldregs.edi = reg_edi;
	mvwprintw(dbg.win_reg, 1, 20, "%04X %04X", (reg_edi >> 16) & 0xFFFF, reg_edi & 0xFFFF);
	SetColor(reg_esp != oldregs.esp);
	oldregs.esp = reg_esp;
	mvwprintw(dbg.win_reg, 2, 20, "%04X %04X", (reg_esp >> 16) & 0xFFFF, reg_esp & 0xFFFF);
	SetColor(reg_ebp != oldregs.ebp);
	oldregs.ebp = reg_ebp;
	mvwprintw(dbg.win_reg, 3, 20, "%04X %04X", (reg_ebp >> 16) & 0xFFFF, reg_ebp & 0xFFFF);

	SetColor(SegValue(cs) != oldsegs[cs].val);
	oldsegs[cs].val = SegValue(cs);
	mvwprintw(dbg.win_reg, 0, 35, "%04X", SegValue(cs));
	SetColor(SegValue(ds) != oldsegs[ds].val);
	oldsegs[ds].val = SegValue(ds);
	mvwprintw(dbg.win_reg, 1, 35, "%04X", SegValue(ds));
	SetColor(SegValue(es) != oldsegs[es].val);
	oldsegs[es].val = SegValue(es);
	mvwprintw(dbg.win_reg, 2, 35, "%04X", SegValue(es));

	SetColor(SegValue(fs) != oldsegs[fs].val);
	oldsegs[fs].val = SegValue(fs);
	mvwprintw(dbg.win_reg, 0, 45, "%04X", SegValue(fs));
	SetColor(SegValue(gs) != oldsegs[gs].val);
	oldsegs[gs].val = SegValue(gs);
	mvwprintw(dbg.win_reg, 1, 45, "%04X", SegValue(gs));
	SetColor(SegValue(ss) != oldsegs[ss].val);
	oldsegs[ss].val = SegValue(ss);
	mvwprintw(dbg.win_reg, 2, 45, "%04X", SegValue(ss));

	SetColor(reg_eip != oldregs.eip);
	oldregs.eip = reg_eip;
	mvwprintw(dbg.win_reg, 0, 57, "%04X %04X", (reg_eip >> 16) & 0xFFFF, reg_eip & 0xFFFF);

	/*Individual flags*/
	Bitu changed_flags = reg_flags ^ oldflags;
	oldflags           = reg_flags;

	SetColor(changed_flags & FLAG_CF);
	mvwprintw(dbg.win_reg, 1, 54, "%01X", GETFLAG(CF) ? 1 : 0);
	SetColor(changed_flags & FLAG_ZF);
	mvwprintw(dbg.win_reg, 1, 57, "%01X", GETFLAG(ZF) ? 1 : 0);
	SetColor(changed_flags & FLAG_SF);
	mvwprintw(dbg.win_reg, 1, 60, "%01X", GETFLAG(SF) ? 1 : 0);
	SetColor(changed_flags & FLAG_OF);
	mvwprintw(dbg.win_reg, 1, 63, "%01X", GETFLAG(OF) ? 1 : 0);
	SetColor(changed_flags & FLAG_AF);
	mvwprintw(dbg.win_reg, 1, 66, "%01X", GETFLAG(AF) ? 1 : 0);
	SetColor(changed_flags & FLAG_PF);
	mvwprintw(dbg.win_reg, 1, 69, "%01X", GETFLAG(PF) ? 1 : 0);

	SetColor(changed_flags & FLAG_DF);
	mvwprintw(dbg.win_reg, 1, 72, "%01X", GETFLAG(DF) ? 1 : 0);
	SetColor(changed_flags & FLAG_IF);
	mvwprintw(dbg.win_reg, 1, 75, "%01X", GETFLAG(IF) ? 1 : 0);
	SetColor(changed_flags & FLAG_TF);
	mvwprintw(dbg.win_reg, 1, 78, "%01X", GETFLAG(TF) ? 1 : 0);

	wattrset(dbg.win_reg, 0);
	mvwprintw(dbg.win_reg, 2, 53, "%" PRIuPTR " ", cycle_count);

	SetColor(changed_flags & FLAG_IOPL);
	mvwprintw(dbg.win_reg, 2, 72, "%01X", GETFLAG(IOPL) >> 12);

	SetColor(cpu.cpl ^ oldcpucpl);
	mvwprintw(dbg.win_reg, 2, 78, "%01" PRIXPTR, cpu.cpl);
	oldcpucpl = cpu.cpl;

	if (cpu.pmode) {
		if (reg_flags & FLAG_VM) {
			mvwprintw(dbg.win_reg, 0, 75, "VM86");
		} else if (cpu.code.big) {
			mvwprintw(dbg.win_reg, 0, 75, "Pr32");
		} else {
			mvwprintw(dbg.win_reg, 0, 75, "Pr16");
		}
	} else {
		mvwprintw(dbg.win_reg, 0, 75, "Real");
	}

	// Selector info, if available
	if ((cpu.pmode) && curSelectorName[0]) {
		char out1[200], out2[200];
		GetDescriptorInfo(curSelectorName, out1, out2);
		mvwprintw(dbg.win_reg, 3, 32, out1);
		mvwprintw(dbg.win_reg, 3, 56, out2);
	}
	wrefresh(dbg.win_reg);
}

class CDebugVar;
extern std::vector<CDebugVar*> varList;

#define DEBUG_VAR_BUF_LEN 16
void DrawVariables()
{
	if (varList.empty()) {
		return;
	}

	char buffer[DEBUG_VAR_BUF_LEN] = {};
	bool windowchanges             = false;

	for (size_t i = 0; i < 4 * 3 && i != varList.size(); ++i) {
		auto dv = varList[i];
		uint16_t value;
		bool varchanges   = false;
		bool has_no_value = mem_readw_checked(dv->GetAdr(), &value);
		if (has_no_value) {
			snprintf(buffer, DEBUG_VAR_BUF_LEN, "%s", "??????");
			dv->SetValue(false, 0);
			varchanges = true;
		} else {
			if (dv->HasValue() && dv->GetValue() == value) {
				; // It already had a value and it didn't change
				  // (most likely case)
			} else {
				dv->SetValue(true, value);
				varchanges = true;
			}
			if (dbg.win_var->_clear) {
				snprintf(buffer, DEBUG_VAR_BUF_LEN, "0x%04x", value);
			}
		}

		if (varchanges || dbg.win_var->_clear) {
			int y = i / 3;
			int x = (i % 3) * 26;
			mvwprintw(dbg.win_var, y, x, dv->GetName());
			mvwprintw(dbg.win_var, y, (x + DEBUG_VAR_BUF_LEN + 1), buffer);
			windowchanges = true; // Something has changed in this
			                      // window
		}
	}

	if (windowchanges) {
		wrefresh(dbg.win_var);
	}
}
#undef DEBUG_VAR_BUF_LEN

static void Draw_RegisterLayout(void)
{
	// Quit early if the window hasn't been created yet
	if (!dbg.win_reg) {
		return;
	}

	mvwaddstr(dbg.win_reg, 0, 0, "EAX");
	mvwaddstr(dbg.win_reg, 1, 0, "EBX");
	mvwaddstr(dbg.win_reg, 2, 0, "ECX");
	mvwaddstr(dbg.win_reg, 3, 0, "EDX");

	mvwaddstr(dbg.win_reg, 0, 16, "ESI");
	mvwaddstr(dbg.win_reg, 1, 16, "EDI");
	mvwaddstr(dbg.win_reg, 2, 16, "ESP");
	mvwaddstr(dbg.win_reg, 3, 16, "EBP");

	mvwaddstr(dbg.win_reg, 0, 32, "CS");
	mvwaddstr(dbg.win_reg, 1, 32, "DS");
	mvwaddstr(dbg.win_reg, 2, 32, "ES");
	
	mvwaddstr(dbg.win_reg, 0, 42, "FS");
	mvwaddstr(dbg.win_reg, 1, 42, "GS");
	mvwaddstr(dbg.win_reg, 2, 42, "SS");

	mvwaddstr(dbg.win_reg, 0, 53, "EIP");

	mvwaddstr(dbg.win_reg, 2, 75, "CPL");
	mvwaddstr(dbg.win_reg, 2, 68, "IOPL");

	mvwaddstr(dbg.win_reg, 1, 53, "C  Z  S  O  A  P  D  I  T ");
}

static void DrawBars(void)
{
	if (has_colors()) {
		attrset(COLOR_PAIR(PAIR_BLACK_GREY));
	}
	// Column 1
	int outy = 0;
	/* Show the Code Overview perhaps with special stuff in bar too */
	mvaddstr(outy, 0, "                                      Code               [CTRL]/[SHIFT] Up/Down ");
	outy += dbg.rows_code + 1;
	/* Show the Register bar */
	mvaddstr(outy, 0, "                                    Registers                                   ");
	outy += dbg.rows_registers + 1;
	/* Show the Data Overview bar perhaps with more special stuff in the end */
	if (0U == dbg.active_win_data) {
		attrset(COLOR_PAIR(PAIR_BLACK_GREEN));
		mvaddstr(outy, 0, " [SHIFT] CTRL TAB                     Data          [CTRL]/[SHIFT] Page Up/Down ");
		attrset(COLOR_PAIR(PAIR_BLACK_GREY));
	} else {
		mvaddstr(outy, 0, "                                      Data                    CTRL Page Up/Down ");
	}
	outy += dbg.rows_data[0U] + 1;
	/* Show the Variable Overview bar */
	mvaddstr(outy, 0, "                                    Variables                                   ");
	outy += dbg.rows_variables;
	outy += dbg.rows_con + 1;
	/* Show the Output OverView */
	mvaddstr(outy, 0, "                                     Output                    [SHIFT] Home/End ");
	// Column 2
	outy = 0;
	/* Show the other Data Overview bars */
	for( auto i = 1U; i < NUM_WIN_DATA; ++i ){
		if (i == dbg.active_win_data) {
			attrset(COLOR_PAIR(PAIR_BLACK_GREEN));
			mvaddstr(outy, dbg.colums, " [SHIFT] CTRL TAB                     Data                 [SHIFT] Page Up/Down ");
			attrset(COLOR_PAIR(PAIR_BLACK_GREY));
		} else {
			mvaddstr(outy, dbg.colums, "                                      Data                                      ");
		}
		outy += dbg.rows_data[i] + 1;
	}
	attrset(0);
	// Use height values in rows. So we don't need to touch the internal
	// window structures
}

static void MakeSubWindows(void)
{
	/* The Std output win should go at the bottom */
	/* Make all the subwindows */
	int win_main_maxy, win_main_maxx;
	getmaxyx(dbg.win_main, win_main_maxy, win_main_maxx);
	// Column 1
	int outy = 1;
	/* The Code window */
	dbg.win_code = subwin(dbg.win_main, dbg.rows_code, dbg.colums, outy, 0);
	outy += dbg.rows_code + 1;
	/* The Register window  */
	dbg.win_reg = subwin(dbg.win_main, dbg.rows_registers, dbg.colums, outy, 0);
	outy += dbg.rows_registers + 1;
	/* The Data window */
	dbg.win_data[0U] = subwin(dbg.win_main, dbg.rows_data[0U], dbg.colums, outy, 0);
	outy += dbg.rows_data[0U] + 1;
	/* The Variables window */
	dbg.win_var = subwin(dbg.win_main, dbg.rows_variables, dbg.colums, outy, 0);
	outy += dbg.rows_variables;
	/* The Console window */
	dbg.win_con = subwin(dbg.win_main, dbg.rows_con, dbg.colums, outy, 0);
	outy += dbg.rows_con + 1;
	/* The Output window */
	dbg.rows_output = win_main_maxy - outy; /* Use the rest of main window */
	dbg.win_out = subwin(dbg.win_main, dbg.rows_output, dbg.colums, outy, 0);
	// Column 2
	outy = 1;
	/* The other Data windows */
	for (auto i = 1U; i < NUM_WIN_DATA; ++i) {
		dbg.win_data[i] = subwin(dbg.win_main,
		                         dbg.rows_data[i],
		                         dbg.colums,
		                         outy,
		                         dbg.colums);
		outy += dbg.rows_data[i] + 1;
	}
	if (!dbg.win_reg || !dbg.win_data[0U] || !dbg.win_code ||
	    !dbg.win_var || !dbg.win_con || !dbg.win_out || !dbg.win_data[1U] ||
	    !dbg.win_data[2U] || !dbg.win_data[3U]) {
		E_Exit("Setting up windows failed");
	}
	//	dbg.input_y=win_main_maxy-1;
	scrollok(dbg.win_out, TRUE);
	DrawBars();
	Draw_RegisterLayout();
	refresh();
}

void DEBUG_RefreshLayout()
{
	wclear(dbg.win_main);
	DEBUG_RefreshPage(0);
	wclear(dbg.win_var);
	Draw_RegisterLayout();
	DrawBars();
}

void DEBUG_DrawScreen(void)
{
	DrawData();
	DrawCode();
	DrawRegisters();
	DrawVariables();
	DrawConsole();
}

static void MakePairs()
{
	init_pair(PAIR_BLACK_BLUE, COLOR_BLACK /*| FOREGROUND_INTENSITY */, COLOR_BLUE);
	init_pair(PAIR_BYELLOW_BLACK,
	          COLOR_YELLOW /*| FOREGROUND_INTENSITY */,
	          COLOR_BLACK);
	init_pair(PAIR_GREEN_BLACK, COLOR_GREEN /*| FOREGROUND_INTENSITY */, COLOR_BLACK);
	init_pair(PAIR_BLACK_GREY, COLOR_BLACK /*| FOREGROUND_INTENSITY */, COLOR_WHITE);
	init_pair(PAIR_GREY_RED, COLOR_WHITE /*| FOREGROUND_INTENSITY */, COLOR_RED);
	init_pair(PAIR_BLACK_GREEN, COLOR_BLACK /*| FOREGROUND_INTENSITY */, COLOR_GREEN);
}

void DBGUI_StartUp(void)
{
	// Start the main window
	dbg.win_main = initscr();

	// Take input chars one at a time, no wait for \n
	cbreak();

	// don't echo input
	noecho();

	nodelay(dbg.win_main, true);
	keypad(dbg.win_main, true);

	resize_term(500, 160);
	touchwin(dbg.win_main);

	old_cursor_state = curs_set(0);
	start_color();
	cycle_count = 0;

	// calculate minimum required size (in rows)
	auto min_size = 1 + dbg.rows_registers
	              + 1 + dbg.rows_data[0U]
	              + 1 + dbg.rows_code
	              + 1 + dbg.rows_variables
	              + 1 + dbg.rows_output;

	if (getmaxy(dbg.win_main) - min_size <= 0) {
		LOG_ERR("DEBUG: Couldn't fit layout elements, screen size is too small");
		dbg.win_main = NULL;
		endwin();
		return;
	}

	MakePairs();
	MakeSubWindows();
}

#endif
