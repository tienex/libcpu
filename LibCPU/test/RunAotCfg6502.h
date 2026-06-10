/** @file
  Control-flow AOT test: a 6502 program with a real conditional branch, translated
  into a CFG (GenerateAotCfg -- one block per instruction, wired with CondBranch)
  and compiled once. Requires a backend implementing the ICpuEmitter block ops
  (LLVM). The branch is taken at run time, so the skipped instruction must NOT run.

    $0000  A9 05   LDA #$05    ; A = 5, Z = 0
    $0002  D0 02   BNE $0006   ; Z==0 -> taken: skip the next instruction
    $0004  A9 FF   LDA #$FF    ; (must be skipped)
    $0006  85 10   STA $10     ; M[$10] = A

  Correct CFG + condition => A = $05, M[$10] = $05. A broken branch (not taken, or
  inverted) would leave A = $FF.
**/
#ifndef LIBCPU_RUNAOTCFG6502_H
#define LIBCPU_RUNAOTCFG6502_H

#include "Cpu6502.h"
#include "LibCPU/CpuState.h"
#include "LibCPU/PCom.h"
#include "../aot/AotGenerator.h"
#include <cstdio>
#include <cstring>

namespace LibCPU {

static inline int
RunAotCfg6502Program (ICpuBackend *pBackend)
{
    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));

    UINT8 const Program[] = {
        0xA9, 0x05,  0xD0, 0x02,  0xA9, 0xFF,  0x85, 0x10
    };
    std::memcpy (Ram, Program, sizeof (Program));

    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));

    ICpuArchitecture *pArch = Create6502 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));
    std::printf ("backend = '%s'  (AOT-CFG: conditional branch)\n", pBackend->GetName ());

    ComPtr<ICpuCode> Code;
    UINT32 Count = 0;
    HRESULT hr = GenerateAotCfg (pArch, pBackend, 0, (CPU_ADDR) sizeof (Program), &Code, &Count);
    if (FAILED (hr) || Code == nullptr) {
        std::printf ("AOT-CFG generation failed (hr=0x%lx) -- backend may not implement block ops\n",
                     (unsigned long) hr);
        pArch->Release ();
        return 1;
    }
    std::printf ("AOT-CFG: %u instruction blocks wired with branches -> 1 artifact; running once...\n", Count);

    Code->Execute (Ram, &State, nullptr);

    bool Ok = (State.Reg[Reg6502A] & 0xff) == 0x05 && Ram[0x10] == 0x05;
    std::printf ("RESULT: %s  (A=$%02llx exp $05 [branch taken, LDA #$FF skipped], M[$10]=$%02x exp $05)\n",
                 Ok ? "PASS" : "FAIL",
                 (unsigned long long)(State.Reg[Reg6502A] & 0xff), Ram[0x10]);

    pArch->Release ();
    return Ok ? 0 : 1;
}

} // namespace LibCPU

#endif // LIBCPU_RUNAOTCFG6502_H
