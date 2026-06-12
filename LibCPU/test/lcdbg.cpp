/** @file
  lcdbg -- the LibCPU single-step debugger driver. Loads a backend bundle (the
  interpreter, which provides the translated-code listing), a frontend, and a small
  built-in demo program, then drops into the interactive/scriptable debugger.

  Usage: lcdbg <path-to.backend> [--arch v20|6502]
**/
#include "CpuV20.h"
#include "Cpu6502.h"
#include "LibCPU/CpuState.h"
#include "../core/Debugger.h"
#include "LibCPU/Loader.h"
#include <cstdio>
#include <cstring>

using namespace LibCPU;

int
main (int argc, char **argv)
{
    CHAR8 CONST *pBundle = nullptr;
    CHAR8 CONST *pArchName = "v20";
    for (int I = 1; I < argc; I++) {
        if (std::strcmp (argv[I], "--arch") == 0 && I + 1 < argc) {
            pArchName = argv[++I];
        } else {
            pBundle = argv[I];
        }
    }
    if (pBundle == nullptr) {
        std::printf ("usage: %s <path-to.backend> [--arch v20|6502]\n", argv[0]);
        return 2;
    }
    ICpuBackend *pBackend = LoadBackendBundle (pBundle);
    if (pBackend == nullptr) {
        std::printf ("failed to load bundle '%s'\n", pBundle);
        return 2;
    }

    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));
    State.RamSize = sizeof (Ram);

    ICpuArchitecture       *pArch = nullptr;
    CPU_ADDR                 End  = 0;
    std::vector<std::string> Regs;
    UINT32                   RegBytes = 2;
    std::vector<std::string> Flags = { "N", "V", "Z", "C" };

    if (std::strcmp (pArchName, "6502") == 0) {
        // LDA #5; STA $80; LDA #0; loop: CLC; ADC #3; DEC $80; BNE loop; STA $0200
        UINT8 const Prog[] = {
            0xA9, 0x05,  0x85, 0x80,  0xA9, 0x00,
            0x18,  0x69, 0x03,  0xC6, 0x80,  0xD0, 0xF9,
            0x8D, 0x00, 0x02
        };
        std::memcpy (Ram, Prog, sizeof (Prog));
        End  = (CPU_ADDR) sizeof (Prog);
        pArch = Create6502 ();
        Regs = { "A", "X", "Y", "S" };
        RegBytes = 1;
        State.Reg[Reg6502S] = 0xFF;
    } else {
        // MOV AX,3; MOV CX,5; loop: ADD AX,CX; DEC CX; JNZ loop; MOV [0x200],AX
        UINT8 const Prog[] = {
            0xB8, 0x03, 0x00,  0xB9, 0x05, 0x00,
            0x01, 0xC8,  0x49,  0x75, 0xFB,
            0xA3, 0x00, 0x02
        };
        std::memcpy (Ram, Prog, sizeof (Prog));
        End  = (CPU_ADDR) sizeof (Prog);
        pArch = CreateV20 ();
        Regs = { "AX", "CX", "DX", "BX", "SP", "BP", "SI", "DI", "ES", "CS", "SS", "DS" };
        RegBytes = 2;
    }
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    std::printf ("== lcdbg: arch=%s backend=%s, %llu-byte demo program\n",
                 pArchName, pBackend->GetName (), (unsigned long long) End);

    LcDebugger Debugger (pArch, pBackend, Ram, sizeof (Ram), &State, 0, End, Regs, RegBytes, Flags);
    int Rc = Debugger.Repl ();

    pArch->Release ();
    pBackend->Release ();
    return Rc;
}
