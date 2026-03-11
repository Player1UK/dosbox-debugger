// SPDX-FileCopyrightText:  2002-2025 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dosbox.h"

#if C_DEBUGGER
#include <SDL3/SDL.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include "cbreakpoint.h"
#include "cpu/cpu.h"
#include "cpu/paging.h"
#include "debugger_inc.h"
#include "utils/string_utils.h"

#include "IBM_VGA_8x16.h"

extern DBGBlock dbg;
extern bool debugging;

extern FILE* debuglog;
extern std::vector<CDebugVar*> varList;

SCodeViewData codeViewData = {};

// Scroll state for Output window (lines from bottom, 0 = at bottom)
static int output_scroll_offset = 0;
Bitu cycle_count = 0;

bool showExtend;
bool showPrintable = true;

char curSelectorName[3] = {0, 0, 0};

static bool imgui_initialized = false;
static float display_scale = 1.0f;

bool DBGUI_IsInitialized()
{
	return imgui_initialized;
}

#define MAX_LOG_BUFFER 500
static std::list<std::string> logBuff = {};
static std::list<std::string>::iterator logBuffPos = logBuff.end();

void DEBUG_ShowMsg(const char* format, ...)
{
	if (!imgui_initialized) {
		return;
	}

	char buf[DBGUI::MsgBufferSize];
	va_list msg;
	va_start(msg, format);
	vsnprintf(buf, sizeof(buf), format, msg);
	va_end(msg);

	buf[sizeof(buf) - 1] = '\0';

	/* Add newline if not present */
	size_t len = safe_strlen(buf);
	if (len > 0 && buf[len - 1] != '\n' && len + 1 < sizeof(buf)) {
		strcat(buf, "\n");
	}

	if (debuglog) {
		fprintf(debuglog, "%s", buf);
		fflush(debuglog);
	}

	logBuff.emplace_back(buf);
	if (logBuff.size() > DBGUI::MaxLogBuffer) {
		logBuff.pop_front();
	}
	// Don't reset scroll offset - let user stay at their scroll position
}

void DEBUG_RefreshPage(int scroll)
{
	if (!imgui_initialized) {
		return;
	}

	output_scroll_offset -= scroll;
	if (output_scroll_offset < 0) {
		output_scroll_offset = 0;
	}
}

void SetCodeWinStart()
{
	if ((SegValue(cs) == codeViewData.useCS) && (reg_eip >= codeViewData.useEIP) &&
		(reg_eip <= codeViewData.useEIPlast)) {
		// in valid window - scroll ?
		if (reg_eip >= codeViewData.useEIPmid) {
			codeViewData.useEIP += codeViewData.firstInstSize;
		}

	}
	else {
		// totally out of range.
		codeViewData.useCS = SegValue(cs);
		codeViewData.useEIP = codeViewData.goodEIP = reg_eip;
	}
	codeViewData.cursorPos = -1; // Recalc Cursor position
}

/********************/
/*   Draw windows   */
/********************/

extern char* AnalyzeInstruction(char*, bool);
extern uint32_t GetAddress(uint16_t, uint32_t);
extern bool GetDescriptorInfo(char*, char*, char*);

