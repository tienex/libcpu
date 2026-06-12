/** @file
  V20/V30 test runner. Translates whole programs through GenerateAotCfg (the CFG
  path) and runs them once, checking the 16-bit result word at 0x200. Scenarios:

  (1) LOOP (backward branch) summing 5+4+3+2+1                        -> 15
  (2) NEC bit instructions SET1/SET1/NOT1                             -> 1
  (3) memory-operand ModR/M: sum a 4-word array via ADD AX,[BX]       -> 100
  (4) PUSH/POP round-trip + MOV [disp16],reg (memory ModR/M store)    -> 0x1234
**/
#ifndef LIBCPU_RUNAOTV20_H
#define LIBCPU_RUNAOTV20_H

#include "CpuV20.h"
#include "LibCPU/CpuState.h"
#include "LibCPU/PCom.h"
#include "../aot/AotGenerator.h"
#include <cstdio>
#include <cstring>

namespace LibCPU {

//
// Run a program (optionally seeding Data at DataAddr first) and return the 16-bit
// word stored at 0x200, or -1 on failure.
//
static inline int
AotV20Run (ICpuBackend *pBackend, UINT8 CONST *pProg, UINT32 ProgLen,
           UINT8 CONST *pData, UINT32 DataLen, UINT16 DataAddr)
{
    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    std::memcpy (Ram, pProg, ProgLen);
    if (pData != nullptr) {
        std::memcpy (Ram + DataAddr, pData, DataLen);
    }

    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));

    ICpuArchitecture *pArch = CreateV20 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    ComPtr<ICpuCode> Code;
    UINT32 Count = 0;
    HRESULT hr = GenerateAotCfg (pArch, pBackend, 0, (CPU_ADDR) ProgLen, &Code, &Count);
    if (FAILED (hr) || Code == nullptr) {
        pArch->Release ();
        return -1;
    }
    Code->Execute (Ram, &State, nullptr);
    pArch->Release ();
    return (int) (Ram[0x200] | (Ram[0x201] << 8));
}

//
// Run a program that uses indirect control flow (RET): RET stores its runtime
// target in CPU_STATE.TrapPc and stops; the host re-translates from there and
// resumes -- the same resume loop the SMC support uses. Returns the word at 0x200.
//
static inline int
AotV20CallRet (ICpuBackend *pBackend, UINT8 CONST *pProg, UINT32 ProgLen, int *pTranslations)
{
    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    std::memcpy (Ram, pProg, ProgLen);

    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));

    ICpuArchitecture *pArch = CreateV20 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    CPU_ADDR Resume = 0;
    int Trans = 0;
    for (int Iter = 0; Iter < 64; Iter++) {   // watchdog against runaway indirection
        ComPtr<ICpuCode> Code;
        UINT32 Count = 0;
        State.TrapPc = CPU_SMC_NO_TRAP;
        HRESULT hr = GenerateAotCfg (pArch, pBackend, Resume, (CPU_ADDR) ProgLen, &Code, &Count);
        if (FAILED (hr) || Code == nullptr) {
            pArch->Release ();
            return -1;
        }
        Trans++;
        Code->Execute (Ram, &State, nullptr);
        if (State.TrapPc == CPU_SMC_NO_TRAP) {
            break;                            // ran to completion
        }
        Resume = (CPU_ADDR) State.TrapPc;     // RET / indirect transfer: resume at the target
    }
    pArch->Release ();
    if (pTranslations != nullptr) {
        *pTranslations = Trans;
    }
    return (int) (Ram[0x200] | (Ram[0x201] << 8));
}

//
// Run a program over a 1 MB RAM and return the word at linear address LinAddr;
// *pUnseg gets the word at the bare offset 0x10 (must stay 0, proving the DS shift).
// State.RamSize is set so the marshalling backends (cpython/jvm/clr) copy the full
// 1 MB -- segmented linear addresses past 64 KB now work on every backend.
//
static inline int
AotV20Seg (ICpuBackend *pBackend, UINT8 CONST *pProg, UINT32 ProgLen, UINT32 LinAddr, int *pUnseg)
{
    static UINT8 Ram[0x100000];   // 1 MB
    std::memset (Ram, 0, sizeof (Ram));
    std::memcpy (Ram, pProg, ProgLen);

    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));
    State.RamSize = sizeof (Ram);   // marshal the full 1 MB

    ICpuArchitecture *pArch = CreateV20 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    ComPtr<ICpuCode> Code;
    UINT32 Count = 0;
    HRESULT hr = GenerateAotCfg (pArch, pBackend, 0, (CPU_ADDR) ProgLen, &Code, &Count);
    if (FAILED (hr) || Code == nullptr) {
        pArch->Release ();
        return -1;
    }
    Code->Execute (Ram, &State, nullptr);
    pArch->Release ();
    *pUnseg = (int) (Ram[0x10] | (Ram[0x11] << 8));
    return (int) (Ram[LinAddr] | (Ram[LinAddr + 1] << 8));
}

