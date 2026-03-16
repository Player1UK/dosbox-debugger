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

extern void DasmRecursiveDisassemble( char *, const uint32_t, const uint32_t, const bool, const bool );

extern DBGBlock dbg;
extern bool debugging;

extern FILE* debuglog;
extern std::vector<CDebugVar*> varList;

SCodeViewData codeViewData = {};

char codeBuffer[24 * 4096 * 128];

// Scroll state for Output window (lines from bottom, 0 = at bottom)
static int output_scroll_offset = 0;
Bitu cycle_count = 0;

bool showExtend;
bool showPrintable = true;

char curSelectorName[3] = { 0, 0, 0 };

static bool imgui_initialized = false;
static float display_scale = 1.0f;

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

void DEBUG_RefreshPage( int scroll ) {
	if( !imgui_initialized ) {
		return;
	}
	output_scroll_offset -= scroll;
	if( output_scroll_offset < 0 ) {
		output_scroll_offset = 0;
	}
	dbg.update_win[WIN_OUT] = true;
}

void SetCodeWinStart( ) {
	if( ( SegValue( cs ) == codeViewData.useCS ) && ( reg_eip >= codeViewData.useEIP ) &&
		( reg_eip <= codeViewData.useEIPlast ) ) {
		// in valid window - scroll ?
		if( reg_eip >= codeViewData.useEIPmid ) {
			codeViewData.useEIP += codeViewData.firstInstSize;
		}

	} else {
		// totally out of range.
		codeViewData.useCS = SegValue( cs );
		codeViewData.useEIP = codeViewData.goodEIP = reg_eip;
	}
	dbg.update_win[WIN_CODE] = true;
}

/********************/
/*   Draw windows   */
/********************/

extern char* AnalyzeInstruction( char*, bool );
extern uint32_t GetAddress( uint16_t, uint32_t );
extern bool GetDescriptorInfo( char*, char*, char* );

/*const uint32_t MAXSIZE_EIPARRAY = 150U;
uint32_t indexEIParray = 0;
uint32_t EIParray[MAXSIZE_EIPARRAY];

static void PopulateEIParray( ) {
	PhysPt start = GetAddress( codeViewData.useCS, 0 );
	Bitu size = 0;
	indexEIParray = 0;
	EIParray[MAXSIZE_EIPARRAY - 1] = -1;
	for( uint32_t newEIP = 0; newEIP < codeViewData.useEIP;
		newEIP += size, start += size ) {
		EIParray[indexEIParray] = newEIP;
		if( ++indexEIParray > MAXSIZE_EIPARRAY ) {
			indexEIParray = 0U;
		}
		char dline[200];
		size = DasmI386( dline, start, newEIP, cpu.code.big );
	}
}

static bool UseExistingEIP( uint32_t gap ) {
	if( indexEIParray >= MAXSIZE_EIPARRAY ) {
		return false;
	}
	auto indexEIParray_original = indexEIParray;
	if( indexEIParray >= gap ) {
		indexEIParray -= gap;
	} else if( EIParray[MAXSIZE_EIPARRAY - 1] < EIParray[0] ) {
		indexEIParray = MAXSIZE_EIPARRAY - ( gap - indexEIParray );
	} else {
		indexEIParray = 0U;
	}
	if( indexEIParray != indexEIParray_original && EIParray[indexEIParray] < codeViewData.useEIP ) {
		codeViewData.useEIP = EIParray[indexEIParray];
		return true;
	}
	return false;
}*/

const ImVec4 green_color = ImVec4( 0.0f, 1.0f, 0.0f, 1.0f );
const ImVec4 red_bg_color = ImVec4( 1.0f, 0.0f, 0.0f, 1.0f );

