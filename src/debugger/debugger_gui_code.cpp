// SPDX-FileCopyrightText:  2002-2025 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "debugger_gui.h"

#if C_DEBUGGER
#include "breakpoint.h"
#include "cpu/cpu.h"
#include "hardware/timer.h"

SCodeView codeView = { WIN_CODE };

const ImVec4 white_color = ImVec4( 1.0f, 1.0f, 1.0f, 1.0f );
const ImVec4 red_color = ImVec4( 1.0f, 0.0f, 0.0f, 1.0f );
const ImVec4 dark_red_color = ImVec4( 0.76f, 0.11f, 0.12f, 1.0f );
const ImVec4 dark_green_color = ImVec4( 0.11f, 0.76f, 0.11f, 1.0f );
const ImVec4 blue_color = ImVec4( 0.0f, 0.64f, 0.91f, 1.0f );
const ImVec4 yellow_color = ImVec4( 1.0f, 0.99f, 0.33f, 1.0f );
const ImVec4 gold_color = ImVec4( 0.97f, 0.69f, 0.17f, 1.0f );
const ImVec4 light_pink_color = ImVec4( 1.0f, 0.68f, 0.79f, 1.0f );
const ImVec4 pink_color = ImVec4( 0.93f, 0.54f, 0.97f, 1.0f );
const ImVec4 purple_color = ImVec4( 0.75f, 0.72f, 1.0f, 1.0f );
//const ImVec4 dark_purple_color = ImVec4( 0.74f, 0.38f, 0.97f, 1.0f );
const ImVec4 light_violet_color = ImVec4( 0.97f, 0.54f, 0.72f, 1.0f );
const ImVec4 violet_color = ImVec4( 0.97f, 0.38f, 0.64f, 1.0f );
const ImVec4 jmp_color = ImVec4( 1.0f, 1.0f, 0.57f, 1.0f );
const ImVec4 ret_color = ImVec4( 0.94f, 0.53f, 0.52f, 1.0f );
//const ImVec4 light_torquoise_color = ImVec4( 0.37f, 0.86f, 0.97f, 1.0f );
//const ImVec4 dark_brown_color = ImVec4( 0.47f, 0.26f, 0.08f, 1.0f );

static void UpdateMemoryViews( ) {
	for( DATA_ID data_i = static_cast<DATA_ID>( 0U ); data_i < NUM_DATA_VIEWS; ++data_i )
		dbg.update_win[win_data_view[data_i]] = true;
}

uint16_t num_indexed_segments = 0U;
static bool UpdateOrderedSegments( ) {
	if( num_indexed_segments == ordered_segments.size( ) )
		return false;
	uint16_t seg_index = 0U;
	for( const auto &segment : ordered_segments ) {
		if( SEG_BASE == segment.extra.type || SEG_MAX == segment.extra.type || ( SEG_DATA == segment.extra.type && segment.value == dbg.segment[SEG_DATA] ) )
			continue;
		const_cast<uint8_t &>( segment.extra.index ) = ( seg_index++ % ( MAX_ADDRESS_COLORS - 1U ) ) + 1U;
	}
	num_indexed_segments = ordered_segments.size( );
	return true;
}

static bool fTemporaryDataSegment = false;

static void CheckSegmentRegisters( ) {
	for( SegNames seg_name = static_cast<SegNames>( 0 ); seg_name <= SegNames::gs; seg_name = static_cast<SegNames>( seg_name + 1 ) ) {
		uint16_t seg_val = RealSegValue( seg_name );
		if( seg_val > dbg.segment[SEG_CODE] ) {
			const auto it = ordered_segments.find( { seg_val, {} } );
			if( it != ordered_segments.end( ) ) {
				if( cs == seg_name && it->extra.type != SEG_CODE )
					const_cast<SEGTYPE &>( it->extra.type ) = SEG_CODE;
				else if( ds == seg_name && fTemporaryDataSegment )
					fTemporaryDataSegment = false;
			} else {
				if( fTemporaryDataSegment && ds == seg_name ) {
					fTemporaryDataSegment = false;
					ordered_segments.extract( { dbg.segment[SEG_DATA], {} } );
					dbg.segment[SEG_DATA] = seg_val;
				}
				if( ss == seg_name ) {
					uint16_t seg_end = GetPhysicalAddress( { SegValue( ss ), reg_sp } ) >> 4;
					ordered_segments.insert( { seg_val, { SEG_STACK, 0U, seg_end } } );
					if( !ordered_segments.contains( { seg_end, {} } ) )
						ordered_segments.insert( { seg_end, { SEG_STACK_END, 0U, seg_val } } );
				} else
					ordered_segments.insert( { seg_val, { ( cs == seg_name ? SEG_CODE : SEG_DATA ), 0U } } );
				if( seg_val > dbg.segment[SEG_HEAP] ) {
					uint32_t phys_seg_val = 0xFFFF + ( seg_val << 4U );
					if( data_buffer_size < phys_seg_val )
						data_buffer_size = phys_seg_val;
				}
			}
		}
	}
}

