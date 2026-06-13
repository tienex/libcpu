/** @file  Single-step debugger implementation. See Debugger.h. */

#include "Debugger.h"
#include "LibCPU/PCom.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace LibCPU {

// Canonical command set (LineEditor colours + completes these; ResolveCommand
// also accepts any unambiguous prefix and a few one-letter aliases).
static std::vector<std::string>
CommandList ()
{
    return { "step", "continue", "break", "delete", "info", "registers",
             "disassemble", "tdis", "examine", "reset", "help", "quit" };
}

static std::vector<std::string>
Split (std::string CONST &Line)
{
    std::vector<std::string> Out;
    size_t I = 0;
    while (I < Line.size ()) {
        while (I < Line.size () && Line[I] == ' ') { I++; }
        size_t J = I;
        while (J < Line.size () && Line[J] != ' ') { J++; }
        if (J > I) { Out.push_back (Line.substr (I, J - I)); }
        I = J;
    }
    return Out;
}

Debugger::Debugger (ICpuArchitecture *pArch, ICpuBackend *pBackend,
                        UINT8 *pRAM, UINT64 RamSize, CPU_STATE *pState, CPU_ADDR Entry, CPU_ADDR End,
                        std::vector<std::string> RegNames, UINT32 RegBytes,
                        std::vector<std::string> FlagNames)
    : m_pArch (pArch), m_pBackend (pBackend), m_pRAM (pRAM), m_RamSize (RamSize),
      m_pState (pState), m_Entry (Entry), m_End (End), m_Pc (Entry), m_Halted (false),
      m_RegNames (std::move (RegNames)), m_RegBytes (RegBytes), m_FlagNames (std::move (FlagNames))
{
}

std::string
Debugger::ResolveCommand (std::string CONST &Tok) CONST
{
    static struct { CHAR8 CONST *a; CHAR8 CONST *c; } const Alias[] = {
        { "s", "step" }, { "c", "continue" }, { "b", "break" }, { "x", "examine" },
        { "q", "quit" }, { "r", "registers" }, { "i", "info" }, { "h", "help" }, { "?", "help" }
    };
    for (auto CONST &A : Alias) {
        if (Tok == A.a) { return A.c; }
    }
    std::string Match;
    int N = 0;
    for (std::string CONST &C : CommandList ()) {
        if (C == Tok) { return C; }
        if (C.size () > Tok.size () && C.compare (0, Tok.size (), Tok) == 0) {
            Match = C;
            N++;
        }
    }
    return (N == 1) ? Match : std::string ();
}

bool
Debugger::ParseAddr (std::string CONST &S, CPU_ADDR *pOut) CONST
{
    std::string T = S;
    if (!T.empty () && T[0] == '*') { T = T.substr (1); }   // gdb-style *addr
    if (T.empty ()) { return false; }
    char *pEnd = nullptr;
    unsigned long long V = std::strtoull (T.c_str (), &pEnd, 0);
    if (pEnd == T.c_str ()) { return false; }
    *pOut = (CPU_ADDR) V;
    return true;
}

// --- single-step engine ----------------------------------------------------

bool
Debugger::EvalCond (CPU_ADDR Pc)
{
    ComPtr<ICpuEmitter> Em;
    if (FAILED (m_pBackend->CreateEmitter (m_pArch, &Em)) || Em == nullptr) {
        return false;
    }
    ComPtr<ICpuValue> Cond;
    if (FAILED (m_pArch->TranslateCond (Pc, Em, &Cond)) || Cond == nullptr) {
        return false;
    }
    CPU_ARCH_INFO Info;
    std::memset (&Info, 0, sizeof (Info));
    m_pArch->GetInfo (&Info);
    UINT32   AddrBits = Info.AddressSize ? Info.AddressSize : 32;
    CPU_ADDR Scratch  = (CPU_ADDR) (m_RamSize - 1);   // top byte, restored after
    ComPtr<ICpuValue> Addr;
    Em->ConstInt (AddrBits, Scratch, &Addr);
    Em->Store (Cond, Addr, 8);
    ComPtr<ICpuCode> Code;
    if (FAILED (m_pBackend->Compile (Em, &Code)) || Code == nullptr) {
        return false;
    }
    UINT8 Save = m_pRAM[Scratch];
    Code->Execute (m_pRAM, m_pState, nullptr);
    bool Taken = m_pRAM[Scratch] != 0;
    m_pRAM[Scratch] = Save;
    return Taken;
}

