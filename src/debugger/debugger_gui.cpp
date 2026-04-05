// SPDX-FileCopyrightText:  2002-2025 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "debugger_inc.h"

#if C_DEBUGGER
#include <SDL3/SDL.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <imgui_internal.h>

#include "breakpoint.h"
#include "cpu/cpu.h"
#include "cpu/paging.h"
#include "debugger_disasm.h"
#include "debugvar.h"
#include "utils/string_utils.h"

#include "IBM_VGA_8x16.h"

extern bool AddressVisited( uint16_t, uint32_t );
extern bool ParseCommand( char * );
extern char *AnalyzeInstruction( char *, bool, const bool );
extern uint32_t GetAddress( uint16_t, uint32_t );
extern bool GetDescriptorInfo( char *, char *, char * );

extern DBGBlock dbg;
extern bool debugging;

extern FILE* debuglog;
extern std::vector<CDebugVar*> varList;

SCodeViewData codeViewData = {};

Bitu cycle_count = 0;

char curSelectorName[3] = { 0, 0, 0 };

static bool imgui_initialized = false;
static float display_scale = 1.0f;

static float char_width, digit_width, space_width, address_width;
static float line_height;
static float line_height_no_spacing;
static ImVec2 padding;
static float title_bar_height;
static ImVec2 window_pos[NUM_WINDOWS];
static ImVec2 window_size[NUM_WINDOWS];

//static const ImGuiWindowFlags_ EDIT_LAYOUT = ImGuiWindowFlags_NoFocusOnAppearing;
static const ImGuiWindowFlags_ STATIC_LAYOUT = static_cast<ImGuiWindowFlags_>( ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing );
static ImGuiWindowFlags_ ADDITIONAL_FLAGS = STATIC_LAYOUT;

static float CalcWindowHeight( uint8_t rows, bool fTitle = true );
static float CalcWindowWidth( uint8_t cols );

SEGTYPE &operator++( SEGTYPE &type ) {
	type = static_cast<SEGTYPE>( ++reinterpret_cast<uint8_t &>( type ) );
	return type;
}
SEGTYPE operator++( SEGTYPE &source, int ) { // Postfix increment
	const SEGTYPE original = source;
	++source;
	return original;
}
WINDOW_ID &operator++( WINDOW_ID &id ) {
	id = static_cast<WINDOW_ID>( ++reinterpret_cast<uint8_t &>( id ) );
	return id;
}
DATA_ID &operator++( DATA_ID &id ) {
	id = static_cast<DATA_ID>( ++reinterpret_cast<uint8_t &>( id ) );
	return id;
}
uint16_t dataSeg[NUM_DATA_VIEWS] = { 0, 0 };
uint32_t dataOfs[NUM_DATA_VIEWS] = { 0, 0 };
WINDOW_ID win_data_view[NUM_DATA_VIEWS] = { WIN_DATA, WIN_STACK };
WINDOW_ID win_diff_view[NUM_DATA_VIEWS] = { WIN_DDIFF, WIN_SDIFF };

const ImVec4 white_color = ImVec4( 1.0f, 1.0f, 1.0f, 1.0f );
const ImVec4 light_grey_color = ImVec4( 0.75f, 0.75f, 0.75f, 1.0f );
const ImVec4 grey_color = ImVec4( 0.5f, 0.5f, 0.5f, 1.0f );
const ImVec4 red_color = ImVec4( 1.0f, 0.0f, 0.0f, 1.0f );
const ImVec4 dark_red_color = ImVec4( 0.76f, 0.11f, 0.12f, 1.0f );
const ImVec4 green_color = ImVec4( 0.0f, 1.0f, 0.0f, 1.0f );
const ImVec4 dark_green_color = ImVec4( 0.11f, 0.76f, 0.11f, 1.0f );
const ImVec4 blue_color = ImVec4( 0.0f, 0.64f, 0.91f, 1.0f );
const ImVec4 yellow_color = ImVec4( 1.0f, 0.99f, 0.33f, 1.0f );
const ImVec4 gold_color = ImVec4( 0.97f, 0.69f, 0.17f, 1.0f );
const ImVec4 purple_color = ImVec4( 0.75f, 0.72f, 1.0f, 1.0f );
const ImVec4 dark_purple_color = ImVec4( 0.74f, 0.38f, 0.97f, 1.0f );
const ImVec4 violet_color = ImVec4( 0.97f, 0.38f, 0.64f, 1.0f );
const ImVec4 jmp_color = ImVec4( 1.0f, 1.0f, 0.57f, 1.0f );
const ImVec4 ret_color = ImVec4( 0.94f, 0.53f, 0.52f, 1.0f );
//const ImVec4 light_torquoise_color = ImVec4( 0.37f, 0.86f, 0.97f, 1.0f );
//const ImVec4 dark_brown_color = ImVec4( 0.47f, 0.26f, 0.08f, 1.0f );

const ImVec4 address_colors[][2] = {
{ grey_color, light_grey_color }, // grey
{ ImVec4( 0.73f, 0.48f, 0.34f, 1.0f ), ImVec4( 0.94f, 0.53f, 0.31f, 1.0f ) }, // orange
{ ImVec4( 0.19f, 0.51f, 0.97f, 1.0f ), ImVec4( 0.41f, 0.63f, 0.97f, 1.0f ) }, // blue
{ ImVec4( 0.97f, 0.69f, 0.17f, 1.0f ), ImVec4( 0.97f, 0.77f, 0.37f, 1.0f ) }, // gold
{ ImVec4( 0.32f, 0.59f, 0.23f, 1.0f ), ImVec4( 0.39f, 0.75f, 0.28f, 1.0f ) }, // green
{ ImVec4( 0.68f, 0.25f, 0.97f, 1.0f ), ImVec4( 0.78f, 0.39f, 0.97f, 1.0f ) }, // purple
{ ImVec4( 0.8f, 0.29f, 0.32f, 1.0f ), ImVec4( 0.83f, 0.43f, 0.44f, 1.0f ) },  // red
{ ImVec4( 0.97f, 0.19f, 0.53f, 1.0f ), ImVec4( 0.97f, 0.38f, 0.64f, 1.0f ) }, // violet
{ ImVec4( 0.18f, 0.62f, 0.77f, 1.0f ), ImVec4( 0.18f, 0.82f, 0.97f, 1.0f ) }, // torquoise
};
const uint8_t MAX_ADDRESS_COLORS = static_cast<uint8_t>( sizeof( address_colors ) / sizeof( address_colors[0] ) );

#define MAX_LOG_BUFFER 500
static std::list<std::string> logBuff = {};
static std::list<std::string>::iterator logBuffPos = logBuff.end( );

void DEBUG_ShowMsg( const char* format, ... ) {
	if( !imgui_initialized ) {
		return;
	}

	char buf[DBGUI::MsgBufferSize];
	va_list msg;
	va_start( msg, format );
	vsnprintf( buf, sizeof( buf ), format, msg );
	va_end( msg );

	buf[sizeof( buf ) - 1] = '\0';

	/* Add newline if not present */
	size_t len = safe_strlen( buf );
	if( len > 0 && buf[len - 1] != '\n' && len + 1 < sizeof( buf ) ) {
		strcat( buf, "\n" );
	}

	if( debuglog ) {
		fprintf( debuglog, "%s", buf );
		fflush( debuglog );
	}

	logBuff.emplace_back( buf );
	if( logBuff.size( ) > DBGUI::MaxLogBuffer ) {
		logBuff.pop_front( );
	}
	dbg.update_win[WIN_OUT] = true;
	// Don't reset scroll offset - let user stay at their scroll position
}

uint16_t RealSegValue( const SegNames index ) {
	uint16_t seg_value = SegValue( index );
	if( ( cpu.pmode || seg_value < 32U ) && !( reg_flags & FLAG_VM ) ) {
		Descriptor desc;
		if( cpu.gdt.GetDescriptor( seg_value, desc ) )
			return desc.GetBase( ) >> 4;
	}
	return seg_value;
}

void DBGUI_SetCodeWinToEIP( ) {
	if( AddressVisited( GetAddress( SegValue( cs ), reg_eip ) ) )
		dbg.update_win_scroll[WIN_CODE] = true;
	else // address not already disassembled
		dbg.update_win[WIN_CODE] = true;
	codeViewData.useCS = RealSegValue( cs );
	codeViewData.useEIP = reg_eip;
}

void DBGUI_SetCodeWinToAddress( const uint16_t segment, const uint32_t offset ) {
	if( AddressVisited( segment, offset ) )
		dbg.update_win_scroll[WIN_CODE] = true;
	else // address not already disassembled
		dbg.update_win[WIN_CODE] = true;
	codeViewData.useCS = segment;
	codeViewData.useEIP = offset;
}

void DBGUI_UpdateMemoryViews( ) {
	for( DATA_ID data_i = static_cast<DATA_ID>( 0U ); data_i < NUM_DATA_VIEWS; ++data_i )
		dbg.update_win[win_data_view[data_i]] = true;
}

void DBGUI_UpdateOrderedSegments( const bool refresh_memory_views ) {
	uint16_t num_segments = ordered_segments.size( );
	for( SegNames seg_name = static_cast<SegNames>( 0 ); seg_name <= SegNames::gs; seg_name = static_cast<SegNames>( seg_name + 1 ) ) {
		uint16_t seg_val = RealSegValue( seg_name );
		if( !ordered_segments.count( { seg_val, {} } ) )
			ordered_segments.insert( { seg_val, { ( seg_val < dbg.segment[SEG_PSP] ? SEG_BASE : seg_name == cs ? SEG_CODE : seg_name == ss ? SEG_STACK : SEG_DATA ), 0U } } );
	}
	uint16_t seg_index = 0U;
	for( const auto &[segment, info] : ordered_segments ) {
		if( info.type == SEG_BASE || info.type == SEG_MAX )
			continue;
		const_cast<uint8_t &>( info.index ) = ( seg_index++ % ( MAX_ADDRESS_COLORS - 1U ) ) + 1U;
	}
	if( refresh_memory_views || ordered_segments.size( ) != num_segments )
		DBGUI_UpdateMemoryViews( );
}