bool SCodeView::Set( const ADDRESS_PAIR &address_pair, const VIEW_MASK update_mask ) {
//	if( ( update_mask & V_UPDATE_HISTORY ) && cursorRealAddress != realAddress )
//		HistoryInsert( cursorRealAddress );
	if( !SView::Set( address_pair, ( update_mask & static_cast<VIEW_MASK>( ~V_UPDATE_VIEW ) ) ) )
		return false;
	cursorRealAddress = address_pair;
	cursorAddress = address;
	if( ( update_mask & V_UPDATE_VIEW ) && !AddressVisited( address ) ) { // address not already disassembled
		DasmRecursiveDisassemble( address, address_pair.offset, cpu.code.big, cpu.pmode );
		if( debugging && UpdateOrderedSegments( ) )
			UpdateMemoryViews( );
	}
	return true;
}

bool SCodeView::SetToEIP( ) {
	return Set( { RealSegValue( cs ), reg_eip } );
}

bool SCodeView::SetCursor( const ADDRESS_PAIR &address_pair ) {
	if( address_pair == cursorRealAddress )
		return false;
	cursorRealAddress = address_pair;
	cursorAddress = address_pair.offset + ( address_pair.segment << 4 );
	return true;
}

void DrawCode( ) {
	static auto window_width = window_size[WIN_CODE].x;
	if( dbg.update_win_frame[WIN_CODE] )
		dbg.update_win_scroll[WIN_CODE] = true;
	if( BeginSubWindow( WIN_CODE, "Code" , ImGuiWindowFlags_HorizontalScrollbar ) && !DecodedLine::isEmpty( ) ) {
		if( ImGui::IsWindowHovered( ImGuiHoveredFlags_ChildWindows ) ) {
			if( ImGui::IsMouseClicked( 3 ) )
				codeView.HistoryPrev( );
			else if( ImGui::IsMouseClicked( 4 ) )
				codeView.HistoryNext( );
		}
		uint32_t index = 0U;
		uint16_t currentSegment = 0U;
		float yPosStart = ImGui::GetScrollY( );
		float yPosEnd = yPosStart + window_size[WIN_CODE].y;
		yPosStart -= window_size[WIN_CODE].y;
		yPosEnd += window_size[WIN_CODE].y;
		for( auto dline = DecodedLine::first( ), previous_dline = dline; !DecodedLine::isEnd( ); previous_dline = dline, ++dline, ++index ) {
			static uint8_t addressColorIndex = 0U;
			if( dbg.update_win_scroll[WIN_CODE] && dline.address >= codeView.address ) {
				dbg.update_win_scroll[WIN_CODE] = false;
				ImGui::SetScrollHereY( );
			}
			if( currentSegment != dline.realAddress.segment ) { // match segment to defined segments
				if( currentSegment && !( previous_dline.mnemonicMask & ( MM_Branch | MM_INT | MM_RET | MM_IO ) ) ) {
					ImGui::Spacing( );
					ImGui::Separator( );
					ImGui::Spacing( );
				}
				currentSegment = dline.realAddress.segment;
				const auto &ordered_segment = ordered_segments.find( { currentSegment, {} } );
				if( ordered_segment != ordered_segments.end( ) )
					addressColorIndex = ordered_segment->extra.index % MAX_ADDRESS_COLORS;
			}
			char id[12];
			if( index && dline.mnemonicMask && !( dline.mnemonicMask & MM_Data_Label ) && ( previous_dline.mnemonicMask & MM_Data_Label ) ) {
				ImGui::Spacing( );
				ImGui::Separator( );
				ImGui::Spacing( );
			}
			if( dline.mnemonicMask & MM_Label ) {
				sprintf( id, "##L%08X", dline.address );
				if( ImGui::Selectable( id, false, ImGuiSelectableFlags_SelectOnClick ) ) {
					codeView.SetCursor( dline.realAddress );
					labelView.Set( dline.realAddress, true );
				}
				ImGui::SameLine( ( ( dline.mnemonicMask & MM_Call_Label ) ? 16 : 21 ) * char_width );
				ImGui::TextColored( ( dline.mnemonicMask & MM_Call_Label ) ? blue_color : yellow_color, ( dline.mnemonicMask & MM_Call_Label ) ? "Call label:" : "label:" );
			} else if( dline.mnemonicMask & MM_Data_Label ) {
				if( index && previous_dline.mnemonicMask && !( previous_dline.mnemonicMask & MM_Data_Label ) && dline.realAddress.offset ) {
					ImGui::Separator( );
					ImGui::Spacing( );
				}
			} else if( index && previous_dline.mnemonicMask & ( MM_RET | MM_JMP ) ) {
				if( !( dline.mnemonicMask & ( MM_RET | MM_JMP ) ) ) {
					ImGui::Separator( );
					ImGui::Spacing( );
				}
			} else if( dline.mnemonicMask & MM_IO ) {
				ImGui::Spacing( );
			} else if( start_address == dline.address ) {
				ImGui::SetCursorPosX( 21 * char_width );
				ImGui::TextColored( green_color, "start:" );
			}
			float yPos = ImGui::GetCursorPosY( );
			if( yPos >= yPosStart && yPos <= yPosEnd ) {
				const bool is_current_ip = ( dline.realAddress.segment == RealSegValue( cs ) ) && ( dline.realAddress.offset == reg_eip );
				const bool is_breakpoint = CBreakpoint::IsBreakpoint( dline.realAddress );
				const bool is_temporary_breakpoint = CBreakpoint::IsBreakpoint( dline.realAddress, true );
				sprintf( id, "##%08X", dline.address );
				if( ImGui::Selectable( id, dline.address == codeView.cursorAddress, ImGuiSelectableFlags_SelectOnClick ) ) {
					if( ( dline.mnemonicMask & MM_Branch ) && ImGui::IsKeyDown( ImGuiMod_Ctrl ) ) {
						ADDRESS_PAIR labelRealAddress;
						if( CallerLabelRealAddress( dline.address, labelRealAddress ) ) {
							codeView.Set( labelRealAddress, V_UPDATE_SCROLL_HISTORY );
							break;
						}
					} else {
						codeView.SetCursor( dline.realAddress );
						if( dline.mnemonicMask & ( MM_Branch | MM_Label | MM_Data_Label | MM_Memory_Access ) )
							labelView.Set( dline.realAddress, ( dline.mnemonicMask & ( MM_Branch | MM_Memory_Access ) ) ? false : true );
					}
				}
				ImGui::SameLine( 0.0f, 0.0f );
				ImGui::TextColored( ( is_current_ip ? dark_green_color : ( is_breakpoint ? dark_red_color :
					( is_temporary_breakpoint ? grey_color : address_colors[addressColorIndex][0] ) ) ), "%04X", dline.realAddress.segment );
				ImGui::SameLine( 0.0f, 0.0f );
				ImGui::TextColored( is_breakpoint || is_temporary_breakpoint || is_current_ip ? white_color : grey_color, ":" );
				ImGui::SameLine( 0.0f, 0.0f );
				ImGui::TextColored( ( is_current_ip ? green_color : ( is_temporary_breakpoint ? light_grey_color :
					( is_breakpoint ? red_color : address_colors[addressColorIndex][1] ) ) ), "%04X%c", dline.realAddress.offset,
					is_breakpoint && is_temporary_breakpoint ? '+' : is_breakpoint ? '*' : is_temporary_breakpoint ? '-' : ' ' );
				ImGui::SameLine( 0.0f, 0.0f );
				ImGui::TextColored( is_current_ip ? light_grey_color : grey_color, "%s", dline.szOpcode );
				if( is_current_ip ) {
					ImGui::SameLine( 28 * char_width );
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
				else if( dline.mnemonicMask & MM_IO )
					operator_color = &white_color;
				else if( dline.mnemonicMask & MM_Logical )
					operator_color = &light_violet_color;
				else if( dline.mnemonicMask & MM_Math )
					operator_color = &pink_color;
				else if( dline.mnemonicMask & MM_String )
					operator_color = &light_pink_color;
				else if( dline.mnemonicMask & MM_Stack )
					operator_color = &light_grey_color;
				else if( dline.mnemonicMask & ( MM_ConditionalJump | MM_LOOP ) )
					operator_color = &yellow_color;
				ImGui::SameLine( 30 * char_width );
				ImGui::TextColored( *operator_color, "%s", dline.pMnemonic );
				const ImVec4 *operands_color = is_current_ip ? &green_color : &purple_color;
				if( dline.pOperands ) {
					ImGui::SameLine( );
					if( ImGui::GetCursorPosX( ) < 36 * char_width )
						ImGui::SetCursorPosX( 36 * char_width );
					ImGui::TextColored( *operands_color, "%s", dline.pOperands );
				}
				if( dline.szComment[0] ) {
					ImGui::SameLine( );
					float xPos = ImGui::GetCursorPosX( );
					if( xPos > 49 * char_width )
						ImGui::SetCursorPosX( xPos + char_width );
					else
						ImGui::SetCursorPosX( 50 * char_width );
					ImGui::TextColored( light_grey_color, "%s", dline.szComment );
				}
				if( is_current_ip ) {
					ImGui::SameLine( window_width - char_width * 2 - scrollbar_width );
					ImGui::TextColored( green_color, "<" );
				}
				if( is_breakpoint || is_temporary_breakpoint ) {
					ImGui::SameLine( window_width - char_width - scrollbar_width );
					if( is_breakpoint && is_temporary_breakpoint )
						ImGui::TextColored( violet_color, "+" );
					else if( is_breakpoint )
						ImGui::TextColored( red_color, "*" );
					else
						ImGui::TextColored( light_grey_color, "-" );
				}
			} else
				ImGui::NewLine( );
			if( dline.mnemonicMask & ( MM_Branch | MM_INT | MM_RET | MM_IO ) )
				ImGui::Spacing( );
		}
	}
	EndSubWindow( WIN_CODE );
}

void TemporaryDataSegment( ) {
	if( dbg.segment[SEG_DATA] && dbg.segment[SEG_DATA] != dbg.segment[SEG_PSP] )
		return;
	auto dline = DecodedLine::last( );
	if( dline.mnemonicMask & MM_Data_Label )
		dbg.segment[SEG_DATA] = dline.realAddress.segment;
	else {
		uint32_t address = dline.address + dline.length;
		dbg.segment[SEG_DATA] = ( address >> 4U ) + ( address & 0x0000000F ? 1U : 0U );
	}
	ordered_segments.insert( { dbg.segment[SEG_DATA], { SEG_DATA, 0U } } );
	fTemporaryDataSegment = true;
}

void UpdateDataSegment( const uint16_t segment ) {
	dbg.segment[SEG_DATA] = segment;
	fTemporaryDataSegment = false;
}

extern char curSelectorName[3];

static uint32_t last_ip = static_cast<uint32_t>( -1 );
static void AnalyzeCurrentInstruction( ) {
	if( reg_eip == last_ip )
		return;
	auto dline = DecodedLine::find( { RealSegValue( cs ), reg_eip } );
	if( dline ) {
		auto szResult = AnalyzeInstruction( dline->pMnemonic, dline->pOperands, curSelectorName );
		if( *szResult )
			strcpy( const_cast<char *>( &dline->szComment[0] ), szResult );
		last_ip = reg_eip;
	}
}

constexpr const uint8_t MAX_RECENT_IP = 64U;
static uint32_t recent_ip_addresses[MAX_RECENT_IP] = {};
static uint8_t recent_ip_index = 0U;

void DEBUG_NewInstruction( ) {
	if( !imgui_initialized )
		return;
	const ADDRESS_PAIR ip_address_pair = { RealSegValue( cs ), reg_eip };
	if( !debugging ) {
		const uint32_t ip_address = ip_address_pair.address( );
		uint8_t count = 0U;
		for( const auto &recent_ip_address : recent_ip_addresses ) {
			if( recent_ip_address == ip_address ) {
				++count;
				break;
			}
		}
		recent_ip_addresses[recent_ip_index] = ip_address;
		if( MAX_RECENT_IP == ++recent_ip_index )
			recent_ip_index = 0U;
		if( count )
			return;
	}
	CheckSegmentRegisters( );
	codeView.Set( ip_address_pair, debugging ? V_UPDATE_DEFAULT : V_UPDATE_VIEW );
	if( debugging ) {
		UpdateOrderedSegments( );
		UpdateMemoryViews( );
	} else if( dbg.graphics_window_hidden && GetTicksSince( normalLoopTickCount ) > 2000 )
		DEBUG_ShowDOSBox( );
	AnalyzeCurrentInstruction( );
}
#endif // C_DEBUGGER