void DrawCode(void)
{
	if (!DBGUI_IsInitialized()) {
		return;
	}

	// Calculate window dimensions based on character rows/columns
	// (code rows + 1 for input line)
	float line_height      = ImGui::GetTextLineHeightWithSpacing();
	float title_bar_height = ImGui::GetFrameHeight();
	float padding          = ImGui::GetStyle().WindowPadding.y * 2;
	float separator_height = 4.0f;
	float window_width     = DBGUI_GetWindowWidth();
	float window_height = (dbg.rows_code * line_height) + title_bar_height +
	                      padding + separator_height;

	ImGui::SetNextWindowPos(ImVec2(0, DBGUI_GetWindowY(WIN_CODE)),
	                        ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(window_width, window_height),
	                         ImGuiCond_FirstUseEver);

	if (DBGUI_BeginWindowWithStyledTitle("                                      Code               [CTRL]/[SHIFT] Up/Down ",
	                                     ImGuiWindowFlags_NoCollapse)) {
		// Handle mouse wheel scrolling when hovering over this window
		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
			float wheel = ImGui::GetIO().MouseWheel;
			if (wheel > 0) {
				// Scroll up - move cursor up or scroll code
				if (codeViewData.cursorPos > 0) {
					codeViewData.cursorPos--;
				} else {
					codeViewData.useEIP -= 1;
				}
			} else if (wheel < 0) {
				// Scroll down - move cursor down or scroll code
				if (codeViewData.cursorPos < dbg.rows_code - 2) {
					codeViewData.cursorPos++;
				} else {
					codeViewData.useEIP += codeViewData.firstInstSize;
				}
			}
		}

		ImVec4 green_color  = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
		ImVec4 grey_color   = ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
		ImVec4 red_bg_color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);

		// Reserve space at the bottom for the input line
		float input_line_height = ImGui::GetTextLineHeightWithSpacing() + 4.0f;
		float available_height = ImGui::GetContentRegionAvail().y -
		                         input_line_height;

		// Scrollable code region
		ImGui::BeginChild("CodeScrolling",
		                  ImVec2(0, available_height),
		                  false,
		                  ImGuiWindowFlags_HorizontalScrollbar);

		bool saveSel;
		uint32_t disEIP = codeViewData.useEIP;
		PhysPt start = GetAddress(codeViewData.useCS, codeViewData.useEIP);
		char dline[200];
		Bitu size;
		Bitu c;

		for (int i = 0, iMid = dbg.rows_code * 0.95; i < dbg.rows_code - 1; ++i) {
			saveSel = false;
			bool is_current_ip = (codeViewData.useCS == SegValue(cs)) &&
			                     (disEIP == reg_eip);
			bool is_cursor     = (i == codeViewData.cursorPos);
			bool is_breakpoint = CBreakpoint::IsBreakpoint(
			        codeViewData.useCS, disEIP);

			if (is_current_ip) {
				if (codeViewData.cursorPos == -1) {
					codeViewData.cursorPos = i;
				}
				if (is_cursor) {
					codeViewData.cursorSeg = SegValue(cs);
					codeViewData.cursorOfs = disEIP;
				}
				saveSel = is_cursor;
			} else if (is_cursor) {
				codeViewData.cursorSeg = codeViewData.useCS;
				codeViewData.cursorOfs = disEIP;
				saveSel                = true;
			}

			// Build the line
			char line[256];
			char* ptr = line;
			ptr += sprintf(ptr, "%04X:%04X  ", codeViewData.useCS, disEIP);

			Bitu drawsize = size =
			        DasmI386(dline, start, disEIP, cpu.code.big);
			bool toolarge = false;

			if (drawsize > check_cast<uint32_t>(dbg.rows_code - 1)) {
				toolarge = true;
				drawsize = dbg.rows_code - 2;
			}

			// Hex bytes
			for (c = 0; c < drawsize; c++) {
				uint8_t value;
				if (mem_readb_checked(start + c, &value)) {
					value = 0;
				}
				ptr += sprintf(ptr, "%02X", value);
			}
			if (toolarge) {
				ptr += sprintf(ptr, "..");
				drawsize++;
			}

			// Pad hex to fixed width
			int hex_len = drawsize * 2 + (toolarge ? 2 : 0);
			while (hex_len < 20) {
				*ptr++ = ' ';
				hex_len++;
			}

			// Disassembly
			char empty_res[] = {0};
			char* res        = empty_res;
			if (showExtend) {
				res = AnalyzeInstruction(dline, saveSel);
			}

			// Pad disassembly
			size_t dline_len = safe_strlen(dline);
			if (dline_len > 28) {
				dline_len = 28;
			}
			memcpy(ptr, dline, dline_len);
			ptr += dline_len;
			for (size_t pad = dline_len; pad < 28; pad++) {
				*ptr++ = ' ';
			}

			// Result
			if (res && res[0]) {
				size_t res_len = strlen(res);
				if (res_len > 20) {
					res_len = 20;
				}
				memcpy(ptr, res, res_len);
				ptr += res_len;
			}
			*ptr = '\0';

			// Determine color
			if (is_current_ip) {
				ImGui::PushStyleColor(ImGuiCol_Text, green_color);
			} else if (is_breakpoint) {
				ImGui::PushStyleColor(ImGuiCol_Text, red_bg_color);
			} else if (is_cursor) {
				ImGui::PushStyleColor(ImGuiCol_Text, grey_color);
			}

			// Make line selectable
			bool selected = is_cursor;
			if (ImGui::Selectable(line, selected)) {
				codeViewData.cursorPos = i;
				codeViewData.cursorSeg = codeViewData.useCS;
				codeViewData.cursorOfs = disEIP;
			}

			if (is_current_ip || is_breakpoint || is_cursor) {
				ImGui::PopStyleColor();
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

		ImGui::EndChild();

		// Input line or running indicator (fixed at bottom)
		ImGui::Separator();
		if (!debugging) {
			ImGui::PushStyleColor(ImGuiCol_Text, green_color);
			ImGui::Text("(Running)");
			ImGui::PopStyleColor();
		} else {
			char* dispPtr = codeViewData.inputStr;
			ImGui::Text("%c-> %s_",
			            (codeViewData.ovrMode ? 'O' : 'I'),
			            dispPtr);
		}
	}
	DBGUI_EndWindowWithStyledTitle();
}

void DrawConsole(void)
{
}

