// SPDX-FileCopyrightText:  2002-2025 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "debugger_inc.h"

#if C_DEBUGGER
#include "debugger_gui.h"

#include <SDL3/SDL.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include "cpu/cpu.h"
#include "cpu/paging.h"
#include "debugvar.h"
#include "utils/string_utils.h"

#include "IBM_VGA_8x16.h"

SLabelView labelView;

int64_t normalLoopTickCount;

extern FILE *debuglog;
extern std::vector<CDebugVar*> varList;

Bitu cycle_count = 0;
char curSelectorName[3] = "";

bool imgui_initialized = false;
static float display_scale = 1.0f;

uint32_t start_address = 0U;
float char_width, digit_width, space_width, tab_width, scrollbar_width, cursor_width;
float line_height;
float line_height_no_spacing;
static ImVec2 padding;
static float title_bar_height;
static ImVec2 window_pos[NUM_WINDOWS];
ImVec2 window_size[NUM_WINDOWS];

static bool fStaticLayout = true;

//static const ImGuiWindowFlags_ EDIT_LAYOUT = ImGuiWindowFlags_NoFocusOnAppearing;
static const ImGuiWindowFlags_ STATIC_LAYOUT = static_cast<ImGuiWindowFlags_>( ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing );
static ImGuiWindowFlags_ ADDITIONAL_FLAGS = STATIC_LAYOUT;

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
WINDOW_ID win_data_view[NUM_DATA_VIEWS] = { WIN_DATA, WIN_STACK };

const ImVec4 light_grey_color = ImVec4( 0.75f, 0.75f, 0.75f, 1.0f );
const ImVec4 grey_color = ImVec4( 0.5f, 0.5f, 0.5f, 1.0f );
const ImVec4 green_color = ImVec4( 0.0f, 1.0f, 0.0f, 1.0f );