CPU_ADDR
Debugger::StepOne ()
{
    UINT32   Tag = 0;
    CPU_ADDR NewPc = 0, NextPc = 0;
    if (FAILED (m_pArch->TagInstr (m_Pc, &Tag, &NewPc, &NextPc)) || NextPc <= m_Pc) {
        m_Halted = true;
        return m_Pc;
    }
    // Execute just this instruction's data effect.
    ComPtr<ICpuEmitter> Em;
    if (FAILED (m_pBackend->CreateEmitter (m_pArch, &Em)) || Em == nullptr) {
        m_Halted = true;
        return m_Pc;
    }
    m_pArch->TranslateInstr (m_Pc, Em);
    ComPtr<ICpuCode> Code;
    if (FAILED (m_pBackend->Compile (Em, &Code)) || Code == nullptr) {
        m_Halted = true;
        return m_Pc;
    }
    m_pState->TrapPc        = CPU_SMC_NO_TRAP;
    m_pState->SyscallVector = CPU_NO_SYSCALL;
    m_pState->DispPc        = 0;
    Code->Execute (m_pRAM, m_pState, nullptr);

    if (Tag & TagReturn) {
        return (CPU_ADDR) m_pState->DispPc;            // RET: dispatcher target
    }
    if (Tag & TagTrap) {
        if (m_pState->SyscallVector != CPU_NO_SYSCALL) {
            std::printf ("    (syscall 0x%llx -- not dispatched in debugger; stepping over)\n",
                         (unsigned long long) m_pState->SyscallVector);
        }
        return (CPU_ADDR) m_pState->TrapPc;            // far jump / INT target/return
    }
    if (Tag & TagConditional) {
        return EvalCond (m_Pc) ? NewPc : NextPc;
    }
    if (Tag & (TagBranch | TagCall)) {
        return NewPc;
    }
    return NextPc;
}

// --- output helpers --------------------------------------------------------

void
Debugger::ShowLocation ()
{
    char Line[64];
    m_pArch->Disassemble (m_Pc, Line, sizeof (Line));
    std::printf ("=> $%04llx:  %s\n", (unsigned long long) m_Pc, Line);
}

// --- commands --------------------------------------------------------------

void
Debugger::CmdStep (UINT32 Count)
{
    for (UINT32 I = 0; I < Count && !m_Halted; I++) {
        char Line[64];
        m_pArch->Disassemble (m_Pc, Line, sizeof (Line));
        CPU_ADDR Was = m_Pc;
        m_Pc = StepOne ();
        std::printf ("   $%04llx:  %-22s -> $%04llx\n",
                     (unsigned long long) Was, Line, (unsigned long long) m_Pc);
    }
    if (!m_Halted) { ShowLocation (); }
}

void
Debugger::CmdContinue ()
{
    UINT32 Steps = 0;
    while (!m_Halted && m_Pc < m_End && Steps < 2000000u) {
        m_Pc = StepOne ();
        Steps++;
        if (m_Breaks.count (m_Pc)) {
            std::printf ("breakpoint hit at $%04llx (%u step%s)\n",
                         (unsigned long long) m_Pc, Steps, Steps == 1 ? "" : "s");
            ShowLocation ();
            return;
        }
    }
    std::printf ("stopped at $%04llx after %u step%s%s\n",
                 (unsigned long long) m_Pc, Steps, Steps == 1 ? "" : "s",
                 m_Pc >= m_End ? " (reached end of program)" : "");
}

void
Debugger::CmdBreak (std::string CONST &Arg)
{
    CPU_ADDR Addr = 0;
    if (!ParseAddr (Arg, &Addr)) {
        std::printf ("usage: break <addr>\n");
        return;
    }
    m_Breaks.insert (Addr);
    std::printf ("breakpoint set at $%04llx\n", (unsigned long long) Addr);
}