static void DrawData(void)
{
	if (!DBGUI_IsInitialized()) {
		return;
	}

	// Calculate window dimensions based on character rows/columns
	float line_height      = ImGui::GetTextLineHeightWithSpacing();
	float title_bar_height = ImGui::GetFrameHeight();
	float padding          = ImGui::GetStyle().WindowPadding.y * 2;
	float window_width     = DBGUI_GetWindowWidth();
	float window_height = (dbg.rows_data[dbg.active_win_data] * line_height) + title_bar_height +
	                      padding;

	ImGui::SetNextWindowPos(ImVec2(0, DBGUI_GetWindowY(WIN_DATA)),
	                        ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(window_width, window_height),
	                         ImGuiCond_FirstUseEver);

	if (DBGUI_BeginWindowWithStyledTitle("                                      Data          [CTRL]/[SHIFT] Page Up/Down ",
	                                     ImGuiWindowFlags_NoCollapse)) {
		// Handle mouse wheel scrolling when hovering over this window
		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
			float wheel = ImGui::GetIO().MouseWheel;
			if (wheel != 0.0f) { // Scroll up
				if (wheel > 0.0f && dataOfs[dbg.active_win_data] <= 48U) {
					dataOfs[dbg.active_win_data] = 0U;
				} else {
					dataOfs[dbg.active_win_data] -= 48U * wheel;
				}
			}
		}

		uint8_t ch;
		uint32_t add, address;
		bool f16bit = false;
		for (auto dw = 0U; dw < 1U; ++dw) {
			add = dataOfs[dw];
			for (auto y = 0; y < dbg.rows_data[dw]; ++y) {
				char line[128];

				// Address
				if (add <= 0xFFFF) {
					sprintf(line, "%04X:%04X      ", dataSeg[dw], add);
					f16bit = false;
				}
				else {
					sprintf(line, "%04X:%08X  ", dataSeg[dw], add);
					f16bit = true;
				}

				// Hex values
				for (int x = 0; x < 16; ++x, ++add) {
					address = GetAddress(dataSeg[dw], add);
					if (mem_readb_checked(address, &ch)) {
						ch = 0;
					}
					sprintf(&line[3 * x + (f16bit ? 14 : (11 + (x >> 2)))], " %02X ", ch);
					if (showPrintable) {
						if (ch < 32 ||
							!isprint(static_cast<unsigned char>(ch))) {
							ch = '.';
						}
					}
					else {
						if (ch < 32) {
							ch = '.';
						}
					}
					line[64 + x] = ch;
				}
				if (line[62] == 0) line[62] = ' ';
				if (line[63] != ' ') line[63] = ' ';
				line[80] = '\0';

				ImGui::TextUnformatted(line);
			}
		}
	}
	DBGUI_EndWindowWithStyledTitle();
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

const float fWIDTH = 8.5f;
const float COLUMN[] = { 0.0f * fWIDTH, 16.0f * fWIDTH, 32.0f * fWIDTH, 42.0f * fWIDTH, 52.0f * fWIDTH, 66.0f * fWIDTH, 74.0f * fWIDTH };