//
// Code segmentation: the program is loaded at linear Cs*16 and the frontend is told
// CS = Cs, so instructions fetch from Cs*16 + IP while the driver works in IP space.
// CALL/RET therefore push/pop IP offsets, not linear addresses. Returns the word at
// 0x200 (the CALL/RET result); *pCsVal gets the word at 0x202 (the value MOV-from-CS
// read back, which must equal Cs).
//
static inline int
AotV20Cs (ICpuBackend *pBackend, UINT8 CONST *pProg, UINT32 ProgLen, UINT16 Cs, int *pCsVal)
{
    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    std::memcpy (Ram + ((UINT32) Cs << 4), pProg, ProgLen);   // load code at Cs*16

    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));
    State.Reg[RegV20CS] = Cs;                                 // guest CS register

    ICpuArchitecture *pArch = CreateV20 (Cs);
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    ComPtr<ICpuCode> Code;
    UINT32 Count = 0;
    HRESULT hr = GenerateAotCfg (pArch, pBackend, 0, (CPU_ADDR) ProgLen, &Code, &Count);
    if (FAILED (hr) || Code == nullptr) {
        pArch->Release ();
        return -1;
    }
    State.TrapPc = CPU_SMC_NO_TRAP;
    Code->Execute (Ram, &State, nullptr);
    pArch->Release ();
    *pCsVal = (int) (Ram[0x202] | (Ram[0x203] << 8));
    return (int) (Ram[0x200] | (Ram[0x201] << 8));
}

//
// Far JMP (CS:IP reload). Segment A @ CS=0x40 sets AX then JMP 0x50:0; segment B @
// CS=0x50 stores AX then JMP 0xFFFF:0xFFFF (a halt sentinel). A far JMP traps to the
// host, which re-points the frontend at the new CS and resumes at the new IP.
// Returns the word at 0x200 (= 0x42 written from segment B).
//
static inline int
AotV20Far (ICpuBackend *pBackend)
{
    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    UINT8 const SegA[] = { 0xB8, 0x42, 0x00, 0xEA, 0x00, 0x00, 0x50, 0x00 };  // MOV AX,0x42; JMP 0x50:0
    UINT8 const SegB[] = { 0xA3, 0x00, 0x02, 0xEA, 0xFF, 0xFF, 0xFF, 0xFF };  // MOV [0x200],AX; JMP 0xFFFF:0xFFFF
    std::memcpy (Ram + 0x400, SegA, sizeof (SegA));   // CS 0x40 -> linear 0x400
    std::memcpy (Ram + 0x500, SegB, sizeof (SegB));   // CS 0x50 -> linear 0x500

    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));
    State.Reg[RegV20CS] = 0x40;

    ICpuArchitecture *pArch = CreateV20 (0x40);
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    CPU_ADDR ResumeIp = 0;
    UINT16   Cs       = 0x40;
    for (int Iter = 0; Iter < 16; Iter++) {
        SetV20CodeSegment (pArch, Cs);
        ComPtr<ICpuCode> Code;
        UINT32 Count = 0;
        if (FAILED (GenerateAotCfg (pArch, pBackend, ResumeIp, 0x100, &Code, &Count)) || Code == nullptr) {
            pArch->Release ();
            return -1;
        }
        State.TrapPc = CPU_SMC_NO_TRAP;
        Code->Execute (Ram, &State, nullptr);
        if (State.TrapPc == CPU_SMC_NO_TRAP) {
            break;                                   // ran off the end (no far jump)
        }
        Cs = (UINT16) State.Reg[RegV20CS];           // the far JMP set the new CS
        if (Cs == 0xFFFF) {
            break;                                   // halt sentinel
        }
        ResumeIp = (CPU_ADDR) State.TrapPc;          // ... and the new IP
    }
    pArch->Release ();
    return (int) (Ram[0x200] | (Ram[0x201] << 8));
}

