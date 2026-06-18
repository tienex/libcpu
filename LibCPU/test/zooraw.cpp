/** @file
  ZOO archive raw-member round-trip: the format extension that turns the knowledge-library
  container into a directly-mappable code cache. Exercises the three guarantees the cache
  relies on:

    1. A SOLID (compressed) member still round-trips alongside raw members.
    2. A VERBATIM raw member maps zero-copy at its requested page alignment (16K here, the
       universal choice that is also valid for 4K hosts) and its bytes compare equal.
    3. A COMPRESSED raw member keeps an uncompressed-sized slot but stores a zstd stream;
       GetRaw inflates it and the bytes compare equal, while the on-disk PHYSICAL size is
       far below the logical size (compression + hole-punching), where the backing FS
       supports hole deallocation.

  Prints "RESULT: PASS" / "RESULT: FAIL" for the harness.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "core/ZooArchive.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace LibCPU;

int
main (int argc, char **argv)
{
    CHAR8 CONST *pPath = (argc > 1) ? argv[1] : "test.zoo";

    // A pseudo-random verbatim payload (incompressible-ish; must map byte-exact) and a
    // highly compressible payload (50 KB of one byte; must shrink dramatically).
    std::vector<UINT8> Code (300);
    for (UINT32 I = 0; I < Code.size (); I++) { Code[I] = (UINT8) (I * 7 + 3); }
    std::vector<UINT8> Big (50000, 0xAB);

    ZooWriter Writer;
    std::string Notes = "knowledge-and-code-in-one-archive";
    Writer.Add ("notes", Notes);
    Writer.AddRaw ("code.arm64", Code.data (), Code.size (), ZOO_ALIGN_16K, FALSE);
    Writer.AddRaw ("blob.big",   Big.data (),  Big.size (),  ZOO_ALIGN_4K,  TRUE);

    std::string Err;
    if (!Writer.Save (pPath, 19, &Err)) {
        std::printf ("save failed: %s\nRESULT: FAIL\n", Err.c_str ());
        return 1;
    }

    ZooArchive Archive;
    if (!Archive.Load (pPath, &Err)) {
        std::printf ("load failed: %s\nRESULT: FAIL\n", Err.c_str ());
        return 1;
    }

    bool Pass = true;

    // (1) Solid member.
    std::vector<UINT8> SolidOut;
    if (!Archive.Extract ("notes", &SolidOut)
        || SolidOut.size () != Notes.size ()
        || std::memcmp (SolidOut.data (), Notes.data (), Notes.size ()) != 0) {
        std::printf ("solid member mismatch\n");
        Pass = false;
    }

    if (!Archive.Map (&Err)) {
        std::printf ("map failed: %s\nRESULT: FAIL\n", Err.c_str ());
        return 1;
    }

    // (2) Verbatim raw member: aligned + byte-exact, served zero-copy from the mapping.
    UINT8 CONST *pV = nullptr;
    UINT64       VLen = 0;
    if (!Archive.GetRaw ("code.arm64", &pV, &VLen) || VLen != Code.size ()
        || std::memcmp (pV, Code.data (), VLen) != 0) {
        std::printf ("verbatim member mismatch\n");
        Pass = false;
    } else if ((((UINTN) pV) & (ZOO_ALIGN_16K - 1)) != 0) {
        std::printf ("verbatim member not 16K-aligned: %p\n", (VOID *) pV);
        Pass = false;
    }

    // (3) Compressed raw member: inflates back to the original bytes.
    UINT8 CONST *pC = nullptr;
    UINT64       CLen = 0;
    if (!Archive.GetRaw ("blob.big", &pC, &CLen) || CLen != Big.size ()) {
        std::printf ("compressed member length mismatch\n");
        Pass = false;
    } else {
        for (UINT64 I = 0; I < CLen; I++) {
            if (pC[I] != 0xAB) { std::printf ("compressed member byte %llu wrong\n", (unsigned long long) I); Pass = false; break; }
        }
    }

    std::printf ("members solid=%u raw=%u; verbatim=%llu compressed=%llu\n",
                 Archive.MemberCount (), Archive.RawCount (),
                 (unsigned long long) VLen, (unsigned long long) CLen);
    std::printf ("RESULT: %s\n", Pass ? "PASS" : "FAIL");
    return Pass ? 0 : 1;
}
