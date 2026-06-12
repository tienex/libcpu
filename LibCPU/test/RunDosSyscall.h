/** @file
  Knowledge-library demo: a tiny MS-DOS program -- print a '$'-terminated string
  via INT 21h/AH=09, then terminate via INT 21h/AH=4C -- is translated through the
  V20 frontend and the interpreter backend, and its in-line DOS calls are turned
  into real host libc calls by the dos21.xml knowledge library. The guest's output
  lands on host stdout; the demo captures it to verify the marshalling end-to-end.
**/
#ifndef LIBCPU_RUNDOSSYSCALL_H
#define LIBCPU_RUNDOSSYSCALL_H

#include "CpuV20.h"
#include "LibCPU/CpuState.h"
#include "../core/KnowledgeLibrary.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

namespace LibCPU {

static inline int
RunDosSyscallDemo (ICpuBackend *pBackend, CHAR8 CONST *pXmlPath)
{
    std::printf ("== Knowledge library: DOS INT 21h -> host libc, on '%s'\n", pBackend->GetName ());

    LcKnowledgeLibrary Library;
    if (!Library.Load (pXmlPath)) {
        std::printf ("  failed to load knowledge library '%s'\n", pXmlPath);
        return 1;
    }
    std::printf ("  loaded '%s' -> target=%s host=%s, %u syscall mapping(s):\n",
                 pXmlPath, Library.TargetClass (), Library.HostClass (), Library.Count ());
    for (UINT32 i = 0; i < Library.Count (); i++) {
        KN_SYSCALL CONST &S = Library.At (i);
        std::printf ("    int 0x%02x %s=0x%02x  %-12s -> %s()\n",
                     S.Vector, S.Select.c_str (), S.Value, S.Name.c_str (), S.Host.Call.c_str ());
    }

    // A small DOS program. DS = 0 (cleared state), so DS:DX addresses the message
    // that follows the code at offset 0x0D.
    //
    //   B8 00 09   MOV AX,0x0900   ; AH=09 (print), AL=00
    //   BA 0D 00   MOV DX,0x000D   ; DX = offset of msg
    //   CD 21      INT 0x21        ; print the '$'-string at DS:DX
    //   B8 00 4C   MOV AX,0x4C00   ; AH=4C (terminate), AL=00 (exit code)
    //   CD 21      INT 0x21        ; terminate
    //   "Hello, LibCPU!\r\n$"
    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    UINT8 const Prog[] = {
        0xB8, 0x00, 0x09,
        0xBA, 0x0D, 0x00,
        0xCD, 0x21,
        0xB8, 0x00, 0x4C,
        0xCD, 0x21,
        'H','e','l','l','o',',',' ','L','i','b','C','P','U','!','\r','\n','$'
    };
    std::memcpy (Ram, Prog, sizeof (Prog));
    CHAR8 CONST *pExpect = "Hello, LibCPU!\r\n";

    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));

    ICpuArchitecture *pArch = CreateV20 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    // Capture host stdout around the run so we can verify the bytes the host call
    // actually wrote (the guest's INT 21h/09 becomes a real fputs to fd 1).
    std::fflush (stdout);
    int    SavedFd = dup (1);
    std::FILE *pTmp = std::tmpfile ();
    KN_RUN_RESULT Result;
    if (pTmp != nullptr && SavedFd >= 0) {
        dup2 (fileno (pTmp), 1);
        Result = LcRunWithSyscalls (pArch, pBackend, 0, (CPU_ADDR) sizeof (Prog), Ram, &State, Library);
        std::fflush (stdout);
        dup2 (SavedFd, 1);
        close (SavedFd);
    } else {
        Result = LcRunWithSyscalls (pArch, pBackend, 0, (CPU_ADDR) sizeof (Prog), Ram, &State, Library);
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

    // Echo what the guest wrote (newline-escaped for a clean single line).
    std::string Shown;
    for (char c : Captured) {
        if (c == '\r') { Shown += "\\r"; }
        else if (c == '\n') { Shown += "\\n"; }
        else { Shown.push_back (c); }
    }
    std::printf ("  guest wrote to host stdout: \"%s\"\n", Shown.c_str ());
    std::printf ("  dispatched %u syscall(s) in %u translation(s); exited=%d code=%d unhandled=%d\n",
                 Result.Syscalls, Result.Translations, (int) Result.Exited, Result.ExitCode, (int) Result.Unhandled);

    bool Ok = Result.Exited && Result.ExitCode == 0 && Result.Syscalls == 2 && !Result.Unhandled
              && Captured == pExpect;
    std::printf ("RESULT: %s  (DOS print + terminate dispatched to libc fputs + exit; string marshalled correctly)\n",
                 Ok ? "PASS" : "FAIL");

    pArch->Release ();
    return Ok ? 0 : 1;
}

} // namespace LibCPU

#endif // LIBCPU_RUNDOSSYSCALL_H
