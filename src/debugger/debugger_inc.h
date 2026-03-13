// SPDX-FileCopyrightText:  2002-2025 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

/* Local Debug Function */

#ifndef DOSBOX_DEBUGGER_INC_H
#define DOSBOX_DEBUGGER_INC_H

#include "dosbox.h"

#if C_DEBUGGER

#include "hardware/memory.h"
#include <SDL3/SDL.h>
#include <queue>

enum DebugColorPairs {
	PAIR_BLACK_BLUE    = 1,
	PAIR_BYELLOW_BLACK = 2,
	PAIR_GREEN_BLACK   = 3,
	PAIR_BLACK_GREY    = 4,
	PAIR_GREY_RED      = 5,
	PAIR_BLACK_GREEN   = 6,
};

void DBGUI_StartUp();
void DBGUI_Shutdown();
void DBGUI_NewFrame();
void DBGUI_Render();
bool DBGUI_IsInitialized();
void DBGUI_DrawOutputWindow();
float DBGUI_GetWindowY(uint32_t window_index);
float DBGUI_GetTotalHeight();
float DBGUI_GetWindowWidth();

// Window title styling helpers (cyan background, black text)
bool DBGUI_BeginWindowWithStyledTitle(const char* title, int flags);
void DBGUI_EndWindowWithStyledTitle();
bool DBGUI_BeginWindow( const char* name, int flags );
void DBGUI_EndWindow( );

// Key constants (matching ncurses/PDCurses)
constexpr int KEY_NONE            = -1;
constexpr int DBGUI_KEY_CLOSE     = 0x162; /* close key */
constexpr int DBGUI_KEY_UP        = 0x103;
constexpr int DBGUI_KEY_DOWN      = 0x102;
constexpr int DBGUI_KEY_SUP       = 0x223; /* Shifted up arrow */
constexpr int DBGUI_KEY_SDOWN     = 0x224; /* Shifted down arrow */
constexpr int DBGUI_CTL_UP        = 0x1e0; /* ctl-up arrow */
constexpr int DBGUI_CTL_DOWN      = 0x1e1; /* ctl-down arrow */
constexpr int DBGUI_CTL_TAB       = 0x1e2; /* ctl-tab */
constexpr int DBGUI_KEY_BTAB      = 0x15f; /* Back tab key */
constexpr int DBGUI_KEY_LEFT      = 0x104;
constexpr int DBGUI_KEY_RIGHT     = 0x105;
constexpr int DBGUI_CTL_PGUP      = 0x1bd;
constexpr int DBGUI_CTL_PGDN      = 0x1be;
constexpr int DBGUI_KEY_PPAGE     = 0x153;
constexpr int DBGUI_KEY_SPREVIOUS = 0x18c; /* shifted prev key */
constexpr int DBGUI_KEY_NPAGE     = 0x152;
constexpr int DBGUI_KEY_SNEXT     = 0x18a; /* shifted next key */
constexpr int DBGUI_KEY_HOME      = 0x106;
constexpr int DBGUI_KEY_SHOME     = 0x184; /* shifted home key */
constexpr int DBGUI_KEY_SEND      = 0x180; /* shifted end key */
constexpr int DBGUI_KEY_END       = 0x166;
constexpr int DBGUI_KEY_BACKSPACE = 0x107;
constexpr int DBGUI_KEY_DC        = 0x14A; // Delete character
constexpr int DBGUI_KEY_IC        = 0x14B; // Insert character
constexpr int DBGUI_KEY_F(int n)
{
	return 0x108 + n - 1;
}

// GUI layout and styling constants
namespace DBGUI {

// Buffer sizes
constexpr size_t MaxLogBuffer      = 500;
constexpr size_t MsgBufferSize     = 512;
constexpr size_t LogNameBufferSize = 64;

// Window dimensions (in characters/rows)
// +2 to account for padding
constexpr int DefaultWindowCols = 82;

// ImGui style
constexpr float WindowRounding    = 0.0f;
constexpr float FrameRounding     = 0.0f;
constexpr float ScrollbarRounding = 0.0f;
constexpr float FontSize          = 18.0f;

// Layout spacing
constexpr float WindowSeparatorSpacing = 4.0f;
constexpr float AutoScrollThreshold    = 0.0f;

// Clear color (RGBA)
constexpr uint8_t ClearColorR = 0;
constexpr uint8_t ClearColorG = 0;
constexpr uint8_t ClearColorB = 0;
constexpr uint8_t ClearColorA = 255;

} // namespace DBGUI
const uint32_t NUM_WIN_DATA = 1;

enum WINDOW_ID :uint32_t {
	WIN_CODE = 0,
	WIN_REG,
	WIN_DATA,
	WIN_VAR,
	WIN_CON,
	WIN_OUT,
	NUM_WINDOWS
};

struct DBGBlock {
	SDL_Window* win_main     = nullptr;
	SDL_GPUDevice* gpu_device = nullptr;
	uint32_t active_win    = 0;
	uint32_t active_win_data = 0;    /* Current active data window */
	bool update_win[NUM_WINDOWS] = { true, true, true, true, true, true };
	uint32_t input_y       = 0;
	uint32_t global_mask   = 0;
	/* Window height values in rows */
	int32_t rows_registers = 4;
	int32_t rows_data[NUM_WIN_DATA] = { 48 };// , 50, 50, 50 };
	int32_t rows_code      = 36;
	int32_t rows_variables = 4;
	int32_t rows_console   = 1;
	int32_t rows_output    = 8;

	// Scrolling state
	float output_scroll_y = 0.0f;

	// Window dimensions (in characters)
	int32_t window_cols = DBGUI::DefaultWindowCols;

	// Computed window dimensions (in pixels, calculated from rows/cols)
	int window_width  = 0;
	int window_height = 0;
};

extern uint16_t dataSeg[NUM_WIN_DATA];
extern uint32_t dataOfs[NUM_WIN_DATA];

struct DASMLine {
	uint32_t pc = 0;
	char dasm[80] = {0};
	PhysPt ea = 0;
	uint16_t easeg = 0;
	uint32_t eaoff = 0;
};

extern DBGBlock dbg;

#define MAXCMDLEN 254
struct SCodeViewData {
	int cursorPos          = 0;
	uint16_t firstInstSize = 0;
	uint16_t useCS         = 0;
	uint32_t useEIPlast    = 0;
	uint32_t useEIPmid     = 0;
	uint32_t useEIP        = 0;
	uint32_t goodEIP       = 0;
	uint16_t cursorSeg     = 0;
	uint32_t cursorOfs     = 0;

	bool ovrMode = false;

	char inputStr[MAXCMDLEN + 1]     = {};
	char suspInputStr[MAXCMDLEN + 1] = {};

	int inputPos = 0;
};


// Event queue for debugger input
struct DebuggerInputEvent {
	SDL_Event ev     = {};
	std::string text = {};
};

extern std::queue<DebuggerInputEvent> debugger_event_queue;

/* Local Debug Stuff */
Bitu DasmI386(char* buffer, PhysPt pc, Bitu cur_ip, bool bit32);
int DasmLastOperandSize();

#endif // C_DEBUGGER

#endif // DOSBOX_DEBUGGER_INC_H