static void DrawRegisters(void)
{
	if (!DBGUI_IsInitialized()) {
		return;
	}

	// Calculate window dimensions based on character rows/columns
	float line_height      = ImGui::GetTextLineHeightWithSpacing();
	float title_bar_height = ImGui::GetFrameHeight();
	float padding          = ImGui::GetStyle().WindowPadding.y * 2;
	float window_width     = DBGUI_GetWindowWidth();
	float window_height    = (dbg.rows_registers * line_height) +
	                      title_bar_height + padding;

	ImGui::SetNextWindowPos(ImVec2(0, DBGUI_GetWindowY(WIN_REG)),
	                        ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(window_width, window_height),
	                         ImGuiCond_FirstUseEver);

	if (DBGUI_BeginWindowWithStyledTitle("                                    Registers                                   ",
	                                     ImGuiWindowFlags_NoCollapse)) {
		ImVec4 highlight_color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);

		// Row 1: EAX, ESI, CS, FS, EIP, Mode
		ImGui::Text("EAX");
		ImGui::SameLine();
		if (reg_eax != oldregs.eax) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%04X %04X", (reg_eax >> 16) & 0xFFFF, reg_eax & 0xFFFF);
		if (reg_eax != oldregs.eax) {
			ImGui::PopStyleColor();
		}
		oldregs.eax = reg_eax;

		ImGui::SameLine(COLUMN[1]);
		ImGui::Text("ESI");
		ImGui::SameLine();
		if (reg_esi != oldregs.esi) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%04X %04X", (reg_esi >> 16) & 0xFFFF, reg_esi & 0xFFFF);
		if (reg_esi != oldregs.esi) {
			ImGui::PopStyleColor();
		}
		oldregs.esi = reg_esi;

		ImGui::SameLine(COLUMN[2]);
		ImGui::Text("CS");
		ImGui::SameLine();
		if (SegValue(cs) != oldsegs[cs].val) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%04X", SegValue(cs));
		if (SegValue(cs) != oldsegs[cs].val) {
			ImGui::PopStyleColor();
		}
		oldsegs[cs].val = SegValue(cs);

		ImGui::SameLine(COLUMN[3]);
		ImGui::Text("FS");
		ImGui::SameLine();
		if (SegValue(fs) != oldsegs[fs].val) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%04X", SegValue(fs));
		if (SegValue(fs) != oldsegs[fs].val) {
			ImGui::PopStyleColor();
		}
		oldsegs[fs].val = SegValue(fs);

		ImGui::SameLine(COLUMN[4]);
		ImGui::Text("EIP");
		ImGui::SameLine();
		if (reg_eip != oldregs.eip) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%04X %04X", (reg_eip >> 16) & 0xFFFF, reg_eip & 0xFFFF);
		if (reg_eip != oldregs.eip) {
			ImGui::PopStyleColor();
		}
		oldregs.eip = reg_eip;

		ImGui::SameLine(COLUMN[6]);
		const char* mode_str = "Real";
		if (cpu.pmode) {
			if (reg_flags & FLAG_VM) {
				mode_str = "VM86";
			}
			else if (cpu.code.big) {
				mode_str = "Pr32";
			}
			else {
				mode_str = "Pr16";
			}
		}
		ImGui::Text("%s", mode_str);

		// Row 2: EBX, EDI, DS, GS, Flags
		ImGui::Text("EBX");
		ImGui::SameLine();
		if (reg_ebx != oldregs.ebx) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%04X %04X", (reg_ebx >> 16) & 0xFFFF, reg_ebx & 0xFFFF);
		if (reg_ebx != oldregs.ebx) {
			ImGui::PopStyleColor();
		}
		oldregs.ebx = reg_ebx;

		ImGui::SameLine(COLUMN[1], 0.0f);
		ImGui::Text("EDI");
		ImGui::SameLine();
		if (reg_edi != oldregs.edi) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%04X %04X", (reg_edi >> 16) & 0xFFFF, reg_edi & 0xFFFF);
		if (reg_edi != oldregs.edi) {
			ImGui::PopStyleColor();
		}
		oldregs.edi = reg_edi;

		ImGui::SameLine(COLUMN[2]);
		ImGui::Text("DS");
		ImGui::SameLine();
		if (SegValue(ds) != oldsegs[ds].val) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%04X", SegValue(ds));
		if (SegValue(ds) != oldsegs[ds].val) {
			ImGui::PopStyleColor();
		}
		oldsegs[ds].val = SegValue(ds);

		ImGui::SameLine(COLUMN[3]);
		ImGui::Text("GS");
		ImGui::SameLine();
		if (SegValue(gs) != oldsegs[gs].val) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%04X", SegValue(gs));
		if (SegValue(gs) != oldsegs[gs].val) {
			ImGui::PopStyleColor();
		}
		oldsegs[gs].val = SegValue(gs);


		Bitu changed_flags = reg_flags ^ oldflags;
		oldflags           = reg_flags;

		ImGui::SameLine(COLUMN[4]);
		ImGui::Text("C");
		ImGui::SameLine(0.0f, 0.0f);
		if (changed_flags & FLAG_CF) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%d", GETFLAG(CF) ? 1 : 0);
		if (changed_flags & FLAG_CF) {
			ImGui::PopStyleColor();
		}

		ImGui::SameLine();
		ImGui::Text("Z");
		ImGui::SameLine(0.0f, 0.0f);
		if (changed_flags & FLAG_ZF) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%d", GETFLAG(ZF) ? 1 : 0);
		if (changed_flags & FLAG_ZF) {
			ImGui::PopStyleColor();
		}

		ImGui::SameLine();
		ImGui::Text("S");
		ImGui::SameLine(0.0f, 0.0f);
		if (changed_flags & FLAG_SF) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%d", GETFLAG(SF) ? 1 : 0);
		if (changed_flags & FLAG_SF) {
			ImGui::PopStyleColor();
		}

		ImGui::SameLine();
		ImGui::Text("O");
		ImGui::SameLine(0.0f, 0.0f);
		if (changed_flags & FLAG_OF) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%d", GETFLAG(OF) ? 1 : 0);
		if (changed_flags & FLAG_OF) {
			ImGui::PopStyleColor();
		}

		ImGui::SameLine();
		ImGui::Text("A");
		ImGui::SameLine(0.0f, 0.0f);
		if (changed_flags & FLAG_AF) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%d", GETFLAG(AF) ? 1 : 0);
		if (changed_flags & FLAG_AF) {
			ImGui::PopStyleColor();
		}

		ImGui::SameLine();
		ImGui::Text("P");
		ImGui::SameLine(0.0f, 0.0f);
		if (changed_flags & FLAG_PF) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%d", GETFLAG(PF) ? 1 : 0);
		if (changed_flags & FLAG_PF) {
			ImGui::PopStyleColor();
		}

		ImGui::SameLine();
		ImGui::Text("D");
		ImGui::SameLine(0.0f, 0.0f);
		if (changed_flags & FLAG_DF) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%d", GETFLAG(DF) ? 1 : 0);
		if (changed_flags & FLAG_DF) {
			ImGui::PopStyleColor();
		}

		ImGui::SameLine();
		ImGui::Text("I");
		ImGui::SameLine(0.0f, 0.0f);
		if (changed_flags & FLAG_IF) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%d", GETFLAG(IF) ? 1 : 0);
		if (changed_flags & FLAG_IF) {
			ImGui::PopStyleColor();
		}

		ImGui::SameLine();
		ImGui::Text("T");
		ImGui::SameLine(0.0f, 0.0f);
		if (changed_flags & FLAG_TF) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%d", GETFLAG(TF) ? 1 : 0);
		if (changed_flags & FLAG_TF) {
			ImGui::PopStyleColor();
		}

		// Row 3: ECX, EBP, ES, SS, Cycles, IOPL, CPL
		ImGui::Text("ECX");
		ImGui::SameLine(COLUMN[0]);
		if (reg_ecx != oldregs.ecx) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%04X %04X", (reg_ecx >> 16) & 0xFFFF, reg_ecx & 0xFFFF);
		if (reg_ecx != oldregs.ecx) {
			ImGui::PopStyleColor();
		}
		oldregs.ecx = reg_ecx;

		ImGui::SameLine(COLUMN[1]);
		ImGui::Text("EBP");
		ImGui::SameLine();
		if (reg_ebp != oldregs.ebp) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%04X %04X", (reg_ebp >> 16) & 0xFFFF, reg_ebp & 0xFFFF);
		if (reg_ebp != oldregs.ebp) {
			ImGui::PopStyleColor();
		}
		oldregs.ebp = reg_ebp;

		ImGui::SameLine(COLUMN[2]);
		ImGui::Text("ES");
		ImGui::SameLine();
		if (SegValue(es) != oldsegs[es].val) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%04X", SegValue(es));
		if (SegValue(es) != oldsegs[es].val) {
			ImGui::PopStyleColor();
		}
		oldsegs[es].val = SegValue(es);

		ImGui::SameLine(COLUMN[3]);
		ImGui::Text("SS");
		ImGui::SameLine();
		if (SegValue(ss) != oldsegs[ss].val) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%04X", SegValue(ss));
		if (SegValue(ss) != oldsegs[ss].val) {
			ImGui::PopStyleColor();
		}
		oldsegs[ss].val = SegValue(ss);

		ImGui::SameLine(COLUMN[4]);
		ImGui::Text("%" PRIuPTR, cycle_count);

		ImGui::SameLine(COLUMN[5]);
		ImGui::Text("IOPL");
		ImGui::SameLine(0.0f, 0.0f);
		if (changed_flags & FLAG_IOPL) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%d", (int)(GETFLAG(IOPL) >> 12));
		if (changed_flags & FLAG_IOPL) {
			ImGui::PopStyleColor();
		}

		ImGui::SameLine(COLUMN[6]);
		ImGui::Text("CPL");
		ImGui::SameLine(0.0f, 0.0f);
		if (cpu.cpl != oldcpucpl) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%d", (int)cpu.cpl);
		if (cpu.cpl != oldcpucpl) {
			ImGui::PopStyleColor();
		}
		oldcpucpl = cpu.cpl;

		// Row 4: EDX, ESP
		ImGui::Text("EDX");
		ImGui::SameLine();
		if (reg_edx != oldregs.edx) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%04X %04X", (reg_edx >> 16) & 0xFFFF, reg_edx & 0xFFFF);
		if (reg_edx != oldregs.edx) {
			ImGui::PopStyleColor();
		}
		oldregs.edx = reg_edx;

		ImGui::SameLine(COLUMN[1]);
		ImGui::Text("ESP");
		ImGui::SameLine();
		if (reg_esp != oldregs.esp) {
			ImGui::PushStyleColor(ImGuiCol_Text, highlight_color);
		}
		ImGui::Text("%04X %04X", (reg_esp >> 16) & 0xFFFF, reg_esp & 0xFFFF);
		if (reg_esp != oldregs.esp) {
			ImGui::PopStyleColor();
		}
		oldregs.esp = reg_esp;

		// Selector info, if available
		if ((cpu.pmode) && curSelectorName[0]) {
			char out1[200], out2[200];
			GetDescriptorInfo(curSelectorName, out1, out2);
			ImGui::SameLine(COLUMN[2]);
			ImGui::Text("%s %s", out1, out2);
		}
	}
	DBGUI_EndWindowWithStyledTitle();
}