/********************/
/*   Draw windows   */
/********************/
static void SnapToGrid( WINDOW_ID winID ) { // Detect movement and snap
	if( ImGui::IsWindowHovered( ImGuiHoveredFlags_ChildWindows ) ||
		ImGui::IsWindowFocused( ImGuiHoveredFlags_ChildWindows ) ) {
		if( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) ) {
			ImVec2 pos = ImGui::GetWindowPos( );
			if( pos.x != window_pos[winID].x || pos.y != window_pos[winID].y ) {
				if( pos.x < 0 )
					window_pos[winID].x = 0;
				else
					window_pos[winID].x = roundf( pos.x / char_width ) * char_width;
				if( pos.y < 0 )
					window_pos[winID].y = 0;
				else
					window_pos[winID].y = roundf( pos.y / title_bar_height ) * title_bar_height;
				dbg.update_win_frame[winID] = true;
			}
			if( !ImGui::IsWindowCollapsed( ) ) {
				ImVec2 size = ImGui::GetWindowSize( );
				if( size.x != window_size[winID].x || size.y != window_size[winID].y ) {
					window_size[winID].x = roundf( size.x / char_width ) * char_width;
					window_size[winID].y = roundf( size.y / title_bar_height ) * title_bar_height;
					dbg.update_win_frame[winID] = true;
				}
			}
		}
	}
}

static void DrawCode( ) {
	static auto window_width = window_size[WIN_CODE].x;
	ImGui::SetNextWindowPos( window_pos[WIN_CODE], ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size[WIN_CODE], ImGuiCond_FirstUseEver );

	if( dbg.update_win_frame[WIN_CODE] ) {
		dbg.update_win_frame[WIN_CODE] = false;
		ImGui::SetNextWindowPos( window_pos[WIN_CODE], ImGuiCond_Always );
		ImGui::SetNextWindowSize( window_size[WIN_CODE], ImGuiCond_Always );
		dbg.update_win_scroll[WIN_CODE] = true;
	}
	if( dbg.update_win[WIN_CODE] ) {
		dbg.update_win[WIN_CODE] = false;
		uint16_t previous_num_segments = ordered_segments.size( );
		auto startOffset = GetAddress( codeViewData.useCS, codeViewData.useEIP );
		DasmRecursiveDisassemble( startOffset, codeViewData.useEIP, cpu.code.big, cpu.pmode );
		if( ordered_segments.size( ) != previous_num_segments )
			DBGUI_UpdateOrderedSegments( true );
		dbg.update_win_scroll[WIN_CODE] = true;
	}
	if( DBGUI_BeginWindowWithStyledTitle( "Code" , ImGuiWindowFlags_HorizontalScrollbar ) && !DecodedLine::isEmpty( ) ) {
		static uint32_t selectedIndex = -1;
		static bool setFocus = false;
		uint32_t i = 0;
		uint16_t currentSegment = 0U;
		for( auto dline = DecodedLine::first( ); !DecodedLine::isEnd( ); ++dline, ++i ) {
			static uint8_t addressColorIndex = 0U;
			if( dbg.update_win_scroll[WIN_CODE] && dline.address.segment >= codeViewData.useCS && dline.address.offset >= codeViewData.useEIP ) {
				dbg.update_win_scroll[WIN_CODE] = false;
				ImGui::SetScrollHereY( );
				if( selectedIndex == static_cast<uint32_t>( -1 ) )
					setFocus = true;
				selectedIndex = i;
			}
			if( currentSegment != dline.address.segment ) { // match segment to defined segments
				currentSegment = dline.address.segment;
				const auto &ordered_segment = ordered_segments.find( { currentSegment, {} } );
				if( ordered_segment != ordered_segments.end( ) )
					addressColorIndex = ordered_segment->extra.index % MAX_ADDRESS_COLORS;
			}
			if( dline.mnemonicMask & MM_Proc ) {
				ImGui::SetCursorPosX( window_width * 0.20f );
				ImGui::TextColored( blue_color, "Call label:" );
			} else if( dline.mnemonicMask & MM_Label ) {
				ImGui::SetCursorPosX( window_width * 0.26f );
				ImGui::TextColored( yellow_color, "label:" );
			}
			const bool is_current_ip = ( dline.address.segment == RealSegValue( cs ) ) && ( dline.address.offset == reg_eip );
			const bool is_breakpoint = CBreakpoint::IsBreakpoint( dline.address.segment, dline.address.offset );
			const bool isSelected = ( selectedIndex == i );
			char id[11];
			sprintf( id, "##%08X", dline.base_offset );
			if( ImGui::Selectable( id, isSelected, ImGuiSelectableFlags_SelectOnClick ) ) {
				selectedIndex = i;
				codeViewData.cursorSeg = dline.address.segment;
				codeViewData.cursorOfs = dline.address.offset;
			}
			if( setFocus ) {
				setFocus = false;
				ImGui::SetKeyboardFocusHere( -1 );
			}
			ImGui::SameLine( 0.0f, 0.0f );
			ImGui::TextColored( ( is_current_ip ? dark_green_color : ( is_breakpoint ? dark_red_color : address_colors[addressColorIndex][0] ) ), "%04X", dline.address.segment );
			ImGui::SameLine( 0.0f, 0.0f );
			ImGui::TextColored( is_breakpoint || is_current_ip ? white_color : grey_color, ":" );
			ImGui::SameLine( 0.0f, 0.0f );
			ImGui::TextColored( ( is_current_ip ? green_color : ( is_breakpoint ? red_color : address_colors[addressColorIndex][1] ) ), "%04X%c", dline.address.offset, is_breakpoint ? '*' : ' ' );
			ImGui::SameLine( 0.0f, 0.0f );
			ImGui::TextColored( is_current_ip ? light_grey_color : grey_color, "%s", dline.szOpcode );
			if( is_current_ip ) {
				ImGui::SameLine( window_width * 0.38f );
				ImGui::TextColored( green_color, ">" );
			}
			const ImVec4 *operator_color = &purple_color;
			if( dline.mnemonicMask & MM_CALL )
				operator_color = &blue_color;
			else if( dline.mnemonicMask & MM_INT )
				operator_color = &yellow_color;
			else if( dline.mnemonicMask & MM_JMP )
				operator_color = &jmp_color;
			else if( dline.mnemonicMask & MM_MOV )
				operator_color = &green_color;
			else if( dline.mnemonicMask & MM_RET )
				operator_color = &ret_color;
			else if( dline.mnemonicMask & MM_CMP )
				operator_color = &gold_color;
			else if( dline.mnemonicMask & MM_Logical )
				operator_color = &violet_color;
			else if( dline.mnemonicMask & MM_Math )
				operator_color = &dark_purple_color;
			else if( dline.mnemonicMask & MM_Stack )
				operator_color = &light_grey_color;
			else if( dline.mnemonicMask & MM_ConditionalJump )
				operator_color = &yellow_color;
			ImGui::SameLine( window_width * 0.4f );
			ImGui::TextColored( *operator_color, "%s", dline.szInstruction );
			const ImVec4 *operands_color = is_current_ip ? &green_color : &purple_color;
			if( dline.szOperands ) {
				ImGui::SameLine( window_width * 0.48f );
				ImGui::TextColored( *operands_color, "%s", dline.szOperands );
			}
			if( dline.szComment[0] ) {
				ImGui::SameLine( window_width * 0.70f );
				ImGui::TextColored( light_grey_color, "%s", dline.szComment );
			}
			if( is_current_ip ) {
				ImGui::SameLine( window_width * 0.955f );
				ImGui::TextColored( green_color, "<" );
			}
			if( is_breakpoint ) {
				ImGui::SameLine( window_width * 0.965f );
				ImGui::TextColored( red_color, "*" );
			}
			if( dline.mnemonicMask & ( MM_Branch | MM_INT | MM_RET ) )
				ImGui::Spacing( );
		}
	}
	SnapToGrid( WIN_CODE );
	DBGUI_EndWindowWithStyledTitle( );
}

static std::vector<uint8_t> data_buffer;

void DBGUI_SaveMemoryState( ) {
	uint32_t *mem32 = reinterpret_cast<uint32_t *>( MemBase );
	uint32_t *data32 = reinterpret_cast<uint32_t *>( &data_buffer[0] );
	for( uint32_t count = data_buffer.size( ) >> 2; count; --count )
		*data32++ = *mem32++;
}

typedef struct Diff {
	uint32_t	address;
	uint16_t	segment;
	uint8_t		value;
	ImVec2		pos;
} DIFF;

static std::vector<char> data_text_buffer;
static std::vector<DIFF> data_diff[NUM_DATA_VIEWS];
static ImU32 diff_col = IM_COL32( 0, 96, 0, 255 );
static uint16_t data_segment = static_cast<uint16_t>( -1 );

