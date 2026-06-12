/** @file
  Native-AOT demo: translate a V20 loop to a STANDALONE host executable (guest
  registers become C locals -> host registers), compile it with the host cc, and run
  the resulting binary -- which depends on nothing from LibCPU -- to verify it
  computes the same result (sum of 5..1 added to 3 -> [0x200] = 0x12).
**/
#ifndef LIBCPU_RUNNATIVE_H
#define LIBCPU_RUNNATIVE_H

#include "CpuV20.h"
#include "LibCPU/CpuState.h"
#include "../core/NativeAot.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace LibCPU {

static inline int
RunNativeAotDemo (CHAR8 CONST *pOutExe)
{
    std::printf ("== Native AOT: compile a V20 program to a standalone host executable\n");

    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    // MOV AX,3; MOV CX,5; loop: ADD AX,CX; DEC CX; JNZ loop; MOV [0x200],AX  -> [0x200]=0x12
    UINT8 const Prog[] = {
        0xB8, 0x03, 0x00,  0xB9, 0x05, 0x00,  0x01, 0xC8,  0x49,  0x75, 0xFB,  0xA3, 0x00, 0x02
    };
    std::memcpy (Ram, Prog, sizeof (Prog));

    ICpuArchitecture *pArch = CreateV20 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    LC_NATIVE_OPTIONS Opt;
    Opt.DumpResult = true;
    Opt.ResultAddr = 0x200;
    std::string Source = LcGenerateNativeC (pArch, Ram, (UINT32) sizeof (Prog), 0, (CPU_ADDR) sizeof (Prog), Opt);
    pArch->Release ();
    if (Source.empty ()) {
        std::printf ("  code generation failed\n");
        return 1;
    }
    std::printf ("  generated %zu bytes of C (guest registers -> C locals r0..r31)\n", Source.size ());

    std::string Error;
    if (!LcCompileNative (Source, pOutExe, &Error)) {
        std::printf ("  host cc failed: %s\n", Error.c_str ());
        return 1;
    }
    std::printf ("  compiled standalone executable: %s\n", pOutExe);

    // Run the standalone binary and capture its output (it links no LibCPU code).
    std::string Cmd = std::string ("'") + pOutExe + "'";
    std::FILE *pPipe = popen (Cmd.c_str (), "r");
    std::string Captured;
    if (pPipe != nullptr) {
        char Buf[64];
        size_t N;
        while ((N = std::fread (Buf, 1, sizeof (Buf), pPipe)) > 0) {
            Captured.append (Buf, N);
        }
        pclose (pPipe);
    }
    while (!Captured.empty () && (Captured.back () == '\n' || Captured.back () == '\r')) {
        Captured.pop_back ();
    }
    std::printf ("  standalone run printed: \"%s\"  (exp \"0x0012\")\n", Captured.c_str ());

    bool Ok = Captured == "0x0012";
    std::printf ("RESULT: %s  (guest -> native register-mapped executable, runs independently of LibCPU)\n",
                 Ok ? "PASS" : "FAIL");
    return Ok ? 0 : 1;
}

} // namespace LibCPU

#endif // LIBCPU_RUNNATIVE_H
