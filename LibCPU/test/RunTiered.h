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
#include "../aot/AotGenerator.h"
#include "LibCPU/PCom.h"
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
// A caller @0 that CALLs the SAME leaf sub @3 (ADD AX,5; RET) from TWO sites:
// [0x200] = 3+5 = 8 from site 1, [0x202] = 10+5 = 15 from site 2.
static UINT8 const g_Call[] = {
    0xE9, 0x04, 0x00,   // 0:  JMP main (IP 7)
    0x05, 0x05, 0x00,   // 3:  sub: ADD AX, 5
    0xC3,               // 6:       RET
    0xBC, 0x00, 0x10,   // 7:  main: MOV SP, 0x1000
    0xB8, 0x03, 0x00,   // A:        MOV AX, 3
    0xE8, 0xF3, 0xFF,   // D:        CALL sub (IP 3)   [site 1, ret 0x10]
    0xA3, 0x00, 0x02,   // 10:       MOV [0x200], AX   (= 8)
    0xB8, 0x0A, 0x00,   // 13:       MOV AX, 10
    0xE8, 0xEA, 0xFF,   // 16:       CALL sub (IP 3)   [site 2, ret 0x19]
    0xA3, 0x02, 0x02    // 19:       MOV [0x202], AX   (= 15)
};
// main @E -> outer @7 (+5) -> inner @3 (+3): nested call tree. [0x200] = 1+5+3 = 9.
static UINT8 const g_Nested[] = {
    0xE9, 0x0B, 0x00,   // 0:  JMP main (IP 0xE)
    0x05, 0x03, 0x00,   // 3:  inner: ADD AX, 3
    0xC3,               // 6:         RET
    0x05, 0x05, 0x00,   // 7:  outer: ADD AX, 5
    0xE8, 0xF6, 0xFF,   // A:         CALL inner (IP 3)
    0xC3,               // D:         RET
    0xBC, 0x00, 0x10,   // E:  main:  MOV SP, 0x1000
    0xB8, 0x01, 0x00,   // 11:        MOV AX, 1
    0xE8, 0xF0, 0xFF,   // 14:        CALL outer (IP 7)
    0xA3, 0x00, 0x02    // 17:        MOV [0x200], AX
};

// A loop that CALLs sub @3 (AX += 2) five times per run: [0x200] = 5*2 = 10. The one
// call SITE executes 5x per run -- region count under-counts the edge 5-fold.
static UINT8 const g_Loop[] = {
    0xE9, 0x04, 0x00,   // 0:  JMP main (IP 7)
    0x05, 0x02, 0x00,   // 3:  sub: ADD AX, 2
    0xC3,               // 6:       RET
    0xBC, 0x00, 0x10,   // 7:  main: MOV SP, 0x1000
    0xB9, 0x05, 0x00,   // A:        MOV CX, 5
    0xB8, 0x00, 0x00,   // D:        MOV AX, 0
    0xE8, 0xF0, 0xFF,   // 10: loop: CALL sub (IP 3)
    0x49,               // 13:       DEC CX
    0x75, 0xFA,         // 14:       JNZ loop (IP 0x10)
    0xA3, 0x00, 0x02    // 16:       MOV [0x200], AX
};

