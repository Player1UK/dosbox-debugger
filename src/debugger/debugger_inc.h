// SPDX-FileCopyrightText:  2002-2025 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

/* Local Debug Function */

#ifndef DOSBOX_DEBUGGER_INC_H
#define DOSBOX_DEBUGGER_INC_H

#include "dosbox.h"

#if C_DEBUGGER
#include "cpu/registers.h"
#include "debugtypes.h"
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
	WIN_OUT,
	WIN_LABELS,
	WIN_DIFF,
	WIN_VAR,
	WIN_DATA,
	WIN_STACK,
	NUM_WINDOWS
} WINDOW_ID;
WINDOW_ID &operator++( WINDOW_ID & );

typedef enum Data_ID :uint8_t {
	DATA_VIEW = 0U,
	STACK_VIEW,
	NUM_DATA_VIEWS
} DATA_ID;
DATA_ID &operator++( DATA_ID & );

bool DBGUI_StartUp( );
void DBGUI_Shutdown( );
void DBGUI_NewFrame( );
void DBGUI_Render( );
void DBGUI_Reset( );
void DBGUI_Resize( );
void DBGUI_Resume( );

extern int32_t DEBUG_Run( const RUN_TYPE, const ADDRESS_PAIR & = { 0U, 0U } );

void DEBUG_SaveCurrentState( );

void DEBUG_ShowDOSBox( );
void DEBUG_HideDOSBox( );

extern const char * AnalyzeInstruction( const char *, const char *, char * = nullptr );
extern bool ParseCommand( char * );
extern uint32_t GetPhysicalAddress( const ADDRESS_PAIR & );
static SIZE_TYPE hex_value_size_type = SIZE_BYTE;
static uint32_t hex_value = 0U;
extern uint32_t GetHexValue( char *&, SIZE_TYPE & = hex_value_size_type );
extern void ResetHexValueSizeType( );
extern uint16_t RealSegValue( const SegNames index );
extern bool GetDescriptorInfo( char *, char *, char * );

extern int64_t normalLoopTickCount;

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

typedef struct ColumnRows {
	uint8_t		column;
	uint8_t		rows;
} COLUMNROWS;

static const uint8_t NUM_COLUMNS = 4U;

struct DBGBlock {
	SDL_Window *win_main = nullptr;
	SDL_Window *graphics_window = nullptr;
	bool graphics_window_hidden = false;
	SDL_GPUDevice *gpu_device = nullptr;
	DATA_ID active_data_view = DATA_VIEW;    /* Current active data window */
	bool update_win[NUM_WINDOWS] = { true, true, true, true, true, true, false, true, true };
	bool update_win_frame[NUM_WINDOWS] = { false, false, false, false, false, false, false, false, false };
	bool update_win_scroll[NUM_WINDOWS] = { false, false, false, false, false, false, false, false, false };
	bool visible[NUM_WINDOWS] = { true, true, true, true, true, true, false, true, true };
	bool fTitleBar[NUM_WINDOWS] = { false, false, false, false, false, false, false, false, false };
	uint32_t input_y = 0U;
	uint32_t global_mask = 0U;
	/* Window column selection and height values in rows */
	COLUMNROWS columnRows[NUM_WINDOWS] = { { 1U, 66U }, { 1U, 4U }, { 1U, 8U }, { 1U, 29U }, { 0U, 107U }, { 3U, 107U }, { 2U, 0U }, { 2U, 74U }, { 2U, 33U } };

	// Window dimensions (in characters)
	const uint8_t window_cols[NUM_COLUMNS] = { 12U, DBGUI::DefaultWindowCols + 14U, DBGUI::DefaultWindowCols - 1U, 19U };
	const int8_t height_ratio[NUM_WINDOWS] = { 65, -4, -8, 0, 0, 0, -4, 73, 0 };

	// Computed window dimensions (in pixels, calculated from rows/cols)
	SDL_Rect window_rect;
	uint16_t segment[NUM_SEG_TYPES];
};

extern WINDOW_ID win_data_view[NUM_DATA_VIEWS];
extern WINDOW_ID win_diff_view[NUM_DATA_VIEWS];

