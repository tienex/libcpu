/** @file
  Struct-marshalling demo: a guest passes a far pointer (DS:DX) to a buffer and traps; the
  dispatcher binds the host out-parameter call demo_stat_fill(struct demo_stat *) through
  the derived catalog, calls it into a host-layout buffer, and marshals the filled struct
  back into the guest's 16-bit-packed layout using the catalog's field offsets.

  Host layout (libclang): Size@+0, Mode@+4, Uid@+8 (12 bytes, native 32-bit fields).
  Guest layout (DOS convention): Size@+0, Mode@+2, Uid@+4 (16-bit packed). The offsets
  differ, so a correct result proves the conversion is offset-driven, not a raw copy.
**/
#ifndef LIBCPU_RUNSTRUCT_H
#define LIBCPU_RUNSTRUCT_H

#include "CpuV20.h"
#include "LibCPU/CpuState.h"
#include "../core/KnowledgeLibrary.h"
#include "../core/DerivationEngine.h"
#include <cstdio>
#include <cstring>
#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace LibCPU {

static inline int
RunStructDemo (ICpuBackend *pBackend, CHAR8 CONST *pHeader, CHAR8 CONST *pDylib, CHAR8 CONST *pXml)
{
    std::printf ("== Struct marshalling: %s x %s -> guest layout, on '%s'\n",
                 pHeader, pDylib, pBackend->GetName ());

    // Load the demo library so its symbol resolves via RTLD_DEFAULT in the binder.
    void *pHandle = dlopen (pDylib, RTLD_NOW | RTLD_GLOBAL);
    if (pHandle == nullptr) {
        std::printf ("  cannot dlopen '%s'\n", pDylib);
        return 1;
    }

    HeaderParser Headers;
    SymbolReader Symbols;
    std::string Error;
    CHAR8 CONST *Args[] = { "-x", "c" };
    if (!Headers.Parse (pHeader, Args, 2, &Error) || !Symbols.Read (pDylib, &Error)) {
        std::printf ("  derive inputs failed: %s\n", Error.c_str ());
        return 1;
    }
    KnowledgeCatalog Catalog;
    Catalog.Derive (Headers, Symbols);
    for (HEADER_STRUCT CONST &S : Catalog.Structs ()) {
        std::printf ("  host struct %s: %llu bytes,", S.Name.c_str (), (unsigned long long) S.Size);
        for (HEADER_FIELD CONST &F : S.Fields) {
            std::printf (" %s@+%llu", F.Name.c_str (), (unsigned long long) F.Offset);
        }
        std::printf ("\n");
    }

    KnowledgeLibrary Library;
    if (!Library.Load (pXml)) {
        std::printf ("  failed to load '%s'\n", pXml);
        return 1;
    }

    // Guest: fill demo_stat into [0x200] (AH=0x20), then demo_pack into [0x300] (AH=0x21).
    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    UINT8 const Prog[] = {
        0xBA, 0x00, 0x02,  // mov dx, 0x0200
        0xB4, 0x20,        // mov ah, 0x20
        0xCD, 0x21,        // int 0x21      (demo_stat_fill -> DS:0x200)
        0xBA, 0x00, 0x03,  // mov dx, 0x0300
        0xB4, 0x21,        // mov ah, 0x21
        0xCD, 0x21         // int 0x21      (demo_pack_fill -> DS:0x300)
    };
    std::memcpy (Ram, Prog, sizeof (Prog));

    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));
    ICpuArchitecture *pArch = CreateV20 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    HOST_BINDER Binder = MakeCatalogBinder (&Catalog);
    RunWithSyscalls (pArch, pBackend, 0, (CPU_ADDR) sizeof (Prog), Ram, &State, Library, &Binder);

    // demo_stat at 0x200: three equal-width fields (host 0/4/8 -> guest 0/2/4).
    UINT32 Size = Ram[0x200] | (Ram[0x201] << 8);
    UINT32 Mode = Ram[0x202] | (Ram[0x203] << 8);
    UINT32 Uid  = Ram[0x204] | (Ram[0x205] << 8);
    std::printf ("  guest [DS:0x200] demo_stat: Size=0x%04x Mode=0x%04x Uid=0x%04x\n", Size, Mode, Uid);

    // demo_pack at 0x300: mixed widths with host padding (A@0,B@4,C@8) -> packed guest
    // (A@0 size1, B@1 size2 truncated, C@3 size2): exercises de-padding + truncation.
    UINT32 A = Ram[0x300];
    UINT32 B = Ram[0x301] | (Ram[0x302] << 8);
    UINT32 C = Ram[0x303] | (Ram[0x304] << 8);
    std::printf ("  guest [DS:0x300] demo_pack: A=0x%02x B=0x%04x C=0x%04x\n", A, B, C);

    bool Ok = (Size == 0x1234) && (Mode == 0x0077) && (Uid == 0x0042) &&
              (A == 0x11) && (B == 0x2222) && (C == 0x3333);
    std::printf ("RESULT: %s  (host out-structs marshalled to guest packed layout by catalog field offsets)\n",
                 Ok ? "PASS" : "FAIL");

    pArch->Release ();
    return Ok ? 0 : 1;
}

} // namespace LibCPU

#endif // LIBCPU_RUNSTRUCT_H
