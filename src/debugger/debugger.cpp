// SPDX-FileCopyrightText:  2021-2026 The DOSBox Staging Team
// SPDX-FileCopyrightText:  2002-2021 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dosbox.h"

#if C_DEBUGGER

#include <fstream>

#include "cbreakpoint.h"
#include "cpu/paging.h"
#include "debugger.h"
#include "debugger_inc.h"
#include "gui/common.h"
#include "gui/mapper.h"
#include "hardware/input/keyboard.h"
#include "hardware/pic.h"
#include "hardware/timer.h"
#include "shell/shell.h"

extern int old_cursor_state;

extern uint32_t DEBUG_CheckKeys(void);

extern void DrawVariables(void);

extern uint32_t GetHexValue(char*, char*&);

bool exitLoop    = false;

extern DBGBlock dbg;
bool debugging = false;

SCodeViewData codeViewData = {};

/***********/
/* Helpers */
/***********/

uint32_t PhysMakeProt(uint16_t selector, uint32_t offset)
{
	Descriptor desc;
	if (cpu.gdt.GetDescriptor(selector, desc)) {
		return desc.GetBase() + offset;
	}
	return 0;
}

uint32_t GetAddress(uint16_t seg, uint32_t offset)
{
	if (seg == SegValue(cs)) {
		return SegPhys(cs) + offset;
	}
	if (cpu.pmode && !(reg_flags & FLAG_VM)) {
		Descriptor desc;
		if (cpu.gdt.GetDescriptor(seg, desc)) {
			return PhysMakeProt(seg, offset);
		}
	}
	return (seg << 4) + offset;
}

static char empty_sel[] = {' ', ' ', 0};

bool GetDescriptorInfo(char* selname, char* out1, char* out2)
{
	Bitu sel;
	Descriptor desc;

	if (strstr(selname, "cs") || strstr(selname, "CS")) {
		sel = SegValue(cs);
	} else if (strstr(selname, "ds") || strstr(selname, "DS")) {
		sel = SegValue(ds);
	} else if (strstr(selname, "es") || strstr(selname, "ES")) {
		sel = SegValue(es);
	} else if (strstr(selname, "fs") || strstr(selname, "FS")) {
		sel = SegValue(fs);
	} else if (strstr(selname, "gs") || strstr(selname, "GS")) {
		sel = SegValue(gs);
	} else if (strstr(selname, "ss") || strstr(selname, "SS")) {
		sel = SegValue(ss);
	} else {
		sel = GetHexValue(selname, selname);
		if (*selname == 0) {
			selname = empty_sel;
		}
	}
	if (cpu.gdt.GetDescriptor(sel, desc)) {
		switch (desc.Type()) {
		case DESC_TASK_GATE:
			sprintf(out1,
			        "%s: s:%08X type:%02X p",
			        selname,
			        desc.GetSelector(),
			        desc.saved.gate.type);
			sprintf(out2,
			        "    TaskGate   dpl : %01X %1X",
			        desc.saved.gate.dpl,
			        desc.saved.gate.p);
			return true;
		case DESC_LDT:
		case DESC_286_TSS_A:
		case DESC_286_TSS_B:
		case DESC_386_TSS_A:
		case DESC_386_TSS_B:
			sprintf(out1,
			        "%s: b:%08X type:%02X pag",
			        selname,
			        desc.GetBase(),
			        desc.saved.seg.type);
			sprintf(out2,
			        "    l:%08X dpl : %01X %1X%1X%1X",
			        desc.GetLimit(),
			        desc.saved.seg.dpl,
			        desc.saved.seg.p,
			        desc.saved.seg.avl,
			        desc.saved.seg.g);
			return true;
		case DESC_286_CALL_GATE:
		case DESC_386_CALL_GATE:
			sprintf(out1,
			        "%s: s:%08X type:%02X p params: %02X",
			        selname,
			        desc.GetSelector(),
			        desc.saved.gate.type,
			        desc.saved.gate.paramcount);
			sprintf(out2,
			        "    o:%08X dpl : %01X %1X",
			        desc.GetOffset(),
			        desc.saved.gate.dpl,
			        desc.saved.gate.p);
			return true;
		case DESC_286_INT_GATE:
		case DESC_286_TRAP_GATE:
		case DESC_386_INT_GATE:
		case DESC_386_TRAP_GATE:
			sprintf(out1,
			        "%s: s:%08X type:%02X p",
			        selname,
			        desc.GetSelector(),
			        desc.saved.gate.type);
			sprintf(out2,
			        "    o:%08X dpl : %01X %1X",
			        desc.GetOffset(),
			        desc.saved.gate.dpl,
			        desc.saved.gate.p);
			return true;
		}
		sprintf(out1,
		        "%s: b:%08X type:%02X parbg",
		        selname,
		        desc.GetBase(),
		        desc.saved.seg.type);
		sprintf(out2,
		        "    l:%08X dpl : %01X %1X%1X%1X%1X%1X",
		        desc.GetLimit(),
		        desc.saved.seg.dpl,
		        desc.saved.seg.p,
		        desc.saved.seg.avl,
		        desc.saved.seg.r,
		        desc.saved.seg.big,
		        desc.saved.seg.g);
		return true;
	} else {
		strcpy(out1, "                                     ");
		strcpy(out2, "                                     ");
	}
	return false;
}

