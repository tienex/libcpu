/** @file
  Content-addressed, host-aware code cache round-trip. Proves the cache layer the adaptive
  JIT builds on:

    1. The content hash is deterministic and the key pins guest arch/cpu/length.
    2. One guest region hangs MULTIPLE host-targeted artifacts: storing the same code for
       two different host fingerprints, flushing, and reloading yields a HIT for each of the
       two fingerprints and a MISS for an absent third -- the "never run a blob on a host it
       was not built for" rule, and the "same code across machines" sharing.
    3. The per-artifact profile (runs/tier/opt) survives the flush.
    4. The archive ACCRETES across runs: a second cache instance opens the same file, adds an
       artifact, flushes, and reloads -- all prior artifacts plus the new one are present.

  Prints "RESULT: PASS" / "RESULT: FAIL".

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "core/CodeCache.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace LibCPU;

// A fingerprint standing in for a host target (in production these come from the producing
// backend's ICpuBackendTarget::GetTargetFingerprint).
static UINT64 CONST FP_ARM64_NEON = UINT64_C (0xA64E0001);
static UINT64 CONST FP_X64_AVX2   = UINT64_C (0x8664A0F2);
static UINT64 CONST FP_X64_BASE   = UINT64_C (0x8664B001);

int
main (int argc, char **argv)
{
    std::string Path = (argc > 1) ? argv[1] : "test.codecache";
    std::remove (Path.c_str ());

    bool Pass = true;
    std::string Err;

    // Two distinct host-code blobs for the SAME guest region (as two backends/targets would
    // emit), and the guest region bytes themselves.
    std::vector<UINT8> Region (200);
    for (UINT32 I = 0; I < Region.size (); I++) { Region[I] = (UINT8) (0x90 + (I & 7)); }
    std::vector<UINT8> ArmCode (512, 0x11);
    std::vector<UINT8> X64Code (700, 0x22);

    LC_CONTENT_HASH H1 = CodeCache::Hash (Region.data (), (UINT32) Region.size ());
    LC_CONTENT_HASH H2 = CodeCache::Hash (Region.data (), (UINT32) Region.size ());
    if (H1.Lo != H2.Lo || H1.Hi != H2.Hi) {
        std::printf ("hash not deterministic\n");
        Pass = false;
    }

    std::string KeyArm = CodeCache::MakeKey ("v20", "8086", H1, (UINT32) Region.size (), TRUE, FP_ARM64_NEON, 0);
    std::string KeyX64 = CodeCache::MakeKey ("v20", "8086", H1, (UINT32) Region.size (), TRUE, FP_X64_AVX2, 0);
    std::string KeyMiss = CodeCache::MakeKey ("v20", "8086", H1, (UINT32) Region.size (), TRUE, FP_X64_BASE, 0);
    if (KeyArm == KeyX64) {
        std::printf ("keys for different fingerprints collided\n");
        Pass = false;
    }

    // Run 1: store both targets with profiles, flush.
    {
        CodeCache Cache (Path);
        if (!Cache.Load (&Err)) { std::printf ("load1: %s\nRESULT: FAIL\n", Err.c_str ()); return 1; }
        LC_CACHE_PROFILE PArm = { 1234, 2, 1 };
        LC_CACHE_PROFILE PX64 = { 99, 3, 2 };
        Cache.Store (KeyArm, ArmCode.data (), ArmCode.size (), PArm, FALSE);
        Cache.Store (KeyX64, X64Code.data (), X64Code.size (), PX64, TRUE);   // compressed
        if (!Cache.Flush (&Err)) { std::printf ("flush1: %s\nRESULT: FAIL\n", Err.c_str ()); return 1; }
    }

    // Run 2: reopen, expect hits for both stored fingerprints, miss for the third, and the
    // profile to have survived. Then accrete a third artifact.
    {
        CodeCache Cache (Path);
        if (!Cache.Load (&Err)) { std::printf ("load2: %s\nRESULT: FAIL\n", Err.c_str ()); return 1; }

        UINT8 CONST *pB = nullptr;
        UINT64 L = 0;
        LC_CACHE_PROFILE Prof;

        if (!Cache.Lookup (KeyArm, &pB, &L, &Prof) || L != ArmCode.size ()
            || std::memcmp (pB, ArmCode.data (), L) != 0) {
            std::printf ("arm artifact lost\n"); Pass = false;
        } else if (Prof.Runs != 1234 || Prof.Tier != 2 || Prof.OptLevel != 1) {
            std::printf ("arm profile wrong: runs=%llu tier=%u opt=%u\n",
                         (unsigned long long) Prof.Runs, Prof.Tier, Prof.OptLevel);
            Pass = false;
        }

        if (!Cache.Lookup (KeyX64, &pB, &L, &Prof) || L != X64Code.size ()
            || std::memcmp (pB, X64Code.data (), L) != 0) {
            std::printf ("x64 (compressed) artifact lost\n"); Pass = false;
        }

        if (Cache.Lookup (KeyMiss, &pB, &L, &Prof)) {
            std::printf ("absent fingerprint must MISS but hit\n"); Pass = false;
        }

        // Accrete the third (baseline) target.
        std::vector<UINT8> BaseCode (300, 0x33);
        LC_CACHE_PROFILE PBase = { 7, 1, 0 };
        Cache.Store (KeyMiss, BaseCode.data (), BaseCode.size (), PBase, FALSE);
        if (!Cache.Flush (&Err)) { std::printf ("flush2: %s\nRESULT: FAIL\n", Err.c_str ()); return 1; }
    }

    // Run 3: all three artifacts present.
    {
        CodeCache Cache (Path);
        if (!Cache.Load (&Err)) { std::printf ("load3: %s\nRESULT: FAIL\n", Err.c_str ()); return 1; }
        UINT8 CONST *pB = nullptr; UINT64 L = 0;
        bool A = Cache.Lookup (KeyArm, &pB, &L, nullptr);
        bool B = Cache.Lookup (KeyX64, &pB, &L, nullptr);
        bool C = Cache.Lookup (KeyMiss, &pB, &L, nullptr);
        if (!A || !B || !C || Cache.Count () != 3) {
            std::printf ("accretion failed: arm=%d x64=%d base=%d count=%u\n", A, B, C, Cache.Count ());
            Pass = false;
        }

        // Compression policy must survive the merge-flush: arm was stored verbatim (it
        // remains map-in-place), x64 was stored compressed.
        UINT64 ArmComp = 0, X64Comp = 0;
        Cache.RawInfo (KeyArm, nullptr, &ArmComp, nullptr);
        Cache.RawInfo (KeyX64, nullptr, &X64Comp, nullptr);
        if (ArmComp != 0) { std::printf ("verbatim member got compressed on merge\n"); Pass = false; }
        if (X64Comp == 0) { std::printf ("compressed member lost compression on merge\n"); Pass = false; }
    }

    std::printf ("RESULT: %s\n", Pass ? "PASS" : "FAIL");
    return Pass ? 0 : 1;
}
