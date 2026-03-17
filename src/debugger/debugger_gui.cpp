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
#include "utils/string_utils.h"

#include "IBM_VGA_8x16.h"

extern bool AddressVisited( uint16_t, uint32_t );
extern void DasmRecursiveDisassemble( char *, const uint32_t, const uint32_t, const bool, const bool );
extern bool ParseCommand( char * );

extern DBGBlock dbg;
extern bool debugging;

extern FILE* debuglog;
extern std::vector<CDebugVar*> varList;

SCodeViewData codeViewData = {};

char codeBuffer[24 * 4096 * 128];
char dataBuffer[24 * 4096 * 128];

Bitu cycle_count = 0;

bool showExtend;
bool showPrintable = true;

char curSelectorName[3] = { 0, 0, 0 };

static bool imgui_initialized = false;
static float display_scale = 1.0f;

static float char_width;
static float line_height;
static float line_height_no_spacing;
static float padding;
static float title_bar_height;
static float CalcWindowHeight( int rows, bool fTitle = true );

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
	if( !AddressVisited( SegValue( cs ), reg_eip ) ) // address not already disassembled
		dbg.update_win[WIN_CODE] = true;
	codeViewData.useCS = SegValue( cs );
	codeViewData.useEIP = reg_eip;
	dbg.update_win_scroll[WIN_CODE] = true;
}

/********************/
/*   Draw windows   */
/********************/

extern char* AnalyzeInstruction( char*, bool );
extern uint32_t GetAddress( uint16_t, uint32_t );
extern bool GetDescriptorInfo( char*, char*, char* );

const ImVec4 green_color = ImVec4( 0.0f, 1.0f, 0.0f, 1.0f );
const ImVec4 red_bg_color = ImVec4( 1.0f, 0.0f, 0.0f, 1.0f );

void DrawCode( void ) {
	if( !DBGUI_IsInitialized( ) ) {
		return;
	}
	static auto window_pos = ImVec2( 0, DBGUI_GetWindowY( WIN_CODE ) );
	static auto window_size = ImVec2( DBGUI_GetWindowWidth( ), CalcWindowHeight( dbg.rows[WIN_CODE] ) );
	ImGui::SetNextWindowPos( window_pos, ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size, ImGuiCond_FirstUseEver );

	if( dbg.update_win[WIN_CODE] ) {
		dbg.update_win[WIN_CODE] = false;
		auto startOffset = GetAddress( codeViewData.useCS, codeViewData.useEIP );
		DasmRecursiveDisassemble( codeBuffer, startOffset, codeViewData.useEIP, cpu.code.big, cpu.pmode );
		dbg.update_win_scroll[WIN_CODE] = true;
	}
	if( DBGUI_BeginWindowWithStyledTitle( " Code", ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize ) ) {
		static uint32_t selectedIndex = 0;
		uint32_t i = 0;
		for( auto line = codeBuffer; *line; ++line, ++i ) {
			if( *line == '\n' ) {
				ImGui::Spacing( );
			} else if( line[4] == ':' ) {
				char *endptr;
				uint16_t segment = strtol( line, &endptr, 16 );
				if( *endptr == ':' )
					++endptr;
				uint32_t offset = strtol( endptr, &endptr, 16 );

				if( dbg.update_win_scroll[WIN_CODE] && segment == codeViewData.useCS && offset == codeViewData.useEIP ) {
					dbg.update_win_scroll[WIN_CODE] = false;
					ImGui::SetScrollHereY( );
				}
				
				bool is_current_ip = ( segment == SegValue( cs ) ) && ( offset == reg_eip );
				bool is_breakpoint = CBreakpoint::IsBreakpoint( segment, offset );
				if( is_current_ip )
					ImGui::PushStyleColor( ImGuiCol_Text, green_color );
				else if( is_breakpoint )
					ImGui::PushStyleColor( ImGuiCol_Text, red_bg_color );
				// Make line selectable
				bool isSelected = ( selectedIndex == i );
				if( ImGui::Selectable( line, isSelected, ImGuiSelectableFlags_SelectOnClick ) ) {
					selectedIndex = i;
					codeViewData.cursorSeg = segment;
					codeViewData.cursorOfs = offset;
				}
				if( isSelected && ImGui::IsItemFocused( ) ) {
					selectedIndex = i;
				}
				if( is_current_ip || is_breakpoint )
					ImGui::PopStyleColor( );
			} else {
				ImGui::TextUnformatted( line );
			}
			while( *line ) ++line;
		}
	}
	DBGUI_EndWindowWithStyledTitle( );
}