/********************/
/* DebugVar   stuff */
/********************/

CDebugVar::CDebugVar(const char* vname, PhysPt address) : adr(address)
{
	safe_strcpy(name, vname);
}

std::vector<CDebugVar*> varList = {};

/********************/
/* Breakpoint stuff */
/********************/

bool skipFirstInstruction = false;
extern std::list<CBreakpoint*> BPoints;

#if C_HEAVY_DEBUGGER
template <typename T>
void DEBUG_UpdateMemoryReadBreakpoints(const PhysPt addr)
{
	static_assert(std::is_unsigned_v<T>);
	static_assert(std::is_integral_v<T>);

	for (CBreakpoint* bp : BPoints) {
		if (bp->GetType() == BKPNT_MEMORY_READ) {
			const PhysPt location_begin = bp->GetLocation();
			const PhysPt location_end = location_begin + sizeof(T);
			if ((addr >= location_begin) && (addr < location_end)) {
				DEBUG_ShowMsg("bpmr hit: %04X:%04X, cs:ip = %04X:%04X",
				              bp->GetSegment(),
				              bp->GetOffset(),
				              SegValue(cs),
				              reg_eip);
				bp->FlagMemoryAsRead();
			}
		}
	}
}
// Explicit instantiations
template void DEBUG_UpdateMemoryReadBreakpoints<uint8_t>(const PhysPt addr);
template void DEBUG_UpdateMemoryReadBreakpoints<uint16_t>(const PhysPt addr);
template void DEBUG_UpdateMemoryReadBreakpoints<uint32_t>(const PhysPt addr);
template void DEBUG_UpdateMemoryReadBreakpoints<uint64_t>(const PhysPt addr);
#endif

bool DEBUG_Breakpoint(void)
{
	/* First get the physical address and check for a set Breakpoint */
	if (!CBreakpoint::CheckBreakpoint(SegValue(cs), reg_eip)) {
		return false;
	}
	// Found. Breakpoint is valid
	// PhysPt where=GetAddress(SegValue(cs),reg_eip); -- "where" is unused
	CBreakpoint::DeactivateBreakpoints(); // Deactivate all breakpoints
	return true;
}

bool DEBUG_IntBreakpoint(uint8_t intNum)
{
	/* First get the physical address and check for a set Breakpoint */
	PhysPt where = GetAddress(SegValue(cs), reg_eip);
	if (!CBreakpoint::CheckIntBreakpoint(where, intNum, reg_ah, reg_al)) {
		return false;
	}
	// Found. Breakpoint is valid
	CBreakpoint::DeactivateBreakpoints(); // Deactivate all breakpoints
	return true;
}

bool DEBUG_ExitLoop(void)
{
#if C_HEAVY_DEBUGGER
	DrawVariables();
#endif

	if (exitLoop) {
		exitLoop = false;
		return true;
	}
	return false;
}

void SetCodeWinStart()
{
	if ((SegValue(cs) == codeViewData.useCS) && (reg_eip >= codeViewData.useEIP) &&
	    (reg_eip <= codeViewData.useEIPlast)) {
		// in valid window - scroll ?
		if (reg_eip >= codeViewData.useEIPmid) {
			codeViewData.useEIP += codeViewData.firstInstSize;
		}

	} else {
		// totally out of range.
		codeViewData.useCS  = SegValue(cs);
		codeViewData.useEIP = codeViewData.goodEIP = reg_eip;
	}
	codeViewData.cursorPos = -1; // Recalc Cursor position
}

int32_t DEBUG_Run(int32_t amount, bool quickexit)
{
	skipFirstInstruction = true;
	CPU_CycleLeft += CPU_Cycles - amount;
	CPU_Cycles  = amount;
	int32_t ret = (*cpudecoder)();
	if (quickexit) {
		SetCodeWinStart();
	} else {
		// ensure all breakpoints are activated
		CBreakpoint::ActivateBreakpoints();

		const auto graphics_window = GFX_GetWindow();
		SDL_RaiseWindow(graphics_window);
		SDL_SetWindowInputFocus(graphics_window);

		DOSBOX_SetNormalLoop();
	}
	return ret;
}

