/** @file
  Demonstration of the adaptive tiered execution engine (LibCPU core).

  A hot V20 loop and a cold straight-line region share one engine configured with
  two tiers: tier 0 = a cheap backend (instant compile, interpreted), tier 1 = an
  optimizing backend (slower compile, fast native code, e.g. cc with -O2). The hot
  region crosses the heat threshold, is recompiled on a BACKGROUND thread, and is
  hot-swapped to tier 1; the cold region never crosses the threshold and stays on
  tier 0. Results stay correct throughout (during interpretation, during the
  background compile, and after the swap).
**/
#ifndef LIBCPU_RUNTIERED_H
#define LIBCPU_RUNTIERED_H

#include "CpuV20.h"
#include "LibCPU/CpuState.h"
#include "../core/TieredEngine.h"
#include <cstdio>
#include <cstring>
#include <chrono>

namespace LibCPU {

static inline int
ResultWord (UINT8 CONST *pRam, UINT32 At)
{
    return (int) (pRam[At] | (pRam[At + 1] << 8));
}

static inline int
RunTieredDemo (ICpuBackend *pTier0, ICpuBackend *pTier1)
{
    std::printf ("tiers: 0 = '%s' (cheap)  ->  1 = '%s' (optimizing, background)\n",
                 pTier0->GetName (), pTier1->GetName ());

    // Hot region @ 0: sum 5+4+3+2+1 in a loop -> [0x200] = 15.
    UINT8 const Hot[] = {
        0xB9, 0x05, 0x00,   // MOV CX, 5
        0xB8, 0x00, 0x00,   // MOV AX, 0
        0x01, 0xC8,         // loop: ADD AX, CX
        0x49,               //       DEC CX
        0x75, 0xFB,         //       JNZ loop
        0xA3, 0x00, 0x02    // MOV [0x200], AX
    };
    // Cold region @ 0x40: MOV AX,7 ; MOV [0x202],AX -> [0x202] = 7.
    UINT8 const Cold[] = {
        0xB8, 0x07, 0x00,   // MOV AX, 7
        0xA3, 0x02, 0x02    // MOV [0x202], AX
    };

    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    std::memcpy (Ram + 0x00, Hot,  sizeof (Hot));
    std::memcpy (Ram + 0x40, Cold, sizeof (Cold));

    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));

    ICpuArchitecture *pArch = CreateV20 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    CPU_TIER Tiers[2] = { { pTier0, pTier0->GetName () }, { pTier1, pTier1->GetName () } };
    LcTieredEngine Engine (pArch, Tiers, 2, /*HotThreshold=*/ 100);

    CPU_ADDR const HotEntry = 0,    HotEnd = (CPU_ADDR) sizeof (Hot);
    CPU_ADDR const ColdEntry = 0x40, ColdEnd = 0x40 + (CPU_ADDR) sizeof (Cold);

    bool Ok = true;

    // --- warm phase: interpret the hot loop; the upgrade fires at the threshold and
    //     compiles in the background (interpreter keeps running meanwhile). --------
    auto T0 = std::chrono::steady_clock::now ();
    for (int i = 0; i < 4000; i++) {
        Engine.Run (HotEntry, HotEnd, Ram, &State, nullptr);
        if (ResultWord (Ram, 0x200) != 15) { Ok = false; }
    }
    auto T1 = std::chrono::steady_clock::now ();

    // A cold region: run it a handful of times -- never hot, stays on tier 0.
    for (int i = 0; i < 3; i++) {
        Engine.Run (ColdEntry, ColdEnd, Ram, &State, nullptr);
        if (ResultWord (Ram, 0x202) != 7) { Ok = false; }
    }

    // Wait for the background recompile + hot-swap to land.
    Engine.Drain ();

    // --- optimized phase: the hot loop now runs on the better tier. -------------
    auto T2 = std::chrono::steady_clock::now ();
    for (int i = 0; i < 4000; i++) {
        Engine.Run (HotEntry, HotEnd, Ram, &State, nullptr);
        if (ResultWord (Ram, 0x200) != 15) { Ok = false; }
    }
    auto T3 = std::chrono::steady_clock::now ();

    UINT32 HotTier   = Engine.TierOf (HotEntry);
    UINT32 HotUp     = Engine.UpgradesOf (HotEntry);
    UINT64 HotCount  = Engine.CountOf (HotEntry);
    UINT32 ColdTier  = Engine.TierOf (ColdEntry);
    UINT64 ColdCount = Engine.CountOf (ColdEntry);

    double WarmNs = std::chrono::duration<double, std::nano> (T1 - T0).count () / 4000.0;
    double OptNs  = std::chrono::duration<double, std::nano> (T3 - T2).count () / 4000.0;

    std::printf ("  hot  @0x000: %llu runs, tier %u (%u upgrade%s)  -> [0x200] = %d (exp 15)\n",
                 (unsigned long long) HotCount, HotTier, HotUp, HotUp == 1 ? "" : "s", ResultWord (Ram, 0x200));
    std::printf ("  cold @0x040: %llu runs, tier %u             -> [0x202] = %d (exp 7)\n",
                 (unsigned long long) ColdCount, ColdTier, ResultWord (Ram, 0x202));
    std::printf ("  per-run: tier0 ~%.0f ns  ->  tier1 ~%.0f ns  (%.1fx)\n",
                 WarmNs, OptNs, OptNs > 0 ? WarmNs / OptNs : 0.0);

    Ok = Ok && HotTier == 1 && HotUp == 1 && ColdTier == 0;
    std::printf ("RESULT: %s  (hot promoted to tier 1, cold stayed tier 0, all results correct)\n",
                 Ok ? "PASS" : "FAIL");

    pArch->Release ();
    return Ok ? 0 : 1;
}

} // namespace LibCPU

#endif // LIBCPU_RUNTIERED_H