extern uint16_t NumCodeSegments( );
extern uint16_t CodeSegment( uint16_t );

static void DrawSegments( void ) {
	static auto window_pos = ImVec2( 0, DBGUI_GetWindowY( WIN_SEG ) );
	static auto window_size = ImVec2( DBGUI_GetWindowWidth( ), CalcWindowHeight( dbg.rows[WIN_SEG] ) );
	ImGui::SetNextWindowPos( window_pos, ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size, ImGuiCond_FirstUseEver );

	if( DBGUI_BeginWindow( "Segments", ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse ) ) {
		ImGui::Text( "Segments: " );
		for( uint16_t count = NumCodeSegments( ), i = 0U; count; --count, ++i ) {
			ImGui::SameLine( );
			ImGui::Text( "%04X ", CodeSegment( i ) );
		}
	}
	DBGUI_EndWindow( );
}

static void DrawData( void ) {
	if( !DBGUI_IsInitialized( ) ) {
		return;
	}
	static auto window_pos = ImVec2( 0, DBGUI_GetWindowY( WIN_DATA ) );
	static auto window_size = ImVec2( DBGUI_GetWindowWidth( ), CalcWindowHeight( dbg.rows[WIN_DATA] ) );
	ImGui::SetNextWindowPos( window_pos, ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size, ImGuiCond_FirstUseEver );

	//for( auto dw = 0U; dw < 1U; ++dw )

	if( DBGUI_BeginWindowWithStyledTitle( " Data", ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize ) ) {
		if( dbg.update_win[WIN_DATA] ) {
			uint32_t add = 0U;
			char *line = dataBuffer;
			for( uint16_t count = 0xFFF; count; --count, line += 81 ) {
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
					line[64 + x] = ch;
				}
				if( line[62] == 0 ) line[62] = ' ';
				if( line[63] != ' ' ) line[63] = ' ';
				line[80] = '\n';
			}
		}
		ImGui::TextUnformatted( dataBuffer );
		if( dbg.update_win[WIN_DATA] ) {
			dbg.update_win[WIN_DATA] = false;
			ImGui::SetScrollY( ( dataOfs[0] >> 4 ) * line_height_no_spacing );
		}
	}
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
	static auto window_size = ImVec2( DBGUI_GetWindowWidth( ), CalcWindowHeight( dbg.rows[WIN_REG] ) );
	ImGui::SetNextWindowPos( window_pos, ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size, ImGuiCond_FirstUseEver );

	static const float TAB_POS[] = { 0.0f, window_size.x * 0.205f, window_size.x * 0.405f, window_size.x * 0.53f, window_size.x * 0.655f, window_size.x * 0.84f, window_size.x * 0.93f, window_size.x };

	if( DBGUI_BeginWindowWithStyledTitle( " Registers",
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoResize ) ) {

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
	DBGUI_EndWindowWithStyledTitle( );
}

