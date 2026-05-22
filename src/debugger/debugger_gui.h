#ifndef DOSBOX_DEBUGGER_GUI_H
#define DOSBOX_DEBUGGER_GUI_H

#include "debugger_inc.h"

#if C_DEBUGGER
#include "cpu/cpu.h"
#include "debugger_disasm.h"
#include <imgui.h>
#include <imgui_internal.h>

constexpr const uint8_t MAX_ADDRESS_COLORS = 9U;
extern const ImVec4 address_colors[MAX_ADDRESS_COLORS][2];
extern const ImVec4 light_grey_color;
extern const ImVec4 grey_color;
extern const ImVec4 green_color;

extern bool BeginSubWindow( const WINDOW_ID, const char *, int );
extern void EndSubWindow( const WINDOW_ID );

extern void DrawCode( );
extern void DrawData( );
extern void DrawStack( );
extern void DrawDiff( );

extern void SaveMemoryState( );

extern bool imgui_initialized;

extern uint32_t start_address;
extern float char_width, digit_width, space_width, tab_width, scrollbar_width, cursor_width;
extern float line_height;
extern float line_height_no_spacing;
extern ImVec2 window_size[NUM_WINDOWS];

struct SLabelView {
	ADDRESS_PAIR realAddress;
	bool fLabel = true;

	void Set( const ADDRESS_PAIR &, const bool, const bool = true );
	bool IsMatch( const ADDRESS_PAIR &, const uint32_t, const bool ) const;
} extern labelView;

enum DataCursor : uint8_t {
	DI_CURSOR = 0U,
	SI_CURSOR,
	NUM_DATA_CURSORS
};

struct Cursor {
	ImU32			color = IM_COL32( 255, 255, 255, 255 );
	ImVec2			pos = { 0.0f, 0.0f };
	bool			visible = false;
	ADDRESS_PAIR	realAddress = { 0U, 0U };
} extern data_cursor[NUM_DATA_CURSORS];

typedef struct Diff {
	uint32_t	address;
	uint16_t	segment;
	uint8_t		value;
	ImVec2		pos;
} DIFF;

extern uint32_t data_buffer_size;
#endif // C_DEBUGGER
#endif // DOSBOX_DEBUGGER_GUI_H