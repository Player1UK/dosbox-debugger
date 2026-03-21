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

extern DBGBlock dbg;
extern bool debugging;

extern FILE* debuglog;
extern std::vector<CDebugVar*> varList;

SCodeViewData codeViewData = {};

char dataBuffer[24 * 4096 * 128];
char stackBuffer[24 * 4096 * 128];

Bitu cycle_count = 0;

bool showExtend;
bool showPrintable = true;

char curSelectorName[3] = { 0, 0, 0 };

static bool imgui_initialized = false;
static float display_scale = 1.0f;

static float char_width;
static float line_height;
static float line_height_no_spacing;
static ImVec2 padding;
static float title_bar_height;
static float CalcWindowHeight( int rows, bool fTitle = true );
static float CalcWindowWidth( int cols );

bool DBGUI_IsInitialized( ) {
	return imgui_initialized;
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

void SetCodeWinToEIP( ) {
	if( AddressVisited( SegValue( cs ), reg_eip ) )
		dbg.update_win_scroll[WIN_CODE] = true;
	else // address not already disassembled
		dbg.update_win[WIN_CODE] = true;
	codeViewData.useCS = SegValue( cs );
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

/********************/
/*   Draw windows   */
/********************/
void SnapToGrid( WINDOW_ID winID, ImVec2 &pos, ImVec2 &size ) {
	// Detect movement and snap
	if( ImGui::IsWindowHovered( ImGuiHoveredFlags_ChildWindows ) ||
		ImGui::IsWindowFocused( ImGuiHoveredFlags_ChildWindows ) ) {
		if( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) ) {
			ImVec2 newPos = ImGui::GetWindowPos( );
			if( newPos.x != pos.x || newPos.y != pos.y ) {
				if( newPos.x < 0 )
					pos.x = 0;
				else
					pos.x = roundf( newPos.x / char_width ) * char_width;
				if( newPos.y < 0 )
					pos.y = 0;
				else
					pos.y = roundf( newPos.y / title_bar_height ) * title_bar_height;
				dbg.update_win_frame[winID] = true;
			}
			if( !ImGui::IsWindowCollapsed( ) ) {
				ImVec2 newSize = ImGui::GetWindowSize( );
				if( newSize.x != size.x || newSize.y != size.y ) {
					size.x = roundf( newSize.x / char_width ) * char_width;
					size.y = roundf( newSize.y / title_bar_height ) * title_bar_height;
					dbg.update_win_frame[winID] = true;
				}
			}
		}
	}
}
void SnapWindow( WINDOW_ID winID, ImVec2 &pos, ImVec2 &size ) {
	// Detect movement and snap
	if( ImGui::IsWindowHovered( ImGuiHoveredFlags_ChildWindows ) ||
		ImGui::IsWindowFocused( ImGuiHoveredFlags_ChildWindows ) ) {
		if( ImGui::IsMouseDown( ImGuiMouseButton_Left ) ) {
			ImVec2 newPos = ImGui::GetWindowPos( );
			if( newPos.x != pos.x || newPos.y != pos.y ) {
				dbg.update_win_frame[winID] = true;
			}
		} else if( !ImGui::IsWindowCollapsed( ) ) {
			ImVec2 newSize = ImGui::GetWindowSize( );
			if( newSize.x != size.x || newSize.y != size.y ) {
				dbg.update_win_frame[winID] = true;
			}
		}
	}
}
extern char* AnalyzeInstruction( char*, bool, const bool );
extern uint32_t GetAddress( uint16_t, uint32_t );
extern bool GetDescriptorInfo( char*, char*, char* );

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

void DrawCode( void ) {
	if( !DBGUI_IsInitialized( ) ) {
		return;
	}
	static auto window_pos = ImVec2( 0, DBGUI_GetWindowY( WIN_CODE ) );
	static auto window_size = ImVec2( CalcWindowWidth( dbg.window_cols ), CalcWindowHeight( dbg.rows[WIN_CODE] ) );
	static auto window_width = window_size.x;
	ImGui::SetNextWindowPos( window_pos, ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size, ImGuiCond_FirstUseEver );

	if( dbg.update_win_frame[WIN_CODE] ) {
		dbg.update_win_frame[WIN_CODE] = false;
		ImGui::SetNextWindowPos( window_pos, ImGuiCond_Always );
		ImGui::SetNextWindowSize( window_size, ImGuiCond_Always );
		dbg.update_win_scroll[WIN_CODE] = true;
	}
	if( dbg.update_win[WIN_CODE] ) {
		dbg.update_win[WIN_CODE] = false;
		auto startOffset = GetAddress( codeViewData.useCS, codeViewData.useEIP );
		DasmRecursiveDisassemble( startOffset, codeViewData.useEIP, cpu.code.big, cpu.pmode );
		dbg.update_win_scroll[WIN_CODE] = true;
	}
	if( DBGUI_BeginWindowWithStyledTitle( "Code" , ImGuiWindowFlags_HorizontalScrollbar ) ) {
		static uint32_t selectedIndex = -1;
		static bool setFocus = false;
		uint32_t i = 0;
		uint8_t addressColorIndex = 0;
		uint16_t currentSegment = CodeSegment( 0U );
		for( auto dline = DecodedLine::first( ); !DecodedLine::isEnd( ); ++dline, ++i ) {
			if( dbg.update_win_scroll[WIN_CODE] && dline.address.segment == codeViewData.useCS && dline.address.offset == codeViewData.useEIP ) {
				dbg.update_win_scroll[WIN_CODE] = false;
				ImGui::SetScrollHereY( );
			}
			if( dline.address.segment != currentSegment ) {
				currentSegment = dline.address.segment;
				if( ++addressColorIndex >= MAX_ADDRESS_COLORS )
					addressColorIndex = 0U;
			}
			bool is_current_ip = ( dline.address.segment == SegValue( cs ) ) && ( dline.address.offset == reg_eip );
			bool is_breakpoint = CBreakpoint::IsBreakpoint( dline.address.segment, dline.address.offset );
			// Make line selectable
			if( is_current_ip && selectedIndex == static_cast<uint32_t>( -1 ) ) {
				selectedIndex = i;
				setFocus = true;
			}
			bool isSelected = ( selectedIndex == i );
			char id[11];
			sprintf( id, "##%08X", dline.base_offset );
			if( ImGui::Selectable( id, isSelected, ImGuiSelectableFlags_SelectOnClick ) ) {
				selectedIndex = i;
				codeViewData.cursorSeg = dline.address.segment;
				codeViewData.cursorOfs = dline.address.offset;
			}
			if( setFocus ) {
				setFocus = false;
				ImGui::SetItemDefaultFocus( );
				ImGui::SetKeyboardFocusHere( -1 );
			}
			if( isSelected && ImGui::IsItemFocused( ) ) {
				selectedIndex = i;
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
	SnapToGrid( WIN_CODE, window_pos, window_size );
	DBGUI_EndWindowWithStyledTitle( );
}

static void DrawData( void ) {
	if( !DBGUI_IsInitialized( ) ) {
		return;
	}
	static auto window_pos = ImVec2( CalcWindowWidth( dbg.window_cols ), DBGUI_GetWindowY( WIN_DATA ) );
	static auto window_size = ImVec2( CalcWindowWidth( dbg.window_cols ), CalcWindowHeight( dbg.rows[WIN_DATA] ) );
	ImGui::SetNextWindowPos( window_pos, ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size, ImGuiCond_FirstUseEver );

	if( dbg.update_win_frame[WIN_DATA] ) {
		dbg.update_win_frame[WIN_DATA] = false;
		ImGui::SetNextWindowPos( window_pos, ImGuiCond_Always );
		ImGui::SetNextWindowSize( window_size, ImGuiCond_Always );
	}
	if( DBGUI_BeginWindowWithStyledTitle( "Data", ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoFocusOnAppearing ) ) {
		if( ImGui::IsWindowFocused( ImGuiHoveredFlags_ChildWindows ) && dbg.active_win_data != 0U )
			dbg.active_win_data = 0U;
		if( dbg.update_win[WIN_DATA] ) {
			dbg.update_win[WIN_DATA] = false;
			uint32_t add = 0U;
			char *line = dataBuffer;
			for( uint16_t count = 0xFFF; count; --count, line += 82 ) {
				bool f16bit = false;
				// Address
				if( add <= 0xFFFF )
					sprintf( line, "%04X:%04X      ", dataSeg[0], add );
				else {
					sprintf( line, "%04X:%08X  ", dataSeg[0], add );
					f16bit = true;
				}
				// Hex values
				for( int x = 0; x < 16; ++x, ++add ) {
					uint32_t address = GetAddress( dataSeg[0], add );
					uint8_t ch;
					if( mem_readb_checked( address, &ch ) ) {
						ch = 0;
					}
					sprintf( &line[3 * x + ( f16bit ? 14 : ( 11 + ( x >> 2 ) ) )], " %02X ", ch );
					if( showPrintable ) {
						if( ch < 32 ||
							!isprint( static_cast<unsigned char>( ch ) ) ) {
							ch = '.';
						}
					} else {
						if( ch < 32 ) {
							ch = '.';
						}
					}
					line[65 + x] = ch;
				}
				if( line[62] == 0 ) line[62] = ' ';
				if( line[63] != ' ' ) line[63] = ' ';
				if( line[64] != ' ' ) line[64] = ' ';
				line[81] = '\n';
			}
			dbg.update_win_scroll[WIN_DATA] = true;
		}
		ImGui::TextUnformatted( dataBuffer );
		if( dbg.update_win_scroll[WIN_DATA] ) {
			dbg.update_win_scroll[WIN_DATA] = false;
			ImGui::SetScrollY( ( dataOfs[0] >> 4 ) * line_height_no_spacing );
		}
	}
	SnapToGrid( WIN_DATA, window_pos, window_size );
	DBGUI_EndWindowWithStyledTitle( );
}

static void DrawStack( void ) {
	if( !DBGUI_IsInitialized( ) ) {
		return;
	}
	static auto window_pos = ImVec2( CalcWindowWidth( dbg.window_cols ), DBGUI_GetWindowY( WIN_STACK ) );
	static auto window_size = ImVec2( CalcWindowWidth( dbg.window_cols ), CalcWindowHeight( dbg.rows[WIN_STACK] ) );
	ImGui::SetNextWindowPos( window_pos, ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size, ImGuiCond_FirstUseEver );

	if( dbg.update_win_frame[WIN_STACK] ) {
		dbg.update_win_frame[WIN_STACK] = false;
		ImGui::SetNextWindowPos( window_pos, ImGuiCond_Always );
		ImGui::SetNextWindowSize( window_size, ImGuiCond_Always );
	}
	if( DBGUI_BeginWindowWithStyledTitle( "Stack", ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoFocusOnAppearing ) ) {
		if( ImGui::IsWindowFocused( ImGuiHoveredFlags_ChildWindows ) && dbg.active_win_data != 1U )
			dbg.active_win_data = 1U;
		if( dbg.update_win[WIN_STACK] ) {
			dbg.update_win[WIN_STACK] = false;
			uint32_t add = 0U;
			char *line = stackBuffer;
			for( uint16_t count = 0xFFF; count; --count, line += 82 ) {
				bool f16bit = false;
				// Address
				if( add <= 0xFFFF )
					sprintf( line, "%04X:%04X      ", dataSeg[1], add );
				else {
					sprintf( line, "%04X:%08X  ", dataSeg[1], add );
					f16bit = true;
				}
				// Hex values
				for( int x = 0; x < 16; ++x, ++add ) {
					uint32_t address = GetAddress( dataSeg[1], add );
					uint8_t ch;
					if( mem_readb_checked( address, &ch ) ) {
						ch = 0;
					}
					sprintf( &line[3 * x + ( f16bit ? 14 : ( 11 + ( x >> 2 ) ) )], " %02X ", ch );
					if( showPrintable ) {
						if( ch < 32 ||
							!isprint( static_cast<unsigned char>( ch ) ) ) {
							ch = '.';
						}
					} else {
						if( ch < 32 ) {
							ch = '.';
						}
					}
					line[65 + x] = ch;
				}
				if( line[62] == 0 ) line[62] = ' ';
				if( line[63] != ' ' ) line[63] = ' ';
				if( line[64] != ' ' ) line[64] = ' ';
				line[81] = '\n';
			}
			dbg.update_win_scroll[WIN_STACK] = true;
		}
		ImGui::TextUnformatted( stackBuffer );
		if( dbg.update_win_scroll[WIN_STACK] ) {
			dbg.update_win_scroll[WIN_STACK] = false;
			ImGui::SetScrollY( ( dataOfs[1] >> 4 ) * line_height_no_spacing );
		}
	}
	SnapToGrid( WIN_STACK, window_pos, window_size );
	DBGUI_EndWindowWithStyledTitle( );
}

const uint8_t oEIP = 8;
static uint32_t oldregs[oEIP + 1] = {};
static Segment oldsegs[6] = {};
static auto oldcpucpl = cpu.cpl;
static auto oldflags = cpu_regs.flags;

void SaveCPUstate( ) {
	for( uint8_t i = 0; i < oEIP; ++i )
		oldregs[i] = cpu_regs.regs[i].dword[DW_INDEX];
	oldregs[oEIP] = reg_eip;
	for( uint8_t i = 0; i < 6; ++i )
		oldsegs[i].val = SegValue( static_cast<SegNames>( i ) );
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

static void DrawRegisters( void ) {
	if( !DBGUI_IsInitialized( ) ) {
		return;
	}
	static auto window_pos = ImVec2( 0, DBGUI_GetWindowY( WIN_REG ) );
	static auto window_size = ImVec2( CalcWindowWidth( dbg.window_cols ), CalcWindowHeight( dbg.rows[WIN_REG] ) );
	static auto window_width = window_size.x;
	ImGui::SetNextWindowPos( window_pos, ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size, ImGuiCond_FirstUseEver );

	if( dbg.update_win_frame[WIN_REG] ) {
		dbg.update_win_frame[WIN_REG] = false;
		ImGui::SetNextWindowPos( window_pos, ImGuiCond_Always );
		//ImGui::SetNextWindowSize( window_size, ImGuiCond_Always );
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
			case tSEG:
				if( SegValue( static_cast<SegNames>( e.x ) ) != oldsegs[e.x].val )
					ImGui::TextColored( highlight_color, "%04X", SegValue( static_cast<SegNames>( e.x ) ) );
				else
					ImGui::Text( "%04X", SegValue( static_cast<SegNames>( e.x ) ) );
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
	SnapToGrid( WIN_REG, window_pos, window_size );
	DBGUI_EndWindowWithStyledTitle( );
}

extern uint16_t NumCodeSegments( );
extern uint16_t CodeSegment( uint16_t );

static void DrawSegments( void ) {
	static auto window_pos = ImVec2( 0, DBGUI_GetWindowY( WIN_SEG ) );
	static auto window_size = ImVec2( CalcWindowWidth( dbg.window_cols ), CalcWindowHeight( dbg.rows[WIN_SEG] ) );
	ImGui::SetNextWindowPos( window_pos, ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size, ImGuiCond_FirstUseEver );

	if( dbg.update_win_frame[WIN_SEG] ) {
		dbg.update_win_frame[WIN_SEG] = false;
		ImGui::SetNextWindowPos( window_pos, ImGuiCond_Always );
		ImGui::SetNextWindowSize( window_size, ImGuiCond_Always );
	}
	if( DBGUI_BeginWindowWithStyledTitle( "Segments", ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing ) ) {
		for( uint16_t count = NumCodeSegments( ), i = 0U, addressColorIndex = 0U; count; --count, ++i ) {
			ImGui::SameLine( );
			ImGui::TextColored( address_colors[addressColorIndex][0], "%04X ", CodeSegment( i ) );
			if( ++addressColorIndex >= MAX_ADDRESS_COLORS )
				addressColorIndex = 0U;
		}
	}
	SnapToGrid( WIN_SEG, window_pos, window_size );
	DBGUI_EndWindowWithStyledTitle( );
}

#define DEBUG_VAR_BUF_LEN 16
static void DrawVariables( ) {
	if( !DBGUI_IsInitialized( ) ) {
		return;
	}
	static auto window_pos = ImVec2( CalcWindowWidth( dbg.window_cols ), DBGUI_GetWindowY( WIN_VAR ) );
	static auto window_size = ImVec2( CalcWindowWidth( dbg.window_cols ), CalcWindowHeight( dbg.rows[WIN_VAR] ) );
	ImGui::SetNextWindowPos( window_pos, ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size, ImGuiCond_FirstUseEver );

	if( dbg.update_win_frame[WIN_VAR] ) {
		dbg.update_win_frame[WIN_VAR] = false;
		ImGui::SetNextWindowPos( window_pos, ImGuiCond_Always );
		ImGui::SetNextWindowSize( window_size, ImGuiCond_Always );
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
	SnapToGrid( WIN_VAR, window_pos, window_size );
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

static void DrawConsole( void ) {
	static auto window_pos = ImVec2( 0, DBGUI_GetWindowY( WIN_CON ) );
	static auto window_size = ImVec2( CalcWindowWidth( dbg.window_cols ), CalcWindowHeight( dbg.rows[WIN_CON] ) );
	ImGui::SetNextWindowPos( window_pos, ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size, ImGuiCond_FirstUseEver );

	if( dbg.update_win_frame[WIN_CON] ) {
		dbg.update_win_frame[WIN_CON] = false;
		ImGui::SetNextWindowPos( window_pos, ImGuiCond_Always );
		ImGui::SetNextWindowSize( window_size, ImGuiCond_Always );
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
	SnapToGrid( WIN_CON, window_pos, window_size );
	DBGUI_EndWindowWithStyledTitle( );
	ImGui::PopStyleVar( );
}

void DBGUI_DrawOutputWindow( void ) {
	if( !imgui_initialized ) {
		return;
	}
	static auto window_pos = ImVec2( 0, DBGUI_GetWindowY( WIN_OUT ) );
	static auto window_size = ImVec2( CalcWindowWidth( dbg.window_cols ), CalcWindowHeight( dbg.rows[WIN_OUT] ) );
	ImGui::SetNextWindowPos( window_pos, ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size, ImGuiCond_FirstUseEver );

	if( dbg.update_win_frame[WIN_OUT] ) {
		dbg.update_win_frame[WIN_OUT] = false;
		ImGui::SetNextWindowPos( window_pos, ImGuiCond_Always );
		ImGui::SetNextWindowSize( window_size, ImGuiCond_Always );
	}
	if( DBGUI_BeginWindowWithStyledTitle( "Output", ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoFocusOnAppearing ) ) {

		for( auto it = logBuff.begin( ); it != logBuff.end( ); ++it )
			ImGui::TextUnformatted( it->c_str( ) );
	}
	if( dbg.update_win[WIN_OUT] ) {
		dbg.update_win[WIN_OUT] = false;
		ImGui::SetScrollHereY( );
	}
	SnapToGrid( WIN_OUT, window_pos, window_size );
	DBGUI_EndWindowWithStyledTitle( );
}

// Calculate window height for a given number of rows
static float CalcWindowHeight( int rows, bool fTitle ) {
	return ( rows * title_bar_height ) + ( fTitle ? title_bar_height : 0.0f );
}

// Calculate window width for a given number of columns
static float CalcWindowWidth( int cols ) {
	return ( cols * char_width );
}

// Get the calculated Y positions for each window
float DBGUI_GetWindowY( uint32_t window_index ) {
	float y = 0;
	switch( window_index ) {
	case NUM_WINDOWS:
		y += CalcWindowHeight( dbg.rows[WIN_STACK] );
	case WIN_STACK:
		y += CalcWindowHeight( dbg.rows[WIN_VAR] );
	case WIN_VAR:
		y += CalcWindowHeight( dbg.rows[WIN_DATA] );
	case WIN_DATA:
		break;
	case WIN_OUT:
		y += CalcWindowHeight( dbg.rows[WIN_CON] );
	case WIN_CON:
		y += CalcWindowHeight( dbg.rows[WIN_SEG] );
	case WIN_SEG:
		y += CalcWindowHeight( dbg.rows[WIN_REG] );
	case WIN_REG:
		y += CalcWindowHeight( dbg.rows[WIN_CODE] );
	case WIN_CODE:
	default:
		break;
	}
	return y;
}

// Calculate total height needed for all windows
float DBGUI_GetTotalHeight( ) {
	return DBGUI_GetWindowY( NUM_WINDOWS );
}

// Calculate window width based on column count
float DBGUI_GetWindowWidth( ) {
	return CalcWindowWidth( dbg.window_cols ) * 2.0f;
}

void DEBUG_DrawScreen( void ) {
	DrawCode( );
	DrawRegisters( );
	DrawSegments( );
	DrawData( );
	DrawVariables( );
	DrawStack( );
	DrawConsole( );
	DBGUI_DrawOutputWindow( );
}

void DBGUI_StartUp( void ) {
	if( imgui_initialized ) {
		return;
	}

	// Get display scale for high DPI support
	display_scale = SDL_GetDisplayContentScale( SDL_GetPrimaryDisplay( ) );
	if( display_scale <= 0.0f ) {
		display_scale = 1.0f;
	}

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
		return;
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
		return;
	}

	if( !SDL_ClaimWindowForGPUDevice( dbg.gpu_device, dbg.win_main ) ) {
		LOG_ERR( "DEBUG: Failed to claim window for GPU device: %s",
			SDL_GetError( ) );
		SDL_DestroyGPUDevice( dbg.gpu_device );
		SDL_DestroyWindow( dbg.win_main );
		dbg.gpu_device = nullptr;
		dbg.win_main = nullptr;
		return;
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
	dbg.window_width = static_cast<int>( DBGUI_GetWindowWidth( ) );
	dbg.window_height = static_cast<int>( DBGUI_GetTotalHeight( ) );
	SDL_SetWindowSize( dbg.win_main, dbg.window_width, dbg.window_height );
	SDL_SetWindowPosition( dbg.win_main, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED );
	SDL_ShowWindow( dbg.win_main );

	ImGui::EndFrame( );
}

void DBGUI_Shutdown( void ) {
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

void DBGUI_NewFrame( void ) {
	if( !imgui_initialized ) {
		return;
	}

	ImGui_ImplSDLGPU3_NewFrame( );
	ImGui_ImplSDL3_NewFrame( );
	ImGui::NewFrame( );
}

void DBGUI_Render( void ) {
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

// Render functions for debugger windows - called from debugger.cpp
void DBGUI_DrawRegisterWindow( void ) {
	if( !imgui_initialized ) {
		return;
	}

	// Calculate window dimensions based on character rows/columns
	float window_width = CalcWindowWidth( dbg.window_cols );
	float window_height = CalcWindowHeight( dbg.rows[WIN_REG] );

	ImGui::SetNextWindowPos( ImVec2( 0, 0 ), ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( ImVec2( window_width, window_height ),
		ImGuiCond_FirstUseEver );

	if( ImGui::Begin( "Registers", nullptr, ImGuiWindowFlags_NoCollapse ) ) {
		// Row 1: EAX, ESI, DS, ES, FS, GS, SS
		ImGui::Text( "EAX=%08X  ESI=%08X  DS=%04X  ES=%04X  FS=%04X  GS=%04X  SS=%04X",
			reg_eax,
			reg_esi,
			SegValue( ds ),
			SegValue( es ),
			SegValue( fs ),
			SegValue( gs ),
			SegValue( ss ) );

		// Row 2: EBX, EDI, CS, EIP, Flags
		ImGui::Text( "EBX=%08X  EDI=%08X  CS=%04X  EIP=%08X  C=%d Z=%d S=%d O=%d A=%d P=%d D=%d I=%d T=%d",
			reg_ebx,
			reg_edi,
			SegValue( cs ),
			reg_eip,
			GETFLAG( CF ) ? 1 : 0,
			GETFLAG( ZF ) ? 1 : 0,
			GETFLAG( SF ) ? 1 : 0,
			GETFLAG( OF ) ? 1 : 0,
			GETFLAG( AF ) ? 1 : 0,
			GETFLAG( PF ) ? 1 : 0,
			GETFLAG( DF ) ? 1 : 0,
			GETFLAG( IF ) ? 1 : 0,
			GETFLAG( TF ) ? 1 : 0 );

		// Row 3: ECX, EBP, IOPL, CPL
		ImGui::Text( "ECX=%08X  EBP=%08X  IOPL=%d  CPL=%d",
			reg_ecx,
			reg_ebp,
			(int) ( GETFLAG( IOPL ) >> 12 ),
			(int) cpu.cpl );

		// Row 4: EDX, ESP, Mode, Cycles
		const char* mode_str = "Real";
		if( cpu.pmode ) {
			if( GETFLAG( VM ) ) {
				mode_str = "VM86";
			} else if( cpu.code.big ) {
				mode_str = "Pr32";
			} else {
				mode_str = "Pr16";
			}
		}
		ImGui::Text( "EDX=%08X  ESP=%08X  %s  Cycles: %" PRIuPTR,
			reg_edx,
			reg_esp,
			mode_str,
			cycle_count );
	}
	ImGui::End( );
}
#endif