void
Debugger::CmdRegisters ()
{
    UINT64 Mask = (m_RegBytes >= 8) ? ~UINT64_C (0) : ((UINT64_C (1) << (m_RegBytes * 8)) - 1);
    int Width = (int) (m_RegBytes * 2);
    int Col = 0;
    for (UINT32 I = 0; I < m_RegNames.size (); I++) {
        std::printf ("%-4s=%0*llx  ", m_RegNames[I].c_str (), Width,
                     (unsigned long long) (m_pState->Reg[I] & Mask));
        if (++Col % 4 == 0) { std::printf ("\n"); }
    }
    if (Col % 4 != 0) { std::printf ("\n"); }
    if (!m_FlagNames.empty ()) {
        std::printf ("flags: ");
        for (UINT32 I = 0; I < m_FlagNames.size (); I++) {
            std::printf ("%s=%d ", m_FlagNames[I].c_str (), m_pState->Flag[I] & 1);
        }
        std::printf ("\n");
    }
    std::printf ("pc=$%04llx\n", (unsigned long long) m_Pc);
}

void
Debugger::CmdInfo (std::string CONST &What)
{
    std::string W = What.empty () ? "registers" : What;
    if (std::string ("registers").compare (0, W.size (), W) == 0 || W == "r" || W == "reg") {
        CmdRegisters ();
    } else if (std::string ("breakpoints").compare (0, W.size (), W) == 0 || W == "b") {
        if (m_Breaks.empty ()) {
            std::printf ("no breakpoints\n");
        } else {
            for (CPU_ADDR B : m_Breaks) {
                std::printf ("  breakpoint at $%04llx\n", (unsigned long long) B);
            }
        }
    } else {
        std::printf ("info: registers | breakpoints\n");
    }
}

void
Debugger::CmdDisas (std::string CONST &AddrArg, std::string CONST &CountArg)
{
    CPU_ADDR Addr = m_Pc;
    if (!AddrArg.empty ()) { ParseAddr (AddrArg, &Addr); }
    UINT32 Count = CountArg.empty () ? 8 : (UINT32) std::strtoul (CountArg.c_str (), nullptr, 0);
    for (UINT32 I = 0; I < Count; I++) {
        char Line[64];
        m_pArch->Disassemble (Addr, Line, sizeof (Line));
        UINT32   Tag = 0;
        CPU_ADDR NewPc = 0, NextPc = 0;
        if (FAILED (m_pArch->TagInstr (Addr, &Tag, &NewPc, &NextPc)) || NextPc <= Addr) {
            break;
        }
        std::printf ("%s $%04llx:  %s\n", Addr == m_Pc ? "=>" : "  ",
                     (unsigned long long) Addr, Line);
        Addr = NextPc;
    }
}

void
Debugger::CmdTransDisas (std::string CONST &AddrArg)
{
    CPU_ADDR Addr = m_Pc;
    if (!AddrArg.empty ()) { ParseAddr (AddrArg, &Addr); }

    char Guest[64];
    m_pArch->Disassemble (Addr, Guest, sizeof (Guest));
    std::printf ("guest  $%04llx:  %s\n", (unsigned long long) Addr, Guest);

    ComPtr<ICpuEmitter> Em;
    if (FAILED (m_pBackend->CreateEmitter (m_pArch, &Em)) || Em == nullptr) {
        std::printf ("  (backend has no emitter)\n");
        return;
    }
    m_pArch->TranslateInstr (Addr, Em);
    ComPtr<ICpuCode> Code;
    if (FAILED (m_pBackend->Compile (Em, &Code)) || Code == nullptr) {
        std::printf ("  (translation failed)\n");
        return;
    }
    ICpuCodeListing *pList = nullptr;
    if (FAILED (Code->QueryInterface (IID_ICpuCodeListing, (VOID **) &pList)) || pList == nullptr) {
        std::printf ("  (backend '%s' does not expose a translated listing)\n", m_pBackend->GetName ());
        return;
    }
    UINT32 Needed = 0;
    pList->GetListing (nullptr, 0, &Needed);
    std::vector<char> Buf (Needed + 1);
    pList->GetListing (Buf.data (), (UINT32) Buf.size (), nullptr);
    pList->Release ();
    std::printf ("translated (%s):\n%s", m_pBackend->GetName (), Buf.data ());
}

