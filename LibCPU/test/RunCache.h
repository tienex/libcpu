/** @file
  Disk-cached-translation demo. The same V20 region is translated twice: the first
  time misses the cache (translate + store the serialised artifact), the second time
  hits it (reload from disk, no translation). The reloaded artifact is executed to
  prove the round-trip is faithful. Then the cache management surface is exercised:
  list, total size, cap (oldest-first eviction), and clean.
**/
#ifndef LIBCPU_RUNCACHE_H
#define LIBCPU_RUNCACHE_H

#include "CpuV20.h"
#include "LibCPU/CpuState.h"
#include "../core/TranslationCache.h"
#include "LibCPU/PCom.h"
#include <cstdio>
#include <cstring>

namespace LibCPU {

static inline int
CacheResultWord (UINT8 CONST *pRam, UINT32 At)
{
    return (int) (pRam[At] | (pRam[At + 1] << 8));
}

static inline int
RunCacheDemo (ICpuBackend *pBackend, CHAR8 CONST *pCacheDir)
{
    std::printf ("== Disk-cached translation on '%s', cache dir '%s'\n", pBackend->GetName (), pCacheDir);

    LcTranslationCache Cache (pCacheDir);
    Cache.Clean ();                                   // start from an empty cache

    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    // ProgA: MOV AX,3; MOV CX,5; loop: ADD AX,CX; DEC CX; JNZ loop; MOV [0x200],AX  -> [0x200]=0x12
    UINT8 const ProgA[] = { 0xB8, 0x03, 0x00,  0xB9, 0x05, 0x00,  0x01, 0xC8,  0x49,  0x75, 0xFB,  0xA3, 0x00, 0x02 };
    std::memcpy (Ram, ProgA, sizeof (ProgA));
    CPU_ADDR EntryA = 0, EndA = (CPU_ADDR) sizeof (ProgA);

    ICpuArchitecture *pArch = CreateV20 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    auto RunArtifact = [&] (ICpuCode *pCode, UINT32 At) -> int {
        CPU_STATE State;
        std::memset (&State, 0, sizeof (State));
        State.RamSize = sizeof (Ram);
        std::memset (Ram + At, 0, 2);
        pCode->Execute (Ram, &State, nullptr);
        return CacheResultWord (Ram, At);
    };

    // --- pass 1: cold cache -> miss, translate + store ----------------------
    bool Hit1 = false;
    ComPtr<ICpuCode> Code1;
    LcCachedTranslate (Cache, pArch, pBackend, Ram, EntryA, EndA, &Code1, &Hit1);
    int Res1 = Code1 != nullptr ? RunArtifact (Code1, 0x200) : -1;

    // --- pass 2: warm cache (a fresh cache object on the same dir) -> hit ----
    LcTranslationCache Cache2 (pCacheDir);
    bool Hit2 = false;
    ComPtr<ICpuCode> Code2;
    LcCachedTranslate (Cache2, pArch, pBackend, Ram, EntryA, EndA, &Code2, &Hit2);
    int Res2 = Code2 != nullptr ? RunArtifact (Code2, 0x200) : -1;

    std::printf ("  pass 1: hit=%d -> [0x200]=0x%02x (exp 0x12)   [miss: translated + stored]\n", (int) Hit1, Res1);
    std::printf ("  pass 2: hit=%d -> [0x200]=0x%02x (exp 0x12)   [hit: reloaded from disk]\n", (int) Hit2, Res2);

    // --- add a second, different region so capping has something to choose ---
    UINT8 const ProgB[] = { 0xB8, 0x34, 0x12,  0xA3, 0x02, 0x02 };   // MOV AX,0x1234; MOV [0x202],AX
    std::memcpy (Ram + 0x40, ProgB, sizeof (ProgB));
    bool HitB = false;
    ComPtr<ICpuCode> CodeB;
    LcCachedTranslate (Cache2, pArch, pBackend, Ram, 0x40, 0x40 + (CPU_ADDR) sizeof (ProgB), &CodeB, &HitB);

    std::vector<LcTranslationCache::LC_CACHE_ENTRY> Entries = Cache2.List ();
    std::printf ("  cache now holds %zu artifact(s), %llu bytes total:\n",
                 Entries.size (), (unsigned long long) Cache2.TotalSize ());
    UINT64 SmallSize = ~UINT64_C (0);
    for (LcTranslationCache::LC_CACHE_ENTRY CONST &E : Entries) {
        std::printf ("    %s  arch=%s backend=%s  %llu bytes\n",
                     E.Path.substr (E.Path.find_last_of ('/') + 1).c_str (),
                     E.Arch.c_str (), E.Backend.c_str (), (unsigned long long) E.Size);
        if (E.Size < SmallSize) { SmallSize = E.Size; }
    }

    // --- cap to the smaller entry's size: evicts the older (larger) one -----
    UINT32 Evicted = Cache2.Cap (SmallSize);
    size_t AfterCap = Cache2.List ().size ();
    std::printf ("  cap(%llu bytes): evicted %u oldest artifact(s), %zu remain\n",
                 (unsigned long long) SmallSize, Evicted, AfterCap);

    // --- clean removes the rest --------------------------------------------
    UINT32 Cleaned = Cache2.Clean ();
    size_t AfterClean = Cache2.List ().size ();
    std::printf ("  clean: removed %u, %zu remain\n", Cleaned, AfterClean);

    bool Ok = !Hit1 && Hit2 && Res1 == 0x12 && Res2 == 0x12
              && Entries.size () == 2 && Evicted == 1 && AfterCap == 1 && AfterClean == 0;
    std::printf ("RESULT: %s  (miss stores; hit reloads + runs identically; cap evicts oldest; clean empties)\n",
                 Ok ? "PASS" : "FAIL");

    pArch->Release ();
    return Ok ? 0 : 1;
}

} // namespace LibCPU

#endif // LIBCPU_RUNCACHE_H