void DrawCode( void ) {
	if( !DBGUI_IsInitialized( ) ) {
		return;
	}

	// Calculate window dimensions based on character rows/columns
	// (code rows + 1 for input line)
	float line_height = ImGui::GetTextLineHeightWithSpacing( );
	float title_bar_height = ImGui::GetFrameHeight( );
	float padding = ImGui::GetStyle( ).WindowPadding.y * 2;
	float window_width = DBGUI_GetWindowWidth( );
	float window_height = ( dbg.rows_code * line_height ) + title_bar_height +
		padding;

	ImGui::SetNextWindowPos( ImVec2( 0, DBGUI_GetWindowY( WIN_CODE ) ),
		ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( ImVec2( window_width, window_height ),
		ImGuiCond_FirstUseEver );

	if( dbg.update_win[WIN_CODE] ) {
		dbg.update_win[WIN_CODE] = false;
		auto startOffset = GetAddress( codeViewData.useCS, codeViewData.useEIP );
		DasmRecursiveDisassemble( codeBuffer, startOffset, codeViewData.useEIP, cpu.code.big, cpu.pmode );
	}

	if( DBGUI_BeginWindowWithStyledTitle( "Code",
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize ) ) {
		// Handle mouse wheel scrolling when hovering over this window

		//ImGui::TextUnformatted( codeBuffer );
		static uint32_t selectedIndex = 0;
		uint32_t i = 0;
		for( auto line = codeBuffer; *line; ++line, ++i ) {
			// Make line selectable
			bool isSelected = ( selectedIndex == i );
			if( ImGui::Selectable( line, isSelected, ImGuiSelectableFlags_SelectOnClick ) ) {
				selectedIndex = i;
				char *endptr;
				codeViewData.cursorSeg = strtol( line, &endptr, 16 );
				if( *endptr == ':' )
					++endptr;
				codeViewData.cursorOfs = strtol( endptr, &endptr, 16 );
			}
			if( isSelected && ImGui::IsItemFocused( ) ) {
				selectedIndex = i;
			}
			while( *line ) ++line;
		}
	}
	if( false) {
		if( ImGui::IsWindowHovered( ImGuiHoveredFlags_ChildWindows ) ) {
			float wheel = ImGui::GetIO( ).MouseWheel;
			if( wheel ) {
				if( wheel > 0 ) { // Scroll up - move cursor up or scroll code
					//--codeViewData.cursorPos;
				} else { // Scroll down - move cursor down or scroll code
					//if( codeViewData.cursorPos == dbg.rows_code ) {
						codeViewData.useEIP += codeViewData.firstInstSize;
					//}
					//++codeViewData.cursorPos;
				}
				//dbg.update_win[WIN_CODE] = true;
			}
		}
		/*if( codeViewData.cursorPos < 0 ) {
			if( !UseExistingEIP( -codeViewData.cursorPos ) ) {
				PopulateEIParray( );
				UseExistingEIP( -codeViewData.cursorPos );
			}
			codeViewData.cursorPos = 0;
		} else if( codeViewData.cursorPos >= dbg.rows_code ) {
			indexEIParray = MAXSIZE_EIPARRAY;
			codeViewData.cursorPos = dbg.rows_code - 1;
		}*/
		uint32_t disEIP = codeViewData.useEIP;
		PhysPt start = GetAddress( codeViewData.useCS, codeViewData.useEIP );
		char dline[2048];
		Bitu size;
		Bitu c;

		static int selectedIndex = 0;

		for( int i = 0, iMid = dbg.rows_code * 0.95; i < dbg.rows_code; ++i ) {
			bool is_current_ip = ( codeViewData.useCS == SegValue( cs ) ) && ( disEIP == reg_eip );
			bool is_breakpoint = CBreakpoint::IsBreakpoint( codeViewData.useCS, disEIP );

			// Build the line
			char line[2048];
			char* ptr = line;
			ptr += sprintf( ptr, "%04X:%04X %c", codeViewData.useCS, disEIP, ( is_breakpoint ? '*' : ' ' ) );

			Bitu drawsize = size = DasmI386( dline, start, disEIP, cpu.code.big, cpu.pmode );
			if( disEIP < codeViewData.goodEIP &&
				disEIP + size > codeViewData.goodEIP ) {
				size = drawsize = codeViewData.goodEIP - disEIP;
			}
			bool toolarge = false;

			if( drawsize > check_cast<uint32_t>( dbg.rows_code ) ) {
				toolarge = true;
				drawsize = dbg.rows_code - 1;
			}
			for( c = 0; c < drawsize; c++ ) { // Hex bytes
				uint8_t value;
				if( mem_readb_checked( start + c, &value ) ) {
					value = 0;
				}
				ptr += sprintf( ptr, "%02X", value );
			}
			if( toolarge ) {
				ptr += sprintf( ptr, ".." );
				++drawsize;
			}
			// Pad hex to fixed width
			int hex_len = drawsize * 2 + ( toolarge ? 2 : 0 );
			while( hex_len < 20 ) {
				*ptr++ = ' ';
				++hex_len;
			}
			// Disassembly
			char empty_res[] = { 0 };
			char* res = empty_res;
			if( showExtend ) {
				res = AnalyzeInstruction( dline, false );
			}
			// Pad disassembly
			size_t dline_len = safe_strlen( dline );
			if( dline_len > 28 ) {
				dline_len = 28;
			}
			memcpy( ptr, dline, dline_len );
			ptr += dline_len;
			for( size_t pad = dline_len; pad < 28; ++pad ) {
				*ptr++ = ' ';
			}
			// Result
			if( res && res[0] ) {
				size_t res_len = strlen( res );
				if( res_len > 20 ) {
					res_len = 20;
				}
				memcpy( ptr, res, res_len );
				ptr += res_len;
			}
			*ptr = '\0';

			// Determine color
			if( is_current_ip ) {
				ImGui::PushStyleColor( ImGuiCol_Text, green_color );
			} else if( is_breakpoint ) {
				ImGui::PushStyleColor( ImGuiCol_Text, red_bg_color );
			}
			// Make line selectable
			bool isSelected = ( selectedIndex == i );
			if( ImGui::Selectable( line, isSelected, ImGuiSelectableFlags_SelectOnClick ) ) {
				selectedIndex = i;
				codeViewData.cursorSeg = codeViewData.useCS;
				codeViewData.cursorOfs = disEIP;
			}
			if( isSelected && ImGui::IsItemFocused( ) ) {
				selectedIndex = i;
			}
			if( is_current_ip || is_breakpoint ) {
				ImGui::PopStyleColor( );
			}
			start += size;
			disEIP += size;

			if( i == 0 ) {
				codeViewData.firstInstSize = size;
			}
			if( i == iMid ) {
				codeViewData.useEIPmid = disEIP;
			}
		}
		codeViewData.useEIPlast = disEIP;
	}
	DBGUI_EndWindowWithStyledTitle( );
}

static void DrawData( void ) {
	if( !DBGUI_IsInitialized( ) ) {
		return;
	}

	// Calculate window dimensions based on character rows/columns
	float line_height = ImGui::GetTextLineHeightWithSpacing( );
	float title_bar_height = ImGui::GetFrameHeight( );
	float padding = ImGui::GetStyle( ).WindowPadding.y * 2;
	float window_width = DBGUI_GetWindowWidth( );
	float window_height = ( dbg.rows_data[dbg.active_win_data] * line_height ) + title_bar_height +
		padding;

	ImGui::SetNextWindowPos( ImVec2( 0, DBGUI_GetWindowY( WIN_DATA ) ),
		ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( ImVec2( window_width, window_height ),
		ImGuiCond_FirstUseEver );

	if( DBGUI_BeginWindowWithStyledTitle( "Data",
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize ) ) {
		// Handle mouse wheel scrolling when hovering over this window
		if( ImGui::IsWindowHovered( ImGuiHoveredFlags_ChildWindows ) ) {
			float wheel = ImGui::GetIO( ).MouseWheel;
			if( wheel ) {
				if( wheel > 0.0f && dataOfs[dbg.active_win_data] <= 48U ) {
					dataOfs[dbg.active_win_data] = 0U;
				} else {
					dataOfs[dbg.active_win_data] -= 48U * wheel;
				}
				dbg.update_win[WIN_DATA] = true;
			}
		}
		uint8_t ch;
		uint32_t add, address;
		bool f16bit = false;
		for( auto dw = 0U; dw < 1U; ++dw ) {
			add = dataOfs[dw];
			for( auto y = 0; y < dbg.rows_data[dw]; ++y ) {
				char line[128];

				// Address
				if( add <= 0xFFFF ) {
					sprintf( line, "%04X:%04X      ", dataSeg[dw], add );
					f16bit = false;
				} else {
					sprintf( line, "%04X:%08X  ", dataSeg[dw], add );
					f16bit = true;
				}

				// Hex values
				for( int x = 0; x < 16; ++x, ++add ) {
					address = GetAddress( dataSeg[dw], add );
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
				line[80] = '\0';

				ImGui::TextUnformatted( line );
			}
		}
		if( dbg.update_win[WIN_DATA] ) {
			dbg.update_win[WIN_DATA] = false;
		}
	}
	DBGUI_EndWindowWithStyledTitle( );
}

const uint8_t oEIP = 8;
static uint32_t oldregs[oEIP + 1] = {};
static Segment oldsegs[6] = {};
static auto oldcpucpl = cpu.cpl;
static auto oldflags = cpu_regs.flags;

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

	// Calculate window dimensions based on character rows/columns
	float line_height = ImGui::GetTextLineHeightWithSpacing( );
	float title_bar_height = ImGui::GetFrameHeight( );
	float padding = ImGui::GetStyle( ).WindowPadding.y * 2;
	float window_width = DBGUI_GetWindowWidth( );
	float window_height = ( dbg.rows_registers * line_height ) +
		title_bar_height + padding;

	ImGui::SetNextWindowPos( ImVec2( 0, DBGUI_GetWindowY( WIN_REG ) ),
		ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( ImVec2( window_width, window_height ),
		ImGuiCond_FirstUseEver );

	static const float TAB_POS[] = { 0.0f, window_width * 0.205f, window_width * 0.405f, window_width * 0.53f, window_width * 0.655f, window_width * 0.84f, window_width * 0.93f, window_width };

	if( DBGUI_BeginWindowWithStyledTitle( "                                    Registers                                   ",
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoResize ) ) {

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
			for( uint8_t i = 0; i < oEIP; ++i )
				oldregs[i] = cpu_regs.regs[i].dword[DW_INDEX];
			oldregs[oEIP] = reg_eip;
			for( uint8_t i = 0; i < 6; ++i )
				oldsegs[i].val = SegValue( static_cast<SegNames>( i ) );
			oldcpucpl = cpu.cpl;
			oldflags = reg_flags;
		}
	}
	DBGUI_EndWindowWithStyledTitle( );
}

#define DEBUG_VAR_BUF_LEN 16
static void DrawVariables( ) {
	if( !DBGUI_IsInitialized( ) ) {
		return;
	}

	// Calculate window dimensions based on character rows/columns
	float line_height = ImGui::GetTextLineHeightWithSpacing( );
	float title_bar_height = ImGui::GetFrameHeight( );
	float padding = ImGui::GetStyle( ).WindowPadding.y * 2;
	float window_width = DBGUI_GetWindowWidth( );
	float window_height = ( dbg.rows_variables * line_height ) +
		title_bar_height + padding;

	ImGui::SetNextWindowPos( ImVec2( 0, DBGUI_GetWindowY( WIN_VAR ) ),
		ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( ImVec2( window_width, window_height ),
		ImGuiCond_FirstUseEver );

	if( DBGUI_BeginWindowWithStyledTitle( "                                    Variables                                   ",
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoResize ) ) {
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

void DrawConsole( void ) {
	// Calculate window dimensions based on character rows/columns
	float line_height = ImGui::GetTextLineHeightWithSpacing( );
	float padding = ImGui::GetStyle( ).WindowPadding.y * 2;
	float window_width = DBGUI_GetWindowWidth( );
	float window_height = ( dbg.rows_console * line_height ) + padding;

	ImGui::SetNextWindowPos( ImVec2( 0, DBGUI_GetWindowY( WIN_CON ) ),
		ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( ImVec2( window_width, window_height ),
		ImGuiCond_FirstUseEver );

	if( DBGUI_BeginWindow( "Console", ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse ) ) {
		if( !debugging ) {
			ImGui::PushStyleColor( ImGuiCol_Text, green_color );
			ImGui::Text( "(Running)" );
			ImGui::PopStyleColor( );
		} else {
			ImGui::Text( "%c", ( codeViewData.ovrMode ? 'O' : 'I' ) );
			ImGui::SameLine( );
			if( ImGui::InputText( "##console_input", codeViewData.inputStr, IM_ARRAYSIZE( codeViewData.inputStr ), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll ) ) {
				// buf now contains the updated text
			}
		}
	}
	DBGUI_EndWindow( );
}

// Calculate window height for a given number of rows
static float CalcWindowHeight( int rows ) {
	float line_height = ImGui::GetTextLineHeightWithSpacing( );
	float title_bar_height = ImGui::GetFrameHeight( );
	float padding = ImGui::GetStyle( ).WindowPadding.y * 2;
	return ( rows * line_height ) + title_bar_height + padding;
}

// Calculate window width for a given number of columns
static float CalcWindowWidth( int cols ) {
	// Use a representative character to get monospace font width
	float char_width = ImGui::CalcTextSize( "X" ).x;
	float padding = ImGui::GetStyle( ).WindowPadding.x * 2;
	return ( cols * char_width ) + padding;
}

// Get the calculated Y positions for each window
float DBGUI_GetWindowY( uint32_t window_index ) {
	float y = 0;
	switch( window_index ) {
	case NUM_WINDOWS:
		y += CalcWindowHeight( dbg.rows_output );
	case WIN_OUT:
		y += CalcWindowHeight( dbg.rows_console );
	case WIN_CON:
		y += CalcWindowHeight( dbg.rows_variables );
	case WIN_VAR:
		y += CalcWindowHeight( dbg.rows_data[0] );
	case WIN_DATA:
		y += CalcWindowHeight( dbg.rows_registers );
	case WIN_REG:
		y += CalcWindowHeight( dbg.rows_code ) +
			DBGUI::WindowSeparatorSpacing; // After Code (with separator)
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

static bool BeginWindow( const char* name, ImGuiWindowFlags flags ) {
	return ImGui::Begin( name, nullptr, flags );
}

// Title bar colors - purple background with black text
static const ImVec4 TitleBgColor = ImVec4( 0.42f, 0.41f, 0.84f, 0.8f ); // Purple
static const ImVec4 TitleTextColor = ImVec4( 0.0f, 0.0f, 0.0f, 1.0f );  // Black

// Helper to begin a window with cyan title bar and black title text
static bool BeginWindowWithStyledTitle( const char* title, ImGuiWindowFlags flags ) {
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

static void EndWindow( ) {
	ImGui::End( );
}

// Helper to end a styled window
static void EndWindowWithStyledTitle( ) {
	ImGui::End( );
	ImGui::PopStyleColor( 3 ); // Pop title bar colors
}

// Public versions for use from debugger.cpp
bool DBGUI_BeginWindow( const char* name, int flags ) {
	return BeginWindow( name, static_cast<ImGuiWindowFlags>( flags | ImGuiWindowFlags_NoTitleBar ) );
}

void DBGUI_EndWindow( ) {
	EndWindow( );
}

bool DBGUI_BeginWindowWithStyledTitle( const char* title, int flags ) {
	return BeginWindowWithStyledTitle( title, static_cast<ImGuiWindowFlags>( flags ) );
}

void DBGUI_EndWindowWithStyledTitle( ) {
	EndWindowWithStyledTitle( );
}

// Render functions for debugger windows - called from debugger.cpp
void DBGUI_DrawRegisterWindow( void ) {
	if( !imgui_initialized ) {
		return;
	}

	// Calculate window dimensions based on character rows/columns
	float window_width = DBGUI_GetWindowWidth( );
	float window_height = CalcWindowHeight( dbg.rows_registers );

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

void DBGUI_DrawOutputWindow( void ) {
	if( !imgui_initialized ) {
		return;
	}
	// Calculate window dimensions based on character rows/columns
	float window_width = DBGUI_GetWindowWidth( );
	float window_height = CalcWindowHeight( dbg.rows_output );

	ImGui::SetNextWindowPos( ImVec2( 0, DBGUI_GetWindowY( WIN_OUT ) ),
		ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( ImVec2( window_width, window_height ),
		ImGuiCond_FirstUseEver );

	if( DBGUI_BeginWindowWithStyledTitle( "                                     Output                    [SHIFT] Home/End ",
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoNavFocus ) ) {
		// Handle mouse wheel scrolling when hovering over this window
		if( ImGui::IsWindowHovered( ) ) {
			float wheel = ImGui::GetIO( ).MouseWheel;
			if( wheel > 0 ) {
				output_scroll_offset++; // Scroll up (older messages)
			} else if( wheel < 0 && output_scroll_offset > 0 ) {
				output_scroll_offset--; // Scroll down (newer messages)
			}
		}

		// Calculate how many lines we can display
		int visible_lines = dbg.rows_output - 1; // -1 for title bar
		int total_lines = static_cast<int>( logBuff.size( ) );

		// Clamp scroll offset to valid range
		int max_offset = total_lines > visible_lines ? total_lines - visible_lines : 0;
		if( output_scroll_offset > max_offset ) {
			output_scroll_offset = max_offset;
		}

		// Calculate start index (from the end, accounting for offset)
		int start_idx = total_lines - visible_lines - output_scroll_offset;
		if( start_idx < 0 ) {
			start_idx = 0;
		}

		// Display visible lines
		auto it = logBuff.begin( );
		std::advance( it, start_idx );
		int lines_shown = 0;
		while( it != logBuff.end( ) && lines_shown < visible_lines ) {
			ImGui::TextUnformatted( it->c_str( ) );
			++it;
			++lines_shown;
		}
	}
	EndWindowWithStyledTitle( );
	if( dbg.update_win[WIN_OUT] ) {
		dbg.update_win[WIN_OUT] = false;
	}
}

#endif
