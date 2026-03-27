// SPDX-FileCopyrightText:  2002-2025 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dosbox.h"

#if C_DEBUGGER
#include <SDL3/SDL.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <imgui_internal.h>

#include "cbreakpoint.h"
#include "cpu/cpu.h"
#include "cpu/paging.h"
#include "debugger_inc.h"
#include "debugger_disasm.h"
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

char dataBuffer[NUM_WIN_DATA][24 * 4096 * 128];

Bitu cycle_count = 0;

char curSelectorName[3] = { 0, 0, 0 };

static bool imgui_initialized = false;
static float display_scale = 1.0f;

static float char_width;
static float line_height;
static float line_height_no_spacing;
static ImVec2 padding;
static float title_bar_height;
static ImVec2 window_pos[NUM_WINDOWS];
static ImVec2 window_size[NUM_WINDOWS];

static float CalcWindowHeight( uint8_t rows, bool fTitle = true );
static float CalcWindowWidth( uint8_t cols );

WINDOW_ID &operator++( WINDOW_ID &id ) {
	id = static_cast<WINDOW_ID>( id + 1 );
	return id;
}

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
	if( cpu.pmode && !( reg_flags & FLAG_VM ) ) {
		Descriptor desc;
		if( cpu.gdt.GetDescriptor( SegValue( index ), desc ) )
			return desc.GetBase( ) >> 4;
	}
	return SegValue( index );
}

void SetCodeWinToEIP( ) {
	if( AddressVisited( GetAddress( SegValue( cs ), reg_eip ) ) )
		dbg.update_win_scroll[WIN_CODE] = true;
	else // address not already disassembled
		dbg.update_win[WIN_CODE] = true;
	codeViewData.useCS = RealSegValue( cs );
	codeViewData.useEIP = reg_eip;
}

void SetCodeWinToAddress( uint16_t segment, uint32_t offset ) {
	if( AddressVisited( segment, offset ) )
		dbg.update_win_scroll[WIN_CODE] = true;
	else // address not already disassembled
		dbg.update_win[WIN_CODE] = true;
	codeViewData.useCS = segment;
	codeViewData.useEIP = offset;
}

