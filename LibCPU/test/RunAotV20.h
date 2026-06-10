/** @file
  V20/V30 test runner. Translates whole programs through GenerateAotCfg (the CFG
  path) and runs them once. Two scenarios:

  (1) A real LOOP (backward conditional branch) summing 5+4+3+2+1:
        MOV CX,5 ; MOV AX,0 ; loop: ADD AX,CX ; DEC CX ; JNZ loop ; MOV [0x200],AX
      -> word @ 0x200 == 15.

  (2) The NEC-only bit instructions:
        MOV AX,0 ; SET1 AX,5 ; SET1 AX,0 ; NOT1 AX,5 ; MOV [0x200],AX
      -> AX = 0x20 | 0x01, then bit 5 toggled off -> 0x01; word @ 0x200 == 1.
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

static inline int
AotV20Word (ICpuBackend *pBackend, UINT8 CONST *pProgram, UINT32 ProgLen, int *pOk)
{
    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    std::memcpy (Ram, pProgram, ProgLen);

    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));

    ICpuArchitecture *pArch = CreateV20 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    ComPtr<ICpuCode> Code;
    UINT32 Count = 0;
    HRESULT hr = GenerateAotCfg (pArch, pBackend, 0, (CPU_ADDR) ProgLen, &Code, &Count);
    if (FAILED (hr) || Code == nullptr) {
        std::printf ("  AOT-CFG generation failed (hr=0x%lx)\n", (unsigned long) hr);
        pArch->Release ();
        *pOk = 0;
        return -1;
    }
    Code->Execute (Ram, &State, nullptr);
    pArch->Release ();
    *pOk = 1;
    return (int) (Ram[0x200] | (Ram[0x201] << 8));   // result word at 0x200
}

static inline int
RunAotV20Program (ICpuBackend *pBackend)
{
    std::printf ("backend = '%s'  (NEC V20/V30: loop + bit instructions, via CFG)\n", pBackend->GetName ());

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

    int Ok1 = 0, Ok2 = 0;
    int SumW = AotV20Word (pBackend, Loop, (UINT32) sizeof (Loop), &Ok1);
    int BitW = AotV20Word (pBackend, Bits, (UINT32) sizeof (Bits), &Ok2);

    std::printf ("  loop sum 5+4+3+2+1 -> [0x200] = %d (exp 15)\n", SumW);
    std::printf ("  SET1/SET1/NOT1 bit ops -> [0x200] = %d (exp 1)\n", BitW);

    bool Ok = Ok1 && Ok2 && SumW == 15 && BitW == 1;
    std::printf ("RESULT: %s  (loop %d==15, bits %d==1)\n", Ok ? "PASS" : "FAIL", SumW, BitW);
    return Ok ? 0 : 1;
}

} // namespace LibCPU

#endif // LIBCPU_RUNAOTV20_H