// Recursive sum: f(CX) accumulates CX+...+1 into BX, recursing; main calls f(3).
// [0x200] = 1+2+3 = 6. The recursion runs through real CALL/RET -- every return
// address (B and 0x18) is a known block, so the in-artifact dispatcher routes them
// all and the whole recursion resolves in ONE translation.
static UINT8 const g_Rec[] = {
    0xE9, 0x09, 0x00,         // 0:  JMP main (IP 0xC)
    0x01, 0xCB,               // 3:  f: ADD BX, CX
    0x49,                     // 5:     DEC CX
    0x74, 0x03,               // 6:     JZ done (IP 0xB)
    0xE8, 0xF8, 0xFF,         // 8:     CALL f (IP 3)
    0xC3,                     // B:  done: RET
    0xBC, 0x00, 0x10,         // C:  main: MOV SP, 0x1000
    0xB9, 0x03, 0x00,         // F:        MOV CX, 3
    0xBB, 0x00, 0x00,         // 12:       MOV BX, 0
    0xE8, 0xEB, 0xFF,         // 15:       CALL f (IP 3)
    0x89, 0x1E, 0x00, 0x02    // 18:       MOV [0x200], BX
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
    TieredEngine Engine (pArch, Tiers, 2, /*HotThreshold=*/ 100);

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
        TieredEngine Profiler (pArch, One, 1, ~(UINT64) 0);   // never promotes: pure counting
        for (int i = 0; i < 200; i++) { Profiler.Run (HOT_ENTRY, HOT_END, Ram, &State, nullptr); }
        for (int i = 0; i < 3;   i++) { Profiler.Run (COLD_ENTRY, COLD_END, Ram, &State, nullptr); }
        PerfTrace Trace;
        Profiler.ExportTrace (Trace);
        Trace.Save (pTracePath);
        std::printf ("  profiled %u regions on '%s'; trace saved to %s\n",
                     Trace.RegionCount (), pTier0->GetName (), pTracePath);
    }

    // ---- re-use pass: a fresh run loads the trace and compiles each region at the
    //      tier its recorded hotness warrants -- BEFORE executing anything. ---------
    PerfTrace Reloaded;
    if (!Reloaded.Load (pTracePath)) {
        std::printf ("  failed to reload trace\n");
        pArch->Release ();
        return 1;
    }
    std::printf ("  reloaded trace: hot count=%llu, cold count=%llu\n",
                 (unsigned long long) Reloaded.CountOf (HOT_ENTRY),
                 (unsigned long long) Reloaded.CountOf (COLD_ENTRY));

    CPU_TIER Tiers[2] = { { pTier0, pTier0->GetName () }, { pTier1, pTier1->GetName () } };
    ProfiledAot Pgo (pArch, Tiers, 2, /*HotThreshold=*/ 100);
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

//
// Edge-aware PGO: an edge/call-count trace lets profile-guided AOT INLINE a hot
// callee -- eliding the CALL's return-address push, the RET's pop, and the dispatcher
// round-trip. A caller @0 hot-calls a leaf sub @3; the trace records the edge, and
// the inlined build embeds sub's body, branching its RET straight to the continuation.
//
static inline int
RunInlineDemo (ICpuBackend *pCheap, ICpuBackend *pOpt, CHAR8 CONST *pTracePath)
{
    std::printf ("\n== PGO inlining (edge trace): hot caller inlines a leaf callee from TWO sites\n");

    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    std::memcpy (Ram, g_Call, sizeof (g_Call));
    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));

    ICpuArchitecture *pArch = CreateV20 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    CPU_ADDR const Entry = 0, End = (CPU_ADDR) sizeof (g_Call);

    // ---- profiling pass: count the region; ExportTrace weights the call edge. -----
    {
        CPU_TIER One[1] = { { pCheap, pCheap->GetName () } };
        TieredEngine Profiler (pArch, One, 1, ~(UINT64) 0);
        for (int i = 0; i < 200; i++) { Profiler.Run (Entry, End, Ram, &State, nullptr); }
        PerfTrace Trace;
        Profiler.ExportTrace (Trace);
        Trace.Save (pTracePath);
    }

    PerfTrace Trace;
    Trace.Load (pTracePath);
    for (UINT32 i = 0; i < Trace.EdgeCount (); i++) {
        CPU_TRACE_EDGE E = Trace.Edge (i);
        std::printf ("  edge: CALL@0x%llx -> 0x%llx (ret 0x%llx), count %llu\n",
                     (unsigned long long) E.Site, (unsigned long long) E.Callee,
                     (unsigned long long) E.ReturnPoint, (unsigned long long) E.Count);
    }

    // ---- profile-guided AOT with inlining. ----------------------------------------
    CPU_TIER Tiers[2] = { { pCheap, pCheap->GetName () }, { pOpt, pOpt->GetName () } };
    ProfiledAot Pgo (pArch, Tiers, 2, /*HotThreshold=*/ 100);
    Pgo.Build (Trace);
    UINT32 Inlined = Pgo.InlinedCount ();

    bool Ok = true;
    for (int i = 0; i < 4000; i++) {
        Pgo.Run (Entry, Ram, &State, nullptr);
        if (ResultWord (Ram, 0x200) != 8 || ResultWord (Ram, 0x202) != 15) { Ok = false; }
    }
    int Site1 = ResultWord (Ram, 0x200);
    int Site2 = ResultWord (Ram, 0x202);

    // ---- benefit: same optimizing backend, with vs without inlining. --------------
    ComPtr<ICpuCode> Plain;
    GenerateAotCfg (pArch, pOpt, Entry, End, &Plain, nullptr);
    double PlainNs = 0, InlinedNs = 0;
    int PlainResult = -1;
    if (Plain != nullptr) {
        std::memset (Ram + 0x200, 0, 2);
        auto P0 = std::chrono::steady_clock::now ();
        for (int i = 0; i < 20000; i++) { Plain->Execute (Ram, &State, nullptr); }
        auto P1 = std::chrono::steady_clock::now ();
        PlainResult = ResultWord (Ram, 0x200);
        PlainNs = std::chrono::duration<double, std::nano> (P1 - P0).count () / 20000.0;

        std::memset (Ram + 0x200, 0, 2);
        auto Q0 = std::chrono::steady_clock::now ();
        for (int i = 0; i < 20000; i++) { Pgo.Run (Entry, Ram, &State, nullptr); }
        auto Q1 = std::chrono::steady_clock::now ();
        InlinedNs = std::chrono::duration<double, std::nano> (Q1 - Q0).count () / 20000.0;
    }

    std::printf ("  inlined %u copies of the callee; [0x200] = %d (exp 8), [0x202] = %d (exp 15)\n",
                 Inlined, Site1, Site2);
    std::printf ("  on '%s': CALL/RET via dispatcher ~%.0f ns  ->  inlined ~%.0f ns  (%.2fx)  [plain results %d/%d]\n",
                 pOpt->GetName (), PlainNs, InlinedNs, InlinedNs > 0 ? PlainNs / InlinedNs : 0.0,
                 PlainResult, ResultWord (Ram, 0x202));

    Ok = Ok && Inlined == 2 && Site1 == 8 && Site2 == 15 && PlainResult == 8;
    std::printf ("RESULT: %s  (edge trace drove duplicate-and-specialize: 2 sites -> 2 private copies of the callee)\n",
                 Ok ? "PASS" : "FAIL");

    pArch->Release ();
    return Ok ? 0 : 1;
}

