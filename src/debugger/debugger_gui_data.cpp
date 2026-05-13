// SPDX-FileCopyrightText:  2002-2025 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "debugger_gui.h"

#if C_DEBUGGER
SDataView dataView = { WIN_DATA };
SDataView stackView = { WIN_STACK };
Cursor data_cursor[NUM_DATA_CURSORS] = { { IM_COL32( 234, 63, 247, 255 ) }, { IM_COL32( 255, 142, 85, 255 ) } };

bool SDataView::Set( const ADDRESS_PAIR &address_pair, const VIEW_MASK update_mask ) {
	if( !SView::Set( address_pair, update_mask ) )
		return false;
	return true;
}

std::vector<uint8_t> data_buffer;

uint32_t data_buffer_size = 0U;
static uint32_t savedMemorySize = 0U;
void SaveMemoryState( ) {
	if( data_buffer.size( ) != data_buffer_size ) {
		data_buffer.clear( );
		data_buffer.resize( data_buffer_size );
		data_buffer_size = data_buffer.size( );
	}
	uint32_t *mem32 = reinterpret_cast<uint32_t *>( MemBase );
	uint32_t *data32 = reinterpret_cast<uint32_t *>( &data_buffer[0] );
	for( uint32_t count = data_buffer.size( ) >> 2; count; --count )
		*data32++ = *mem32++;
	savedMemorySize = data_buffer.size( );
}

std::vector<char> data_text_buffer;
static std::vector<DIFF> data_diff[NUM_DATA_VIEWS];
static ImU32 diff_col = IM_COL32( 0, 96, 0, 255 );
uint16_t data_segment = static_cast<uint16_t>( -1 );

static void SetCursors( ) {
	data_cursor[DI_CURSOR].visible = false;
	data_cursor[DI_CURSOR].realAddress = { RealSegValue( es ), reg_edi };
	data_cursor[SI_CURSOR].visible = false;
	data_cursor[SI_CURSOR].realAddress = { RealSegValue( ds ), reg_esi };
	auto dline = DecodedLine::find( { RealSegValue( cs ), reg_eip } );
	if( dline ) {
		if( dline->mnemonicMask & MM_Has_Segment ) {
			SegNames seg_name;
			if( dline->instruction.attributes & ZYDIS_ATTRIB_HAS_SEGMENT_CS ) seg_name = cs;
			else if( dline->instruction.attributes & ZYDIS_ATTRIB_HAS_SEGMENT_SS ) seg_name = ss;
			else if( dline->instruction.attributes & ZYDIS_ATTRIB_HAS_SEGMENT_DS ) seg_name = ds;
			else if( dline->instruction.attributes & ZYDIS_ATTRIB_HAS_SEGMENT_ES ) seg_name = es;
			else if( dline->instruction.attributes & ZYDIS_ATTRIB_HAS_SEGMENT_FS ) seg_name = fs;
			else if( dline->instruction.attributes & ZYDIS_ATTRIB_HAS_SEGMENT_GS ) seg_name = gs;
			if( dline->instruction.operand_count >= 3 ) {
				if( dline->operands[2].type == ZYDIS_OPERAND_TYPE_REGISTER ) {
					if( dline->operands[2].reg.value == ZYDIS_REGISTER_SI )
						data_cursor[SI_CURSOR].realAddress.segment = RealSegValue( seg_name );
					else if( dline->operands[2].reg.value == ZYDIS_REGISTER_DI )
						data_cursor[DI_CURSOR].realAddress.segment = RealSegValue( seg_name );
				}
			}
		}
	}
}

static uint16_t NumSegDiff( uint16_t segA, uint16_t segB ) {
	if( segA == segB )
		return 0U;
	uint16_t result = 1U;
	if( segB < segA )
		std::swap( segA, segB );
	auto ordered_segment_a = ordered_segments.find( { segA, {} } );
	if( ordered_segment_a != ordered_segments.end( ) ) {
		auto ordered_segment_b = ordered_segments.find( { segB, {} } );
		if( ordered_segment_b != ordered_segments.end( ) ) {
			while( ++ordered_segment_a != ordered_segment_b )
				++result;
		}
	}
	return result;
}