const ImVec4 address_colors[MAX_ADDRESS_COLORS][2] = {
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

bool SView::Set( const ADDRESS_PAIR &address_pair, const VIEW_MASK update_mask ) {
	if( address_pair == realAddress )
		return false;
	if( update_mask & V_UPDATE_HISTORY )
		HistoryInsert( realAddress );
	realAddress = address_pair;
	address = address_pair.address( );
	dbg.update_win[win_id] = debugging && ( update_mask & V_UPDATE_VIEW );
	dbg.update_win_scroll[win_id] = debugging && ( update_mask & V_UPDATE_SCROLL );
	return true;
}

void SView::HistoryInsert( const ADDRESS_PAIR &address_pair ) {
	if( !UniquePrevious( address_pair ) )
		return;
	history[history_index] = address_pair;
	IndexInc( );
	history_end = history_index;
}
void SView::IndexInc( ) {
	++history_index;
	if( history_index >= VIEW_HISTORY_LIMIT )
		history_index = 0U;
	if( history_index == history_begin )
		++history_begin;
}
void SView::IndexDec( ) {
	if( history_index )
		--history_index;
	else
		history_index = VIEW_HISTORY_LIMIT - 1U;
}
bool SView::UniquePrevious( const ADDRESS_PAIR &address_pair ) {
	if( history_end == history_begin )
		return true;
	auto index = history_index;
	if( index )
		--index;
	else
		index = VIEW_HISTORY_LIMIT - 1U;
	return history[index] != address_pair;
}

void SView::HistoryNext( ) {
	if( history_index == history_end )
		return;
	++history_index;
	if( history_index >= VIEW_HISTORY_LIMIT )
		history_index = 0U;
	Set( history_index == history_end ? backup : history[history_index], V_UPDATE_SCROLL );
}
void SView::HistoryPrev( ) {
	if( history_index == history_begin )
		return;
	if( history_index == history_end )
		backup = realAddress;
	IndexDec( );
	Set( history[history_index], V_UPDATE_SCROLL );
}

void SLabelView::Set( const ADDRESS_PAIR &address_pair, const bool fLabel, const bool update_scroll ) {
	realAddress = address_pair;
	this->fLabel = fLabel;
	dbg.update_win_scroll[WIN_LABELS] = update_scroll;
}

bool SLabelView::IsMatch( const ADDRESS_PAIR &address_pair, const bool fLabel ) const {
	return( realAddress == address_pair && this->fLabel == fLabel );
}

static void SnapToGrid( WINDOW_ID winID ) { // Detect movement and snap
	if( fStaticLayout )
		return;
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

/********************/
/*   Draw windows   */
/********************/
// Title bar colors - purple background with black text
static const ImVec4 TitleBgColor = ImVec4( 0.42f, 0.41f, 0.84f, 0.8f ); // Purple
static const ImVec4 TitleBgColorActive = ImVec4( 0.41f, 0.84f, 0.41f, 0.8f ); // Green
static const ImVec4 TitleTextColor = ImVec4( 0.0f, 0.0f, 0.0f, 1.0f );  // Black

bool BeginSubWindow( const WINDOW_ID winID, const char *name, int flags ) {
	if( !dbg.visible[winID] )
		return false;
	ImGui::SetNextWindowPos( window_pos[winID], ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowSize( window_size[winID], ImGuiCond_FirstUseEver );
	if( dbg.update_win_frame[winID] ) {
		dbg.update_win_frame[winID] = false;
		ImGui::SetNextWindowPos( window_pos[winID], ImGuiCond_Always );
		ImGui::SetNextWindowSize( window_size[winID], ImGuiCond_Always );
	}
	if( dbg.fTitleBar[winID] ) { // Push title bar colors
		ImGui::PushStyleColor( ImGuiCol_TitleBg, TitleBgColor );
		ImGui::PushStyleColor( ImGuiCol_TitleBgActive, TitleBgColorActive );
		ImGui::PushStyleColor( ImGuiCol_TitleBgCollapsed, TitleBgColor );
		ImGui::PushStyleColor( ImGuiCol_Text, TitleTextColor );
	} else
		flags |= ImGuiWindowFlags_NoTitleBar;
	bool result = ImGui::Begin( name, nullptr, flags | ADDITIONAL_FLAGS );
	if( dbg.fTitleBar[winID] )
		ImGui::PopStyleColor( ); // Restore text color for window content (keep title bar colors)
	return result;
}

void EndSubWindow( const WINDOW_ID winID ) {
	if( !dbg.visible[winID] )
		return;
	SnapToGrid( winID );
	ImGui::End( );
	if( dbg.fTitleBar[winID] )
		ImGui::PopStyleColor( 3 ); // Pop title bar colors
}

const uint8_t oEIP = 8;
static uint32_t oldregs[oEIP + 1] = {};
static Segment oldsegs[6] = {};
static auto oldcpucpl = cpu.cpl;
static auto oldflags = cpu_regs.flags;

static void SaveCPUstate( ) {
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
	static const float TAB_POS[] = { 0.0f, window_width * 0.205f, window_width * 0.405f, window_width * 0.53f, window_width * 0.655f, window_width * 0.84f, window_width * 0.93f, window_width };

	if( BeginSubWindow( WIN_REG, "Registers", ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse ) ) {
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
						uint16_t segment = 0U;
						switch( e.x ) {
						case REGI_SP:
						case REGI_BP:
							stackView.Set( { RealSegValue( ss ), cpu_regs.regs[e.x].word[W_INDEX] }, V_UPDATE_SCROLL_HISTORY | ( stack_segment != stackView.realAddress.segment ? V_UPDATE_VIEW : V_UPDATE_NONE ) );
							break;
						case REGI_DX:
							segment = RealSegValue( ds );
						case REGI_SI:
							if( e.x == REGI_SI )
								segment = data_cursor[SI_CURSOR].realAddress.segment;
						case REGI_BX:
						case REGI_DI:
							if( e.x == REGI_DI )
								segment = data_cursor[DI_CURSOR].realAddress.segment;
							else if( e.x == REGI_BX )
								segment = RealSegValue( es );
							dataView.Set( { segment, cpu_regs.regs[e.x].word[W_INDEX] }, V_UPDATE_SCROLL_HISTORY | ( data_segment != dataView.realAddress.segment ? V_UPDATE_VIEW : V_UPDATE_NONE ) );
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
				ImGui::SameLine( 0.0f, 0.0f );
				entry_width = ImGui::GetCursorPosX( ) - label_x;
				ImGui::SetCursorPosX( label_x );
				char id[9];
				sprintf( id, "##seg_%s", e.label );
				if( ImGui::Selectable( id, false, ImGuiSelectableFlags_SelectOnClick, { entry_width, 0.0f } ) ) {
					switch( e.x ) {
					case ss:
						stackView.Set( { segVal, reg_esp }, V_UPDATE_SCROLL_HISTORY | ( stack_segment != segVal ? V_UPDATE_VIEW : V_UPDATE_NONE ) );
						break;
					case cs:
						codeView.Set( { segVal, 0U }, V_UPDATE_SCROLL_HISTORY );
					default:
						dataView.Set( { segVal, 0U }, V_UPDATE_SCROLL_HISTORY | ( data_segment != segVal ? V_UPDATE_VIEW : V_UPDATE_NONE ) );
						break;
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
				if( ImGui::Selectable( "##reg_ip", false, ImGuiSelectableFlags_SelectOnClick, { entry_width, 0.0f } ) )
					codeView.SetToEIP( );
				break;
			case tMODE: {
				const char* mode_str = "Real";
				if( cpu.pmode ) {
					if( reg_flags & FLAG_VM )
						mode_str = "VM86";
					else if( cpu.code.big )
						mode_str = "Pr32";
					else
						mode_str = "Pr16";
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
	}
	EndSubWindow( WIN_REG );
}

const char seg_label[NUM_SEG_TYPES][4] = { "   ", "En:", "PS:", "CS:", "SS:", "SP:", "HP:", "DS:", "   " };

static void DrawSegments( ) {
	if( BeginSubWindow( WIN_SEG, "Segments", ImGuiWindowFlags_NoNav ) ) {
		for( const auto &segment : ordered_segments ) {
			if( segment.extra.type == SEG_MAX )
				continue;
			float text_start_x = ImGui::GetCursorPosX( );
			ImGui::TextColored( grey_color, "%s", seg_label[segment.extra.type] );
			ImGui::SameLine( 0.0f, 0.0f );
			ImGui::TextColored( address_colors[segment.extra.index % MAX_ADDRESS_COLORS][1], "%04X", segment.value );
			ImGui::SameLine( 0.0f, 0.0f );
			float text_width = ImGui::GetCursorPosX( ) - text_start_x;
			ImGui::SetCursorPosX( text_start_x );
			const bool isSelected = ( codeView.realAddress.segment == segment.value );
			char id[11];
			sprintf( id, "##%04X", segment.value );
			if( ImGui::Selectable( id, isSelected, ImGuiSelectableFlags_SelectOnClick, { text_width, 0.0f } ) ) {
				if( SEG_CODE == segment.extra.type || ( SEG_DATA == segment.extra.type && segment.value == dbg.segment[SEG_DATA] ) )
					codeView.Set( { segment.value, 0U }, V_UPDATE_SCROLL_HISTORY );
				if( SEG_STACK == segment.extra.type ) {
					const auto it = ++ordered_segments.find( { segment.value, {} } );
					stackView.Set( { segment.value, static_cast<uint32_t>( it->value - segment.value ) << 4 }, V_UPDATE_SCROLL_HISTORY | ( stack_segment != segment.value ? V_UPDATE_VIEW : V_UPDATE_NONE ) );
				} else
					dataView.Set( { segment.value, 0U }, V_UPDATE_SCROLL_HISTORY | ( data_segment != segment.value ? V_UPDATE_VIEW : V_UPDATE_NONE ) );
			}
			ImGui::SameLine( );
			if( ImGui::GetCursorPosX( ) + ( text_width + space_width ) > window_size[WIN_SEG].x )
				ImGui::NewLine( );
		}
	}
	EndSubWindow( WIN_SEG );
}

static void DrawLabels( ) {
	if( BeginSubWindow( WIN_LABELS, "Labels", ImGuiWindowFlags_NoNavFocus ) ) {
		uint16_t currentSegment = 0U;
		for( const auto &label : labels ) {
			static uint8_t addressColorIndex = 0U;
			const uint32_t offset = label.value - ( label.extra.segment << 4 );
			if( currentSegment != label.extra.segment ) { // match segment to defined segments
				currentSegment = label.extra.segment;
				const auto &ordered_segment = ordered_segments.find( { currentSegment, {} } );
				if( ordered_segment != ordered_segments.end( ) )
					addressColorIndex = ordered_segment->extra.index % MAX_ADDRESS_COLORS;
			}
			const bool isSelected = labelView.IsMatch( { label.extra.segment, offset }, true );
			if( dbg.update_win_scroll[WIN_LABELS] && isSelected ) {
				dbg.update_win_scroll[WIN_LABELS] = false;
				ImGui::SetScrollHereY( );
			}
			char id[11];
			sprintf( id, "##%08X", label.value );
			if( ImGui::Selectable( id, isSelected, ImGuiSelectableFlags_SelectOnClick ) ) {
				labelView.Set( { label.extra.segment, offset }, true, false );
				codeView.Set( { label.extra.segment, offset }, V_UPDATE_SCROLL_HISTORY );
				if( label.extra.type & LABEL_DATA )
					dataView.Set( { label.extra.segment, offset }, V_UPDATE_SCROLL_HISTORY | ( data_segment != label.extra.segment ? V_UPDATE_VIEW : V_UPDATE_NONE ) );
			}
			ImGui::SameLine( 0.0f, 0.0f );
			ImGui::TextColored( address_colors[addressColorIndex][0], "%04X", label.extra.segment );
			ImGui::SameLine( 0.0f, 0.0f );
			ImGui::TextColored( grey_color, ":" );
			ImGui::SameLine( 0.0f, 0.0f );
			ImGui::TextColored( address_colors[addressColorIndex][1], "%04X", offset );
			for( const auto &caller : label.extra.callers ) {
				const uint32_t offset = ADDRESS_PAIR::Offset( caller.value, caller.extra );
				const bool isSelected = labelView.IsMatch( { caller.extra, offset }, false );
				if( dbg.update_win_scroll[WIN_LABELS] && isSelected ) {
					dbg.update_win_scroll[WIN_LABELS] = false;
					ImGui::SetScrollHereY( );
				}
				char id[12];
				sprintf( id, "##c%08X", caller.value );
				if( ImGui::Selectable( id, isSelected, ImGuiSelectableFlags_SelectOnClick ) ) {
					labelView.Set( { caller.extra, offset }, false, false );
					codeView.Set( { caller.extra, offset }, V_UPDATE_SCROLL_HISTORY );
				}
				uint8_t color_index = addressColorIndex;
				const auto &ordered_segment = ordered_segments.find( { caller.extra, {} } );
				if( ordered_segment != ordered_segments.end( ) )
					color_index = ordered_segment->extra.index % MAX_ADDRESS_COLORS;
				ImGui::SameLine( tab_width );
				ImGui::TextColored( address_colors[color_index][0], "%04X", caller.extra );
				ImGui::SameLine( 0.0f, 0.0f );
				ImGui::TextColored( grey_color, ":" );
				ImGui::SameLine( 0.0f, 0.0f );
				ImGui::TextColored( address_colors[color_index][1], "%04X", offset );
			}
		}
	}
	EndSubWindow( WIN_LABELS );
}

#define DEBUG_VAR_BUF_LEN 16
static void DrawVariables( ) {
	if( BeginSubWindow( WIN_VAR, "Variables", ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNav ) ) {
		if( varList.empty( ) )
			ImGui::TextDisabled( "(no variables defined)" );
		else {
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
				if( i % 3 != 0 )
					ImGui::SameLine( );
				ImGui::Text( "%s: %s", dv->GetName( ), buffer );
			}
		}
	}
	EndSubWindow( WIN_VAR );
}
#undef DEBUG_VAR_BUF_LEN

// History stuff
#define MAX_HIST_BUFFER 50
static std::list<std::string> histBuff = {};
static std::list<std::string>::iterator histBuffPos = histBuff.end( );
static char inputStr[MAXCMDLEN + 1] = {};
static char inputStrCpy[MAXCMDLEN + 1] = {};

static int ConsoleEditCallback( ImGuiInputTextCallbackData *data ) {
	switch( data->EventFlag ) {
	case ImGuiInputTextFlags_CallbackHistory:
		if( data->EventKey == ImGuiKey_UpArrow ) {
			if( histBuffPos == histBuff.begin( ) )
				break;
			data->DeleteChars( 0, data->BufTextLen );
			if( histBuffPos == histBuff.end( ) ) // copy inputStr to inputStrCpy so we can restore it
				safe_strcpy( inputStrCpy, inputStr );
			data->InsertChars( 0, ( --histBuffPos )->c_str( ) );
		} else if( data->EventKey == ImGuiKey_DownArrow ) {
			if( histBuffPos == histBuff.end( ) )
				break;
			data->DeleteChars( 0, data->BufTextLen );
			if( ++histBuffPos != histBuff.end( ) )
				data->InsertChars( 0, histBuffPos->c_str( ) );
			else // copy inputStrCpy back into inputStr
				data->InsertChars( 0, inputStrCpy );
		}
		break;
	}
	return 0;
}

static void DrawOutputWindow( ) {
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( padding.x, 0.0f ) );
	if( BeginSubWindow( WIN_OUT, "Output", ImGuiWindowFlags_NoNavFocus ) ) {
		ImGui::PushStyleColor( ImGuiCol_NavHighlight, IM_COL32( 0, 0, 0, 0 ) ); // Transparent highlight
		ImGui::PushStyleColor( ImGuiCol_NavWindowingHighlight, IM_COL32( 0, 0, 0, 0 ) ); // Transparent window highlight
		ImGui::PushStyleColor( ImGuiCol_Text, green_color );
		if( debugging ) {
			if( ( !ImGui::IsWindowFocused( ImGuiFocusedFlags_None ) && ImGui::IsWindowHovered( ImGuiHoveredFlags_ChildWindows ) && ImGui::IsMouseDown( 0 ) )
				|| ( ImGui::IsWindowFocused( ImGuiFocusedFlags_ChildWindows ) && ImGui::IsWindowHovered( ImGuiHoveredFlags_ChildWindows ) ) )
				ImGui::SetKeyboardFocusHere( );
			ImGui::PushItemWidth( window_size->x * 0.99f );
			if( ImGui::InputText( "##con", inputStr, IM_ARRAYSIZE( inputStr ), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll | ImGuiInputTextFlags_ElideLeft | ImGuiInputTextFlags_CallbackHistory, ConsoleEditCallback ) ) {
				inputStr[MAXCMDLEN] = '\0';
				if( ParseCommand( inputStr ) ) {
					char *cmd = ltrim( inputStr );
					if( histBuff.empty( ) || *--histBuff.end( ) != cmd )
						histBuff.emplace_back( cmd );
					if( histBuff.size( ) > MAX_HIST_BUFFER )
						histBuff.pop_front( );
					histBuffPos = histBuff.end( );
					inputStr[0] = 0;
					ImGui::SetKeyboardFocusHere( );
				}
			}
			ImGui::SetItemDefaultFocus( );
			ImGui::PopItemWidth( );
		} else
			ImGui::Text( "(Running)" );
		ImGui::PopStyleColor( );
		if( ImGui::BeginChild( "ScrollRegion", { 0.0f, 0.0f }, false, ImGuiWindowFlags_HorizontalScrollbar ) ) {
			for( auto it = logBuff.begin( ); it != logBuff.end( ); ++it )
				ImGui::TextUnformatted( it->c_str( ) );
		}
		ImGui::EndChild( );
		ImGui::PopStyleColor( 2 );
	}
	if( dbg.update_win[WIN_OUT] ) {
		dbg.update_win[WIN_OUT] = false;
		ImGui::SetScrollHereY( );
	}
	EndSubWindow( WIN_OUT );
	ImGui::PopStyleVar( );
}

// Calculate window height for a given number of rows
static float CalcWindowHeight( WINDOW_ID window_id ) {
	if( window_id >= NUM_WINDOWS || !dbg.visible[window_id] )
		return 0.0f;
	return ( dbg.columnRows[window_id].rows * title_bar_height );
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
		y += CalcWindowHeight( WIN_STACK );
	case WIN_STACK:
		y += CalcWindowHeight( WIN_VAR );
	case WIN_VAR:
		y += CalcWindowHeight( WIN_DATA );
		break;
	case WIN_OUT:
		y += CalcWindowHeight( WIN_SEG );
	case WIN_SEG:
		y += CalcWindowHeight( WIN_REG );
	case WIN_REG:
		y += CalcWindowHeight( WIN_CODE );
	default:
		break;
	}
	return y;
}
static float CalcWindowWidth( WINDOW_ID window_id ) {
	return CalcWindowWidth( dbg.window_cols[dbg.columnRows[window_id].column] );
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
		codeView.SetToEIP( );
	}
	DrawCode( );
	DrawRegisters( );
	DrawSegments( );
	DrawLabels( );
	DrawData( );
	DrawStack( );
	DrawDiff( );
	DrawVariables( );
	DrawOutputWindow( );
}

//auto data_buffers[] = { data_buffer, stack_buffer };
extern uint16_t num_indexed_segments;
extern std::vector<char> data_text_buffer;
extern std::vector<char> stack_text_buffer;
extern uint32_t stack_lines;

void DBGUI_Reset( ) {
	//codeView = { WIN_CODE };
	data_text_buffer[0] = 0U;
	stack_text_buffer[0] = 0U;
	//for( DATA_ID data_i = static_cast<DATA_ID>( 0U ); data_i < NUM_DATA_VIEWS; ++data_i ) {
	//	//for( auto data = &data_buffers[i][0]; *data; ++data )
	//		//*data = 0;
	//	dataAddress[data_i] = { 0U, 0U };
	//}
	for( WINDOW_ID win_i = static_cast<WINDOW_ID>( 0U ); win_i < NUM_WINDOWS; ++win_i )
		dbg.update_win[win_i] = true;
	dbg.active_data_view = DATA_VIEW;
	stack_lines = 0U;
	num_indexed_segments = 0U;
	DasmReset( );
	fReset = true;
}

void DBGUI_Resize( ) {
	SDL_GetWindowSizeInPixels( dbg.win_main, &dbg.window_rect.w, &dbg.window_rect.h );
	const uint16_t TOTAL_ROWS = dbg.window_rect.h / title_bar_height;
	uint16_t column_rows[NUM_COLUMNS] = { TOTAL_ROWS, TOTAL_ROWS, TOTAL_ROWS, TOTAL_ROWS };
	for( WINDOW_ID i = static_cast<WINDOW_ID>( 0U ); i < NUM_WINDOWS; ++i ) {
		if( !dbg.visible[i] ) {
			dbg.columnRows[i].rows = 0U;
			continue;
		}
		if( dbg.height_ratio[i] < 0 )
			dbg.columnRows[i].rows = -dbg.height_ratio[i] + ( dbg.fTitleBar[i] ? 1U : 0U );
		else if( dbg.height_ratio[i] > 0 )
			dbg.columnRows[i].rows = TOTAL_ROWS * ( dbg.height_ratio[i] * 0.01f );
		else
			dbg.columnRows[i].rows = column_rows[dbg.columnRows[i].column];
		column_rows[dbg.columnRows[i].column] -= dbg.columnRows[i].rows;
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
	const SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;

	dbg.win_main = SDL_CreateWindow( debugger_title,
		static_cast<int>( InitialWindowWidth * display_scale ),
		static_cast<int>( InitialWindowHeight * display_scale ),
		window_flags );

	if( !dbg.win_main ) {
		LOG_ERR( "DEBUG: Failed to create debugger window: %s", SDL_GetError( ) );
		return false;
	}

	// Create GPU device - SDL_GPU uses Vulkan/Metal/D3D12 under the hood,
	// avoiding OpenGL context conflicts with the main DOSBox window
	dbg.gpu_device = SDL_CreateGPUDevice(
		SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
		SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB,
		true, nullptr );
	if( !dbg.gpu_device ) {
		LOG_ERR( "DEBUG: Failed to create GPU device: %s", SDL_GetError( ) );
		SDL_DestroyWindow( dbg.win_main );
		dbg.win_main = nullptr;
		return false;
	}

	if( !SDL_ClaimWindowForGPUDevice( dbg.gpu_device, dbg.win_main ) ) {
		LOG_ERR( "DEBUG: Failed to claim window for GPU device: %s", SDL_GetError( ) );
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
	init_info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat( dbg.gpu_device, dbg.win_main );
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
	dbg.segment[SEG_DATA] = RealSegValue( ds );
	dbg.segment[SEG_PSP] = RealSegValue( es ); // could also use es or ds segments, or dos.psp( )
	dbg.segment[SEG_STACK] = RealSegValue( ss );
	dbg.segment[SEG_STACK_END] = GetPhysicalAddress( { SegValue( ss ), reg_sp } ) >> 4;
	const auto psp = &MemBase[dbg.segment[SEG_PSP] << 4];
	dbg.segment[SEG_HEAP] = reinterpret_cast<uint16_t &>( psp[0x2] ); // Segment of the first byte beyond the memory allocated to the program
	dbg.segment[SEG_ENV] = reinterpret_cast<uint16_t &>( psp[0x2C] ); // Environment segment
	SEGTYPE seg_type = SEG_BASE;
	for( const auto &seg : dbg.segment ) {
		if( !ordered_segments.contains( { seg, {} } ) )
			ordered_segments.insert( { seg, { seg_type, 0U, ( SEG_STACK == seg_type ? dbg.segment[SEG_STACK_END] : SEG_STACK_END == seg_type ? dbg.segment[SEG_STACK] : static_cast<uint16_t>( 0U ) ) } } );
		++seg_type;
	}
	// Point stack view to top of stack
	stackView.Set( { RealSegValue( ss ), reg_sp } );

	start_address = GetPhysicalAddress( { SegValue( cs ), reg_eip } );

	data_buffer_size = 0xFFFF + ( dbg.segment[SEG_HEAP] << 4 );
	SaveMemoryState( ); // Required to prevent every difference from zero being incorrectly identified.

	char program_name[debugger_title_length + 11U] = "";
	strncpy( program_name, reinterpret_cast<char *>( &MemBase[( dbg.segment[SEG_ENV] << 4 ) + 0xB8] ), 8 );
	strcat( program_name, " - " );
	strcat( program_name, debugger_title );
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
	scrollbar_width = style.ScrollbarSize;
	cursor_width = space_width * 0.8f;
	tab_width = char_width * 2.0f;
	line_height = ImGui::GetTextLineHeightWithSpacing( );
	line_height_no_spacing = ImGui::GetTextLineHeight( );
	title_bar_height = ImGui::GetFrameHeight( );
	padding = ImGui::GetStyle( ).WindowPadding = ImVec2( char_width * 0.5f, ( title_bar_height - line_height_no_spacing ) * 0.5f ); // X = horizontal, Y = vertical padding;
	dbg.window_rect.w = static_cast<int>( CalcTotalWidth( ) );
	dbg.window_rect.h = static_cast<int>( CalcTotalHeight( ) );

	// Center window in target display
	const int targetDisplay = 2;
	const bool maximiseWindowHeight = true;
	int display_count = 0;
	SDL_DisplayID *displays = SDL_GetDisplays( &display_count );
	SDL_DisplayID display_id = displays[0];
	if( targetDisplay >= display_count )
		display_id = SDL_GetDisplayForWindow( dbg.win_main );
	else
		display_id = displays[targetDisplay];
	SDL_free( displays );
	int windowTopFrame;
	SDL_Rect displayBounds;
	SDL_GetDisplayUsableBounds( display_id, &displayBounds );
	SDL_GetWindowBordersSize( dbg.win_main, &windowTopFrame, NULL, NULL, NULL );
	dbg.window_rect.x = displayBounds.x + ( ( displayBounds.w - dbg.window_rect.w ) >> 1 );
	if( maximiseWindowHeight ) {
		dbg.window_rect.y = displayBounds.y + windowTopFrame;
		dbg.window_rect.h = displayBounds.h - windowTopFrame;
	} else
		dbg.window_rect.y = displayBounds.y + ( ( displayBounds.h - ( dbg.window_rect.h + windowTopFrame ) ) >> 1 ) + windowTopFrame;
	SDL_SetWindowSize( dbg.win_main, dbg.window_rect.w, dbg.window_rect.h );
	SDL_SetWindowPosition( dbg.win_main, dbg.window_rect.x, dbg.window_rect.y );
	SDL_GetWindowSize( dbg.win_main, &dbg.window_rect.w, &dbg.window_rect.h );
	SDL_GetWindowPosition( dbg.win_main, &dbg.window_rect.x, &dbg.window_rect.y );
	SDL_ShowWindow( dbg.win_main );

	for( WINDOW_ID i = static_cast<WINDOW_ID>( 0U ); i < NUM_WINDOWS; ++i ) {
		window_pos[i] = ImVec2( CalcWindowX( i ), CalcWindowY( i ) );
		window_size[i] = ImVec2( CalcWindowWidth( i ), CalcWindowHeight( i ) );
	}
	ImGui::EndFrame( );

	inputStr[0] = 0;

	return imgui_initialized;
}

void DBGUI_Shutdown( ) {
	if( !imgui_initialized )
		return;

	DBGUI_Reset( );

	DasmShutdown( );

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
	if( !imgui_initialized )
		return;

	ImGui_ImplSDLGPU3_NewFrame( );
	ImGui_ImplSDL3_NewFrame( );
	ImGui::NewFrame( );
}

void DBGUI_Render( ) {
	if( !imgui_initialized )
		return;

	ImGui::Render( );

	SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer( dbg.gpu_device );
	if( !command_buffer ) {
		LOG_ERR( "DEBUG: Failed to acquire GPU command buffer: %s", SDL_GetError( ) );
		return;
	}

	SDL_GPUTexture *swapchain_texture = nullptr;
	if( !SDL_WaitAndAcquireGPUSwapchainTexture( command_buffer, dbg.win_main, &swapchain_texture, nullptr, nullptr ) ) {
		LOG_ERR( "DEBUG: Failed to acquire swapchain texture: %s", SDL_GetError( ) );
		SDL_SubmitGPUCommandBuffer( command_buffer );
		return;
	}

	if( swapchain_texture ) {
		ImDrawData *draw_data = ImGui::GetDrawData( );

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

		SDL_GPURenderPass *render_pass = SDL_BeginGPURenderPass( command_buffer, &target_info, 1, nullptr );

		ImGui_ImplSDLGPU3_RenderDrawData( draw_data, command_buffer, render_pass );
		SDL_EndGPURenderPass( render_pass );
	}
	SDL_SubmitGPUCommandBuffer( command_buffer );
}

void DEBUG_SaveCurrentState( ) {
	SaveCPUstate( );
	SaveMemoryState( );
}
#endif // C_DEBUGGER