//
// Recursive inlining: a hot call tree (main -> outer -> inner) is inlined whole. One
// top-level site expands to a TREE of copies (outer + inner), each nested RET
// branching to its parent copy's continuation -- no CALL/RET, no dispatcher anywhere.
//
static inline int
RunNestedInlineDemo (ICpuBackend *pCheap, ICpuBackend *pOpt, CHAR8 CONST *pTracePath)
{
    std::printf ("\n== PGO inlining (recursive): inline a whole call tree (main -> outer -> inner)\n");

    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    std::memcpy (Ram, g_Nested, sizeof (g_Nested));
    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));

    ICpuArchitecture *pArch = CreateV20 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    CPU_ADDR const Entry = 0, End = (CPU_ADDR) sizeof (g_Nested);

    {
        CPU_TIER One[1] = { { pCheap, pCheap->GetName () } };
        TieredEngine Profiler (pArch, One, 1, ~(UINT64) 0);
        for (int i = 0; i < 200; i++) { Profiler.Run (Entry, End, Ram, &State, nullptr); }
        PerfTrace Trace;
        Profiler.ExportTrace (Trace);
        Trace.Save (pTracePath);
    }

    PerfTrace Trace;
    Trace.Load (pTracePath);
    std::printf ("  trace edges (incl. nested): %u\n", Trace.EdgeCount ());
    for (UINT32 i = 0; i < Trace.EdgeCount (); i++) {
        CPU_TRACE_EDGE E = Trace.Edge (i);
        std::printf ("    CALL@0x%llx -> 0x%llx (count %llu)\n",
                     (unsigned long long) E.Site, (unsigned long long) E.Callee, (unsigned long long) E.Count);
    }

    CPU_TIER Tiers[2] = { { pCheap, pCheap->GetName () }, { pOpt, pOpt->GetName () } };
    ProfiledAot Pgo (pArch, Tiers, 2, /*HotThreshold=*/ 100);
    Pgo.Build (Trace);
    UINT32 Inlined = Pgo.InlinedCount ();

    bool Ok = true;
    for (int i = 0; i < 4000; i++) {
        Pgo.Run (Entry, Ram, &State, nullptr);
        if (ResultWord (Ram, 0x200) != 9) { Ok = false; }
    }
    int Result = ResultWord (Ram, 0x200);

    std::printf ("  inlined %u copies (outer + inner from ONE top-level site); [0x200] = %d (exp 9)\n",
                 Inlined, Result);

    Ok = Ok && Inlined == 2 && Result == 9;
    std::printf ("RESULT: %s  (recursive inlining: the whole call tree embedded, every RET a direct branch)\n",
                 Ok ? "PASS" : "FAIL");

    pArch->Release ();
    return Ok ? 0 : 1;
}

