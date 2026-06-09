/** @file
  End-to-end decoupling proof: single-step a real 6502 program through the
  6502 frontend and the interpreter backend, with NO LLVM linked at all.

  Program:
      A9 05   LDA #$05      ; A = 5
      85 10   STA $10       ; M[$10] = 5
      E6 10   INC $10       ; M[$10] = 6
      69 03   ADC #$03      ; A = 5 + 3 + C(0) = 8, C=0, V=0
      85 11   STA $11       ; M[$11] = 8
**/
#include "Cpu6502.h"
#include "Interp.h"
#include <cstdio>
#include <cstring>

using namespace LibCPU;

int main (void) {
    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));

    UINT8 const Program[] = {
        0xA9, 0x05,   // LDA #$05
        0x85, 0x10,   // STA $10
        0xE6, 0x10,   // INC $10
        0x69, 0x03,   // ADC #$03
        0x85, 0x11    // STA $11
    };
    std::memcpy (Ram, Program, sizeof (Program));

    INTERP_STATE State;
    std::memset (&State, 0, sizeof (State));

    ICpuBackend      *pBackend = CreateInterpreterBackend ();
    ICpuArchitecture *pArch    = Create6502 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    std::printf ("backend = '%s'\n", pBackend->GetName ());

    CPU_ADDR Pc  = 0;
    CPU_ADDR End = (CPU_ADDR) sizeof (Program);
    while (Pc < End) {
        char Line[32];
        pArch->Disassemble (Pc, Line, sizeof (Line));

        // Translate this single instruction and run it (single-step).
        ComPtr<ICpuEmitter> Emitter;
        pBackend->CreateEmitter (pArch, &Emitter);
        pArch->TranslateInstr (Pc, Emitter);

        ComPtr<ICpuCode> Code;
        pBackend->Compile (Emitter, &Code);
        Code->Execute (Ram, &State, nullptr);

        UINT32   Tag; CPU_ADDR NewPc, NextPc;
        pArch->TagInstr (Pc, &Tag, &NewPc, &NextPc);
        std::printf ("  $%04llx: %-12s  A=$%02llx  M[$10]=$%02x M[$11]=$%02x  NZVC=%d%d%d%d\n",
                     (unsigned long long) Pc, Line,
                     (unsigned long long)(State.Reg[Reg6502A] & 0xff),
                     Ram[0x10], Ram[0x11],
                     State.Flag[FlagNegative], State.Flag[FlagZero],
                     State.Flag[FlagOverflow], State.Flag[FlagCarry]);
        Pc = NextPc;
    }

    // Verify expected final state.
    bool Ok = (State.Reg[Reg6502A] & 0xff) == 0x08 &&
              Ram[0x10] == 0x06 &&
              Ram[0x11] == 0x08 &&
              State.Flag[FlagCarry] == 0 &&
              State.Flag[FlagZero] == 0 &&
              State.Flag[FlagNegative] == 0 &&
              State.Flag[FlagOverflow] == 0;

    std::printf ("RESULT: %s  (A=$%02llx exp $08, M[$10]=$%02x exp $06, M[$11]=$%02x exp $08)\n",
                 Ok ? "PASS" : "FAIL",
                 (unsigned long long)(State.Reg[Reg6502A] & 0xff), Ram[0x10], Ram[0x11]);

    pArch->Release ();
    pBackend->Release ();
    return Ok ? 0 : 1;
}