#define DEBUG_VAR_BUF_LEN 16
static void DrawVariables( ) {
	if( !DBGUI_IsInitialized( ) ) {
		return;
	}
	static auto window_pos = ImVec2( 0, DBGUI_GetWindowY( WIN_VAR ) );
	static auto window_size = ImVec2( DBGUI_GetWindowWidth( ), CalcWindowHeight( dbg.rows[WIN_VAR] ) );
	ImGui::SetNextWindowPos( window_pos, ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size, ImGuiCond_FirstUseEver );

	if( DBGUI_BeginWindowWithStyledTitle( " Variables",
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoResize ) ) {
		if( varList.empty( ) ) {
			ImGui::TextDisabled( "(no variables defined)" );
		} else {
			char buffer[DEBUG_VAR_BUF_LEN] = {};

			for( size_t i = 0; i < 4 * 3 && i != varList.size( ); ++i ) {
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
	static auto window_size = ImVec2( DBGUI_GetWindowWidth( ), CalcWindowHeight( dbg.rows[WIN_CON], false ) );
	ImGui::SetNextWindowPos( window_pos, ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size, ImGuiCond_FirstUseEver );

	if( DBGUI_BeginWindow( "Console", ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse ) ) {
		if( !debugging ) {
			ImGui::PushStyleColor( ImGuiCol_Text, green_color );
			ImGui::Text( "(Running)" );
			ImGui::PopStyleColor( );
		} else {
			ImGui::Text( "%c", ( codeViewData.ovrMode ? 'O' : 'I' ) );
			ImGui::SameLine( );
			ImGui::PushStyleColor( ImGuiCol_Text, green_color );
			if( ImGui::InputText( "##console_input", codeViewData.inputStr, IM_ARRAYSIZE( codeViewData.inputStr ), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll ) ) {
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
	DBGUI_EndWindow( );
}

void DBGUI_DrawOutputWindow( void ) {
	if( !imgui_initialized ) {
		return;
	}
	static auto window_pos = ImVec2( 0, DBGUI_GetWindowY( WIN_OUT ) );
	static auto window_size = ImVec2( DBGUI_GetWindowWidth( ), CalcWindowHeight( dbg.rows[WIN_OUT] ) );
	ImGui::SetNextWindowPos( window_pos, ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size, ImGuiCond_FirstUseEver );

	if( DBGUI_BeginWindowWithStyledTitle( " Output",
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNavFocus ) ) {

		for( auto it = logBuff.begin( ); it != logBuff.end( ); ++it )
			ImGui::TextUnformatted( it->c_str( ) );
	}
	DBGUI_EndWindowWithStyledTitle( );
	if( dbg.update_win[WIN_OUT] ) {
		dbg.update_win[WIN_OUT] = false;
	}
}

// Calculate window height for a given number of rows
static float CalcWindowHeight( int rows, bool fTitle ) {
	return ( rows * line_height ) + ( fTitle ? title_bar_height : 0.0f ) + padding;
}

// Calculate window width for a given number of columns
static float CalcWindowWidth( int cols ) {
	return ( cols * char_width ) + padding;
}

// Get the calculated Y positions for each window
float DBGUI_GetWindowY( uint32_t window_index ) {
	float y = 0;
	switch( window_index ) {
	case NUM_WINDOWS:
		y += CalcWindowHeight( dbg.rows[WIN_OUT] );
	case WIN_OUT:
		y += CalcWindowHeight( dbg.rows[WIN_CON], false );
	case WIN_CON:
		y += CalcWindowHeight( dbg.rows[WIN_VAR] );
	case WIN_VAR:
		y += CalcWindowHeight( dbg.rows[WIN_DATA] );
	case WIN_DATA:
		y += CalcWindowHeight( dbg.rows[WIN_REG] );
	case WIN_REG:
		y += CalcWindowHeight( dbg.rows[WIN_SEG], false );
	case WIN_SEG:
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
	return CalcWindowWidth( dbg.window_cols );
}

void DEBUG_DrawScreen( void ) {
	DrawCode( );
	DrawSegments( );
	DrawRegisters( );
	DrawData( );
	DrawVariables( );
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
	constexpr int InitialWindowWidth = 800;
	constexpr int InitialWindowHeight = 600;
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
	padding = ImGui::GetStyle( ).WindowPadding.y * 2;
	title_bar_height = ImGui::GetFrameHeight( );
	dbg.window_width = static_cast<int>( DBGUI_GetWindowWidth( ) );
	dbg.window_height = static_cast<int>( DBGUI_GetTotalHeight( ) );
	SDL_SetWindowSize( dbg.win_main, dbg.window_width, dbg.window_height );
	SDL_SetWindowPosition( dbg.win_main,
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED );
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
	float window_width = DBGUI_GetWindowWidth( );
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