static void DrawData( ) {
	ImGui::SetNextWindowPos( window_pos[WIN_DATA], ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size[WIN_DATA], ImGuiCond_FirstUseEver );

	if( dbg.update_win_frame[WIN_DATA] ) {
		dbg.update_win_frame[WIN_DATA] = false;
		ImGui::SetNextWindowPos( window_pos[WIN_DATA], ImGuiCond_Always );
		ImGui::SetNextWindowSize( window_size[WIN_DATA], ImGuiCond_Always );
	}
	if( DBGUI_BeginWindowWithStyledTitle( "Data", ImGuiWindowFlags_HorizontalScrollbar ) ) {
		if( ImGui::IsWindowFocused( ImGuiHoveredFlags_ChildWindows ) && dbg.active_data_view != DATA_VIEW )
			dbg.active_data_view = DATA_VIEW;
		bool scroll_to_diff = false;
		if( dbg.update_win[WIN_DATA] ) {
			dbg.update_win[WIN_DATA] = false;
			uint16_t segment = data_segment = dataSeg[DATA_VIEW];
			auto ordered_segment = ordered_segments.begin( );
			for( ; ordered_segment != ordered_segments.end( ); ++ordered_segment ) {
				if( segment < ordered_segment->value )
					break;
			}
			uint32_t count = ( dataOfs[DATA_VIEW] + 1U ) >> 4;
			if( ( count << 4 ) < dataOfs[DATA_VIEW] + 1U )
				++count;
			count += segment;
			if( count >= dbg.segment[SEG_STACK_END] ) {
				count -= segment;
				count = ( count < 0x1000 ? 0x1000 : count );
			}
			else if( count >= dbg.segment[SEG_STACK] )
				count = dbg.segment[SEG_STACK_END] - segment;
			else
				count = dbg.segment[SEG_STACK] - segment;
			uint32_t offset = 0U;
			uint32_t line_segment = segment;
			const uint32_t data_base = segment << 4;
			auto mem = &MemBase[data_base];
			const auto mem_compare_end = &MemBase[data_buffer.size( )];
			if( data_base >= data_buffer.size( ) )
				const_cast<uint32_t &>( data_base ) = 0U;
			auto data = &data_buffer[data_base];
			data_diff[DATA_VIEW].clear( );
			if( ( count << 7U ) > data_text_buffer.size( ) ) {
				data_text_buffer.clear( );
				data_text_buffer.resize( count << 7U );
			}
			char *line = &data_text_buffer[0];
			float yPos = 0.0f;
			for( ; count; --count, offset += 16U, line += 82U, ++line_segment, yPos += line_height_no_spacing ) {
				if( line_segment >= ordered_segment->value ) {
					segment = ordered_segment->value;
					++ordered_segment;
					offset = 0U;
					*line++ = '\n';
					yPos += line_height_no_spacing;
				}
				bool f32bit = false;
				// Address
				if( offset <= 0xFFFF )
					sprintf( line, "%04X:%04X      ", segment, offset );
				else {
					sprintf( line, "%04X:%08X  ", segment, offset );
					f32bit = true;
				}
				// Hex values
				uint8_t start_digits = f32bit ? 12U : 8U;
				uint8_t start_spaces = f32bit ? 3U : 4U;
				for( uint8_t x = 0U; x < 16U; ++x, ++mem ) {
					uint8_t num_digits = start_digits + ( x << 1U );
					uint8_t num_spaces = start_spaces + x + ( f32bit ? 0U : ( x >> 2U ) );
					uint8_t char_pos = num_digits + num_spaces - 1U;
					sprintf( &line[char_pos], " %02X ", *mem );
					line[65U + x] = ( *mem < 32 || !isprint( *mem ) ? '.' : *mem ); // Ascii representation
					if( mem < mem_compare_end ) {
						if( *data != *mem )
							data_diff[DATA_VIEW].push_back( { static_cast<uint32_t>( mem - MemBase ), segment, *data, { digit_width * num_digits + space_width * num_spaces - space_width * 0.5f, yPos } } );
						++data;
					}
				}
				*reinterpret_cast<int16_t *>( &line[63] ) = 0x2020;
				line[81] = '\n';
			}
			*line = 0;
			if( !data_diff[DATA_VIEW].empty( ) ) {
				scroll_to_diff = true;
				dbg.update_win_scroll[WIN_DATA] = true;
			}
		}
		ImVec2 top_left = ImGui::GetCursorScreenPos( ); // Top-left corner;
		auto drawList = ImGui::GetWindowDrawList( );
		for( const auto &diff: data_diff[DATA_VIEW] ) {
			ImVec2 pos = { top_left.x + diff.pos.x, top_left.y + diff.pos.y };
			drawList->AddRectFilled( pos, { pos.x + digit_width * 2.0f + space_width, pos.y + line_height_no_spacing }, diff_col, 4.0f );
		}
		ImGui::TextUnformatted( &data_text_buffer[0] );
		if( dbg.update_win_scroll[WIN_DATA] ) {
			dbg.update_win_scroll[WIN_DATA] = false;
			if( scroll_to_diff && !data_diff[DATA_VIEW].empty( ) )
				ImGui::SetScrollY( data_diff[DATA_VIEW][0].pos.y );
			else
				ImGui::SetScrollY( ( ( dataSeg[DATA_VIEW] - data_segment) + ( dataOfs[DATA_VIEW] >> 4 ) ) * line_height_no_spacing );
		}
	}
	SnapToGrid( WIN_DATA );
	DBGUI_EndWindowWithStyledTitle( );
}

static std::vector<char> stack_text_buffer;
static uint32_t stack_lines = 0U;
static uint16_t stack_segment = static_cast<uint16_t>( -1 );

static void DrawStack( ) {
	ImGui::SetNextWindowPos( window_pos[WIN_STACK], ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size[WIN_STACK], ImGuiCond_FirstUseEver );

	if( dbg.update_win_frame[WIN_STACK] ) {
		dbg.update_win_frame[WIN_STACK] = false;
		ImGui::SetNextWindowPos( window_pos[WIN_STACK], ImGuiCond_Always );
		ImGui::SetNextWindowSize( window_size[WIN_STACK], ImGuiCond_Always );
	}
	if( DBGUI_BeginWindowWithStyledTitle( "Stack", ImGuiWindowFlags_HorizontalScrollbar ) ) {
		if( ImGui::IsWindowFocused( ImGuiHoveredFlags_ChildWindows ) && dbg.active_data_view != STACK_VIEW )
			dbg.active_data_view = STACK_VIEW;
		bool scroll_to_diff = false;
		if( dbg.update_win[WIN_STACK] ) {
			dbg.update_win[WIN_STACK] = false;
			uint16_t segment = stack_segment = dataSeg[STACK_VIEW];
			uint32_t offset = dataOfs[STACK_VIEW];
			offset = ( offset & 0x0000000F ? 16U : 0U ) + ( ( offset >> 4 ) << 4 );
			uint32_t line_segment = segment + ( offset >> 4 );
			auto ordered_segment = ordered_segments.end( );
			--ordered_segment;
			for( ; ordered_segment != ordered_segments.begin( ); --ordered_segment ) {
				if( line_segment > ordered_segment->value )
					break;
			}
			if( segment < ordered_segment->value ) {
				segment = ordered_segment->value;
				offset = ( ( line_segment - segment ) << 4 ) + 0xF;
			} else {
				offset = ( ( std::next( ordered_segment, 1 )->value - segment ) << 4 ) - 1U;
				line_segment = std::next( ordered_segment, 1 )->value;
			}
			const uint32_t data_base = segment << 4;
			auto mem = &MemBase[data_base + offset];
			const auto mem_compare_end = &MemBase[data_buffer.size( )];
			const_cast<uint32_t &>( data_base ) += offset;
			if( data_base >= data_buffer.size( ) )
				const_cast<uint32_t &>( data_base ) = data_buffer.size( ) - 1U;
			auto data = &data_buffer[data_base];
			data_diff[STACK_VIEW].clear( );
			stack_lines = line_segment - stack_segment;
			if( ( stack_lines << 7U ) > stack_text_buffer.size( ) ) {
				stack_text_buffer.clear( );
				stack_text_buffer.resize( stack_lines << 7U );
			}
			char *line = &stack_text_buffer[0];
			float yPos = 0.0f;
			for( uint32_t count = stack_lines; count; --count, offset -= 16U, line += 82, --line_segment, yPos += line_height_no_spacing ) {
				if( line_segment < ordered_segment->value ) {
					--ordered_segment;
					segment = ordered_segment->value;
					offset = ( ( line_segment - segment ) << 4 ) + 0xF;
				}
				bool f32bit = false;
				// Address
				if( offset <= 0xFFFF )
					sprintf( line, "%04X:%04X      ", segment, offset );
				else {
					sprintf( line, "%04X:%08X  ", segment, offset );
					f32bit = true;
				}
				// Hex values
				uint8_t start_digits = f32bit ? 12U : 8U;
				uint8_t start_spaces = f32bit ? 3U : 4U;
				for( uint8_t x = 0U; x < 16U; ++x, --mem ) {
					uint8_t num_digits = start_digits + ( x << 1U );
					uint8_t num_spaces = start_spaces + x + ( f32bit ? 0U : ( x >> 2U ) );
					uint8_t char_pos = num_digits + num_spaces - 1U;
					sprintf( &line[char_pos], " %02X ", *mem );
					line[65U + x] = ( *mem < 32 || !isprint( *mem ) ? '.' : *mem ); // Ascii representation
					if( mem < mem_compare_end ) {
						if( *data != *mem )
							data_diff[STACK_VIEW].push_back( { static_cast<uint32_t>( mem - MemBase ), segment, *data, { digit_width * num_digits + space_width * num_spaces - space_width * 0.5f, yPos } } );
						--data;
					}
				}
				*reinterpret_cast<int16_t *>( &line[63] ) = 0x2020;
				line[81] = '\n';
			}
			*line = 0;
			if( !data_diff[STACK_VIEW].empty( ) ) {
				scroll_to_diff = true;
				dbg.update_win_scroll[WIN_STACK] = true;
			}
		}
		ImVec2 top_left = ImGui::GetCursorScreenPos( ); // Top-left corner;
		auto drawList = ImGui::GetWindowDrawList( );
		for( const auto &diff : data_diff[STACK_VIEW] ) {
			ImVec2 pos = { top_left.x + diff.pos.x, top_left.y + diff.pos.y };
			drawList->AddRectFilled( pos, { pos.x + digit_width * 2.0f + space_width, pos.y + line_height_no_spacing }, diff_col, 4.0f );
		}
		ImGui::TextUnformatted( &stack_text_buffer[0] );
		if( dbg.update_win_scroll[WIN_STACK] ) {
			dbg.update_win_scroll[WIN_STACK] = false;
			if( scroll_to_diff && !data_diff[STACK_VIEW].empty( ) )
				ImGui::SetScrollY( data_diff[STACK_VIEW][0].pos.y );
			else
				ImGui::SetScrollY( ( stack_lines - ( ( dataSeg[STACK_VIEW] - stack_segment ) + ( dataOfs[STACK_VIEW] >> 4 ) + ( dataOfs[STACK_VIEW] & 0x0000000F ? 1U : 0U ) ) ) * line_height_no_spacing );
		}
	}
	SnapToGrid( WIN_STACK );
	DBGUI_EndWindowWithStyledTitle( );
}