//
// True per-edge runtime counters: a CALL inside a loop runs many times per region
// run, so propagating the REGION count to the edge under-counts it. An instrumented
// build counts the call site directly. With a threshold between the two, true counts
// inline the hot in-loop call while propagated counts would miss it.
//
static inline int
RunEdgeCountDemo (ICpuBackend *pCheap, ICpuBackend *pOpt)
{
    std::printf ("\n== True per-edge counters: a CALL in a loop (region count under-counts the edge)\n");

    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    std::memcpy (Ram, g_Loop, sizeof (g_Loop));
    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));

    ICpuArchitecture *pArch = CreateV20 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));
    CPU_ADDR const Entry = 0, End = (CPU_ADDR) sizeof (g_Loop);
    UINT32 const Runs = 200;

    // (a) TRUE counts: instrumented profile. The instrumentation is emitted through
    // ICpuProfileEmitter, which only the in-process 'interp' backend implements; on
    // any other backend GenerateAotCfgProfiling returns E_NOTIMPL. That is a backend
    // capability gap, not a failure -- report it as SKIP so a cross-backend sweep
    // stays clean instead of flagging a feature the target was never meant to have.
    PerfTrace TrueTrace;
    HRESULT HrProf = CollectEdgeProfile (pArch, pCheap, Entry, End, Ram, &State, Runs, TrueTrace);
    if (HrProf == E_NOTIMPL) {
        std::printf ("  backend '%s' has no ICpuProfileEmitter -- edge instrumentation N/A\n",
                     pCheap->GetName ());
        std::printf ("RESULT: SKIP  (true per-edge counters need a profile-capable backend; only 'interp' qualifies)\n");
        pArch->Release ();
        return 0;
    }
    UINT64 TrueEdge = TrueTrace.EdgeCount () > 0 ? TrueTrace.Edge (0).Count : 0;

    // (b) PROPAGATED counts: the JIT engine attributes the region count to the edge.
    PerfTrace PropTrace;
    {
        CPU_TIER One[1] = { { pCheap, pCheap->GetName () } };
        TieredEngine Profiler (pArch, One, 1, ~(UINT64) 0);
        for (UINT32 i = 0; i < Runs; i++) { Profiler.Run (Entry, End, Ram, &State, nullptr); }
        Profiler.ExportTrace (PropTrace);
    }
    UINT64 PropEdge = PropTrace.EdgeCount () > 0 ? PropTrace.Edge (0).Count : 0;

    std::printf ("  region ran %u times; the loop CALLs sub 5x per run\n", Runs);
    std::printf ("  edge count: TRUE = %llu (instrumented)   vs   PROPAGATED = %llu (region count)\n",
                 (unsigned long long) TrueEdge, (unsigned long long) PropEdge);

    // With a threshold between the two, the decision diverges.
    UINT64 const Threshold = 500;
    int Inl[2];
    for (int Which = 0; Which < 2; Which++) {
        PerfTrace CONST &T = (Which == 0) ? TrueTrace : PropTrace;
        CPU_TIER Tiers[2] = { { pCheap, pCheap->GetName () }, { pOpt, pOpt->GetName () } };
        ProfiledAot Pgo (pArch, Tiers, 2, Threshold);
        Pgo.Build (T);
        Inl[Which] = (int) Pgo.InlinedCount ();
        std::memset (Ram + 0x200, 0, 2);
        Pgo.Run (Entry, Ram, &State, nullptr);
    }
    int Result = ResultWord (Ram, 0x200);

    std::printf ("  with threshold %llu: TRUE trace inlines %d callee(s); PROPAGATED inlines %d; result=%d (exp 10)\n",
                 (unsigned long long) Threshold, Inl[0], Inl[1], Result);

    bool Ok = TrueEdge == (UINT64) (Runs * 5) && PropEdge == (UINT64) Runs
              && Inl[0] == 1 && Inl[1] == 0 && Result == 10;
    std::printf ("RESULT: %s  (true counters saw 5x the calls and inlined the hot in-loop call; propagation missed it)\n",
                 Ok ? "PASS" : "FAIL");

    pArch->Release ();
    return Ok ? 0 : 1;
}

