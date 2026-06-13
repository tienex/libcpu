/** @file
  Derived-mapping demo: build the target->host mapping AUTOMATICALLY from a target ABI
  table + the host catalog (no hand-authored <syscall>/<host>/<arg> blocks), then run a
  real DOS program through the derived KnowledgeLibrary.

  The same "Hello, LibCPU!" program the hand-authored dos21.xml drives is run here through
  a mapping derived from test/dos.abi joined with a libc catalog -- proving the derivation
  reproduces a working mapping, with fputs's FILE* argument auto-filled from its signature.
**/
#ifndef LIBCPU_RUNDERIVEDMAP_H
#define LIBCPU_RUNDERIVEDMAP_H

#include "CpuV20.h"
#include "LibCPU/CpuState.h"
#include "../core/KnowledgeLibrary.h"
#include "../core/DerivationEngine.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

namespace LibCPU {

static inline int
RunDerivedMapDemo (ICpuBackend *pBackend, CHAR8 CONST *pHeader, CHAR8 CONST *pLib, CHAR8 CONST *pAbi)
{
    std::printf ("== Derived mapping: %s (target ABI) x %s (host catalog), on '%s'\n",
                 pAbi, pLib, pBackend->GetName ());

    // 1. Host catalog (signatures x exports) and 2. target ABI table.
    HeaderParser Headers;
    SymbolReader Symbols;
    std::string Error;
    CHAR8 CONST *Args[] = { "-x", "c" };
    if (!Headers.Parse (pHeader, Args, 2, &Error) || !Symbols.Read (pLib, &Error)) {
        std::printf ("  derive inputs failed: %s\n", Error.c_str ());
        return 1;
    }
    KnowledgeCatalog Catalog;
    Catalog.Derive (Headers, Symbols);

    std::vector<TARGET_SYSCALL> Abi;
    if (!LoadTargetAbi (pAbi, &Abi, &Error)) {
        std::printf ("  %s\n", Error.c_str ());
        return 1;
    }

    // 3. Derive the mapping (validate against the catalog, generate arg recipes).
    KnowledgeLibrary Library;
    Library.SetClasses ("dos", Catalog.Library ().empty () ? "libc" : Catalog.Library ().c_str ());
    DERIVE_REPORT Rep = DeriveMapping (Catalog, Abi, &Library);
    std::printf ("  derived %u/%u mapping(s) (%u unresolved); generated recipes:\n",
                 Rep.Resolved, Rep.Total, Rep.Unresolved);
    for (UINT32 i = 0; i < Library.Count (); i++) {
        KN_SYSCALL CONST &S = Library.At (i);
        std::printf ("    int 0x%02x %s=0x%02x %-12s -> %s(", S.Vector, S.Select.c_str (), S.Value,
                     S.Name.c_str (), S.Host.Call.c_str ());
        for (UINT32 a = 0; a < S.Host.Args.size (); a++) {
            KN_HOST_ARG CONST &A = S.Host.Args[a];
            CHAR8 CONST *pDesc = A.Kind == KN_HOST_ARG::ConstName ? A.Const.c_str ()
                               : A.Kind == KN_HOST_ARG::StructPtr ? A.Struct.c_str ()
                               : A.From.c_str ();
            std::printf ("%s%s", a ? ", " : "", pDesc);
        }
        std::printf (")\n");
    }

    // 4. Run the canonical DOS program through the DERIVED library and capture stdout.
    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    UINT8 const Prog[] = {
        0xB8, 0x00, 0x09,   // mov ax, 0x0900   (AH=09 print, AL=00)
        0xBA, 0x0D, 0x00,   // mov dx, 0x000D   (offset of message)
        0xCD, 0x21,         // int 0x21
        0xB8, 0x00, 0x4C,   // mov ax, 0x4C00   (AH=4C terminate)
        0xCD, 0x21,         // int 0x21
        'H','e','l','l','o',',',' ','L','i','b','C','P','U','!','\r','\n','$'
    };
    std::memcpy (Ram, Prog, sizeof (Prog));
    CHAR8 CONST *pExpect = "Hello, LibCPU!\r\n";

    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));
    ICpuArchitecture *pArch = CreateV20 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    std::fflush (stdout);
    int SavedFd = dup (1);
    std::FILE *pTmp = std::tmpfile ();
    KN_RUN_RESULT Result;
    if (pTmp != nullptr && SavedFd >= 0) {
        dup2 (fileno (pTmp), 1);
        Result = RunWithSyscalls (pArch, pBackend, 0, (CPU_ADDR) sizeof (Prog), Ram, &State, Library);
        std::fflush (stdout);
        dup2 (SavedFd, 1);
        close (SavedFd);
    } else {
        Result = RunWithSyscalls (pArch, pBackend, 0, (CPU_ADDR) sizeof (Prog), Ram, &State, Library);
    }
    std::string Captured;
    if (pTmp != nullptr) {
        std::rewind (pTmp);
        char Buf[256];
        size_t Got;
        while ((Got = std::fread (Buf, 1, sizeof (Buf), pTmp)) > 0) {
            Captured.append (Buf, Got);
        }
        std::fclose (pTmp);
    }

    std::string Shown;
    for (char c : Captured) {
        if (c == '\r') { Shown += "\\r"; } else if (c == '\n') { Shown += "\\n"; } else { Shown.push_back (c); }
    }
    std::printf ("  guest wrote: \"%s\"; exited=%d code=%d syscalls=%u\n",
                 Shown.c_str (), (int) Result.Exited, Result.ExitCode, Result.Syscalls);

    bool Ok = Rep.Resolved == Rep.Total && Result.Exited && Result.ExitCode == 0 &&
              Result.Syscalls == 2 && Captured == pExpect;
    std::printf ("RESULT: %s  (target ABI table + host catalog -> derived mapping ran a real DOS program)\n",
                 Ok ? "PASS" : "FAIL");

    pArch->Release ();
    return Ok ? 0 : 1;
}

} // namespace LibCPU

#endif // LIBCPU_RUNDERIVEDMAP_H