void DBGUI_UpdateOrderedSegments( bool refresh ) {
	uint16_t num_segments = ordered_segments.size( );
	for( uint8_t i = 0U; i <= SegNames::gs; ++i ) {
		uint16_t seg_val = RealSegValue( static_cast<SegNames>( i ) );
		if( !ordered_segments.count( seg_val ) )
			ordered_segments.insert( seg_val );
	}
	if( refresh && ordered_segments.size( ) != num_segments ) {
		for( uint8_t i = 0U; i < NUM_WIN_DATA; ++i )
			dbg.update_win[WIN_DATA + i] = true;
	}
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
//const ImVec4 torquoise_color = ImVec4( 0.18f, 0.82f, 0.97f, 1.0f );
//const ImVec4 light_torquoise_color = ImVec4( 0.37f, 0.86f, 0.97f, 1.0f );
//const ImVec4 dark_brown_color = ImVec4( 0.47f, 0.26f, 0.08f, 1.0f );

const uint8_t MAX_ADDRESS_COLORS = 7;
const ImVec4 address_colors[MAX_ADDRESS_COLORS][2] = {
{ ImVec4( 0.97f, 0.69f, 0.17f, 1.0f ), ImVec4( 0.97f, 0.77f, 0.37f, 1.0f ) }, // gold
{ ImVec4( 0.22f, 0.49f, 0.13f, 1.0f ), ImVec4( 0.29f, 0.65f, 0.18f, 1.0f ) }, // green
{ ImVec4( 0.64f, 0.18f, 0.97f, 1.0f ), ImVec4( 0.78f, 0.39f, 0.97f, 1.0f ) }, // purple
{ ImVec4( 0.8f, 0.29f, 0.32f, 1.0f ), ImVec4( 0.83f, 0.43f, 0.44f, 1.0f ) },  // red
{ ImVec4( 0.19f, 0.51f, 0.97f, 1.0f ), ImVec4( 0.41f, 0.63f, 0.97f, 1.0f ) }, // blue
{ ImVec4( 0.73f, 0.48f, 0.34f, 1.0f ), ImVec4( 0.94f, 0.53f, 0.31f, 1.0f ) }, // orange
{ ImVec4( 0.97f, 0.19f, 0.53f, 1.0f ), ImVec4( 0.97f, 0.38f, 0.64f, 1.0f ) }, // violet
};

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
		uint16_t num_segments = ordered_segments.size( );
		auto startOffset = GetAddress( codeViewData.useCS, codeViewData.useEIP );
		DasmRecursiveDisassemble( startOffset, codeViewData.useEIP, cpu.code.big, cpu.pmode );
		if( ordered_segments.size( ) != num_segments ) {
			for( uint8_t i = 0U; i < NUM_WIN_DATA; ++i )
				dbg.update_win[WIN_DATA + i] = true;
		}
		dbg.update_win_scroll[WIN_CODE] = true;
	}
	if( DBGUI_BeginWindowWithStyledTitle( "Code" , ImGuiWindowFlags_HorizontalScrollbar ) && !DecodedLine::isEmpty( ) ) {
		static uint32_t selectedIndex = -1;
		static bool setFocus = false;
		uint32_t i = 0;
		uint8_t addressColorIndex = MAX_ADDRESS_COLORS;
		uint16_t currentSegment = 0U;
		for( auto dline = DecodedLine::first( ); !DecodedLine::isEnd( ); ++dline, ++i ) {
			if( dbg.update_win_scroll[WIN_CODE] && dline.address.segment == codeViewData.useCS && dline.address.offset == codeViewData.useEIP ) {
				dbg.update_win_scroll[WIN_CODE] = false;
				ImGui::SetScrollHereY( );
				if( selectedIndex == static_cast<uint32_t>( -1 ) )
					setFocus = true;
				selectedIndex = i;
			}
			if( currentSegment != dline.address.segment ) { // match segment to defined segments
				currentSegment = dline.address.segment;
				if( ++addressColorIndex >= MAX_ADDRESS_COLORS )
					addressColorIndex = 0U;
			}
			if( dline.mnemonicMask & MM_Proc ) {
				ImGui::SetCursorPosX( window_width * 0.20f );
				ImGui::TextColored( blue_color, "Proc label:" );
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

static void DrawData( ) {
	ImGui::SetNextWindowPos( window_pos[WIN_DATA], ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size[WIN_DATA], ImGuiCond_FirstUseEver );

	if( dbg.update_win_frame[WIN_DATA] ) {
		dbg.update_win_frame[WIN_DATA] = false;
		ImGui::SetNextWindowPos( window_pos[WIN_DATA], ImGuiCond_Always );
		ImGui::SetNextWindowSize( window_size[WIN_DATA], ImGuiCond_Always );
	}
	if( DBGUI_BeginWindowWithStyledTitle( "Data", ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoFocusOnAppearing ) ) {
		if( ImGui::IsWindowFocused( ImGuiHoveredFlags_ChildWindows ) && dbg.active_win_data != WIN_DATA - WIN_DATA )
			dbg.active_win_data = WIN_DATA - WIN_DATA;
		if( dbg.update_win[WIN_DATA] ) {
			dbg.update_win[WIN_DATA] = false;
			const uint32_t stack_end = GetAddress( SegValue( ss ), reg_sp ) - 1U;
			char *line = dataBuffer[WIN_DATA - WIN_DATA];
			uint16_t segment = dataSeg[WIN_DATA - WIN_DATA];
			uint32_t base_segment = segment;
			auto ordered_segment = ordered_segments.begin( );
			for( ; ordered_segment != ordered_segments.end( ); ++ordered_segment ) {
				if( base_segment < *ordered_segment )
					break;
			}
			auto data = &MemBase[segment << 4];
			uint32_t offset = 0U;
			for( uint32_t count = ( ( ( segment << 4 ) + dataOfs[WIN_DATA - WIN_DATA] > stack_end ? dataOfs[WIN_DATA - WIN_DATA] + 1600 : stack_end ) >> 4 ) + 1U; count; --count, line += 82U, ++base_segment ) {
				if( base_segment >= *ordered_segment ) {
					segment = *ordered_segment;
					++ordered_segment;
					offset = 0U;
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
				for( int x = 0; x < 16; ++x, ++data, ++offset ) {
					sprintf( &line[3 * x + ( f32bit ? 14 : ( 11 + ( x >> 2 ) ) )], " %02X ", *data );
					line[65 + x] = ( *data < 32 || !isprint( *data ) ? '.' : *data ); // Ascii representation
				}
				*reinterpret_cast<int16_t *>( &line[63] ) = 0x2020;
				line[81] = '\n';
			}
			*line = 0;
		}
		ImGui::TextUnformatted( dataBuffer[WIN_DATA - WIN_DATA] );
		if( dbg.update_win_scroll[WIN_DATA] ) {
			dbg.update_win_scroll[WIN_DATA] = false;
			ImGui::SetScrollY( ( dataOfs[WIN_DATA - WIN_DATA] >> 4 ) * line_height_no_spacing );
		}
	}
	SnapToGrid( WIN_DATA );
	DBGUI_EndWindowWithStyledTitle( );
}

static uint32_t stack_lines = 0U;

static void DrawStack( ) {
	ImGui::SetNextWindowPos( window_pos[WIN_STACK], ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size[WIN_STACK], ImGuiCond_FirstUseEver );

	if( dbg.update_win_frame[WIN_STACK] ) {
		dbg.update_win_frame[WIN_STACK] = false;
		ImGui::SetNextWindowPos( window_pos[WIN_STACK], ImGuiCond_Always );
		ImGui::SetNextWindowSize( window_size[WIN_STACK], ImGuiCond_Always );
	}
	if( DBGUI_BeginWindowWithStyledTitle( "Stack", ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoFocusOnAppearing ) ) {
		if( ImGui::IsWindowFocused( ImGuiHoveredFlags_ChildWindows ) && dbg.active_win_data != WIN_STACK - WIN_DATA )
			dbg.active_win_data = WIN_STACK - WIN_DATA;
		if( dbg.update_win[WIN_STACK] ) {
			dbg.update_win[WIN_STACK] = false;
			char *line = dataBuffer[WIN_STACK - WIN_DATA];
			uint16_t start_segment = dataSeg[WIN_STACK - WIN_DATA];
			uint16_t segment = start_segment;
			uint32_t offset = dataOfs[WIN_STACK - WIN_DATA];
			offset = ( offset & 0x0000000F ? 16U : 0U ) + ( ( offset >> 4 ) << 4 );
			uint32_t base_segment = segment + ( offset >> 4 );
			auto ordered_segment = ordered_segments.end( );
			--ordered_segment;
			for( ; ordered_segment != ordered_segments.begin( ); --ordered_segment ) {
				if( base_segment > *ordered_segment )
					break;
			}
			if( segment < *ordered_segment ) {
				segment = *ordered_segment;
				offset = ( ( base_segment - segment ) << 4 ) + 0xF;
			} else {
				offset = ( ( *std::next( ordered_segment, 1 ) - segment ) << 4 ) - 1U;
				base_segment = *std::next( ordered_segment, 1 );
			}
			auto data = &MemBase[offset + ( segment << 4 )];
			stack_lines = base_segment - start_segment;
			for( uint32_t count = stack_lines; count; --count, line += 82, --base_segment ) {
				if( base_segment < *ordered_segment ) {
					--ordered_segment;
					segment = *ordered_segment;
					offset = ( ( base_segment - segment ) << 4 ) + 0xF;
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
				for( int x = 0; x < 16; ++x, --data, --offset ) {
					sprintf( &line[3 * x + ( f32bit ? 14 : ( 11 + ( x >> 2 ) ) )], " %02X ", *data );
					line[65 + x] = ( *data < 32 || !isprint( *data ) ? '.' : *data ); // Ascii representation
				}
				*reinterpret_cast<int16_t *>( &line[63] ) = 0x2020;
				line[81] = '\n';
			}
			*line = 0;
			dbg.update_win_scroll[WIN_STACK] = true;
		}
		ImGui::TextUnformatted( dataBuffer[WIN_STACK - WIN_DATA] );
		if( dbg.update_win_scroll[WIN_STACK] ) {
			dbg.update_win_scroll[WIN_STACK] = false;
			ImGui::SetScrollY( ( stack_lines - ( ( dataOfs[WIN_STACK - WIN_DATA] >> 4 ) + ( dataOfs[WIN_STACK - WIN_DATA] & 0x0000000F ? 1U : 0U ) ) ) * line_height_no_spacing );
		}
	}
	SnapToGrid( WIN_STACK );
	DBGUI_EndWindowWithStyledTitle( );
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

	if( DBGUI_BeginWindowWithStyledTitle( "Registers", ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing ) ) {

		Bitu changed_flags = reg_flags ^ oldflags;
		for( ENTRY e : layout ) {
			if( e.label[0] ) {
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
				break;
			case tSEG: {
				const auto segVal = RealSegValue( static_cast<SegNames>( e.x ) );
				if( segVal != oldsegs[e.x].val )
					ImGui::TextColored( highlight_color, "%04X", segVal );
				else
					ImGui::Text( "%04X", segVal );
			}
				break;
			case tIP:
				if( reg_eip != oldregs[oEIP] )
					ImGui::TextColored( highlight_color, "%04X %04X", ( reg_eip >> 16 ) & 0xFFFF, reg_eip & 0xFFFF );
				else
					ImGui::Text( "%04X %04X", ( reg_eip >> 16 ) & 0xFFFF, reg_eip & 0xFFFF );
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

static void DrawSegments( ) {
	ImGui::SetNextWindowPos( window_pos[WIN_SEG], ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size[WIN_SEG], ImGuiCond_FirstUseEver );

	if( dbg.update_win_frame[WIN_SEG] ) {
		dbg.update_win_frame[WIN_SEG] = false;
		ImGui::SetNextWindowPos( window_pos[WIN_SEG], ImGuiCond_Always );
		ImGui::SetNextWindowSize( window_size[WIN_SEG], ImGuiCond_Always );
	}
	if( DBGUI_BeginWindowWithStyledTitle( "Segments", ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing ) ) {
		uint16_t addressColorIndex = 0U;
		for( const auto &segment : ordered_segments ) {
			if( segment == 0U || segment == static_cast<uint16_t>( -1 ) )
				continue;
			ImGui::SameLine( );
			ImGui::TextColored( address_colors[addressColorIndex][0], "%04X ", segment );
			if( ++addressColorIndex >= MAX_ADDRESS_COLORS )
				addressColorIndex = 0U;
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
	if( DBGUI_BeginWindowWithStyledTitle( "Proc labels", ImGuiWindowFlags_NoFocusOnAppearing ) ) {
		static uint32_t selectedIndex = -1;
		uint8_t addressColorIndex = MAX_ADDRESS_COLORS;
		uint16_t currentSegment = 0U;
		for( const auto &[address, segment] : calls ) {
			const uint32_t offset = address - ( segment << 4 );
			if( currentSegment != segment ) { // match segment to defined segments
				currentSegment = segment;
				if( ++addressColorIndex >= MAX_ADDRESS_COLORS )
					addressColorIndex = 0U;
			}
			const bool isSelected = ( selectedIndex == address );
			char id[11];
			sprintf( id, "##%08X", address );
			if( ImGui::Selectable( id, isSelected, ImGuiSelectableFlags_SelectOnClick ) ) {
				selectedIndex = address;
				codeViewData.useCS = segment;
				codeViewData.useEIP = offset;
				dbg.update_win_scroll[WIN_CODE] = true;
			}
			ImGui::SameLine( 0.0f, 0.0f );
			ImGui::TextColored( address_colors[addressColorIndex][0], "%04X", segment );
			ImGui::SameLine( 0.0f, 0.0f );
			ImGui::TextColored( grey_color, ":" );
			ImGui::SameLine( 0.0f, 0.0f );
			ImGui::TextColored( address_colors[addressColorIndex][1], "%04X", offset );
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
	if( DBGUI_BeginWindowWithStyledTitle( "Labels", ImGuiWindowFlags_NoFocusOnAppearing ) ) {
		static uint32_t selectedIndex = -1;
		uint8_t addressColorIndex = MAX_ADDRESS_COLORS;
		uint16_t currentSegment = 0U;
		for( const auto &[address, segment] : jumps ) {
			const uint32_t offset = address - ( segment << 4 );
			if( currentSegment != segment ) { // match segment to defined segments
				currentSegment = segment;
				if( ++addressColorIndex >= MAX_ADDRESS_COLORS )
					addressColorIndex = 0U;
			}
			const bool isSelected = ( selectedIndex == address );
			char id[11];
			sprintf( id, "##%08X", address );
			if( ImGui::Selectable( id, isSelected, ImGuiSelectableFlags_SelectOnClick ) ) {
				selectedIndex = address;
				codeViewData.useCS = segment;
				codeViewData.useEIP = offset;
				dbg.update_win_scroll[WIN_CODE] = true;
			}
			ImGui::SameLine( 0.0f, 0.0f );
			ImGui::TextColored( address_colors[addressColorIndex][0], "%04X", segment );
			ImGui::SameLine( 0.0f, 0.0f );
			ImGui::TextColored( grey_color, ":" );
			ImGui::SameLine( 0.0f, 0.0f );
			ImGui::TextColored( address_colors[addressColorIndex][1], "%04X", offset );
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

	if( DBGUI_BeginWindowWithStyledTitle( "Variables", ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing ) ) {
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
	if( DBGUI_BeginWindowWithStyledTitle( "Output", ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoFocusOnAppearing ) ) {

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
		SetCodeWinToEIP( );
	}
	DrawConsole( );
	DrawCode( );
	DrawRegisters( );
	DrawSegments( );
	DrawCalls( );
	DrawLabels( );
	DrawData( );
	DrawStack( );
	DrawVariables( );
	DrawOutputWindow( );
}

void DBGUI_Reset( ) {
	codeViewData = {};
	for( uint8_t i = 0U; i < NUM_WIN_DATA; ++i ) {
		for( auto data = &dataBuffer[i][0]; *data; ++data )
			*data = 0;
		dataSeg[i] = 0U;
		dataOfs[i] = 0U;
	}
	for( WINDOW_ID win_i = static_cast<WINDOW_ID>( 0U ); win_i < NUM_WINDOWS; ++win_i )
		dbg.update_win[win_i] = true;
	dbg.active_win_data = 0U;
	stack_lines = 0U;
	DasmReset( );
	fReset = true;
}

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
		"DOSBox Staging Debugger",
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
		if( std::filesystem::exists( fontPath, ec ) ) {
			font = io.Fonts->AddFontFromFileTTF( fontPath, style.FontSizeBase );
		}
	}
	if( !font ) {
		io.Fonts->AddFontFromMemoryCompressedTTF( IBM_VGA_8x16_compressed_data,
			IBM_VGA_8x16_compressed_size );
	}
	imgui_initialized = true;
	cycle_count = 0;

	uint16_t heap_seg = GetAddress( SegValue( ss ), reg_sp ) >> 4;
	if( !ordered_segments.count( heap_seg ) )
		ordered_segments.insert( heap_seg );
	dataSeg[WIN_STACK - WIN_DATA] = RealSegValue( ss );
	dataOfs[WIN_STACK - WIN_DATA] = reg_sp;

	// Now that ImGui is initialized, resize the window to fit all child
	// windows. Need to do a dummy frame to get accurate font metrics.
	ImGui_ImplSDLGPU3_NewFrame( );
	ImGui_ImplSDL3_NewFrame( );
	ImGui::NewFrame( );
	// Calculate window dimensions from character rows/columns
	char_width = ImGui::CalcTextSize( "X" ).x; // Use a representative character to get monospace font width
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
	if( !imgui_initialized ) {
		return;
	}

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

	bool result = ImGui::Begin( title, nullptr, flags );

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
