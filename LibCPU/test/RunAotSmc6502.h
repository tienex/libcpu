/** @file
  Self-modifying-code demonstration + test (AOT, LLVM).

  The program overwrites the OPERAND of a later instruction before executing it:

    $0000  A9 EE   LDA #$EE    ; A = $EE
    $0002  85 07   STA $07     ; RAM[$07] = $EE  -- this is the ADC operand below
    $0004  A9 01   LDA #$01    ; A = 1
    $0006  69 03   ADC #$03    ; operand byte lives at $07; originally $03
    $0008  85 10   STA $10     ; M[$10] = A

  The ADC operand is read from address $07 at TRANSLATION time. Plain AOT bakes the
  original $03, so it computes 1 + $03 = $04 -- WRONG. With SMC handling, the store
  to $07 dirties the code region, the guard at the next block traps, and the host
  re-translates from there against live RAM, so ADC sees the new $EE: 1 + $EE = $EF.

  This runs the program both ways and requires: no-SMC => $04 (bug reproduced),
  SMC => $EF (fixed). Needs a backend exposing ICpuSmcEmitter (LLVM).
**/
#ifndef LIBCPU_RUNAOTSMC6502_H
#define LIBCPU_RUNAOTSMC6502_H

#include "Cpu6502.h"
#include "LibCPU/CpuState.h"
#include "LibCPU/PCom.h"
#include "../aot/AotGenerator.h"
#include <cstdio>
#include <cstring>

namespace LibCPU {

static inline UINT8
AotSmcRun (ICpuBackend *pBackend, BOOLEAN HandleSmc, int *pTranslations)
{
    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    UINT8 const Program[] = {
        0xA9, 0xEE,  0x85, 0x07,  0xA9, 0x01,  0x69, 0x03,  0x85, 0x10
    };
    UINT32 ProgLen = (UINT32) sizeof (Program);
    std::memcpy (Ram, Program, ProgLen);

    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));

    ICpuArchitecture *pArch = Create6502 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    // Arm the watched code region only in the SMC case; otherwise the guards the
    // driver plants stay inert (CodeStart == CodeEnd) and stale code runs.
    if (HandleSmc) {
        State.CodeStart = 0;
        State.CodeEnd   = ProgLen;
    }

    CPU_ADDR Resume       = 0;
    int      Translations = 0;
    for (int Iter = 0; Iter < 64; Iter++) {
        ComPtr<ICpuCode> Code;
        UINT32 Count = 0;
        if (FAILED (GenerateAotCfg (pArch, pBackend, Resume, (CPU_ADDR) ProgLen, &Code, &Count)) || Code == nullptr) {
            pArch->Release ();
            *pTranslations = Translations;
            return 0xFF;
        }
        Translations++;

        State.TrapPc    = CPU_SMC_NO_TRAP;
        State.CodeDirty = 0;   // re-translation already reflects writes so far
        Code->Execute (Ram, &State, nullptr);

        if (!HandleSmc || State.TrapPc == CPU_SMC_NO_TRAP) {
            break;             // no guard armed, or program ran to completion
        }
        Resume = State.TrapPc; // re-translate from the trapped block against live RAM
    }

    pArch->Release ();
    *pTranslations = Translations;
    return Ram[0x10];
}

static inline int
RunAotSmc6502Program (ICpuBackend *pBackend)
{
    std::printf ("backend = '%s'  (self-modifying code: store overwrites a later ADC operand)\n",
                 pBackend->GetName ());

    int NoSmcN = 0, SmcN = 0;
    UINT8 NoSmc = AotSmcRun (pBackend, FALSE, &NoSmcN);
    UINT8 Smc   = AotSmcRun (pBackend, TRUE,  &SmcN);

    std::printf ("  without SMC handling: M[$10]=$%02x  (ADC used the STALE operand $03 -> 1+$03=$04)\n", NoSmc);
    std::printf ("  with SMC handling:    M[$10]=$%02x  (%d translations; ADC saw the NEW operand $EE -> 1+$EE=$EF)\n", Smc, SmcN);

    bool Ok = (NoSmc == 0x04) && (Smc == 0xEF);
    std::printf ("RESULT: %s  (bug reproduced: $%02x==$04; SMC fix: $%02x==$EF)\n",
                 Ok ? "PASS" : "FAIL", NoSmc, Smc);
    return Ok ? 0 : 1;
}

} // namespace LibCPU

#endif // LIBCPU_RUNAOTSMC6502_H