static void DrawDiff( ) {
	for( DATA_ID data_i = static_cast<DATA_ID>( 0U ); data_i < NUM_DATA_VIEWS; ++data_i ) {
		ImGui::SetNextWindowPos( window_pos[win_diff_view[data_i]], ImGuiCond_FirstUseEver );
		ImGui::SetNextWindowSize( window_size[win_diff_view[data_i]], ImGuiCond_FirstUseEver );

		if( dbg.update_win_frame[win_diff_view[data_i]] ) {
			dbg.update_win_frame[win_diff_view[data_i]] = false;
			ImGui::SetNextWindowPos( window_pos[win_diff_view[data_i]], ImGuiCond_Always );
			ImGui::SetNextWindowSize( window_size[win_diff_view[data_i]], ImGuiCond_Always );
		}
		if( DBGUI_BeginWindowWithStyledTitle( ( data_i == DATA_VIEW ? "Data changes" : "Stack changes" ), ImGuiWindowFlags_None ) ) {
			static uint32_t selectedIndex = -1;
			uint16_t currentSegment = 0U;
			for( const auto &diff : data_diff[data_i] ) {
				static uint8_t addressColorIndex = 0U;
				const uint32_t offset = diff.address - ( diff.segment << 4 );
				if( currentSegment != diff.segment ) { // match segment to defined segments
					currentSegment = diff.segment;
					const auto &ordered_segment = ordered_segments.find( { currentSegment, {} } );
					if( ordered_segment != ordered_segments.end( ) )
						addressColorIndex = ordered_segment->extra.index % MAX_ADDRESS_COLORS;
				}
				const bool isSelected = ( selectedIndex == diff.address );
				char id[11];
				sprintf( id, "##%08X", diff.address );
				if( ImGui::Selectable( id, isSelected, ImGuiSelectableFlags_SelectOnClick ) ) {
					selectedIndex = diff.address;
					dataSeg[data_i] = diff.segment;
					dataOfs[data_i] = offset;
					dbg.update_win_scroll[win_data_view[data_i]] = true;
				}
				ImGui::SameLine( 0.0f, 0.0f );
				ImGui::TextColored( address_colors[addressColorIndex][0], "%04X", diff.segment );
				ImGui::SameLine( 0.0f, 0.0f );
				ImGui::TextColored( grey_color, ":" );
				ImGui::SameLine( 0.0f, 0.0f );
				ImGui::TextColored( address_colors[addressColorIndex][1], "%04X", offset );
				ImGui::SameLine( );
				ImGui::TextColored( grey_color, "%02X ->", diff.value );
				ImGui::SameLine( );
				ImGui::TextColored( green_color, "%02X", MemBase[diff.address] );
			}
		}
		SnapToGrid( win_diff_view[data_i] );
		DBGUI_EndWindowWithStyledTitle( );
		if( dbg.update_win[win_diff_view[data_i]] ) {
			dbg.update_win[win_diff_view[data_i]] = false;
		}
	}
}

const uint8_t oEIP = 8;
static uint32_t oldregs[oEIP + 1] = {};
static Segment oldsegs[6] = {};
static auto oldcpucpl = cpu.cpl;
static auto oldflags = cpu_regs.flags;

void DBGUI_SaveCPUstate( ) {
	for( uint8_t i = 0; i < oEIP; ++i )
		oldregs[i] = cpu_regs.regs[i].dword[DW_INDEX];
	oldregs[oEIP] = reg_eip;
	{
		uint8_t i = 0;
		for( auto &oldseg : oldsegs )
			oldseg.val = RealSegValue( static_cast<SegNames>( i++ ) );
	}
	oldcpucpl = cpu.cpl;
	oldflags = reg_flags;
}

enum :uint8_t { COL_1, COL_2, COL_3, COL_4, COL_5, COL_6 };

const ImVec4 highlight_color = ImVec4( 1.0f, 1.0f, 0.0f, 1.0f );

enum TYPE :uint8_t { tREG, tSEG, tIP, tMODE, tFLAG, tCYCLE, tIOPL, tCPL, tXTRA };
struct ENTRY {
	const TYPE type;
	const char label[8] = "";
	const uint32_t x = 0U;
	const char separator = ' ';
	const char delimeter = '\t';
};
const ENTRY layout[] = {
	{ tREG, "EAX", REGI_AX }, { tREG, "ESI", REGI_SI }, { tSEG, "CS", cs }, { tSEG, "FS", fs }, { tIP, "EIP", 0U, ' ', 2 }, { tMODE, "", 0U, 0, '\n' },\
	{ tREG, "EBX", REGI_BX }, { tREG, "EDI", REGI_DI }, { tSEG, "DS", ds }, { tSEG, "GS", gs }, { tFLAG, "C", FLAG_CF, 0, ' ' },\
	{ tFLAG, "Z", FLAG_ZF, 0, ' ' }, { tFLAG, "S", FLAG_SF, 0, ' ' }, { tFLAG, "O", FLAG_OF, 0, ' ' }, { tFLAG, "A", FLAG_AF, 0, ' ' },\
	{ tFLAG, "P", FLAG_PF, 0, ' ' }, { tFLAG, "D", FLAG_DF, 0, ' ' }, { tFLAG, "I", FLAG_IF, 0, ' ' }, { tFLAG, "T", FLAG_TF, 0, '\n' },\
	{ tREG, "ECX", REGI_CX }, { tREG, "EBP", REGI_BP }, { tSEG, "ES", es }, { tSEG, "SS", ss }, { tCYCLE }, { tIOPL, "IOPL", FLAG_IOPL, 0, '\t' }, { tCPL, "CPL", 0U, 0, '\n' },\
	{ tREG, "EDX", REGI_DX }, { tREG, "ESP", REGI_SP }, { tXTRA }
};