#define DEBUG_VAR_BUF_LEN 16
static void DrawVariables()
{
	if (!DBGUI_IsInitialized()) {
		return;
	}

	// Calculate window dimensions based on character rows/columns
	float line_height      = ImGui::GetTextLineHeightWithSpacing();
	float title_bar_height = ImGui::GetFrameHeight();
	float padding          = ImGui::GetStyle().WindowPadding.y * 2;
	float window_width     = DBGUI_GetWindowWidth();
	float window_height    = (dbg.rows_variables * line_height) +
	                      title_bar_height + padding;

	ImGui::SetNextWindowPos(ImVec2(0, DBGUI_GetWindowY(WIN_VAR)),
	                        ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(window_width, window_height),
	                         ImGuiCond_FirstUseEver);

	if (DBGUI_BeginWindowWithStyledTitle("                                    Variables                                   ",
	                                     ImGuiWindowFlags_NoCollapse)) {
		if (varList.empty()) {
			ImGui::TextDisabled("(no variables defined)");
		} else {
			char buffer[DEBUG_VAR_BUF_LEN] = {};

			for (size_t i = 0; i < 4 * 3 && i != varList.size(); ++i) {
				auto dv = varList[i];
				uint16_t value;
				bool has_no_value = mem_readw_checked(dv->GetAdr(),
				                                      &value);

				if (has_no_value) {
					snprintf(buffer, DEBUG_VAR_BUF_LEN, "%s", "??????");
					dv->SetValue(false, 0);
				} else {
					if (!dv->HasValue() ||
					    dv->GetValue() != value) {
						dv->SetValue(true, value);
					}
					snprintf(buffer, DEBUG_VAR_BUF_LEN, "0x%04x", value);
				}

				if (i % 3 != 0) {
					ImGui::SameLine();
				}
				ImGui::Text("%s: %s", dv->GetName(), buffer);
			}
		}
	}
	DBGUI_EndWindowWithStyledTitle();
}
#undef DEBUG_VAR_BUF_LEN