Bitu DEBUG_Loop(void)
{
	// TODO Disable sound
	GFX_PollAndHandleEvents();
	// Interrupt started ? - then skip it
	uint16_t oldCS  = SegValue(cs);
	uint32_t oldEIP = reg_eip;
	PIC_runIRQs();
	Delay(1);
	if ((oldCS != SegValue(cs)) || (oldEIP != reg_eip)) {
		CBreakpoint::AddBreakpoint(oldCS, oldEIP, true);
		CBreakpoint::ActivateBreakpointsExceptAt(SegPhys(cs) + reg_eip);
		debugging = false;
		DOSBOX_SetNormalLoop();
		return 0;
	}
	return DEBUG_CheckKeys();
}

#include <queue>
extern SDL_Window* pdc_window;
extern std::queue<SDL_Event> pdc_event_queue;

void DEBUG_Enable(bool pressed)
{
	if (!pressed) {
		return;
	}

	// Maybe construct the debugger's UI
	static bool was_ui_started = false;
	if (!was_ui_started) {
		DBGUI_StartUp();
		was_ui_started = (pdc_window != nullptr);
	} else {
		SDL_ShowWindow(pdc_window);
		DEBUG_RefreshLayout();
	}

	// The debugger is run in release mode so cannot use asserts
	if (!was_ui_started) {
		LOG_ERR("DEBUG: Failed to start up the debug window");
		return;
	}

	// Defocus the graphical UI and bring the debugger UI into focus
	GFX_LosingFocus();
	pdc_event_queue = {};
	SDL_RaiseWindow(pdc_window);
	SDL_SetWindowInputFocus(pdc_window);
	SetCodeWinStart();
	debugging = true;
	DEBUG_DrawScreen();

	// Maybe show help for the first time in the debugger
	static bool was_help_shown = false;
	if (!was_help_shown) {
		DEBUG_ShowMsg("           TYPE ? or HELP (+ENTER) TO GET AN OVERVIEW OF ALL COMMANDS           \n");
		was_help_shown = true;
	}

	mouse_on(MOUSE_WHEEL_SCROLL | BUTTON1_RELEASED);
	// Start the debugging loops
	DOSBOX_SetLoop(&DEBUG_Loop);

	KEYBOARD_ClrBuffer();
}

void DEBUG_Close() {
	SDL_HideWindow(pdc_window);
	debugging = false;
	DOSBOX_SetNormalLoop();
}

class DEBUG;
DEBUG* pDebugcom = nullptr;

// DEBUG.COM stuff
class DEBUG final : public Program {
public:
	DEBUG() : active(false)
	{
		pDebugcom = this;
	}

	~DEBUG() override
	{
		pDebugcom = nullptr;
	}

	bool IsActive() const
	{
		return active;
	}

	void Run() override
	{
		if (cmd->FindExist("/NOMOUSE", false)) {
			real_writed(0, 0x33 << 2, 0);
			return;
		}

		uint16_t commandNr = 1;
		if (!cmd->FindCommand(commandNr++, temp_line)) {
			return;
		}
		// Get filename
		char filename[128];
		safe_strcpy(filename, temp_line.c_str());
		// Setup commandline
		char args[256 + 1];
		args[0]    = 0;
		bool found = cmd->FindCommand(commandNr++, temp_line);
		while (found) {
			if (safe_strlen(args) + temp_line.length() + 1 > 256) {
				break;
			}
			strcat(args, temp_line.c_str());
			found = cmd->FindCommand(commandNr++, temp_line);
			if (found) {
				strcat(args, " ");
			}
		}
		// Start new shell and execute prog
		active = true;
		// Save cpu state....
		uint16_t oldcs  = SegValue(cs);
		uint32_t oldeip = reg_eip;
		uint16_t oldss  = SegValue(ss);
		uint32_t oldesp = reg_esp;

		// Start shell
		DOS_Shell shell;
		if (!shell.ExecuteProgram(filename, args)) {
			WriteOut(MSG_Get("PROGRAM_EXECUTABLE_MISSING"), filename);
		}

		// set old reg values
		SegSet16(ss, oldss);
		reg_esp = oldesp;
		SegSet16(cs, oldcs);
		reg_eip = oldeip;
	}

private:
	bool active;
};

