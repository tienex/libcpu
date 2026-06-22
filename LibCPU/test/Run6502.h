/** @file
  Shared 6502 test runner: single-step a fixed 6502 program through the given
  backend and the 6502 frontend, verifying final state. Used by both the
  interpreter and the LLVM runners -- same frontend, any backend.
**/
#ifndef LIBCPU_RUN6502_H
#define LIBCPU_RUN6502_H

#include "Cpu6502.h"
#include "LibCPU/CpuState.h"
#include <cstdio>
#include <cstring>

namespace LibCPU {

//
// Single-step a 6502 image (loaded at 0) through the backend + frontend, leaving the final
// RAM and CPU state in the caller's buffers.
//
static inline void
Run6502Image (ICpuBackend *pBackend, UINT8 *pRam, CPU_STATE *pState, UINT8 CONST *pProgram, UINT32 Length)
{
    std::memset (pRam, 0, 65536);
    std::memcpy (pRam, pProgram, Length);
    std::memset (pState, 0, sizeof (*pState));

    ICpuArchitecture *pArch = Create6502 ();
    pArch->SetCodeMemory (pRam, 65536);

    CPU_ADDR Pc  = 0;
    CPU_ADDR End = (CPU_ADDR) Length;
    while (Pc < End) {
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
// Program 1 (the slice exercise): A9 05 LDA#5  85 10 STA$10  E6 10 INC$10  69 03 ADC#3  85 11 STA$11
//   -> A = 0x08, M[$10] = 0x06, M[$11] = 0x08, NZVC all clear.
//
// Program 2 (ADC carry + overflow, driven entirely by %CC): A9 80 LDA#$80  69 80 ADC#$80
//   -> A = 0x00; signed 0x80 + 0x80 wraps so CARRY = 1 and (signed) OVERFLOW = 1; Z = 1; N = 0.
//   A naive 8-bit %CC that adds the carry-in after the operand add would drop this carry.
//
static inline int
Run6502Program (ICpuBackend *pBackend)
{
    static UINT8 Ram[65536];
    CPU_STATE State;

    std::printf ("backend = '%s'\n", pBackend->GetName ());

    UINT8 const Program1[] = { 0xA9, 0x05, 0x85, 0x10, 0xE6, 0x10, 0x69, 0x03, 0x85, 0x11 };
    Run6502Image (pBackend, Ram, &State, Program1, (UINT32) sizeof (Program1));
    bool Ok1 = (State.Reg[Reg6502A] & 0xff) == 0x08 && Ram[0x10] == 0x06 && Ram[0x11] == 0x08 &&
               State.Flag[FlagCarry] == 0 && State.Flag[FlagZero] == 0 &&
               State.Flag[FlagNegative] == 0 && State.Flag[FlagOverflow] == 0;
    std::printf ("  prog1: A=$%02llx (exp $08) M[$10]=$%02x M[$11]=$%02x NZVC=%d%d%d%d -> %s\n",
                 (unsigned long long)(State.Reg[Reg6502A] & 0xff), Ram[0x10], Ram[0x11],
                 State.Flag[FlagNegative], State.Flag[FlagZero], State.Flag[FlagOverflow], State.Flag[FlagCarry],
                 Ok1 ? "ok" : "BAD");

    UINT8 const Program2[] = { 0xA9, 0x80, 0x69, 0x80 };
    Run6502Image (pBackend, Ram, &State, Program2, (UINT32) sizeof (Program2));
    bool Ok2 = (State.Reg[Reg6502A] & 0xff) == 0x00 &&
               State.Flag[FlagCarry] == 1 && State.Flag[FlagOverflow] == 1 &&
               State.Flag[FlagZero] == 1 && State.Flag[FlagNegative] == 0;
    std::printf ("  prog2 (ADC $80+$80): A=$%02llx (exp $00) NZVC=%d%d%d%d (exp 0,1,1,1) -> %s\n",
                 (unsigned long long)(State.Reg[Reg6502A] & 0xff),
                 State.Flag[FlagNegative], State.Flag[FlagZero], State.Flag[FlagOverflow], State.Flag[FlagCarry],
                 Ok2 ? "ok" : "BAD");

    bool Ok = Ok1 && Ok2;
    std::printf ("RESULT: %s\n", Ok ? "PASS" : "FAIL");
    return Ok ? 0 : 1;
}

} // namespace LibCPU

#endif // LIBCPU_RUN6502_H