// Calculate window height for a given number of rows
static float CalcWindowHeight(int rows)
{
	float line_height      = ImGui::GetTextLineHeightWithSpacing();
	float title_bar_height = ImGui::GetFrameHeight();
	float padding          = ImGui::GetStyle().WindowPadding.y * 2;
	return (rows * line_height) + title_bar_height + padding;
}

// Calculate window width for a given number of columns
static float CalcWindowWidth(int cols)
{
	// Use a representative character to get monospace font width
	float char_width = ImGui::CalcTextSize("X").x;
	float padding    = ImGui::GetStyle().WindowPadding.x * 2;
	return (cols * char_width) + padding;
}

// Get the calculated Y positions for each window
float DBGUI_GetWindowY(uint32_t window_index)
{
	float y = 0;
	switch (window_index) {
	case NUM_WINDOWS:
		y += CalcWindowHeight(dbg.rows_output);
	case WIN_OUT:
		y += CalcWindowHeight(dbg.rows_variables);
	case WIN_VAR:
		y += CalcWindowHeight(dbg.rows_data[0]);
	case WIN_DATA:
		y += CalcWindowHeight(dbg.rows_registers);
	case WIN_REG:
		y += CalcWindowHeight(dbg.rows_code) +
			DBGUI::WindowSeparatorSpacing; // After Code (with separator)
	case WIN_CODE:
	default:
		break;
	}
	return y;
}

// Calculate total height needed for all windows
float DBGUI_GetTotalHeight()
{
	return DBGUI_GetWindowY(NUM_WINDOWS);
}

// Calculate window width based on column count
float DBGUI_GetWindowWidth()
{
	return CalcWindowWidth(dbg.window_cols);
}

void DEBUG_DrawScreen(void)
{
	DrawData();
	DrawCode();
	DrawRegisters();
	DrawVariables();
	DBGUI_DrawOutputWindow();
}

void DBGUI_StartUp(void)
{
	if (imgui_initialized) {
		return;
	}

	// Get display scale for high DPI support
	display_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
	if (display_scale <= 0.0f) {
		display_scale = 1.0f;
	}

	// Create debugger window with initial size (will be resized after ImGui
	// init). Use approximate pixel values before ImGui metrics are
	// available, scaled for high DPI displays.
	constexpr int InitialWindowWidth   = 800;
	constexpr int InitialWindowHeight  = 600;
	const SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE |
	                                     SDL_WINDOW_HIDDEN |
	                                     SDL_WINDOW_HIGH_PIXEL_DENSITY;

	dbg.win_main = SDL_CreateWindow(
	        "DOSBox Staging Debugger",
	        static_cast<int>(InitialWindowWidth * display_scale),
	        static_cast<int>(InitialWindowHeight * display_scale),
	        window_flags);

	if (!dbg.win_main) {
		LOG_ERR("DEBUG: Failed to create debugger window: %s",
		        SDL_GetError());
		return;
	}

	// Create GPU device - SDL_GPU uses Vulkan/Metal/D3D12 under the hood,
	// avoiding OpenGL context conflicts with the main DOSBox window
	dbg.gpu_device = SDL_CreateGPUDevice(
	        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
	                SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB,
	        true,
	        nullptr);
	if (!dbg.gpu_device) {
		LOG_ERR("DEBUG: Failed to create GPU device: %s", SDL_GetError());
		SDL_DestroyWindow(dbg.win_main);
		dbg.win_main = nullptr;
		return;
	}

	if (!SDL_ClaimWindowForGPUDevice(dbg.gpu_device, dbg.win_main)) {
		LOG_ERR("DEBUG: Failed to claim window for GPU device: %s",
		        SDL_GetError());
		SDL_DestroyGPUDevice(dbg.gpu_device);
		SDL_DestroyWindow(dbg.win_main);
		dbg.gpu_device = nullptr;
		dbg.win_main   = nullptr;
		return;
	}

	// Setup ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.IniFilename = nullptr; // Disable saving/loading window layout

	// Setup ImGui style
	ImGui::StyleColorsDark();
	ImGuiStyle& style       = ImGui::GetStyle();
	style.WindowRounding    = DBGUI::WindowRounding;
	style.FrameRounding     = DBGUI::FrameRounding;
	style.ScrollbarRounding = DBGUI::ScrollbarRounding;
	style.FontSizeBase      = DBGUI::FontSize;

	// Scale style for high DPI displays
	style.ScaleAllSizes(display_scale);
	style.FontScaleDpi = display_scale;

	// Setup Platform/Renderer backends for SDL_GPU
	ImGui_ImplSDLGPU3_InitInfo init_info   = {};
	init_info.Device                       = dbg.gpu_device;
	init_info.ColorTargetFormat            = SDL_GetGPUSwapchainTextureFormat(
                dbg.gpu_device, dbg.win_main);
	init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;

	ImGui_ImplSDL3_InitForOther(dbg.win_main);
	ImGui_ImplSDLGPU3_Init(&init_info);

	io.Fonts->AddFontFromMemoryCompressedTTF(IBM_VGA_8x16_compressed_data,
	                                         IBM_VGA_8x16_compressed_size);

	imgui_initialized = true;
	cycle_count       = 0;

	// Now that ImGui is initialized, resize the window to fit all child
	// windows. Need to do a dummy frame to get accurate font metrics.
	ImGui_ImplSDLGPU3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	// Calculate window dimensions from character rows/columns
	dbg.window_width  = static_cast<int>(DBGUI_GetWindowWidth());
	dbg.window_height = static_cast<int>(DBGUI_GetTotalHeight());
	SDL_SetWindowSize(dbg.win_main, dbg.window_width, dbg.window_height);
	SDL_SetWindowPosition(dbg.win_main,
	                      SDL_WINDOWPOS_CENTERED,
	                      SDL_WINDOWPOS_CENTERED);
	SDL_ShowWindow(dbg.win_main);

	ImGui::EndFrame();
}