//
// Code-size budget: two hot call sites of the same leaf (2 instrs each, cost 2). A
// budget of 2 fits only ONE copy, so only the hottest site inlines; the other stays a
// real CALL (dispatcher). A budget of 0 (unlimited) inlines both. Results stay correct
// either way (mixed inlined / non-inlined).
//
static inline int
RunBudgetDemo (ICpuBackend *pCheap, ICpuBackend *pOpt)
{
    std::printf ("\n== Code-size budget: cap inlining; only the hottest site fits a small budget\n");

    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    std::memcpy (Ram, g_Call, sizeof (g_Call));   // 2 sites -> the same leaf sub
    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));

    ICpuArchitecture *pArch = CreateV20 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));
    CPU_ADDR const Entry = 0, End = (CPU_ADDR) sizeof (g_Call);

    PerfTrace Trace;
    {
        CPU_TIER One[1] = { { pCheap, pCheap->GetName () } };
        TieredEngine Profiler (pArch, One, 1, ~(UINT64) 0);
        for (int i = 0; i < 200; i++) { Profiler.Run (Entry, End, Ram, &State, nullptr); }
        Profiler.ExportTrace (Trace);
    }

    UINT32 Budgets[2] = { 2, 0 };   // 2 = room for one copy; 0 = unlimited
    int    Inl[2], Res1[2], Res2[2];
    for (int W = 0; W < 2; W++) {
        CPU_TIER Tiers[2] = { { pCheap, pCheap->GetName () }, { pOpt, pOpt->GetName () } };
        ProfiledAot Pgo (pArch, Tiers, 2, /*HotThreshold=*/ 100, /*InlineBudget=*/ Budgets[W]);
        Pgo.Build (Trace);
        Inl[W] = (int) Pgo.InlinedCount ();
        std::memset (Ram + 0x200, 0, 4);
        Pgo.Run (Entry, Ram, &State, nullptr);
        Res1[W] = ResultWord (Ram, 0x200);
        Res2[W] = ResultWord (Ram, 0x202);
    }

    std::printf ("  budget 2 (one copy):  inlined %d site(s); results %d/%d (exp 8/15)\n", Inl[0], Res1[0], Res2[0]);
    std::printf ("  budget 0 (unlimited): inlined %d site(s); results %d/%d (exp 8/15)\n", Inl[1], Res1[1], Res2[1]);

    bool Ok = Inl[0] == 1 && Inl[1] == 2
              && Res1[0] == 8 && Res2[0] == 15 && Res1[1] == 8 && Res2[1] == 15;
    std::printf ("RESULT: %s  (budget capped inlining to the hottest site; results correct with the mix)\n",
                 Ok ? "PASS" : "FAIL");

    pArch->Release ();
    return Ok ? 0 : 1;
}

//
// Recursion. f(CX) recurses to depth CX; every return address is a known block, so
// the in-artifact dispatcher resolves the whole recursion with no host round-trip.
// (Note: a recursive callee is NOT inlined -- inlining elides the return-address
// pushes that the recursion's unwinding relies on, so the two return models are
// incompatible at the inline/recursion boundary. Recursion is handled by the
// dispatcher; only non-recursive trees inline.)
//
static inline int
RunRecursionDemo (ICpuBackend *pBackend)
{
    std::printf ("\n== Recursion (via the dispatcher): f(3) = 1+2+3 = 6 in one translation\n");

    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    std::memcpy (Ram, g_Rec, sizeof (g_Rec));
    CPU_STATE State;
    std::memset (&State, 0, sizeof (State));

    ICpuArchitecture *pArch = CreateV20 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));
    CPU_ADDR const Entry = 0, End = (CPU_ADDR) sizeof (g_Rec);

    CPU_ADDR Resume = Entry;
    int      Trans  = 0;
    for (int Iter = 0; Iter < 32; Iter++) {
        ComPtr<ICpuCode> Code;
        UINT32 N = 0;
        if (FAILED (GenerateAotCfg (pArch, pBackend, Resume, End, &Code, &N)) || Code == nullptr) {
            break;
        }
        Trans++;
        State.TrapPc = CPU_SMC_NO_TRAP;
        Code->Execute (Ram, &State, nullptr);
        if (State.TrapPc == CPU_SMC_NO_TRAP) {
            break;
        }
        Resume = (CPU_ADDR) State.TrapPc;
    }
    int Result = ResultWord (Ram, 0x200);

    std::printf ("  f(3) -> [0x200] = %d (exp 6) in %d translation%s\n",
                 Result, Trans, Trans == 1 ? "" : "s");

    bool Ok = Result == 6;
    std::printf ("RESULT: %s  (recursion of depth 3 resolved through the in-artifact dispatcher)\n",
                 Ok ? "PASS" : "FAIL");
    pArch->Release ();
    return Ok ? 0 : 1;
}

} // namespace LibCPU

#endif // LIBCPU_RUNTIERED_H
