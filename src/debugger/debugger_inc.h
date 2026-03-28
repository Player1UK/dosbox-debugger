// SPDX-FileCopyrightText:  2002-2025 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

/* Local Debug Function */

#ifndef DOSBOX_DEBUGGER_INC_H
#define DOSBOX_DEBUGGER_INC_H

#include "dosbox.h"

#if C_DEBUGGER
#include <SDL3/SDL.h>
#include <queue>

enum DebugColorPairs {
	PAIR_BLACK_BLUE = 1,
	PAIR_BYELLOW_BLACK = 2,
	PAIR_GREEN_BLACK = 3,
	PAIR_BLACK_GREY = 4,
	PAIR_GREY_RED = 5,
	PAIR_BLACK_GREEN = 6,
};

typedef enum Window_ID :uint8_t {
	WIN_CODE = 0U,
	WIN_REG,
	WIN_SEG,
	WIN_CON,
	WIN_OUT,
	WIN_CALLS,
	WIN_JUMPS,
	WIN_VAR,
	WIN_DATA,
	WIN_STACK,
	NUM_WINDOWS
} WINDOW_ID;
WINDOW_ID &operator++( WINDOW_ID &id );

bool DBGUI_StartUp( );
void DBGUI_Shutdown( );
void DBGUI_NewFrame( );
void DBGUI_Render( );
void DBGUI_Reset( );
void DBGUI_SaveCPUstate( );
void DBGUI_UpdateOrderedSegments( bool refresh = true );

// Window title styling helpers (cyan background, black text)
bool DBGUI_BeginWindowWithStyledTitle( const char* title, int flags );
void DBGUI_EndWindowWithStyledTitle( );
bool DBGUI_BeginWindow( const char* name, int flags );
void DBGUI_EndWindow( );

// GUI layout and styling constants
namespace DBGUI {

	// Buffer sizes
	constexpr size_t MaxLogBuffer = 500;
	constexpr size_t MsgBufferSize = 512;
	constexpr size_t LogNameBufferSize = 64;

	// Window dimensions (in characters/rows)
	constexpr uint8_t DefaultWindowCols = 80;

	// ImGui style
	constexpr float WindowRounding = 0.0f;
	constexpr float FrameRounding = 0.0f;
	constexpr float ScrollbarRounding = 0.0f;
	constexpr float FontSize = 17.0f;

	// Layout spacing
	constexpr float WindowSeparatorSpacing = 4.0f;
	constexpr float AutoScrollThreshold = 0.0f;

	// Clear color (RGBA)
	constexpr uint8_t ClearColorR = 0;
	constexpr uint8_t ClearColorG = 0;
	constexpr uint8_t ClearColorB = 0;
	constexpr uint8_t ClearColorA = 255;

} // namespace DBGUI
const uint8_t NUM_WIN_DATA = 2;

typedef struct ColumnRows {
	uint8_t		column;
	uint8_t		rows;
} COLUMNROWS;

struct DBGBlock {
	SDL_Window* win_main = nullptr;
	SDL_GPUDevice* gpu_device = nullptr;
	uint32_t active_win = 0;
	uint8_t active_win_data = 0;    /* Current active data window */
	bool update_win[NUM_WINDOWS] = { true, true, true, true, true, true, true, true, true, true };
	bool update_win_frame[NUM_WINDOWS] = { false, false, false, false, false, false, false, false, false, false };
	bool update_win_scroll[NUM_WINDOWS] = { false, false, false, false, false, false, false, false, false, false };
	uint32_t input_y = 0;
	uint32_t global_mask = 0;
	/* Window height values in rows */
	COLUMNROWS columnRows[NUM_WINDOWS] = { { 1U, 60U }, { 1U, 4U }, { 1U, 1U }, { 1U, 1U }, { 1U, 36U }, { 0U, 41U }, { 0U, 64U }, { 2U, 4U }, { 2U, 60U }, { 2U, 40U } };

	// Window dimensions (in characters)
	const uint8_t window_cols[3] = { ( DBGUI::DefaultWindowCols >> 2 ), DBGUI::DefaultWindowCols, DBGUI::DefaultWindowCols };

	// Computed window dimensions (in pixels, calculated from rows/cols)
	int window_width = 0;
	int window_height = 0;
};

extern uint16_t dataSeg[NUM_WIN_DATA];
extern uint32_t dataOfs[NUM_WIN_DATA];

extern DBGBlock dbg;

#define MAXCMDLEN 254
struct SCodeViewData {
	uint16_t firstInstSize = 0;
	uint16_t useCS = 0;
	uint32_t useEIP = 0;
	uint16_t cursorSeg = 0;
	uint32_t cursorOfs = 0;

	bool ovrMode = false;

	char inputStr[MAXCMDLEN + 1] = {};
	char suspInputStr[MAXCMDLEN + 1] = {};
};

// Event queue for debugger input
struct DebuggerInputEvent {
	SDL_Event ev = {};
	std::string text = {};
};
extern std::queue<DebuggerInputEvent> debugger_event_queue;

#endif // C_DEBUGGER

#endif // DOSBOX_DEBUGGER_INC_H