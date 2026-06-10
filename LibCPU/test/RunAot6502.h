/** @file
  AOT counterpart of Run6502.h: translate the whole 6502 program through the given
  backend in ONE compile (GenerateAot) and run the resulting artifact ONCE, instead
  of the JIT path's compile-and-run per instruction. Same frontend, same backend
  interface -- only the granularity differs.
**/
#ifndef LIBCPU_RUNAOT6502_H
#define LIBCPU_RUNAOT6502_H

#include "Cpu6502.h"
#include "LibCPU/CpuState.h"
#include "LibCPU/PCom.h"
#include "../aot/AotGenerator.h"
#include <cstdio>
#include <cstring>

namespace LibCPU {

static inline int
RunAot6502Program (ICpuBackend *pBackend)
{
    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));

    UINT8 const Program[] = {
        0xA9, 0x05,  0x85, 0x10,  0xE6, 0x10,  0x69, 0x03,  0x85, 0x11
    };
    std::memcpy (Ram, Program, sizeof (Program));

    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));

    ICpuArchitecture *pArch = Create6502 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    std::printf ("backend = '%s'  (AOT: whole program -> one compile)\n", pBackend->GetName ());

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

    bool Ok = (State.Reg[Reg6502A] & 0xff) == 0x08 &&
              Ram[0x10] == 0x06 && Ram[0x11] == 0x08 &&
              State.Flag[FlagCarry] == 0 && State.Flag[FlagZero] == 0 &&
              State.Flag[FlagNegative] == 0 && State.Flag[FlagOverflow] == 0;

    std::printf ("RESULT: %s  (A=$%02llx exp $08, M[$10]=$%02x exp $06, M[$11]=$%02x exp $08)\n",
                 Ok ? "PASS" : "FAIL",
                 (unsigned long long)(State.Reg[Reg6502A] & 0xff), Ram[0x10], Ram[0x11]);

    pArch->Release ();
    return Ok ? 0 : 1;
}

} // namespace LibCPU

#endif // LIBCPU_RUNAOT6502_H
