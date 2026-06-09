/** @file
  Shared CHIP-8 test runner: single-step a fixed CHIP-8 program through the given
  backend and the CHIP-8 frontend, verifying final state. Demonstrates a SECOND
  frontend running on every backend unchanged.
**/
#ifndef LIBCPU_RUNCHIP8_H
#define LIBCPU_RUNCHIP8_H

#include "Chip8.h"
#include "LibCPU/CpuState.h"
#include <cstdio>
#include <cstring>

namespace LibCPU {

//
// Program (loaded at 0):
//   60 0A  LD V0,#$0A      61 05  LD V1,#$05
//   80 14  ADD V0,V1       80 15  SUB V0,V1
//   A1 00  LD I,$100       F0 55  store V0 -> [I]
// Expected: V0 = 0x0A (10), VF = 1, M[$100] = 0x0A.
//
static inline int
RunChip8Program (ICpuBackend *pBackend)
{
    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));

    UINT8 const Program[] = {
        0x60, 0x0A,  0x61, 0x05,  0x80, 0x14,  0x80, 0x15,  0xA1, 0x00,  0xF0, 0x55
    };
    std::memcpy (Ram, Program, sizeof (Program));

    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));

    ICpuArchitecture *pArch = CreateChip8 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));
    std::printf ("backend = '%s'  (frontend = chip8)\n", pBackend->GetName ());

    CPU_ADDR Pc  = 0;
    CPU_ADDR End = (CPU_ADDR) sizeof (Program);
    while (Pc < End) {
        char Line[40];
        pArch->Disassemble (Pc, Line, sizeof (Line));

        ComPtr<ICpuEmitter> Emitter;
        pBackend->CreateEmitter (pArch, &Emitter);
        pArch->TranslateInstr (Pc, Emitter);

        ComPtr<ICpuCode> Code;
        pBackend->Compile (Emitter, &Code);
        Code->Execute (Ram, &State, nullptr);

        UINT32 Tag; CPU_ADDR NewPc, NextPc;
        pArch->TagInstr (Pc, &Tag, &NewPc, &NextPc);
        std::printf ("  $%03llx: %-16s  V0=$%02llx V1=$%02llx VF=$%02llx I=$%03llx M[$100]=$%02x\n",
                     (unsigned long long) Pc, Line,
                     (unsigned long long)(State.Reg[0] & 0xff), (unsigned long long)(State.Reg[1] & 0xff),
                     (unsigned long long)(State.Reg[Chip8RegVF] & 0xff), (unsigned long long)(State.Reg[Chip8RegI] & 0xffff),
                     Ram[0x100]);
        Pc = NextPc;
    }

    bool Ok = (State.Reg[0] & 0xff) == 0x0A &&
              (State.Reg[Chip8RegVF] & 0xff) == 0x01 &&
              Ram[0x100] == 0x0A;
    std::printf ("RESULT: %s  (V0=$%02llx exp $0A, VF=$%02llx exp $01, M[$100]=$%02x exp $0A)\n",
                 Ok ? "PASS" : "FAIL",
                 (unsigned long long)(State.Reg[0] & 0xff),
                 (unsigned long long)(State.Reg[Chip8RegVF] & 0xff), Ram[0x100]);

    pArch->Release ();
    return Ok ? 0 : 1;
}

} // namespace LibCPU

#endif // LIBCPU_RUNCHIP8_H
