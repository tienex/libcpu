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
#include "../core/PerfTrace.h"
#include "../core/ProfiledAot.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <cstdint>

namespace LibCPU {

static inline int
ResultWord (UINT8 CONST *pRam, UINT32 At)
{
    return (int) (pRam[At] | (pRam[At + 1] << 8));
}

// Shared guest layout for both demos: a hot loop @0 (sum 5..1 -> [0x200]=15) and a
// cold straight-line region @0x40 (MOV AX,7 -> [0x202]=7).
static UINT8 const g_Hot[] = {
    0xB9, 0x05, 0x00,   // MOV CX, 5
    0xB8, 0x00, 0x00,   // MOV AX, 0
    0x01, 0xC8,         // loop: ADD AX, CX
    0x49,               //       DEC CX
    0x75, 0xFB,         //       JNZ loop
    0xA3, 0x00, 0x02    // MOV [0x200], AX
};
static UINT8 const g_Cold[] = {
    0xB8, 0x07, 0x00,   // MOV AX, 7
    0xA3, 0x02, 0x02    // MOV [0x202], AX
};
static CPU_ADDR const HOT_ENTRY  = 0,    HOT_END  = (CPU_ADDR) sizeof (g_Hot);
static CPU_ADDR const COLD_ENTRY = 0x40, COLD_END = 0x40 + (CPU_ADDR) sizeof (g_Cold);

static inline VOID
LoadV20Demo (UINT8 *pRam)
{
    std::memset (pRam, 0, 65536);
    std::memcpy (pRam + HOT_ENTRY,  g_Hot,  sizeof (g_Hot));
    std::memcpy (pRam + COLD_ENTRY, g_Cold, sizeof (g_Cold));
}

static inline int
RunTieredDemo (ICpuBackend *pTier0, ICpuBackend *pTier1)
{
    std::printf ("== JIT (online adaptive): tier 0 = '%s'  ->  tier 1 = '%s' (background recompile)\n",
                 pTier0->GetName (), pTier1->GetName ());

    static UINT8 Ram[65536];
    LoadV20Demo (Ram);

    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));

    ICpuArchitecture *pArch = CreateV20 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    CPU_TIER Tiers[2] = { { pTier0, pTier0->GetName () }, { pTier1, pTier1->GetName () } };
    LcTieredEngine Engine (pArch, Tiers, 2, /*HotThreshold=*/ 100);

    CPU_ADDR const HotEntry = HOT_ENTRY,  HotEnd  = HOT_END;
    CPU_ADDR const ColdEntry = COLD_ENTRY, ColdEnd = COLD_END;

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

//
// AOT counterpart: a profiling pass emits a performance trace, the trace is
// persisted, then a SEPARATE pass reloads it and builds profile-guided AOT --
// tiers chosen ahead of time from the trace, the hot region native from its first
// run, no background recompiles.
//
static inline int
RunProfiledAotDemo (ICpuBackend *pTier0, ICpuBackend *pTier1, CHAR8 CONST *pTracePath)
{
    std::printf ("\n== AOT (profile-guided): record a trace, persist it, re-use it to pick tiers up front\n");

    static UINT8 Ram[65536];
    LoadV20Demo (Ram);
    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));

    ICpuArchitecture *pArch = CreateV20 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    // ---- profiling pass: count on the cheap tier only (threshold so high it never
    //      upgrades), then export + persist the trace. -----------------------------
    {
        CPU_TIER One[1] = { { pTier0, pTier0->GetName () } };
        LcTieredEngine Profiler (pArch, One, 1, ~(UINT64) 0);   // never promotes: pure counting
        for (int i = 0; i < 200; i++) { Profiler.Run (HOT_ENTRY, HOT_END, Ram, &State, nullptr); }
        for (int i = 0; i < 3;   i++) { Profiler.Run (COLD_ENTRY, COLD_END, Ram, &State, nullptr); }
        LcPerfTrace Trace;
        Profiler.ExportTrace (Trace);
        Trace.Save (pTracePath);
        std::printf ("  profiled %u regions on '%s'; trace saved to %s\n",
                     Trace.RegionCount (), pTier0->GetName (), pTracePath);
    }

    // ---- re-use pass: a fresh run loads the trace and compiles each region at the
    //      tier its recorded hotness warrants -- BEFORE executing anything. ---------
    LcPerfTrace Reloaded;
    if (!Reloaded.Load (pTracePath)) {
        std::printf ("  failed to reload trace\n");
        pArch->Release ();
        return 1;
    }
    std::printf ("  reloaded trace: hot count=%llu, cold count=%llu\n",
                 (unsigned long long) Reloaded.CountOf (HOT_ENTRY),
                 (unsigned long long) Reloaded.CountOf (COLD_ENTRY));

    CPU_TIER Tiers[2] = { { pTier0, pTier0->GetName () }, { pTier1, pTier1->GetName () } };
    LcProfiledAot Pgo (pArch, Tiers, 2, /*HotThreshold=*/ 100);
    Pgo.Build (Reloaded);   // hot -> tier 1 (optimizing) compiled up front; cold -> tier 0

    UINT32 HotTier  = Pgo.TierOf (HOT_ENTRY);
    UINT32 ColdTier = Pgo.TierOf (COLD_ENTRY);

    bool Ok = true;
    // The very FIRST run of the hot region already executes optimized code -- no
    // warm-up, no in-flight recompile.
    for (int i = 0; i < 4000; i++) {
        Pgo.Run (HOT_ENTRY, Ram, &State, nullptr);
        if (ResultWord (Ram, 0x200) != 15) { Ok = false; }
    }
    for (int i = 0; i < 3; i++) {
        Pgo.Run (COLD_ENTRY, Ram, &State, nullptr);
        if (ResultWord (Ram, 0x202) != 7) { Ok = false; }
    }

    std::printf ("  hot  @0x000: built at tier %u ('%s')  -> [0x200] = %d (exp 15)\n",
                 HotTier, Tiers[HotTier].pLabel, ResultWord (Ram, 0x200));
    std::printf ("  cold @0x040: built at tier %u ('%s')  -> [0x202] = %d (exp 7)\n",
                 ColdTier, Tiers[ColdTier].pLabel, ResultWord (Ram, 0x202));

    Ok = Ok && HotTier == 1 && ColdTier == 0;
    std::printf ("RESULT: %s  (trace re-used; hot AOT-built at the optimizing tier ahead of time, cold at the cheap tier)\n",
                 Ok ? "PASS" : "FAIL");

    pArch->Release ();
    return Ok ? 0 : 1;
}

} // namespace LibCPU

#endif // LIBCPU_RUNTIERED_H