static void DrawRegisters( ) {
	static auto window_width = window_size[WIN_REG].x;
	ImGui::SetNextWindowPos( window_pos[WIN_REG], ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size[WIN_REG], ImGuiCond_FirstUseEver );

	if( dbg.update_win_frame[WIN_REG] ) {
		dbg.update_win_frame[WIN_REG] = false;
		ImGui::SetNextWindowPos( window_pos[WIN_REG], ImGuiCond_Always );
		//ImGui::SetNextWindowSize( window_size[WIN_REG], ImGuiCond_Always );
	}
	static const float TAB_POS[] = { 0.0f, window_width * 0.205f, window_width * 0.405f, window_width * 0.53f, window_width * 0.655f, window_width * 0.84f, window_width * 0.93f, window_width };

	if( DBGUI_BeginWindowWithStyledTitle( "Registers", ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse ) ) {

		Bitu changed_flags = reg_flags ^ oldflags;
		for( ENTRY e : layout ) {
			float label_x = 0.0f, entry_width = 1.0f;
			if( e.label[0] ) {
				label_x = ImGui::GetCursorPosX( );
				ImGui::Text( "%s", e.label );
				if( e.separator ) {
					ImGui::SameLine( 0.0f, 0.0f );
					ImGui::Text( "%c", e.separator );
				}
				ImGui::SameLine( 0.0f, 0.0f );
			}
			switch( e.type ) {
			case tREG:
				if( cpu_regs.regs[e.x].dword[DW_INDEX] != oldregs[e.x] )
					ImGui::TextColored( highlight_color, "%04X %04X", ( cpu_regs.regs[e.x].dword[DW_INDEX] >> 16 ) & 0xFFFF, cpu_regs.regs[e.x].dword[DW_INDEX] & 0xFFFF );
				else
					ImGui::Text( "%04X %04X", ( cpu_regs.regs[e.x].dword[DW_INDEX] >> 16 ) & 0xFFFF, cpu_regs.regs[e.x].dword[DW_INDEX] & 0xFFFF );
				if( e.x != REGI_AX && e.x != REGI_CX ) {
					ImGui::SameLine( 0.0f, 0.0f );
					entry_width = ImGui::GetCursorPosX( ) - label_x;
					ImGui::SetCursorPosX( label_x );
					char id[10];
					sprintf( id, "##reg_%s", e.label );
					if( ImGui::Selectable( id, false, ImGuiSelectableFlags_SelectOnClick, { entry_width, 0.0f } ) ) {
						switch( e.x ) {
						case REGI_SP:
						case REGI_BP:
							dataSeg[STACK_VIEW] = RealSegValue( ss );
							dataOfs[STACK_VIEW] = cpu_regs.regs[e.x].dword[DW_INDEX];
							dbg.update_win[WIN_STACK] = stack_segment != dataSeg[STACK_VIEW];
							dbg.update_win_scroll[WIN_STACK] = true;
							break;
						case REGI_DX:
						case REGI_SI:
							dataSeg[DATA_VIEW] = RealSegValue( ds );
						case REGI_BX:
						case REGI_DI:
							if( e.x == REGI_DI || e.x == REGI_BX )
								dataSeg[DATA_VIEW] = RealSegValue( es );
							dataOfs[DATA_VIEW] = cpu_regs.regs[e.x].dword[DW_INDEX];
							dbg.update_win[WIN_DATA] = data_segment != dataSeg[DATA_VIEW];
							dbg.update_win_scroll[WIN_DATA] = true;
							break;
						}
					}
				}
				break;
			case tSEG: {
				const auto segVal = RealSegValue( static_cast<SegNames>( e.x ) );
				if( segVal != oldsegs[e.x].val )
					ImGui::TextColored( highlight_color, "%04X", segVal );
				else
					ImGui::Text( "%04X", segVal );
				if( segVal ) {
					ImGui::SameLine( 0.0f, 0.0f );
					entry_width = ImGui::GetCursorPosX( ) - label_x;
					ImGui::SetCursorPosX( label_x );
					char id[9];
					sprintf( id, "##seg_%s", e.label );
					if( ImGui::Selectable( id, false, ImGuiSelectableFlags_SelectOnClick, { entry_width, 0.0f } ) ) {
						switch( e.x ) {
						case cs:
							codeViewData.useCS = segVal;
							codeViewData.useEIP = 0U;
							dbg.update_win_scroll[WIN_CODE] = true;
							break;
						case ss:
							dataSeg[STACK_VIEW] = segVal;
							dataOfs[STACK_VIEW] = reg_esp;
							dbg.update_win[WIN_STACK] = stack_segment != segVal;
							dbg.update_win_scroll[WIN_STACK] = true;
							break;
						default:
							dataSeg[DATA_VIEW] = segVal;
							dataOfs[DATA_VIEW] = 0U;
							dbg.update_win[WIN_DATA] = data_segment != segVal;
							dbg.update_win_scroll[WIN_DATA] = true;
							break;
						}
					}
				}
			}
				break;
			case tIP:
				if( reg_eip != oldregs[oEIP] )
					ImGui::TextColored( highlight_color, "%04X %04X", ( reg_eip >> 16 ) & 0xFFFF, reg_eip & 0xFFFF );
				else
					ImGui::Text( "%04X %04X", ( reg_eip >> 16 ) & 0xFFFF, reg_eip & 0xFFFF );
				ImGui::SameLine( 0.0f, 0.0f );
				entry_width = ImGui::GetCursorPosX( ) - label_x;
				ImGui::SetCursorPosX( label_x );
				if( ImGui::Selectable( "##reg_ip", false, ImGuiSelectableFlags_SelectOnClick, { entry_width, 0.0f } ) ) {
					codeViewData.useCS = RealSegValue( cs );
					codeViewData.useEIP = reg_eip;
					dbg.update_win_scroll[WIN_CODE] = true;
				}
				break;
			case tMODE: {
				const char* mode_str = "Real";
				if( cpu.pmode ) {
					if( reg_flags & FLAG_VM ) {
						mode_str = "VM86";
					} else if( cpu.code.big ) {
						mode_str = "Pr32";
					} else {
						mode_str = "Pr16";
					}
				}
				ImGui::Text( "%s", mode_str );
			}
				break;
			case tFLAG:
				if( changed_flags & e.x )
					ImGui::TextColored( highlight_color, "%d", ( reg_flags & e.x ) ? 1 : 0 );
				else
					ImGui::Text( "%d", ( reg_flags & e.x ) ? 1 : 0 );
				break;
			case tCYCLE:
				ImGui::Text( "%" PRIuPTR, cycle_count );
				break;
			case tIOPL:
				if( changed_flags & FLAG_IOPL )
					ImGui::TextColored( highlight_color, "%d", (int) ( GETFLAG( IOPL ) >> 12 ) );
				else
					ImGui::Text( "%d", (int) ( GETFLAG( IOPL ) >> 12 ) );
				break;
			case tCPL:
				if( cpu.cpl != oldcpucpl )
					ImGui::TextColored( highlight_color, "%d", (int) cpu.cpl );
				else
					ImGui::Text( "%d", (int) cpu.cpl );
				break;
			case tXTRA: // Selector info, if available
				if( ( cpu.pmode ) && curSelectorName[0] ) {
					char out1[200], out2[200];
					GetDescriptorInfo( curSelectorName, out1, out2 );
					ImGui::Text( "%s %s", out1, out2 );
				}
				break;
			}
			switch( e.delimeter ) {
			case 0:
				ImGui::SameLine( 0.0f, 0.0f );
				break;
			case ' ':
				ImGui::SameLine( );
				break;
			case '\n':
				break;
			case '\t':
			default:{
				ImGui::SameLine( 0.0f, 0.0f );
				uint8_t col = 0;
				for( float xPos = ImGui::GetCursorPosX( ); col < sizeof( TAB_POS ) - 1; ++col )
					if( xPos < TAB_POS[col] )
						break;
				if( e.delimeter != '\t' && static_cast<uint8_t>( col + ( e.delimeter - 1 ) ) < static_cast<uint8_t>( sizeof( TAB_POS ) - 1 ) )
					col += e.delimeter - 1;
				ImGui::SameLine( TAB_POS[col] );
			}
				break;
			}
		}
		if( dbg.update_win[WIN_REG] ) {
			dbg.update_win[WIN_REG] = false;
		}
	}
	SnapToGrid( WIN_REG );
	DBGUI_EndWindowWithStyledTitle( );
}

const char seg_label[NUM_SEG_TYPES][3] = { "", "CS", "DS", "SS", "SE", "HP", "PS", "En", "" };

static void DrawSegments( ) {
	ImGui::SetNextWindowPos( window_pos[WIN_SEG], ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size[WIN_SEG], ImGuiCond_FirstUseEver );

	if( dbg.update_win_frame[WIN_SEG] ) {
		dbg.update_win_frame[WIN_SEG] = false;
		ImGui::SetNextWindowPos( window_pos[WIN_SEG], ImGuiCond_Always );
		ImGui::SetNextWindowSize( window_size[WIN_SEG], ImGuiCond_Always );
	}
	if( DBGUI_BeginWindowWithStyledTitle( "Segments", ImGuiWindowFlags_NoNav ) ) {
		for( const auto &[segment, info] : ordered_segments ) {
			if( info.type == SEG_BASE || info.type == SEG_MAX )
				continue;
			float text_start_x = ImGui::GetCursorPosX( );
			ImGui::TextColored( grey_color, "%s:", seg_label[info.type] );
			ImGui::SameLine( 0.0f, 0.0f );
			ImGui::TextColored( address_colors[info.index % MAX_ADDRESS_COLORS][1], "%04X", segment );
			ImGui::SameLine( 0.0f, 0.0f );
			float text_width = ImGui::GetCursorPosX( ) - text_start_x;
			ImGui::SetCursorPosX( text_start_x );
			const bool isSelected = ( codeViewData.useCS == segment );
			char id[11];
			sprintf( id, "##%04X", segment );
			if( ImGui::Selectable( id, isSelected, ImGuiSelectableFlags_SelectOnClick, { text_width, 0.0f } ) ) {
				if( info.type == SEG_CODE ) {
					codeViewData.useCS = segment;
					codeViewData.useEIP = 0U;
					dbg.update_win_scroll[WIN_CODE] = true;
				}
				dataSeg[DATA_VIEW] = segment;
				dataOfs[DATA_VIEW] = 0U;
				dbg.update_win[WIN_DATA] = data_segment != segment;
				dbg.update_win_scroll[WIN_DATA] = true;
			}
			ImGui::SameLine( );
			if( ImGui::GetCursorPosX( ) + ( text_width + space_width ) > window_size[WIN_SEG].x )
				ImGui::NewLine( );
		}
	}
	SnapToGrid( WIN_SEG );
	DBGUI_EndWindowWithStyledTitle( );
	if( dbg.update_win[WIN_SEG] ) {
		dbg.update_win[WIN_SEG] = false;
	}
}