static uint32_t num_data_lines = 0U;
static uint32_t data_line_index = 0U;

void DrawData( ) {
	if( BeginSubWindow( WIN_DATA, "Data", ImGuiWindowFlags_None ) ) {
		if( ImGui::IsWindowHovered( ImGuiHoveredFlags_ChildWindows ) ) {
			if( ImGui::IsMouseClicked( 3 ) )
				dataView.HistoryPrev( );
			else if( ImGui::IsMouseClicked( 4 ) )
				dataView.HistoryNext( );
		}
		if( ImGui::IsWindowFocused( ImGuiFocusedFlags_ChildWindows ) && dbg.active_data_view != DATA_VIEW )
			dbg.active_data_view = DATA_VIEW;
		static char szAddressStart[5], szAddressEnd[5];
		bool scroll_to_diff = false;
		if( dbg.update_win[WIN_DATA] ) {
			dbg.update_win[WIN_DATA] = false;
			data_segment = dataView.realAddress.segment;
			ADDRESS_PAIR address_pair = { data_segment, 0U };
			auto ordered_segment = ordered_segments.begin( );
			for( ; ordered_segment != ordered_segments.end( ); ++ordered_segment ) {
				if( address_pair.segment < ordered_segment->value )
					break;
			}
			uint32_t count = ( dataView.realAddress.offset + 1U ) >> 4;
			if( ( count << 4 ) < dataView.realAddress.offset + 1U )
				++count;
			count += address_pair.segment;
			if( count >= dbg.segment[SEG_STACK_END] ) {
				count -= address_pair.segment;
				count = ( count < 0x1000 ? 0x1000 : count );
			} else if( count >= dbg.segment[SEG_STACK] )
				count = dbg.segment[SEG_STACK_END] - address_pair.segment;
			else
				count = dbg.segment[SEG_STACK] - address_pair.segment;
			uint32_t line_segment = address_pair.segment;
			const uint32_t data_base = address_pair.segment << 4;
			auto mem = &MemBase[data_base];
			const auto mem_compare_end = &MemBase[savedMemorySize];
			if( data_base >= savedMemorySize )
				const_cast<uint32_t &>( data_base ) = 0U;
			auto data = &data_buffer[data_base];
			data_diff[DATA_VIEW].clear( );
			if( ( count << 7U ) > data_text_buffer.size( ) ) {
				data_text_buffer.clear( );
				data_text_buffer.resize( count << 7U );
			}
			char *line = &data_text_buffer[0];
			float yPos = 0.0f;
			SetCursors( );
			for( num_data_lines = 0; count; --count, line += 82U, ++line_segment, yPos += line_height, ++num_data_lines ) {
				if( line_segment >= ordered_segment->value ) {
					address_pair.segment = ordered_segment->value;
					++ordered_segment;
					address_pair.offset = 0U;
					*line++ = '\n';
					yPos += line_height;
					++num_data_lines;
				}
				// Address
				bool f32bit = false;
				if( address_pair.offset <= 0xFFFF )
					sprintf( line, "%04X:%04X      ", address_pair.segment, address_pair.offset );
				else {
					sprintf( line, "%04X:%08X  ", address_pair.segment, address_pair.offset );
					f32bit = true;
				}
				// Hex values
				uint8_t start_digits = f32bit ? 12U : 8U;
				uint8_t start_spaces = f32bit ? 3U : 4U;
				for( uint8_t x = 0U; x < 16U; ++x, ++mem, ++address_pair.offset ) {
					uint8_t num_digits = start_digits + ( x << 1U );
					uint8_t num_spaces = start_spaces + x + ( f32bit ? 0U : ( x >> 2U ) );
					uint8_t char_pos = num_digits + num_spaces - 1U;
					sprintf( &line[char_pos], " %02X ", *mem );
					line[65U + x] = ( *mem < 32 || !isprint( *mem ) ? '.' : *mem ); // Ascii representation
					if( mem < mem_compare_end ) {
						if( *data != *mem )
							data_diff[DATA_VIEW].push_back( { static_cast<uint32_t>( mem - MemBase ), address_pair.segment, *data, { digit_width * num_digits + space_width * num_spaces - space_width * 0.5f, yPos } } );
						++data;
					}
					for( auto &cursor : data_cursor ) {
						if( address_pair == cursor.realAddress ) {
							cursor.pos = { digit_width * num_digits + space_width * num_spaces - cursor_width, yPos };
							cursor.visible = true;
						}
					}
				}
				*reinterpret_cast<uint16_t *>( &line[63] ) = 0x2020;
				line[81] = '\n';
			}
			*line = 0;
			if( !data_diff[DATA_VIEW].empty( ) ) {
				scroll_to_diff = true;
				dbg.update_win_scroll[WIN_DATA] = true;
			}
			sprintf( szAddressStart, "%04X", data_segment );
			sprintf( szAddressEnd, "%04X", line_segment );
		}
		ImGui::PushStyleColor( ImGuiCol_NavHighlight, IM_COL32( 0, 0, 0, 0 ) ); // Transparent highlight
		ImGui::PushStyleColor( ImGuiCol_NavWindowingHighlight, IM_COL32( 0, 0, 0, 0 ) ); // Transparent window highlight
		ImGui::PushItemWidth( digit_width * 5.0f );
		ImGui::InputText( "##StartAddress", szAddressStart, 5U, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank );
		ImGui::SameLine( );
		ImGui::InputText( "##EndAddress", szAddressEnd, 5U, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank );
		ImGui::PopItemWidth( );
		ImGui::Separator( );
		if( ImGui::BeginChild( "ScrollRegion", { 0.0f, 0.0f }, false, ImGuiWindowFlags_HorizontalScrollbar ) ) {
			float scrollY = ImGui::GetScrollY( );
			if( dbg.update_win_scroll[WIN_DATA] ) {
				if( scroll_to_diff && !dataView.realAddress.offset && !data_diff[DATA_VIEW].empty( ) )
					scrollY = data_diff[DATA_VIEW][0].pos.y;
				else
					scrollY = ( ( dataView.realAddress.segment - data_segment ) + ( dataView.realAddress.offset >> 4 ) + NumSegDiff( data_segment, dataView.realAddress.segment ) ) * line_height;
				data_line_index = scrollY / line_height;
			}
			ImVec2 screen_top_left = ImGui::GetCursorScreenPos( ); // Top-left corner;

			float yPosStart = scrollY;
			float yPosEnd = yPosStart + window_size[WIN_DATA].y;
			yPosStart -= window_size[WIN_DATA].y;
			yPosEnd += window_size[WIN_DATA].y;
			auto screen_yPosStart = screen_top_left.y + yPosStart;
			auto screen_yPosEnd = screen_top_left.y + yPosEnd;

			auto drawList = ImGui::GetWindowDrawList( );
			for( const auto &diff : data_diff[DATA_VIEW] ) {
				ImVec2 posTopLeft = { screen_top_left.x + diff.pos.x, screen_top_left.y + diff.pos.y };
				ImVec2 posBottomRight = { posTopLeft.x + digit_width * 2.0f + space_width, posTopLeft.y + line_height_no_spacing };
				if( posBottomRight.y >= screen_yPosStart && posTopLeft.y <= screen_yPosEnd )
					drawList->AddRectFilled( posTopLeft, posBottomRight, diff_col, 4.0f );
			}
			for( auto &cursor : data_cursor ) {
				if( cursor.visible ) {
					ImVec2 posTopLeft = { screen_top_left.x + cursor.pos.x, screen_top_left.y + cursor.pos.y };
					ImVec2 posBottomRight = { posTopLeft.x + cursor_width, posTopLeft.y + line_height_no_spacing };
					if( posBottomRight.y >= screen_yPosStart && posTopLeft.y <= screen_yPosEnd )
						drawList->AddRectFilled( posTopLeft, posBottomRight, cursor.color );
				}
			}
			char id[12];
			auto pTextBuffer = &data_text_buffer[0];
			for( uint32_t count = num_data_lines, i = 0U; count; --count, ++i ) {
				if( '\n' == *pTextBuffer ) {
					ImGui::NewLine( );
					++pTextBuffer;
				} else {
					float yPos = ImGui::GetCursorPosY( );
					if( yPos >= yPosStart && yPos <= yPosEnd ) {
						sprintf( id, "##%08X", i );
						if( ImGui::Selectable( id, i == data_line_index, ImGuiSelectableFlags_SelectOnClick ) )
							data_line_index = i;
						ImGui::SameLine( 0.0f, 0.0f );
						ImGui::TextUnformatted( pTextBuffer, pTextBuffer + 81 );
					} else
						ImGui::NewLine( );
					pTextBuffer += 82U;
				}
			}
			if( dbg.update_win_scroll[WIN_DATA] ) {
				dbg.update_win_scroll[WIN_DATA] = false;
				ImGui::SetScrollY( scrollY );
			}
		}
		ImGui::EndChild( );
		ImGui::PopStyleColor( 2 );
	}
	EndSubWindow( WIN_DATA );
}