void DEBUG_CheckExecuteBreakpoint(uint16_t seg, uint32_t off)
{
	if (pDebugcom && pDebugcom->IsActive()) {
		CBreakpoint::AddBreakpoint(seg, off, true);
		CBreakpoint::ActivateBreakpointsExceptAt(SegPhys(cs) + reg_eip);
		pDebugcom = nullptr;
	}
}

Bitu DEBUG_EnableDebugger()
{
	exitLoop = true;
	DEBUG_Enable(true);
	CPU_Cycles = CPU_CycleLeft = 0;
	return 0;
}

Bitu debugCallback;

void DEBUG_Init()
{
	// Add some keyhandlers
	MAPPER_AddHandler(DEBUG_Enable, SDL_SCANCODE_PAUSE, MMOD2, "debugger", "Debugger");

	// Reset code overview and input line
	codeViewData = {};

	// setup debug.com
	PROGRAMS_MakeFile("DEBUG.COM", ProgramCreate<DEBUG>);
	PROGRAMS_MakeFile("DBXDEBUG.COM", ProgramCreate<DEBUG>);

	// Setup callback
	debugCallback = CALLBACK_Allocate();

	CALLBACK_Setup(debugCallback, DEBUG_EnableDebugger, CB_RETF, "debugger");
}

void DEBUG_Destroy()
{
	CBreakpoint::DeleteAll();
	CDebugVar::DeleteAll();

	curs_set(old_cursor_state);

	if (pdc_window) {
		endwin();
	}
}

void DEBUG_AddConfigSection(const ConfigPtr& conf)
{
	assert(conf);

	// TODO the [debug] section has no settings, so what's the point?
	conf->AddSection("debug");
}

// DEBUGGING VAR STUFF

void CDebugVar::InsertVariable(char* name, PhysPt adr)
{
	varList.push_back(new CDebugVar(name, adr));
}

void CDebugVar::DeleteAll()
{
	std::vector<CDebugVar*>::iterator i;
	CDebugVar* bp;
	for (i = varList.begin(); i != varList.end(); i++) {
		bp = static_cast<CDebugVar*>(*i);
		delete bp;
	}
	(varList.clear)();
}

CDebugVar* CDebugVar::FindVar(PhysPt pt)
{
	if (varList.empty()) {
		return nullptr;
	}

	std::vector<CDebugVar*>::size_type s = varList.size();
	CDebugVar* bp;
	for (std::vector<CDebugVar*>::size_type i = 0; i != s; i++) {
		bp = static_cast<CDebugVar*>(varList[i]);
		if (bp->GetAdr() == pt) {
			return bp;
		}
	}
	return nullptr;
}

bool CDebugVar::SaveVars(char* name)
{
	if (varList.size() > 65535) {
		return false;
	}
	const std_fs::path vars_file = name;
	FILE* f                      = fopen(vars_file.string().c_str(), "wb+");
	if (!f) {
		DEBUG_ShowMsg("DEBUG: Output of vars failed.\n");
		return false;
	}
	DEBUG_ShowMsg("DEBUG: vars file '%s' created.\n",
	              std_fs::absolute(vars_file).string().c_str());

	// write number of vars
	auto num = (uint16_t)varList.size();
	fwrite(&num, 1, sizeof(num), f);

	std::vector<CDebugVar*>::iterator i;
	CDebugVar* bp;
	for (i = varList.begin(); i != varList.end(); i++) {
		bp = static_cast<CDebugVar*>(*i);
		// name
		fwrite(bp->GetName(), 1, 16, f);
		// adr
		PhysPt adr = bp->GetAdr();
		fwrite(&adr, 1, sizeof(adr), f);
	}
	fclose(f);
	return true;
}

bool CDebugVar::LoadVars(char* name)
{
	const std_fs::path vars_file = name;
	FILE* f                      = fopen(vars_file.string().c_str(), "rb");
	if (!f) {
		DEBUG_ShowMsg("DEBUG: Load of vars from %s failed.\n", name);
		return false;
	}
	DEBUG_ShowMsg("DEBUG: vars file '%s' loaded.\n",
	              std_fs::absolute(vars_file).string().c_str());
	// read number of vars
	uint16_t num;
	if (fread(&num, sizeof(num), 1, f) != 1) {
		fclose(f);
		return false;
	}
	for (uint16_t i = 0; i < num; i++) {
		char name[16];
		// name
		if (fread(name, 16, 1, f) != 1) {
			break;
		}
		// adr
		PhysPt adr;
		if (fread(&adr, sizeof(adr), 1, f) != 1) {
			break;
		}
		// insert
		InsertVariable(name, adr);
	}
	fclose(f);
	return true;
}
#endif // C_DEBUGGER
