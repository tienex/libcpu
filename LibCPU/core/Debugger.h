/** @file
  A single-step debugger for LibCPU guests.

  It drives one guest instruction at a time through the frontend + a backend (the
  interpreter, which also exposes the translated-code listing and the syscall trap),
  maintaining the guest PC, breakpoints, and the live CPU_STATE. It offers gdb-like
  commands -- step / continue / break / info registers / x (examine) / disassemble --
  plus a "tdis" that shows the TRANSLATED form of an instruction beside the guest
  ("real") disassembly. Input comes through LcLineEditor, so an interactive session
  gets colouring + completion and a piped script gets plain batch execution.

  Register and flag names are supplied by the caller (which knows the frontend), so
  the debugger needs no new frontend interface.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_DEBUGGER_H
#define LIBCPU_DEBUGGER_H

#include "LibCPU/ICpu.h"
#include "LibCPU/CpuState.h"
#include "LineEditor.h"
#include <string>
#include <vector>
#include <set>

namespace LibCPU {

class LcDebugger {
public:
    LcDebugger (ICpuArchitecture *pArch, ICpuBackend *pBackend,
                UINT8 *pRAM, UINT64 RamSize, CPU_STATE *pState, CPU_ADDR Entry, CPU_ADDR End,
                std::vector<std::string> RegNames, UINT32 RegBytes,
                std::vector<std::string> FlagNames);

    // Run the read/eval/print loop until "quit" or end of input. Returns 0.
    int Repl ();

private:
    // Execute exactly one guest instruction; returns the next PC and may set m_Halted.
    CPU_ADDR StepOne ();
    bool     EvalCond (CPU_ADDR Pc);

    // Commands.
    void CmdStep (UINT32 Count);
    void CmdContinue ();
    void CmdBreak (std::string CONST &Arg);
    void CmdInfo (std::string CONST &What);
    void CmdRegisters ();
    void CmdDisas (std::string CONST &Addr, std::string CONST &Count);
    void CmdTransDisas (std::string CONST &Addr);
    void CmdExamine (std::string CONST &Addr, std::string CONST &Count);
    void CmdReset ();
    void CmdHelp ();

    void ShowLocation ();                              // current PC + its disassembly
    std::string ResolveCommand (std::string CONST &Token) CONST;
    bool ParseAddr (std::string CONST &S, CPU_ADDR *pOut) CONST;

    ICpuArchitecture *m_pArch;
    ICpuBackend      *m_pBackend;
    UINT8            *m_pRAM;
    UINT64            m_RamSize;
    CPU_STATE        *m_pState;
    CPU_ADDR          m_Entry;
    CPU_ADDR          m_End;
    CPU_ADDR          m_Pc;
    bool              m_Halted;

    std::vector<std::string> m_RegNames;
    UINT32                   m_RegBytes;
    std::vector<std::string> m_FlagNames;
    std::set<CPU_ADDR>       m_Breaks;
};

} // namespace LibCPU

#endif // LIBCPU_DEBUGGER_H
