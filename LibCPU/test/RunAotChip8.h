/** @file
  AOT counterpart of RunChip8.h: translate the whole CHIP-8 program through the
  given backend in ONE compile (GenerateAot) and run the artifact ONCE. Shows the
  AOT driver is frontend-agnostic too (and handles CHIP-8's dynamic addressing).
**/
#ifndef LIBCPU_RUNAOTCHIP8_H
#define LIBCPU_RUNAOTCHIP8_H

#include "Chip8.h"
#include "LibCPU/CpuState.h"
#include "LibCPU/PCom.h"
#include "../aot/AotGenerator.h"
#include <cstdio>
#include <cstring>

namespace LibCPU {

static inline int
RunAotChip8Program (ICpuBackend *pBackend)
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
    std::printf ("backend = '%s'  (frontend = chip8, AOT: whole program -> one compile)\n", pBackend->GetName ());

    ComPtr<ICpuCode> Code;
    UINT32 Count = 0;
    HRESULT hr = GenerateAot (pArch, pBackend, 0, (CPU_ADDR) sizeof (Program), &Code, &Count);
    if (FAILED (hr) || Code == nullptr) {
        std::printf ("AOT generation failed (hr=0x%lx)\n", (unsigned long) hr);
        pArch->Release ();
        return 1;
    }
    std::printf ("AOT: %u instructions folded into 1 artifact; running once...\n", Count);

    Code->Execute (Ram, &State, nullptr);

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

#endif // LIBCPU_RUNAOTCHIP8_H
