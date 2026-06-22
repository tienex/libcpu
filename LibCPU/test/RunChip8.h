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
// Single-step a CHIP-8 image (loaded at 0) through the backend + frontend, leaving the
// final RAM and CPU state in the caller's buffers.
//
static inline void
RunChip8Image (ICpuBackend *pBackend, UINT8 *pRam, CPU_STATE *pState,
               UINT8 CONST *pProgram, UINT32 Length)
{
    std::memset (pRam, 0, 65536);
    std::memcpy (pRam, pProgram, Length);
    std::memset (pState, 0, sizeof (*pState));

    ICpuArchitecture *pArch = CreateChip8 ();
    pArch->SetCodeMemory (pRam, 65536);

    CPU_ADDR Pc  = 0;
    CPU_ADDR End = (CPU_ADDR) Length;
    while (Pc < End) {
        char Line[40];
        pArch->Disassemble (Pc, Line, sizeof (Line));

        ComPtr<ICpuEmitter> Emitter;
        pBackend->CreateEmitter (pArch, &Emitter);
        pArch->TranslateInstr (Pc, Emitter);

        ComPtr<ICpuCode> Code;
        pBackend->Compile (Emitter, &Code);
        Code->Execute (pRam, pState, nullptr);

        UINT32 Tag; CPU_ADDR NewPc, NextPc;
        pArch->TagInstr (Pc, &Tag, &NewPc, &NextPc);
        Pc = NextPc;
    }
    pArch->Release ();
}

//
// Program 1 (the original slice exercise):
//   60 0A  LD V0,#$0A      61 05  LD V1,#$05
//   80 14  ADD V0,V1       80 15  SUB V0,V1
//   A1 00  LD I,$100       F0 55  store V0 -> [I]
// Expected: V0 = 0x0A (10), VF = 1, M[$100] = 0x0A.
//
// Program 2 (the Fx55/Fx65 block loops, multiple iterations):
//   60 AA 61 BB 62 CC   V0=$AA V1=$BB V2=$CC
//   A2 00               I=$200
//   F2 55               store V0..V2 -> [$200..$202]   (3 iterations)
//   60 00 61 00 62 00   clear V0..V2
//   F2 65               load [$200..$202] -> V0..V2    (3 iterations)
// Expected: M[$200..$202] = AA BB CC, and V0..V2 reloaded to AA BB CC.
//
static inline int
RunChip8Program (ICpuBackend *pBackend)
{
    static UINT8 Ram[65536];
    CPU_STATE State;

    std::printf ("backend = '%s'  (frontend = chip8)\n", pBackend->GetName ());

    UINT8 const Program1[] = {
        0x60, 0x0A,  0x61, 0x05,  0x80, 0x14,  0x80, 0x15,  0xA1, 0x00,  0xF0, 0x55
    };
    RunChip8Image (pBackend, Ram, &State, Program1, (UINT32) sizeof (Program1));
    bool Ok1 = (State.Reg[0] & 0xff) == 0x0A &&
               (State.Reg[Chip8RegVF] & 0xff) == 0x01 &&
               Ram[0x100] == 0x0A;
    std::printf ("  prog1: V0=$%02llx (exp $0A) VF=$%02llx (exp $01) M[$100]=$%02x (exp $0A) -> %s\n",
                 (unsigned long long)(State.Reg[0] & 0xff),
                 (unsigned long long)(State.Reg[Chip8RegVF] & 0xff), Ram[0x100], Ok1 ? "ok" : "BAD");

    UINT8 const Program2[] = {
        0x60, 0xAA,  0x61, 0xBB,  0x62, 0xCC,  0xA2, 0x00,  0xF2, 0x55,
        0x60, 0x00,  0x61, 0x00,  0x62, 0x00,  0xF2, 0x65
    };
    RunChip8Image (pBackend, Ram, &State, Program2, (UINT32) sizeof (Program2));
    bool Ok2 = Ram[0x200] == 0xAA && Ram[0x201] == 0xBB && Ram[0x202] == 0xCC &&
               (State.Reg[0] & 0xff) == 0xAA && (State.Reg[1] & 0xff) == 0xBB && (State.Reg[2] & 0xff) == 0xCC;
    std::printf ("  prog2 (Fx55/Fx65 loops): M[$200..2]=%02x %02x %02x  V0..2=%02llx %02llx %02llx (exp AA BB CC) -> %s\n",
                 Ram[0x200], Ram[0x201], Ram[0x202],
                 (unsigned long long)(State.Reg[0] & 0xff), (unsigned long long)(State.Reg[1] & 0xff),
                 (unsigned long long)(State.Reg[2] & 0xff), Ok2 ? "ok" : "BAD");

    bool Ok = Ok1 && Ok2;
    std::printf ("RESULT: %s\n", Ok ? "PASS" : "FAIL");
    return Ok ? 0 : 1;
}

} // namespace LibCPU

#endif // LIBCPU_RUNCHIP8_H