void
Debugger::CmdExamine (std::string CONST &AddrArg, std::string CONST &CountArg)
{
    CPU_ADDR Addr = 0;
    if (!ParseAddr (AddrArg, &Addr)) {
        std::printf ("usage: x <addr> [count]\n");
        return;
    }
    UINT32 Count = CountArg.empty () ? 16 : (UINT32) std::strtoul (CountArg.c_str (), nullptr, 0);
    for (UINT32 Row = 0; Row < Count; Row += 16) {
        std::printf ("$%04llx: ", (unsigned long long) (Addr + Row));
        std::string Ascii;
        for (UINT32 I = 0; I < 16 && Row + I < Count; I++) {
            CPU_ADDR A = Addr + Row + I;
            UINT8 B = (A < m_RamSize) ? m_pRAM[A] : 0;
            std::printf ("%02x ", B);
            Ascii.push_back ((B >= 32 && B < 127) ? (char) B : '.');
        }
        std::printf (" %s\n", Ascii.c_str ());
    }
}

void
Debugger::CmdReset ()
{
    std::memset (m_pState, 0, sizeof (*m_pState));
    m_pState->RamSize = m_RamSize;
    m_Pc = m_Entry;
    m_Halted = false;
    std::printf ("state reset; pc = $%04llx\n", (unsigned long long) m_Entry);
}

void
Debugger::CmdHelp ()
{
    std::printf (
        "commands (any unambiguous prefix works; aliases s c b x q r i h):\n"
        "  step [n]            execute n guest instructions (default 1)\n"
        "  continue            run until a breakpoint or the end of the program\n"
        "  break <addr>        set a breakpoint\n"
        "  info registers|breakpoints\n"
        "  registers           dump registers and flags\n"
        "  disassemble [a] [n] guest (\"real\") disassembly\n"
        "  tdis [addr]         translated-code disassembly (guest + backend op list)\n"
        "  examine <addr> [n]  hex/ASCII dump of guest memory\n"
        "  reset               reset CPU state to the entry point\n"
        "  quit\n");
}

int
Debugger::Repl ()
{
    LineEditor Editor (CommandList ());
    std::printf ("LibCPU debugger -- 'help' for commands, 'quit' to exit.\n");
    ShowLocation ();

    std::string Raw;
    while (Editor.ReadLine ("(lcx) ", Raw)) {
        std::vector<std::string> Tok = Split (Raw);
        if (Tok.empty ()) { continue; }
        std::string Cmd = ResolveCommand (Tok[0]);
        std::string A1 = Tok.size () > 1 ? Tok[1] : std::string ();
        std::string A2 = Tok.size () > 2 ? Tok[2] : std::string ();
        if (Cmd.empty ()) {
            std::printf ("undefined command: \"%s\" (try 'help')\n", Tok[0].c_str ());
        } else if (Cmd == "quit") {
            break;
        } else if (Cmd == "step") {
            CmdStep (A1.empty () ? 1 : (UINT32) std::strtoul (A1.c_str (), nullptr, 0));
        } else if (Cmd == "continue") {
            CmdContinue ();
        } else if (Cmd == "break") {
            CmdBreak (A1);
        } else if (Cmd == "delete") {
            CPU_ADDR Addr = 0;
            if (ParseAddr (A1, &Addr)) { m_Breaks.erase (Addr); std::printf ("deleted $%04llx\n", (unsigned long long) Addr); }
        } else if (Cmd == "info") {
            CmdInfo (A1);
        } else if (Cmd == "registers") {
            CmdRegisters ();
        } else if (Cmd == "disassemble") {
            CmdDisas (A1, A2);
        } else if (Cmd == "tdis") {
            CmdTransDisas (A1);
        } else if (Cmd == "examine") {
            CmdExamine (A1, A2);
        } else if (Cmd == "reset") {
            CmdReset ();
        } else if (Cmd == "help") {
            CmdHelp ();
        }
    }
    return 0;
}

} // namespace LibCPU
