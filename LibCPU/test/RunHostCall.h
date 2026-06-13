/** @file
  Catalog-into-dispatch demo: derive a host-call catalog from a C header + a library's
  symbols, then run a tiny guest whose INT 21h/AH=10h is dispatched -- not to a built-in
  handler, but to the real libc toupper() bound dynamically through the catalog.

  Guest: MOV AL,'a'; MOV AH,0x10; INT 0x21. The knowledge library maps that vector/selector
  to toupper(AL) with the result written back to AL; after the run AL must hold 'A',
  proving the derived catalog drove a real native call with argument + result marshalling.
**/
#ifndef LIBCPU_RUNHOSTCALL_H
#define LIBCPU_RUNHOSTCALL_H

#include "CpuV20.h"
#include "LibCPU/CpuState.h"
#include "../core/KnowledgeLibrary.h"
#include "../core/DerivationEngine.h"
#include <cstdio>
#include <cstring>

namespace LibCPU {

static inline int
RunHostCallDemo (ICpuBackend *pBackend, CHAR8 CONST *pHeader, CHAR8 CONST *pLib, CHAR8 CONST *pXml)
{
    std::printf ("== Catalog dispatch: derive %s x %s, bind at run time, on '%s'\n",
                 pHeader, pLib, pBackend->GetName ());

    // 1. Derive the host-call catalog (signatures x exported symbols).
    HeaderParser Headers;
    SymbolReader Symbols;
    std::string Error;
    CHAR8 CONST *Args[] = { "-x", "c" };
    if (!Headers.Parse (pHeader, Args, 2, &Error)) {
        std::printf ("  header parse failed: %s\n", Error.c_str ());
        return 1;
    }
    if (!Symbols.Read (pLib, &Error)) {
        std::printf ("  symbol read failed: %s\n", Error.c_str ());
        return 1;
    }
    KnowledgeCatalog Catalog;
    Catalog.Derive (Headers, Symbols);
    std::printf ("  catalog: %u function(s), %u exported [%s]\n",
                 (UINT32) Catalog.Functions ().size (), Catalog.ExportedCount (),
                 Headers.UsedClang () ? "libclang" : "built-in");

    // 2. Load the target->host mapping.
    KnowledgeLibrary Library;
    if (!Library.Load (pXml)) {
        std::printf ("  failed to load knowledge library '%s'\n", pXml);
        return 1;
    }

    // 3. The guest: set the selector (AH) and argument (AL) with 8-bit immediate moves,
    // then trap. Exercises B0/B4 (MOV reg8,imm8) through the whole pipeline.
    //   B0 61   mov al, 'a'      ; argument
    //   B4 10   mov ah, 0x10     ; INT 21h selector
    //   CD 21   int 0x21
    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    UINT8 const Prog[] = {
        0xB0, 0x61,        // mov al, 'a'
        0xB4, 0x10,        // mov ah, 0x10
        0xCD, 0x21         // int 0x21
    };
    std::memcpy (Ram, Prog, sizeof (Prog));

    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));
    ICpuArchitecture *pArch = CreateV20 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    HOST_BINDER Binder = MakeCatalogBinder (&Catalog);
    KN_RUN_RESULT Result = RunWithSyscalls (pArch, pBackend, 0, (CPU_ADDR) sizeof (Prog),
                                            Ram, &State, Library, &Binder);

    UINT32 Al = (UINT32) (State.Reg[0] & 0xFF);     // AX is register index 0; AL its low byte
    std::printf ("  guest AL: 'a' (0x61) -> toupper -> '%c' (0x%02x); %u syscall(s), %u translation(s), unhandled=%d\n",
                 Al >= 0x20 && Al < 0x7F ? (char) Al : '?', Al, Result.Syscalls, Result.Translations, (int) Result.Unhandled);

    bool Ok = (Al == 'A') && Result.Syscalls == 1 && !Result.Unhandled;
    std::printf ("RESULT: %s  (derived catalog bound libc toupper dynamically; arg + result marshalled)\n",
                 Ok ? "PASS" : "FAIL");

    pArch->Release ();
    return Ok ? 0 : 1;
}

} // namespace LibCPU

#endif // LIBCPU_RUNHOSTCALL_H