static void DrawCalls( ) {
	ImGui::SetNextWindowPos( window_pos[WIN_CALLS], ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size[WIN_CALLS], ImGuiCond_FirstUseEver );

	if( dbg.update_win_frame[WIN_CALLS] ) {
		dbg.update_win_frame[WIN_CALLS] = false;
		ImGui::SetNextWindowPos( window_pos[WIN_CALLS], ImGuiCond_Always );
		ImGui::SetNextWindowSize( window_size[WIN_CALLS], ImGuiCond_Always );
	}
	if( DBGUI_BeginWindowWithStyledTitle( "Call labels", ImGuiWindowFlags_None ) ) {
		static uint32_t selectedIndex = -1;
		uint16_t currentSegment = 0U;
		for( const auto &[address, info] : calls ) {
			static uint8_t addressColorIndex = 0U;
			const uint32_t offset = address - ( info.segment << 4 );
			if( currentSegment != info.segment ) { // match segment to defined segments
				currentSegment = info.segment;
				const auto &ordered_segment = ordered_segments.find( { currentSegment, {} } );
				if( ordered_segment != ordered_segments.end( ) )
					addressColorIndex = ordered_segment->extra.index % MAX_ADDRESS_COLORS;
			}
			const bool isSelected = ( selectedIndex == address );
			char id[11];
			sprintf( id, "##%08X", address );
			if( ImGui::Selectable( id, isSelected, ImGuiSelectableFlags_SelectOnClick ) ) {
				selectedIndex = address;
				codeViewData.useCS = info.segment;
				codeViewData.useEIP = offset;
				dbg.update_win_scroll[WIN_CODE] = true;
			}
			ImGui::SameLine( 0.0f, 0.0f );
			ImGui::TextColored( address_colors[addressColorIndex][0], "%04X", info.segment );
			ImGui::SameLine( 0.0f, 0.0f );
			ImGui::TextColored( grey_color, ":" );
			ImGui::SameLine( 0.0f, 0.0f );
			ImGui::TextColored( address_colors[addressColorIndex][1], "%04X", offset );
			for( const auto &[caller_address, segment] : info.callers ) {
				const uint32_t offset = caller_address - ( segment << 4 );
				const bool isSelected = ( selectedIndex == caller_address );
				char id[12];
				sprintf( id, "##c%08X", caller_address );
				if( ImGui::Selectable( id, isSelected, ImGuiSelectableFlags_SelectOnClick ) ) {
					selectedIndex = caller_address;
					codeViewData.useCS = segment;
					codeViewData.useEIP = offset;
					dbg.update_win_scroll[WIN_CODE] = true;
				}
				uint8_t color_index = addressColorIndex;
				const auto &ordered_segment = ordered_segments.find( { segment, {} } );
				if( ordered_segment != ordered_segments.end( ) )
					color_index = ordered_segment->extra.index % MAX_ADDRESS_COLORS;
				ImGui::SameLine( address_width );
				ImGui::TextColored( address_colors[color_index][0], "%04X", segment );
				ImGui::SameLine( 0.0f, 0.0f );
				ImGui::TextColored( grey_color, ":" );
				ImGui::SameLine( 0.0f, 0.0f );
				ImGui::TextColored( address_colors[color_index][1], "%04X", offset );
			}
		}
	}
	SnapToGrid( WIN_CALLS );
	DBGUI_EndWindowWithStyledTitle( );
	if( dbg.update_win[WIN_CALLS] ) {
		dbg.update_win[WIN_CALLS] = false;
	}
}

static void DrawLabels( ) {
	ImGui::SetNextWindowPos( window_pos[WIN_JUMPS], ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size[WIN_JUMPS], ImGuiCond_FirstUseEver );

	if( dbg.update_win_frame[WIN_JUMPS] ) {
		dbg.update_win_frame[WIN_JUMPS] = false;
		ImGui::SetNextWindowPos( window_pos[WIN_JUMPS], ImGuiCond_Always );
		ImGui::SetNextWindowSize( window_size[WIN_JUMPS], ImGuiCond_Always );
	}
	if( DBGUI_BeginWindowWithStyledTitle( "Labels", ImGuiWindowFlags_None ) ) {
		static uint32_t selectedIndex = -1;
		uint16_t currentSegment = 0U;
		for( const auto &[address, info] : jumps ) {
			static uint8_t addressColorIndex = 0U;
			const uint32_t offset = address - ( info.segment << 4 );
			if( currentSegment != info.segment ) { // match segment to defined segments
				currentSegment = info.segment;
				const auto &ordered_segment = ordered_segments.find( { currentSegment, {} } );
				if( ordered_segment != ordered_segments.end( ) )
					addressColorIndex = ordered_segment->extra.index % MAX_ADDRESS_COLORS;
			}
			const bool isSelected = ( selectedIndex == address );
			char id[11];
			sprintf( id, "##%08X", address );
			if( ImGui::Selectable( id, isSelected, ImGuiSelectableFlags_SelectOnClick ) ) {
				selectedIndex = address;
				codeViewData.useCS = info.segment;
				codeViewData.useEIP = offset;
				dbg.update_win_scroll[WIN_CODE] = true;
			}
			ImGui::SameLine( 0.0f, 0.0f );
			ImGui::TextColored( address_colors[addressColorIndex][0], "%04X", info.segment );
			ImGui::SameLine( 0.0f, 0.0f );
			ImGui::TextColored( grey_color, ":" );
			ImGui::SameLine( 0.0f, 0.0f );
			ImGui::TextColored( address_colors[addressColorIndex][1], "%04X", offset );
			for( const auto &[caller_address, segment] : info.callers ) {
				const uint32_t offset = caller_address - ( segment << 4 );
				const bool isSelected = ( selectedIndex == caller_address );
				char id[12];
				sprintf( id, "##c%08X", caller_address );
				if( ImGui::Selectable( id, isSelected, ImGuiSelectableFlags_SelectOnClick ) ) {
					selectedIndex = caller_address;
					codeViewData.useCS = segment;
					codeViewData.useEIP = offset;
					dbg.update_win_scroll[WIN_CODE] = true;
				}
				uint8_t color_index = addressColorIndex;
				const auto &ordered_segment = ordered_segments.find( { segment, {} } );
				if( ordered_segment != ordered_segments.end( ) )
					color_index = ordered_segment->extra.index % MAX_ADDRESS_COLORS;
				ImGui::SameLine( address_width );
				ImGui::TextColored( address_colors[color_index][0], "%04X", segment );
				ImGui::SameLine( 0.0f, 0.0f );
				ImGui::TextColored( grey_color, ":" );
				ImGui::SameLine( 0.0f, 0.0f );
				ImGui::TextColored( address_colors[color_index][1], "%04X", offset );
			}
		}
	}
	SnapToGrid( WIN_JUMPS );
	DBGUI_EndWindowWithStyledTitle( );
	if( dbg.update_win[WIN_JUMPS] ) {
		dbg.update_win[WIN_JUMPS] = false;
	}
}

#define DEBUG_VAR_BUF_LEN 16
static void DrawVariables( ) {
	ImGui::SetNextWindowPos( window_pos[WIN_VAR], ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size[WIN_VAR], ImGuiCond_FirstUseEver );

	if( dbg.update_win_frame[WIN_VAR] ) {
		dbg.update_win_frame[WIN_VAR] = false;
		ImGui::SetNextWindowPos( window_pos[WIN_VAR], ImGuiCond_Always );
		ImGui::SetNextWindowSize( window_size[WIN_VAR], ImGuiCond_Always );
	}

	if( DBGUI_BeginWindowWithStyledTitle( "Variables", ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNav ) ) {
		if( varList.empty( ) ) {
			ImGui::TextDisabled( "(no variables defined)" );
		} else {
			char buffer[DEBUG_VAR_BUF_LEN] = {};

			for( size_t i = 0; i < varList.size( ); ++i ) {
				auto dv = varList[i];
				uint16_t value;
				bool has_no_value = mem_readw_checked( dv->GetAdr( ),
					&value );

				if( has_no_value ) {
					snprintf( buffer, DEBUG_VAR_BUF_LEN, "%s", "??????" );
					dv->SetValue( false, 0 );
				} else {
					if( !dv->HasValue( ) ||
						dv->GetValue( ) != value ) {
						dv->SetValue( true, value );
					}
					snprintf( buffer, DEBUG_VAR_BUF_LEN, "0x%04x", value );
				}

				if( i % 3 != 0 ) {
					ImGui::SameLine( );
				}
				ImGui::Text( "%s: %s", dv->GetName( ), buffer );
			}
		}
	}
	SnapToGrid( WIN_VAR );
	DBGUI_EndWindowWithStyledTitle( );
	if( dbg.update_win[WIN_VAR] ) {
		dbg.update_win[WIN_VAR] = false;
	}
}
#undef DEBUG_VAR_BUF_LEN

// History stuff
#define MAX_HIST_BUFFER 50
std::list<std::string> histBuff = {};
std::list<std::string>::iterator histBuffPos = histBuff.end( );

static void DrawConsole( ) {
	ImGui::SetNextWindowPos( window_pos[WIN_CON], ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size[WIN_CON], ImGuiCond_FirstUseEver );

	if( dbg.update_win_frame[WIN_CON] ) {
		dbg.update_win_frame[WIN_CON] = false;
		ImGui::SetNextWindowPos( window_pos[WIN_CON], ImGuiCond_Always );
		ImGui::SetNextWindowSize( window_size[WIN_CON], ImGuiCond_Always );
	}
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( padding.x, 0.0f ) );
	if( DBGUI_BeginWindowWithStyledTitle( "Console", ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize ) ) {
		if( !debugging ) {
			ImGui::PushStyleColor( ImGuiCol_Text, green_color );
			ImGui::Text( "(Running)" );
			ImGui::PopStyleColor( );
		} else {
			ImGui::Text( "%c", ( codeViewData.ovrMode ? 'O' : 'I' ) );
			ImGui::SameLine( );
			ImGui::PushStyleColor( ImGuiCol_Text, green_color );
			if( ImGui::InputText( "##con", codeViewData.inputStr, IM_ARRAYSIZE( codeViewData.inputStr ), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll | ImGuiInputTextFlags_ElideLeft ) ) {
				codeViewData.inputStr[MAXCMDLEN] = '\0';
				if( ParseCommand( codeViewData.inputStr ) ) {
					char *cmd = ltrim( codeViewData.inputStr );
					if( histBuff.empty( ) || *--histBuff.end( ) != cmd ) {
						histBuff.emplace_back( cmd );
					}
					if( histBuff.size( ) > MAX_HIST_BUFFER ) {
						histBuff.pop_front( );
					}
					histBuffPos = histBuff.end( );
					codeViewData.inputStr[0] = 0;
				}
			}
			ImGui::PopStyleColor( );
		}
	}
	SnapToGrid( WIN_CON );
	DBGUI_EndWindowWithStyledTitle( );
	ImGui::PopStyleVar( );
}