std::vector<char> stack_text_buffer;
uint32_t stack_lines = 0U;
uint16_t stack_segment = static_cast<uint16_t>( -1 );
static Cursor sp_cursor = { IM_COL32( 94, 219, 247, 255 ) };

void DrawStack( ) {
	if( BeginSubWindow( WIN_STACK, "Stack", ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNavFocus ) ) {
		if( ImGui::IsWindowHovered( ImGuiHoveredFlags_ChildWindows ) ) {
			if( ImGui::IsMouseClicked( 3 ) )
				stackView.HistoryPrev( );
			else if( ImGui::IsMouseClicked( 4 ) )
				stackView.HistoryNext( );
		}
		if( ImGui::IsWindowFocused( ImGuiFocusedFlags_ChildWindows ) && dbg.active_data_view != STACK_VIEW )
			dbg.active_data_view = STACK_VIEW;
		bool scroll_to_diff = false;
		if( dbg.update_win[WIN_STACK] ) {
			dbg.update_win[WIN_STACK] = false;
			ADDRESS_PAIR address_pair = stackView.realAddress;
			stack_segment = address_pair.segment;
			address_pair.offset = ( address_pair.offset & 0x0000000F ? 16U : 0U ) + ( ( address_pair.offset >> 4 ) << 4 );
			uint32_t line_segment = address_pair.segment + ( address_pair.offset >> 4 );
			auto ordered_segment = ordered_segments.end( );
			--ordered_segment;
			for( ; ordered_segment != ordered_segments.begin( ); --ordered_segment ) {
				if( line_segment > ordered_segment->value )
					break;
			}
			if( address_pair.segment < ordered_segment->value ) {
				address_pair.segment = ordered_segment->value;
				address_pair.offset = ( ( line_segment - address_pair.segment ) << 4 ) + 0xF;
			} else {
				address_pair.offset = ( ( std::next( ordered_segment, 1 )->value - address_pair.segment ) << 4 ) - 1U;
				line_segment = std::next( ordered_segment, 1 )->value;
			}
			const uint32_t data_base = address_pair.segment << 4;
			auto mem = &MemBase[data_base + address_pair.offset];
			const auto mem_compare_end = &MemBase[savedMemorySize];
			const_cast<uint32_t &>( data_base ) += address_pair.offset;
			if( data_buffer.empty( ) )
				const_cast<uint32_t &>( data_base ) = 0U;
			else if( data_base >= savedMemorySize )
				const_cast<uint32_t &>( data_base ) = savedMemorySize - 1U;
			auto data = &data_buffer[data_base];
			data_diff[STACK_VIEW].clear( );
			stack_lines = line_segment - stack_segment;
			if( ( stack_lines << 7U ) > stack_text_buffer.size( ) ) {
				stack_text_buffer.clear( );
				stack_text_buffer.resize( stack_lines << 7U );
			}
			char *line = &stack_text_buffer[0];
			float yPos = 0.0f;
			sp_cursor.visible = false;
			sp_cursor.realAddress = { RealSegValue( ss ), ( cpu.code.big ? reg_esp : reg_sp ) };
			for( uint32_t count = stack_lines; count; --count, line += 82, --line_segment, yPos += line_height_no_spacing ) {
				if( line_segment < ordered_segment->value ) {
					--ordered_segment;
					address_pair.segment = ordered_segment->value;
					address_pair.offset = ( ( line_segment - address_pair.segment ) << 4 ) + 0xF;
				}
				bool f32bit = false;
				// Address
				if( address_pair.offset <= 0xFFFF )
					sprintf( line, "%04X:%04X      ", address_pair.segment, address_pair.offset );
				else {
					sprintf( line, "%04X:%08X  ", address_pair.segment, address_pair.offset );
					f32bit = true;
				}
				// Hex values
				uint8_t start_digits = f32bit ? 12U : 8U;
				uint8_t start_spaces = f32bit ? 3U : 4U;
				for( uint8_t x = 0U; x < 16U; ++x, --mem, --address_pair.offset ) {
					uint8_t num_digits = start_digits + ( x << 1U );
					uint8_t num_spaces = start_spaces + x + ( f32bit ? 0U : ( x >> 2U ) );
					uint8_t char_pos = num_digits + num_spaces - 1U;
					sprintf( &line[char_pos], " %02X ", *mem );
					line[65U + x] = ( *mem < 32 || !isprint( *mem ) ? '.' : *mem ); // Ascii representation
					if( mem < mem_compare_end ) {
						if( *data != *mem )
							data_diff[STACK_VIEW].push_back( { static_cast<uint32_t>( mem - MemBase ), address_pair.segment, *data, { digit_width * num_digits + space_width * num_spaces - space_width * 0.5f, yPos } } );
						--data;
					}
					if( address_pair == sp_cursor.realAddress ) {
						sp_cursor.pos = { digit_width * ( num_digits + 2U ) + space_width * num_spaces, yPos };
						sp_cursor.visible = true;
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
		if( sp_cursor.visible ) {
			ImVec2 pos = { top_left.x + sp_cursor.pos.x, top_left.y + sp_cursor.pos.y };
			drawList->AddRectFilled( pos, { pos.x + cursor_width, pos.y + line_height_no_spacing }, sp_cursor.color );
		}
		ImGui::TextUnformatted( &stack_text_buffer[0] );
		if( dbg.update_win_scroll[WIN_STACK] ) {
			dbg.update_win_scroll[WIN_STACK] = false;
			if( scroll_to_diff && !data_diff[STACK_VIEW].empty( ) )
				ImGui::SetScrollY( data_diff[STACK_VIEW][0].pos.y );
			else
				ImGui::SetScrollY( ( stack_lines - ( ( stackView.realAddress.segment - stack_segment ) + ( stackView.realAddress.offset >> 4 ) + ( stackView.realAddress.offset & 0x0000000F ? 1U : 0U ) ) ) * line_height_no_spacing );
		}
	}
	EndSubWindow( WIN_STACK );
}

void DrawDiff( ) {
	if( BeginSubWindow( WIN_DIFF, "Changes", ImGuiWindowFlags_NoNavFocus ) ) {
		static uint32_t selectedIndex = -1;
		ADDRESS_PAIR currentAddress;
		uint8_t addressColorIndex = 0U;
		for( const auto &diff : data_diff[DATA_VIEW] ) {
			const uint32_t offset = diff.address - ( diff.segment << 4 );
			if( currentAddress.segment != diff.segment ) { // match segment to defined segments
				currentAddress.segment = diff.segment;
				const auto &ordered_segment = ordered_segments.find( { currentAddress.segment, {} } );
				if( ordered_segment != ordered_segments.end( ) )
					addressColorIndex = ordered_segment->extra.index % MAX_ADDRESS_COLORS;
				ImGui::Separator( );
			} else if( currentAddress.offset != offset >> 4U ) {
				currentAddress.offset = offset >> 4U;
				ImGui::Spacing( );
			}
			const bool isSelected = ( selectedIndex == diff.address );
			char id[11];
			sprintf( id, "##%08X", diff.address );
			if( ImGui::Selectable( id, isSelected, ImGuiSelectableFlags_SelectOnClick ) ) {
				selectedIndex = diff.address;
				dataView.Set( { diff.segment, offset }, V_UPDATE_SCROLL_HISTORY );
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
	EndSubWindow( WIN_DIFF );
}
#endif // C_DEBUGGER