extern DBGBlock dbg;
extern bool debugging;
extern bool exitDebugLoop;
extern bool exitNormalLoop;
extern bool showExtend;

#define MAXCMDLEN 254
constexpr const uint8_t VIEW_HISTORY_LIMIT = 64U;
typedef enum View_Mask : unsigned __int8 {
	V_UPDATE_NONE			= 0x00U,
	V_UPDATE_VIEW			= 0x01U,
	V_UPDATE_SCROLL			= 0x02U,
	V_UPDATE_HISTORY		= 0x04U,
	V_UPDATE_DEFAULT		= V_UPDATE_VIEW | V_UPDATE_SCROLL,
	V_UPDATE_CODE_HISTORY	= V_UPDATE_VIEW | V_UPDATE_HISTORY,
	V_UPDATE_SCROLL_HISTORY = V_UPDATE_SCROLL | V_UPDATE_HISTORY,
	V_UPDATE_ALL			= V_UPDATE_VIEW | V_UPDATE_SCROLL | V_UPDATE_HISTORY
} VIEW_MASK;
constexpr VIEW_MASK operator|( VIEW_MASK lhs, VIEW_MASK rhs ) noexcept {
	using Underlying = std::underlying_type_t<VIEW_MASK>;
	return static_cast<VIEW_MASK>( static_cast<Underlying>( lhs ) | static_cast<Underlying>( rhs ) );
}
inline VIEW_MASK &operator|=( VIEW_MASK &lhs, VIEW_MASK rhs ) noexcept {
	lhs = lhs | rhs;
	return lhs;
}
constexpr VIEW_MASK operator&( VIEW_MASK lhs, VIEW_MASK rhs ) noexcept {
	using Underlying = std::underlying_type_t<VIEW_MASK>;
	return static_cast<VIEW_MASK>( static_cast<Underlying>( lhs ) & static_cast<Underlying>( rhs ) );
}
inline VIEW_MASK &operator&=( VIEW_MASK &lhs, VIEW_MASK rhs ) noexcept {
	lhs = lhs & rhs;
	return lhs;
}

struct SView {
	const WINDOW_ID win_id;
	ADDRESS_PAIR realAddress;
	uint32_t address = 0U;
	uint32_t address_min = 0U;
	uint32_t address_max = static_cast<uint32_t>( -1 );

	SView( const WINDOW_ID _win_id ) : win_id( _win_id ) {}

	virtual bool Set( const ADDRESS_PAIR &, const VIEW_MASK = V_UPDATE_DEFAULT );

	void HistorySet( const ADDRESS_PAIR & );
	void HistoryNext( );
	void HistoryPrev( );
protected:
	void HistoryInsert( const ADDRESS_PAIR & );
private:
	ADDRESS_PAIR history[VIEW_HISTORY_LIMIT];
	uint8_t history_index = 0U, history_begin = 0U, history_end = 0U;

	void Inc( uint8_t & );
	void Dec( uint8_t & );
};

struct SCodeView : public SView {
	ADDRESS_PAIR cursorRealAddress;
	uint32_t cursorAddress = 0;

	SCodeView( WINDOW_ID win_id ) : SView( win_id ) {}

	bool Set( const ADDRESS_PAIR &, const VIEW_MASK = V_UPDATE_DEFAULT );
	bool SetToEIP( const VIEW_MASK = V_UPDATE_DEFAULT );
	bool SetCursor( const ADDRESS_PAIR & );
} extern codeView;

struct SDataView : public SView {
	uint16_t	segment;
	uint32_t	address_segment;
	SDataView( WINDOW_ID win_id ) : SView( win_id ) {}
	
	bool Set( const ADDRESS_PAIR &, const VIEW_MASK = V_UPDATE_DEFAULT );
} extern dataView, stackView;

// Event queue for debugger input
struct DebuggerInputEvent {
	SDL_Event ev = {};
	std::string text = {};
};
extern std::queue<DebuggerInputEvent> debugger_event_queue;
#endif // C_DEBUGGER
#endif // DOSBOX_DEBUGGER_INC_H