static void DrawOutputWindow( ) {
	ImGui::SetNextWindowPos( window_pos[WIN_OUT], ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size[WIN_OUT], ImGuiCond_FirstUseEver );

	if( dbg.update_win_frame[WIN_OUT] ) {
		dbg.update_win_frame[WIN_OUT] = false;
		ImGui::SetNextWindowPos( window_pos[WIN_OUT], ImGuiCond_Always );
		ImGui::SetNextWindowSize( window_size[WIN_OUT], ImGuiCond_Always );
	}
	if( DBGUI_BeginWindowWithStyledTitle( "Output", ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNavFocus ) ) {

		for( auto it = logBuff.begin( ); it != logBuff.end( ); ++it )
			ImGui::TextUnformatted( it->c_str( ) );
	}
	if( dbg.update_win[WIN_OUT] ) {
		dbg.update_win[WIN_OUT] = false;
		ImGui::SetScrollHereY( );
	}
	SnapToGrid( WIN_OUT );
	DBGUI_EndWindowWithStyledTitle( );
}

// Calculate window height for a given number of rows
static float CalcWindowHeight( uint8_t rows, bool fTitle ) {
	return ( rows * title_bar_height ) + ( fTitle ? title_bar_height : 0.0f );
}
// Calculate window width for a given number of columns
static float CalcWindowWidth( uint8_t cols ) {
	return ( cols * char_width );
}

static float CalcWindowX( WINDOW_ID window_id ) {
	float x = 0.0f;
	for( uint8_t i = dbg.columnRows[window_id].column; i; --i )
		x += CalcWindowWidth( dbg.window_cols[i - 1] );
	return x;
}
static float CalcWindowY( WINDOW_ID window_id ) {
	float y = 0.0f;
	switch( window_id ) {
	case NUM_WINDOWS:
		y += CalcWindowHeight( dbg.columnRows[WIN_STACK].rows );
	case WIN_STACK:
		y += CalcWindowHeight( dbg.columnRows[WIN_VAR].rows );
	case WIN_VAR:
		y += CalcWindowHeight( dbg.columnRows[WIN_DATA].rows );
	case WIN_DATA:
		break;
	case WIN_JUMPS:
		y += CalcWindowHeight( dbg.columnRows[WIN_CALLS].rows );
	case WIN_CALLS:
		break;
	case WIN_SDIFF:
		y += CalcWindowHeight( dbg.columnRows[WIN_DDIFF].rows );
	case WIN_DDIFF:
		break;
	case WIN_OUT:
		y += CalcWindowHeight( dbg.columnRows[WIN_CON].rows );
	case WIN_CON:
		y += CalcWindowHeight( dbg.columnRows[WIN_SEG].rows );
	case WIN_SEG:
		y += CalcWindowHeight( dbg.columnRows[WIN_REG].rows );
	case WIN_REG:
		y += CalcWindowHeight( dbg.columnRows[WIN_CODE].rows );
	case WIN_CODE:
	default:
		break;
	}
	return y;
}
static float CalcWindowWidth( WINDOW_ID window_id ) {
	return CalcWindowWidth( dbg.window_cols[dbg.columnRows[window_id].column] );
}
static float CalcWindowHeight( WINDOW_ID window_id ) {
	return CalcWindowHeight( dbg.columnRows[window_id].rows );
}

static float CalcTotalHeight( ) {
	return CalcWindowY( NUM_WINDOWS );
}
static float CalcTotalWidth( ) {
	float width = 0.0f;
	for( const auto &window_cols : dbg.window_cols )
		width += CalcWindowWidth( window_cols );
	return width;
}

static bool fReset = false;

void DBGUI_DrawScreen( ) {
	if( !imgui_initialized )
		return;
	if( fReset ) {
		fReset = false;
		DBGUI_SetCodeWinToEIP( );
	}
	DrawConsole( );
	DrawCode( );
	DrawRegisters( );
	DrawSegments( );
	DrawCalls( );
	DrawLabels( );
	DrawData( );
	DrawStack( );
	DrawDiff( );
	DrawVariables( );
	DrawOutputWindow( );
}

//auto data_buffers[] = { data_buffer, stack_buffer };

void DBGUI_Reset( ) {
	codeViewData = {};
	data_text_buffer[0] = 0U;
	stack_text_buffer[0] = 0U;
	for( DATA_ID data_i = static_cast<DATA_ID>( 0U ); data_i < NUM_DATA_VIEWS; ++data_i ) {
		//for( auto data = &data_buffers[i][0]; *data; ++data )
			//*data = 0;
		dataSeg[data_i] = 0U;
		dataOfs[data_i] = 0U;
	}
	for( WINDOW_ID win_i = static_cast<WINDOW_ID>( 0U ); win_i < NUM_WINDOWS; ++win_i )
		dbg.update_win[win_i] = true;
	dbg.active_data_view = DATA_VIEW;
	stack_lines = 0U;
	DasmReset( );
	fReset = true;
}

void DBGUI_Resize( ) {
	SDL_GetWindowSizeInPixels( dbg.win_main, &dbg.window_width, &dbg.window_height );
	const uint16_t TOTAL_ROWS = dbg.window_height / title_bar_height;
	uint16_t column_rows[NUM_COLUMNS] = { TOTAL_ROWS, TOTAL_ROWS, TOTAL_ROWS, TOTAL_ROWS };
	for( WINDOW_ID i = static_cast<WINDOW_ID>( 0U ); i < NUM_WINDOWS; ++i ) {
		if( dbg.height_ratio[i] < 0 )
			dbg.columnRows[i].rows = -dbg.height_ratio[i];
		else if( dbg.height_ratio[i] > 0 )
			dbg.columnRows[i].rows = TOTAL_ROWS * ( dbg.height_ratio[i] * 0.01f ) - 1U;
		else
			dbg.columnRows[i].rows = column_rows[dbg.columnRows[i].column] - 1U;
		column_rows[dbg.columnRows[i].column] -= dbg.columnRows[i].rows + 1U;
	}
	for( WINDOW_ID i = static_cast<WINDOW_ID>( 0U ); i < NUM_WINDOWS; ++i ) {
		window_pos[i] = ImVec2( CalcWindowX( i ), CalcWindowY( i ) );
		window_size[i] = ImVec2( CalcWindowWidth( i ), CalcWindowHeight( i ) );
		dbg.update_win_frame[i] = true;
	}
}

static const char debugger_title[] = "DOSBox Debugger";
static const uint8_t debugger_title_length = sizeof( debugger_title );

