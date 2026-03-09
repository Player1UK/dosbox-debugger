// SPDX-FileCopyrightText:  2002-2021 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

/* Local Debug Function */


#include <curses.h>
#include "hardware/memory.h"

enum NCURSES_COLOR_PAIRS {
	PAIR_BLACK_BLUE = 1,
	PAIR_BYELLOW_BLACK = 2,
	PAIR_GREEN_BLACK = 3,
	PAIR_BLACK_GREY = 4,
	PAIR_GREY_RED = 5,
	PAIR_BLACK_GREEN = 6,
};

void DBGUI_StartUp();

const uint32_t NUM_WIN_DATA = 4;

struct DBGBlock {
	WINDOW *win_main = nullptr; /* The Main Window */
	WINDOW *win_code = nullptr; /* Disassembly/Debug point Window */
	WINDOW* win_data[NUM_WIN_DATA] = {nullptr, nullptr, nullptr, nullptr}; /* Data Output windows */
	WINDOW *win_reg = nullptr;  /* Register Window */
	WINDOW *win_var = nullptr;  /* Variable Window */
	WINDOW *win_con = nullptr;  /* Console Output Window */
	WINDOW *win_out = nullptr;  /* Text Output Window */
	uint32_t active_win = 0;    /* Current active window */
	uint32_t active_win_data = 0;    /* Current active data window */
	uint32_t input_y = 0;
	uint32_t global_mask = 0; /* Current msgmask */
	/* Window height values in rows */
	int32_t rows_code = 60;     /* Disassembly/Debug point window height */
	int32_t rows_data[NUM_WIN_DATA] = {70, 50, 50, 50}; /* Data Output window
	                                                   heights */
	int32_t rows_registers = 4; /* Registers window height */
	int32_t rows_variables = 4;  /* Variable window height */
	int32_t rows_con = 1;		/* Console window height */
	int32_t rows_output = 0;    /* Text Output window height, calculated dynamically */
	int32_t colums = 80;       /* Columns wide */
};

extern uint16_t dataSeg[NUM_WIN_DATA];
extern uint32_t dataOfs[NUM_WIN_DATA];

struct DASMLine {
	uint32_t pc = 0;
	char dasm[80] = {0};
	PhysPt ea = 0;
	uint16_t easeg = 0;
	uint32_t eaoff = 0;
};

#define MAXCMDLEN 254
struct SCodeViewData {
	int cursorPos          = 0;
	uint16_t firstInstSize = 0;
	uint16_t useCS         = 0;
	uint32_t useEIPlast    = 0;
	uint32_t useEIPmid     = 0;
	uint32_t useEIP        = 0;
	uint32_t goodEIP       = 0;
	uint16_t cursorSeg     = 0;
	uint32_t cursorOfs     = 0;

	bool ovrMode = false;

	char inputStr[MAXCMDLEN + 1]     = {};
	char suspInputStr[MAXCMDLEN + 1] = {};

	int inputPos = 0;
};

/* Local Debug Stuff */
Bitu DasmI386(char* buffer, PhysPt pc, Bitu cur_ip, bool bit32);
int DasmLastOperandSize();