void DBGUI_Shutdown(void)
{
	if (!imgui_initialized) {
		return;
	}

	ImGui_ImplSDLGPU3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();

	if (dbg.gpu_device) {
		SDL_ReleaseWindowFromGPUDevice(dbg.gpu_device, dbg.win_main);
		SDL_DestroyGPUDevice(dbg.gpu_device);
		dbg.gpu_device = nullptr;
	}

	if (dbg.win_main) {
		SDL_DestroyWindow(dbg.win_main);
		dbg.win_main = nullptr;
	}

	imgui_initialized = false;
}

void DBGUI_NewFrame(void)
{
	if (!imgui_initialized) {
		return;
	}

	ImGui_ImplSDLGPU3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();
}

void DBGUI_Render(void)
{
	if (!imgui_initialized) {
		return;
	}

	ImGui::Render();

	SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(
	        dbg.gpu_device);
	if (!command_buffer) {
		LOG_ERR("DEBUG: Failed to acquire GPU command buffer: %s",
		        SDL_GetError());
		return;
	}

	SDL_GPUTexture* swapchain_texture = nullptr;
	if (!SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer,
	                                           dbg.win_main,
	                                           &swapchain_texture,
	                                           nullptr,
	                                           nullptr)) {
		LOG_ERR("DEBUG: Failed to acquire swapchain texture: %s",
		        SDL_GetError());
		SDL_SubmitGPUCommandBuffer(command_buffer);
		return;
	}

	if (swapchain_texture) {
		ImDrawData* draw_data = ImGui::GetDrawData();

		// Must call PrepareDrawData before the render pass
		ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, command_buffer);

		// Set up the color target with clear color
		SDL_GPUColorTargetInfo target_info = {};
		target_info.texture                = swapchain_texture;
		target_info.clear_color.r = DBGUI::ClearColorR / 255.0f;
		target_info.clear_color.g = DBGUI::ClearColorG / 255.0f;
		target_info.clear_color.b = DBGUI::ClearColorB / 255.0f;
		target_info.clear_color.a = DBGUI::ClearColorA / 255.0f;
		target_info.load_op       = SDL_GPU_LOADOP_CLEAR;
		target_info.store_op      = SDL_GPU_STOREOP_STORE;

		SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(
		        command_buffer, &target_info, 1, nullptr);

		ImGui_ImplSDLGPU3_RenderDrawData(draw_data,
		                                 command_buffer,
		                                 render_pass);

		SDL_EndGPURenderPass(render_pass);
	}

	SDL_SubmitGPUCommandBuffer(command_buffer);
}

// Title bar colors - cyan background with black text (classic DOS style)
static const ImVec4 TitleBgColor   = ImVec4(0.0f, 0.667f, 0.667f, 1.0f); // Cyan
static const ImVec4 TitleTextColor = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);     // Black

// Helper to begin a window with cyan title bar and black title text
static bool BeginWindowWithStyledTitle(const char* title, ImGuiWindowFlags flags)
{
	// Push title bar colors
	ImGui::PushStyleColor(ImGuiCol_TitleBg, TitleBgColor);
	ImGui::PushStyleColor(ImGuiCol_TitleBgActive, TitleBgColor);
	ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, TitleBgColor);
	ImGui::PushStyleColor(ImGuiCol_Text, TitleTextColor);

	bool result = ImGui::Begin(title, nullptr, flags);

	// Restore text color for window content (keep title bar colors)
	ImGui::PopStyleColor(); // Pop text color

	return result;
}

// Helper to end a styled window
static void EndWindowWithStyledTitle()
{
	ImGui::End();
	ImGui::PopStyleColor(3); // Pop title bar colors
}