bool DBGUI_StartUp( ) {
	if( imgui_initialized )
		return imgui_initialized;

	// Get display scale for high DPI support
	display_scale = SDL_GetDisplayContentScale( SDL_GetPrimaryDisplay( ) );
	if( display_scale <= 0.0f )
		display_scale = 1.0f;

	// Create debugger window with initial size (will be resized after ImGui
	// init). Use approximate pixel values before ImGui metrics are
	// available, scaled for high DPI displays.
	constexpr int InitialWindowWidth = 1600;
	constexpr int InitialWindowHeight = 900;
	const SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE |
		SDL_WINDOW_HIDDEN |
		SDL_WINDOW_HIGH_PIXEL_DENSITY;

	dbg.win_main = SDL_CreateWindow(
		debugger_title,
		static_cast<int>( InitialWindowWidth * display_scale ),
		static_cast<int>( InitialWindowHeight * display_scale ),
		window_flags );

	if( !dbg.win_main ) {
		LOG_ERR( "DEBUG: Failed to create debugger window: %s",
			SDL_GetError( ) );
		return false;
	}

	// Create GPU device - SDL_GPU uses Vulkan/Metal/D3D12 under the hood,
	// avoiding OpenGL context conflicts with the main DOSBox window
	dbg.gpu_device = SDL_CreateGPUDevice(
		SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
		SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB,
		true,
		nullptr );
	if( !dbg.gpu_device ) {
		LOG_ERR( "DEBUG: Failed to create GPU device: %s", SDL_GetError( ) );
		SDL_DestroyWindow( dbg.win_main );
		dbg.win_main = nullptr;
		return false;
	}

	if( !SDL_ClaimWindowForGPUDevice( dbg.gpu_device, dbg.win_main ) ) {
		LOG_ERR( "DEBUG: Failed to claim window for GPU device: %s",
			SDL_GetError( ) );
		SDL_DestroyGPUDevice( dbg.gpu_device );
		SDL_DestroyWindow( dbg.win_main );
		dbg.gpu_device = nullptr;
		dbg.win_main = nullptr;
		return false;
	}

	// Setup ImGui context
	IMGUI_CHECKVERSION( );
	ImGui::CreateContext( );
	ImGuiIO& io = ImGui::GetIO( );
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.IniFilename = nullptr; // Disable saving/loading window layout

	// Setup ImGui style
	ImGui::StyleColorsDark( );
	ImGuiStyle& style = ImGui::GetStyle( );
	style.WindowRounding = DBGUI::WindowRounding;
	style.FrameRounding = DBGUI::FrameRounding;
	style.ScrollbarRounding = DBGUI::ScrollbarRounding;
	style.FontSizeBase = DBGUI::FontSize;

	// Scale style for high DPI displays
	style.ScaleAllSizes( display_scale );
	style.FontScaleDpi = display_scale;

	// Setup Platform/Renderer backends for SDL_GPU
	ImGui_ImplSDLGPU3_InitInfo init_info = {};
	init_info.Device = dbg.gpu_device;
	init_info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(
		dbg.gpu_device, dbg.win_main );
	init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;

	ImGui_ImplSDL3_InitForOther( dbg.win_main );
	ImGui_ImplSDLGPU3_Init( &init_info );

	ImFont* font = nullptr;
	const char* windir = std::getenv( "windir" );
	if( windir ) {
		char fontPath[256];
		sprintf_s( fontPath, sizeof( fontPath ), "%s\\Fonts\\consola.ttf", windir );
		std::error_code ec;
		if( std::filesystem::exists( fontPath, ec ) )
			font = io.Fonts->AddFontFromFileTTF( fontPath, style.FontSizeBase );
	}
	if( !font )
		io.Fonts->AddFontFromMemoryCompressedTTF( IBM_VGA_8x16_compressed_data, IBM_VGA_8x16_compressed_size );
	imgui_initialized = true;
	cycle_count = 0;

	// Add known segments to ordered segments
	dbg.segment[SEG_BASE] = 0U;
	dbg.segment[SEG_MAX] = static_cast<uint16_t>( -1 );
	dbg.segment[SEG_CODE] = RealSegValue( cs );
	dbg.segment[SEG_PSP] = RealSegValue( es ); // could also use es or ds segments, or dos.psp( )
	dbg.segment[SEG_STACK] = RealSegValue( ss );
	dbg.segment[SEG_STACK_END] = GetAddress( SegValue( ss ), reg_sp ) >> 4;
	const auto psp = &MemBase[dbg.segment[SEG_PSP] << 4];
	dbg.segment[SEG_HEAP] = reinterpret_cast<uint16_t &>( psp[0x2] ); // Segment of the first byte beyond the memory allocated to the program
	dbg.segment[SEG_ENV] = reinterpret_cast<uint16_t &>( psp[0x2C] ); // Environment segment
	SEGTYPE seg_type = SEG_BASE;
	for( const auto &seg : dbg.segment ) {
		if( !ordered_segments.count( { seg, {} } ) )
			ordered_segments.insert( { seg, { seg_type } } );
		++seg_type;
	}
	// Point stack view to top of stack
	dataSeg[STACK_VIEW] = RealSegValue( ss );
	dataOfs[STACK_VIEW] = reg_sp;

	data_buffer.resize( dbg.segment[SEG_HEAP] << 4 );
	DBGUI_SaveMemoryState( );

	char program_name[debugger_title_length + 10U] = "";
	strcat( program_name, debugger_title );
	program_name[debugger_title_length - 1] = ':';
	program_name[debugger_title_length] = ' ';
	strncpy( &program_name[debugger_title_length + 1U], reinterpret_cast<char *>( &MemBase[( dbg.segment[SEG_ENV] << 4 ) + 0xB8] ), 8 );
	program_name[debugger_title_length + 9U] = 0U;
	SDL_SetWindowTitle( dbg.win_main, program_name );

	// Now that ImGui is initialized, resize the window to fit all child
	// windows. Need to do a dummy frame to get accurate font metrics.
	ImGui_ImplSDLGPU3_NewFrame( );
	ImGui_ImplSDL3_NewFrame( );
	ImGui::NewFrame( );
	// Calculate window dimensions from character rows/columns
	char_width = ImGui::CalcTextSize( "X" ).x; // Use a representative character to get monospace font width
	digit_width = ImGui::CalcTextSize( "00000000000000000000000000000000", NULL, false, 0.0f ).x * 0.03125f;
	space_width = ImGui::CalcTextSize( "                                ", NULL, false, 0.0f ).x * 0.03125f;
	address_width = char_width * 10.0f;
	line_height = ImGui::GetTextLineHeightWithSpacing( );
	line_height_no_spacing = ImGui::GetTextLineHeight( );
	title_bar_height = ImGui::GetFrameHeight( );
	padding = ImGui::GetStyle( ).WindowPadding = ImVec2( char_width * 0.5f, ( title_bar_height - line_height_no_spacing ) * 0.5f ); // X = horizontal, Y = vertical padding;
	dbg.window_width = static_cast<int>( CalcTotalWidth( ) );
	dbg.window_height = static_cast<int>( CalcTotalHeight( ) );
	SDL_SetWindowSize( dbg.win_main, dbg.window_width, dbg.window_height );
	SDL_SetWindowPosition( dbg.win_main, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED );
	SDL_ShowWindow( dbg.win_main );

	for( WINDOW_ID i = static_cast<WINDOW_ID>( 0U ); i < NUM_WINDOWS; ++i ) {
		window_pos[i] = ImVec2( CalcWindowX( i ), CalcWindowY( i ) );
		window_size[i] = ImVec2( CalcWindowWidth( i ), CalcWindowHeight( i ) );
	}
	ImGui::EndFrame( );

	return imgui_initialized;
}

void DBGUI_Shutdown( ) {
	if( !imgui_initialized ) {
		return;
	}

	ImGui_ImplSDLGPU3_Shutdown( );
	ImGui_ImplSDL3_Shutdown( );
	ImGui::DestroyContext( );

	if( dbg.gpu_device ) {
		SDL_ReleaseWindowFromGPUDevice( dbg.gpu_device, dbg.win_main );
		SDL_DestroyGPUDevice( dbg.gpu_device );
		dbg.gpu_device = nullptr;
	}

	if( dbg.win_main ) {
		SDL_DestroyWindow( dbg.win_main );
		dbg.win_main = nullptr;
	}

	imgui_initialized = false;
}

void DBGUI_NewFrame( ) {
	if( !imgui_initialized ) {
		return;
	}

	ImGui_ImplSDLGPU3_NewFrame( );
	ImGui_ImplSDL3_NewFrame( );
	ImGui::NewFrame( );
}

void DBGUI_Render( ) {
	if( !imgui_initialized )
		return;

	ImGui::Render( );

	SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(
		dbg.gpu_device );
	if( !command_buffer ) {
		LOG_ERR( "DEBUG: Failed to acquire GPU command buffer: %s",
			SDL_GetError( ) );
		return;
	}

	SDL_GPUTexture* swapchain_texture = nullptr;
	if( !SDL_WaitAndAcquireGPUSwapchainTexture( command_buffer,
		dbg.win_main,
		&swapchain_texture,
		nullptr,
		nullptr ) ) {
		LOG_ERR( "DEBUG: Failed to acquire swapchain texture: %s",
			SDL_GetError( ) );
		SDL_SubmitGPUCommandBuffer( command_buffer );
		return;
	}

	if( swapchain_texture ) {
		ImDrawData* draw_data = ImGui::GetDrawData( );

		// Must call PrepareDrawData before the render pass
		ImGui_ImplSDLGPU3_PrepareDrawData( draw_data, command_buffer );

		// Set up the color target with clear color
		SDL_GPUColorTargetInfo target_info = {};
		target_info.texture = swapchain_texture;
		target_info.clear_color.r = DBGUI::ClearColorR / 255.0f;
		target_info.clear_color.g = DBGUI::ClearColorG / 255.0f;
		target_info.clear_color.b = DBGUI::ClearColorB / 255.0f;
		target_info.clear_color.a = DBGUI::ClearColorA / 255.0f;
		target_info.load_op = SDL_GPU_LOADOP_CLEAR;
		target_info.store_op = SDL_GPU_STOREOP_STORE;

		SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(
			command_buffer, &target_info, 1, nullptr );

		ImGui_ImplSDLGPU3_RenderDrawData( draw_data,
			command_buffer,
			render_pass );

		SDL_EndGPURenderPass( render_pass );
	}

	SDL_SubmitGPUCommandBuffer( command_buffer );
}

// Title bar colors - purple background with black text
static const ImVec4 TitleBgColor = ImVec4( 0.42f, 0.41f, 0.84f, 0.8f ); // Purple
static const ImVec4 TitleTextColor = ImVec4( 0.0f, 0.0f, 0.0f, 1.0f );  // Black

// Helper to begin a window with cyan title bar and black title text
bool DBGUI_BeginWindowWithStyledTitle( const char* title, ImGuiWindowFlags flags ) {
	// Push title bar colors
	ImGui::PushStyleColor( ImGuiCol_TitleBg, TitleBgColor );
	ImGui::PushStyleColor( ImGuiCol_TitleBgActive, TitleBgColor );
	ImGui::PushStyleColor( ImGuiCol_TitleBgCollapsed, TitleBgColor );
	ImGui::PushStyleColor( ImGuiCol_Text, TitleTextColor );

	bool result = ImGui::Begin( title, nullptr, flags | ADDITIONAL_FLAGS );

	// Restore text color for window content (keep title bar colors)
	ImGui::PopStyleColor( ); // Pop text color

	return result;
}
// Helper to end a styled window
void DBGUI_EndWindowWithStyledTitle( ) {
	ImGui::End( );
	ImGui::PopStyleColor( 3 ); // Pop title bar colors
}

bool DBGUI_BeginWindow( const char *name, int flags ) {
	return ImGui::Begin( name, nullptr, static_cast<ImGuiWindowFlags>( flags | ImGuiWindowFlags_NoTitleBar ) );
}
void DBGUI_EndWindow( ) {
	ImGui::End( );
}
#endif