static inline int
RunAotV20Program (ICpuBackend *pBackend)
{
    std::printf ("backend = '%s'  (NEC V20/V30: loop, bit ops, memory ModR/M, stack)\n", pBackend->GetName ());

    UINT8 const Loop[] = {
        0xB9, 0x05, 0x00,   // MOV CX, 5
        0xB8, 0x00, 0x00,   // MOV AX, 0
        0x01, 0xC8,         // loop: ADD AX, CX
        0x49,               //       DEC CX
        0x75, 0xFB,         //       JNZ loop
        0xA3, 0x00, 0x02    // MOV [0x200], AX
    };
    UINT8 const Bits[] = {
        0xB8, 0x00, 0x00,         // MOV AX, 0
        0x0F, 0x1D, 0xC0, 0x05,   // SET1 AX, 5   -> 0x20
        0x0F, 0x1D, 0xC0, 0x00,   // SET1 AX, 0   -> 0x21
        0x0F, 0x1F, 0xC0, 0x05,   // NOT1 AX, 5   -> 0x01
        0xA3, 0x00, 0x02          // MOV [0x200], AX
    };
    // Sum an array of four words at 0x300 through the [BX] memory operand.
    UINT8 const Array[] = {
        0xBB, 0x00, 0x03,   // MOV BX, 0x300   (array base)
        0xB9, 0x04, 0x00,   // MOV CX, 4
        0xB8, 0x00, 0x00,   // MOV AX, 0
        0x03, 0x07,         // loop: ADD AX, [BX]   (ModR/M 07 = mod00 reg=AX rm=[BX])
        0x43,               //       INC BX
        0x43,               //       INC BX         (BX += 2)
        0x49,               //       DEC CX
        0x75, 0xF9,         //       JNZ loop
        0xA3, 0x00, 0x02    // MOV [0x200], AX
    };
    UINT8 const ArrayData[] = { 0x0A, 0x00, 0x14, 0x00, 0x1E, 0x00, 0x28, 0x00 };  // 10,20,30,40 -> 100
    // PUSH/POP round-trip; store BX via MOV [disp16],BX (memory ModR/M store).
    UINT8 const Stack[] = {
        0xBC, 0x00, 0x10,   // MOV SP, 0x1000
        0xB8, 0x34, 0x12,   // MOV AX, 0x1234
        0x50,               // PUSH AX
        0xB8, 0x00, 0x00,   // MOV AX, 0
        0x5B,               // POP BX           (BX = 0x1234)
        0x89, 0x1E, 0x00, 0x02  // MOV [0x200], BX  (ModR/M 1E = mod00 reg=BX rm=[disp16])
    };

    // CALL a subroutine and RET back. RET's target is the runtime stack value, so
    // it is an indirect branch resolved by the host resume loop.
    //   JMP main ; sub: ADD AX,5 ; RET ; main: MOV AX,3 ; MOV SP ; CALL sub ; MOV [0x200],AX
    UINT8 const Call[] = {
        0xE9, 0x04, 0x00,         // 0: JMP main (0x7)
        0x05, 0x05, 0x00,         // 3: sub: ADD AX, 5
        0xC3,                     // 6:      RET
        0xB8, 0x03, 0x00,         // 7: main: MOV AX, 3
        0xBC, 0x00, 0x10,         // A:       MOV SP, 0x1000
        0xE8, 0xF3, 0xFF,         // D:       CALL sub (rel16 = -13 -> 0x3)
        0xA3, 0x00, 0x02          // 10:      MOV [0x200], AX   (return point; AX = 3+5 = 8)
    };

    // Code segmentation: same CALL/RET subroutine, but assembled to run at CS=0x40
    // (linear 0x400). CALL pushes IP 0x10 (not 0x410); RET returns there. Then
    // MOV AX,CS reads the CS register (0x40).
    UINT8 const Cs[] = {
        0xE9, 0x04, 0x00,   // 0:  JMP main (IP 0x7)
        0x05, 0x05, 0x00,   // 3:  sub: ADD AX, 5
        0xC3,               // 6:       RET
        0xB8, 0x03, 0x00,   // 7:  main: MOV AX, 3
        0xBC, 0x00, 0x10,   // A:        MOV SP, 0x1000
        0xE8, 0xF3, 0xFF,   // D:        CALL sub (IP 0x3)
        0xA3, 0x00, 0x02,   // 10:       MOV [0x200], AX   (= 8)
        0x8C, 0xC8,         // 13:       MOV AX, CS        (= 0x40)
        0xA3, 0x02, 0x02    // 15:       MOV [0x202], AX
    };

    // Segmentation: set DS = 0x1000, then MOV [0x0010],AX lands at linear
    // 0x1000*16 + 0x10 = 0x10010 (past 64 KB) -- not at offset 0x10.
    UINT8 const Seg[] = {
        0xB8, 0x00, 0x10,   // MOV AX, 0x1000
        0x8E, 0xD8,         // MOV DS, AX        (ModR/M D8 = mod11 reg=DS rm=AX)
        0xB8, 0x34, 0x12,   // MOV AX, 0x1234
        0xA3, 0x10, 0x00    // MOV [0x0010], AX  -> linear 0x10010
    };

    int Sum   = AotV20Run (pBackend, Loop,  (UINT32) sizeof (Loop),  nullptr, 0, 0);
    int Bit   = AotV20Run (pBackend, Bits,  (UINT32) sizeof (Bits),  nullptr, 0, 0);
    int Arr   = AotV20Run (pBackend, Array, (UINT32) sizeof (Array), ArrayData, (UINT32) sizeof (ArrayData), 0x300);
    int Stk   = AotV20Run (pBackend, Stack, (UINT32) sizeof (Stack), nullptr, 0, 0);
    int Trans = 0;
    int Cal   = AotV20CallRet (pBackend, Call, (UINT32) sizeof (Call), &Trans);
    int Unseg = -1;
    int Seg16 = AotV20Seg (pBackend, Seg, (UINT32) sizeof (Seg), 0x10010, &Unseg);
    int CsVal = -1;
    int CsRet = AotV20Cs (pBackend, Cs, (UINT32) sizeof (Cs), 0x0040, &CsVal);
    int Far   = AotV20Far (pBackend);

    std::printf ("  loop 5+4+3+2+1            -> [0x200] = %d (exp 15)\n", Sum);
    std::printf ("  SET1/SET1/NOT1 bit ops   -> [0x200] = %d (exp 1)\n", Bit);
    std::printf ("  array sum via [BX]       -> [0x200] = %d (exp 100)\n", Arr);
    std::printf ("  PUSH/POP + MOV [m],BX    -> [0x200] = 0x%04x (exp 0x1234)\n", Stk);
    std::printf ("  CALL sub / RET (indirect)-> [0x200] = %d (exp 8, %d translation%s; in-artifact dispatch)\n",
                 Cal, Trans, Trans == 1 ? "" : "s");
    std::printf ("  DS=0x1000; MOV [0x10],AX  -> [linear 0x10010] = 0x%04x (exp 0x1234), [0x10] = %d (exp 0)\n",
                 Seg16, Unseg);
    std::printf ("  CS=0x0040 CALL/RET + MOV AX,CS -> [0x200] = %d (exp 8), CS read = 0x%04x (exp 0x0040)\n",
                 CsRet, CsVal);
    std::printf ("  far JMP CS:IP (0x40 -> 0x50)   -> [0x200] = 0x%04x (exp 0x0042)\n", Far);

    // With the in-artifact dispatcher, RET resolves inside the compiled code: one
    // translation, no host re-entry (it took 2 before the dispatch table).
    bool Ok = Sum == 15 && Bit == 1 && Arr == 100 && Stk == 0x1234 && Cal == 8 && Trans == 1
              && Seg16 == 0x1234 && Unseg == 0 && CsRet == 8 && CsVal == 0x0040 && Far == 0x0042;
    std::printf ("RESULT: %s  (loop %d, bits %d, array %d, stack 0x%04x, call/ret %d in %d, seg 0x%04x, cs %d/0x%04x, far 0x%04x)\n",
                 Ok ? "PASS" : "FAIL", Sum, Bit, Arr, Stk, Cal, Trans, Seg16, CsRet, CsVal, Far);
    return Ok ? 0 : 1;
}

} // namespace LibCPU

#endif // LIBCPU_RUNAOTV20_H