// Public versions for use from debugger.cpp
bool DBGUI_BeginWindowWithStyledTitle(const char* title, int flags)
{
	return BeginWindowWithStyledTitle(title, static_cast<ImGuiWindowFlags>(flags));
}

void DBGUI_EndWindowWithStyledTitle()
{
	EndWindowWithStyledTitle();
}

// Render functions for debugger windows - called from debugger.cpp
void DBGUI_DrawRegisterWindow(void)
{
	if (!imgui_initialized) {
		return;
	}

	// Calculate window dimensions based on character rows/columns
	float window_width  = DBGUI_GetWindowWidth();
	float window_height = CalcWindowHeight(dbg.rows_registers);

	ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(window_width, window_height),
	                         ImGuiCond_FirstUseEver);

	if (ImGui::Begin("Registers", nullptr, ImGuiWindowFlags_NoCollapse)) {
		// Row 1: EAX, ESI, DS, ES, FS, GS, SS
		ImGui::Text("EAX=%08X  ESI=%08X  DS=%04X  ES=%04X  FS=%04X  GS=%04X  SS=%04X",
		            reg_eax,
		            reg_esi,
		            SegValue(ds),
		            SegValue(es),
		            SegValue(fs),
		            SegValue(gs),
		            SegValue(ss));

		// Row 2: EBX, EDI, CS, EIP, Flags
		ImGui::Text("EBX=%08X  EDI=%08X  CS=%04X  EIP=%08X  C=%d Z=%d S=%d O=%d A=%d P=%d D=%d I=%d T=%d",
		            reg_ebx,
		            reg_edi,
		            SegValue(cs),
		            reg_eip,
		            GETFLAG(CF) ? 1 : 0,
		            GETFLAG(ZF) ? 1 : 0,
		            GETFLAG(SF) ? 1 : 0,
		            GETFLAG(OF) ? 1 : 0,
		            GETFLAG(AF) ? 1 : 0,
		            GETFLAG(PF) ? 1 : 0,
		            GETFLAG(DF) ? 1 : 0,
		            GETFLAG(IF) ? 1 : 0,
		            GETFLAG(TF) ? 1 : 0);

		// Row 3: ECX, EBP, IOPL, CPL
		ImGui::Text("ECX=%08X  EBP=%08X  IOPL=%d  CPL=%d",
		            reg_ecx,
		            reg_ebp,
		            (int)(GETFLAG(IOPL) >> 12),
		            (int)cpu.cpl);

		// Row 4: EDX, ESP, Mode, Cycles
		const char* mode_str = "Real";
		if (cpu.pmode) {
			if (GETFLAG(VM)) {
				mode_str = "VM86";
			} else if (cpu.code.big) {
				mode_str = "Pr32";
			} else {
				mode_str = "Pr16";
			}
		}
		ImGui::Text("EDX=%08X  ESP=%08X  %s  Cycles: %" PRIuPTR,
		            reg_edx,
		            reg_esp,
		            mode_str,
		            cycle_count);
	}
	ImGui::End();
}

void DBGUI_DrawOutputWindow(void)
{
	if (!imgui_initialized) {
		return;
	}

	// Calculate window dimensions based on character rows/columns
	float window_width  = DBGUI_GetWindowWidth();
	float window_height = CalcWindowHeight(dbg.rows_output);

	ImGui::SetNextWindowPos(ImVec2(0, DBGUI_GetWindowY(WIN_OUT)),
	                        ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(window_width, window_height),
	                         ImGuiCond_FirstUseEver);

	if (DBGUI_BeginWindowWithStyledTitle("                                     Output                    [SHIFT] Home/End ",
	                                ImGuiWindowFlags_NoCollapse |
	                                ImGuiWindowFlags_NoScrollbar |
	                                ImGuiWindowFlags_NoScrollWithMouse)) {
		// Handle mouse wheel scrolling when hovering over this window
		if (ImGui::IsWindowHovered()) {
			float wheel = ImGui::GetIO().MouseWheel;
			if (wheel > 0) {
				output_scroll_offset++; // Scroll up (older messages)
			} else if (wheel < 0 && output_scroll_offset > 0) {
				output_scroll_offset--; // Scroll down (newer messages)
			}
		}

		// Calculate how many lines we can display
		int visible_lines = dbg.rows_output - 1; // -1 for title bar
		int total_lines = static_cast<int>(logBuff.size());

		// Clamp scroll offset to valid range
		int max_offset = total_lines > visible_lines ? total_lines - visible_lines : 0;
		if (output_scroll_offset > max_offset) {
			output_scroll_offset = max_offset;
		}

		// Calculate start index (from the end, accounting for offset)
		int start_idx = total_lines - visible_lines - output_scroll_offset;
		if (start_idx < 0) {
			start_idx = 0;
		}

		// Display visible lines
		auto it = logBuff.begin();
		std::advance(it, start_idx);
		int lines_shown = 0;
		while (it != logBuff.end() && lines_shown < visible_lines) {
			ImGui::TextUnformatted(it->c_str());
			++it;
			++lines_shown;
		}
	}
	EndWindowWithStyledTitle();
}

#endif
