/** @file
  The tiered-execution / PGO suite, ported to the MOS 6502 frontend.

  This is the SAME engine exercised by RunTiered.h (V20) -- TieredEngine,
  ProfiledAot, the AOT inliner, the edge-count instrumentation, and the
  in-artifact PC dispatcher -- driven through a different frontend. Nothing in
  the engine is V20-specific: it operates entirely on ICpuArchitecture, so the
  only things that change here are the guest programs (6502 machine code) and the
  factory (Create6502). The expected results and memory layout match the V20
  suite line-for-line, which is the point: the PGO machinery is frontend-agnostic.

  6502 notes: subroutines use JSR/RTS over the page-1 stack; the host seeds the
  stack pointer (S = 0xFF) since this slice has no TXS. Each program is stack-
  balanced, so S returns to 0xFF between runs. Leaf routines add a fixed amount
  via CLC + ADC #imm (the slice has no ADC $zp), so the loop/recursion bodies sum
  a constant per iteration -- chosen so the totals equal the V20 demos' (8, 15, 9,
  10, 6) for a direct comparison.
**/
#ifndef LIBCPU_RUNTIERED6502_H
#define LIBCPU_RUNTIERED6502_H

#include "Cpu6502.h"
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

// Seed a fresh 6502 state: zeroed, with the page-1 stack pointer at the top.
static inline VOID
Init6502State (CPU_STATE *pState)
{
    std::memset (pState, 0, sizeof (*pState));
    pState->Reg[Reg6502S] = 0xFF;
}

// Hot loop @0: A starts 0, the loop adds 3 five times (counter at $80 -- chosen
// clear of the code, since the AOT build snapshots code from RAM) -> [0x200] = 15.
// Cold region @0x40: LDA #7 -> [0x202] = 7.
static UINT8 const g_Hot[] = {
    0xA9, 0x05,         // 0:  LDA #5
    0x85, 0x80,         // 2:  STA $80        (counter = 5)
    0xA9, 0x00,         // 4:  LDA #0         (A = 0)
    0x18,               // 6:  loop: CLC
    0x69, 0x03,         // 7:        ADC #3   (A += 3)
    0xC6, 0x80,         // 9:        DEC $80
    0xD0, 0xF9,         // B:        BNE loop ($0006)
    0x8D, 0x00, 0x02    // D:  STA $0200      (= 15)
};
static UINT8 const g_Cold[] = {
    0xA9, 0x07,         // 0:  LDA #7
    0x8D, 0x02, 0x02    // 2:  STA $0202      (= 7)
};

// A caller @0 that JSRs the SAME leaf @3 (CLC; ADC #5; RTS) from TWO sites:
// [0x200] = 3+5 = 8 from site 1, [0x202] = 10+5 = 15 from site 2.
static UINT8 const g_Call[] = {
    0x4C, 0x07, 0x00,   // 0:  JMP main ($0007)
    0x18,               // 3:  sub: CLC
    0x69, 0x05,         // 4:       ADC #5
    0x60,               // 6:       RTS
    0xA9, 0x03,         // 7:  main: LDA #3
    0x20, 0x03, 0x00,   // 9:        JSR sub ($0003)   [site 1, ret 0x0C]
    0x8D, 0x00, 0x02,   // C:        STA $0200         (= 8)
    0xA9, 0x0A,         // F:        LDA #10
    0x20, 0x03, 0x00,   // 11:       JSR sub ($0003)   [site 2, ret 0x14]
    0x8D, 0x02, 0x02    // 14:       STA $0202         (= 15)
};

// main @E -> outer @7 (+5) -> inner @3 (+3): nested call tree. [0x200] = 1+5+3 = 9.
static UINT8 const g_Nested[] = {
    0x4C, 0x0E, 0x00,   // 0:  JMP main ($000E)
    0x18,               // 3:  inner: CLC
    0x69, 0x03,         // 4:         ADC #3
    0x60,               // 6:         RTS
    0x18,               // 7:  outer: CLC
    0x69, 0x05,         // 8:         ADC #5
    0x20, 0x03, 0x00,   // A:         JSR inner ($0003)
    0x60,               // D:         RTS
    0xA9, 0x01,         // E:  main:  LDA #1
    0x20, 0x07, 0x00,   // 10:        JSR outer ($0007)
    0x8D, 0x00, 0x02    // 13:        STA $0200         (= 9)
};

// A loop that JSRs sub @3 (A += 2) five times per run: [0x200] = 5*2 = 10. The one
// call SITE executes 5x per run -- region count under-counts the edge 5-fold.
static UINT8 const g_Loop[] = {
    0x4C, 0x07, 0x00,   // 0:  JMP main ($0007)
    0x18,               // 3:  sub: CLC
    0x69, 0x02,         // 4:       ADC #2
    0x60,               // 6:       RTS
    0xA9, 0x05,         // 7:  main: LDA #5
    0x85, 0x80,         // 9:        STA $80        (counter = 5, clear of code)
    0xA9, 0x00,         // B:        LDA #0         (A = 0)
    0x20, 0x03, 0x00,   // D:  loop: JSR sub ($0003)
    0xC6, 0x80,         // 10:       DEC $80
    0xD0, 0xF9,         // 12:       BNE loop ($000D)
    0x8D, 0x00, 0x02    // 14:       STA $0200      (= 10)
};

// Recursive: f(n) adds 2 to A and recurses while n>0 (n at $80, clear of code);
// main calls f(3). [0x200] = 2*3 = 6. Every return address (0x18 and 0x0E) is a
// known block, so the in-artifact dispatcher routes the whole recursion in ONE
// translation.
static UINT8 const g_Rec[] = {
    0x4C, 0x0F, 0x00,   // 0:  JMP main ($000F)
    0x18,               // 3:  f: CLC
    0x69, 0x02,         // 4:     ADC #2
    0xC6, 0x80,         // 6:     DEC $80
    0xD0, 0x01,         // 8:     BNE recurse ($000B)
    0x60,               // A:     RTS               (n == 0)
    0x20, 0x03, 0x00,   // B:  recurse: JSR f ($0003)  [ret 0x0E]
    0x60,               // E:     RTS
    0xA9, 0x03,         // F:  main: LDA #3
    0x85, 0x80,         // 11:       STA $80        (n = 3)
    0xA9, 0x00,         // 13:       LDA #0         (A = 0)
    0x20, 0x03, 0x00,   // 15:       JSR f ($0003)  [ret 0x18]
    0x8D, 0x00, 0x02    // 18:       STA $0200      (= 6)
};

static CPU_ADDR const HOT_ENTRY  = 0,    HOT_END  = (CPU_ADDR) sizeof (g_Hot);
static CPU_ADDR const COLD_ENTRY = 0x40, COLD_END = 0x40 + (CPU_ADDR) sizeof (g_Cold);

static inline VOID
Load6502Demo (UINT8 *pRam)
{
    std::memset (pRam, 0, 65536);
    std::memcpy (pRam + HOT_ENTRY,  g_Hot,  sizeof (g_Hot));
    std::memcpy (pRam + COLD_ENTRY, g_Cold, sizeof (g_Cold));
}

//
// JIT (online adaptive): a hot 6502 loop crosses the heat threshold and is
// recompiled on a background thread + hot-swapped to tier 1; a cold region stays
// on tier 0. Identical engine to the V20 demo -- only the guest differs.
//
static inline int
RunTieredDemo (ICpuBackend *pTier0, ICpuBackend *pTier1)
{
    std::printf ("== JIT (online adaptive): tier 0 = '%s'  ->  tier 1 = '%s' (background recompile) [6502]\n",
                 pTier0->GetName (), pTier1->GetName ());

    static UINT8 Ram[65536];
    Load6502Demo (Ram);

    CPU_STATE State;
    Init6502State (&State);

    ICpuArchitecture *pArch = Create6502 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    CPU_TIER Tiers[2] = { { pTier0, pTier0->GetName () }, { pTier1, pTier1->GetName () } };
    TieredEngine Engine (pArch, Tiers, 2, /*HotThreshold=*/ 100);

    bool Ok = true;

    auto T0 = std::chrono::steady_clock::now ();
    for (int i = 0; i < 4000; i++) {
        Engine.Run (HOT_ENTRY, HOT_END, Ram, &State, nullptr);
        if (ResultWord (Ram, 0x200) != 15) { Ok = false; }
    }
    auto T1 = std::chrono::steady_clock::now ();

    for (int i = 0; i < 3; i++) {
        Engine.Run (COLD_ENTRY, COLD_END, Ram, &State, nullptr);
        if (ResultWord (Ram, 0x202) != 7) { Ok = false; }
    }

    Engine.Drain ();

    auto T2 = std::chrono::steady_clock::now ();
    for (int i = 0; i < 4000; i++) {
        Engine.Run (HOT_ENTRY, HOT_END, Ram, &State, nullptr);
        if (ResultWord (Ram, 0x200) != 15) { Ok = false; }
    }
    auto T3 = std::chrono::steady_clock::now ();

    UINT32 HotTier   = Engine.TierOf (HOT_ENTRY);
    UINT32 HotUp     = Engine.UpgradesOf (HOT_ENTRY);
    UINT64 HotCount  = Engine.CountOf (HOT_ENTRY);
    UINT32 ColdTier  = Engine.TierOf (COLD_ENTRY);
    UINT64 ColdCount = Engine.CountOf (COLD_ENTRY);

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
// AOT (profile-guided): record a trace, persist it, reload it in a separate pass
// to pick tiers up front -- the hot region is native from its first run.
//
static inline int
RunProfiledAotDemo (ICpuBackend *pTier0, ICpuBackend *pTier1, CHAR8 CONST *pTracePath)
{
    std::printf ("\n== AOT (profile-guided): record a trace, persist it, re-use it to pick tiers up front [6502]\n");

    static UINT8 Ram[65536];
    Load6502Demo (Ram);
    CPU_STATE State;
    Init6502State (&State);

    ICpuArchitecture *pArch = Create6502 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    {
        CPU_TIER One[1] = { { pTier0, pTier0->GetName () } };
        TieredEngine Profiler (pArch, One, 1, ~(UINT64) 0);
        for (int i = 0; i < 200; i++) { Profiler.Run (HOT_ENTRY, HOT_END, Ram, &State, nullptr); }
        for (int i = 0; i < 3;   i++) { Profiler.Run (COLD_ENTRY, COLD_END, Ram, &State, nullptr); }
        PerfTrace Trace;
        Profiler.ExportTrace (Trace);
        Trace.Save (pTracePath);
        std::printf ("  profiled %u regions on '%s'; trace saved to %s\n",
                     Trace.RegionCount (), pTier0->GetName (), pTracePath);
    }

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
    Pgo.Build (Reloaded);

    UINT32 HotTier  = Pgo.TierOf (HOT_ENTRY);
    UINT32 ColdTier = Pgo.TierOf (COLD_ENTRY);

    bool Ok = true;
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
// PGO inlining (edge trace): a hot caller inlines a leaf callee from TWO sites
// (duplicate-and-specialize).
//
static inline int
RunInlineDemo (ICpuBackend *pCheap, ICpuBackend *pOpt, CHAR8 CONST *pTracePath)
{
    std::printf ("\n== PGO inlining (edge trace): hot caller inlines a leaf callee from TWO sites [6502]\n");

    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    std::memcpy (Ram, g_Call, sizeof (g_Call));
    CPU_STATE State;
    Init6502State (&State);

    ICpuArchitecture *pArch = Create6502 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    CPU_ADDR const Entry = 0, End = (CPU_ADDR) sizeof (g_Call);

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
        std::printf ("  edge: JSR@0x%llx -> 0x%llx (ret 0x%llx), count %llu\n",
                     (unsigned long long) E.Site, (unsigned long long) E.Callee,
                     (unsigned long long) E.ReturnPoint, (unsigned long long) E.Count);
    }

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

    std::printf ("  inlined %u copies of the callee; [0x200] = %d (exp 8), [0x202] = %d (exp 15)\n",
                 Inlined, Site1, Site2);

    Ok = Ok && Inlined == 2 && Site1 == 8 && Site2 == 15;
    std::printf ("RESULT: %s  (edge trace drove duplicate-and-specialize: 2 sites -> 2 private copies of the callee)\n",
                 Ok ? "PASS" : "FAIL");

    pArch->Release ();
    return Ok ? 0 : 1;
}

//
// PGO inlining (recursive): a hot call tree (main -> outer -> inner) is inlined
// whole from ONE top-level site -- no CALL/RET, no dispatcher anywhere.
//
static inline int
RunNestedInlineDemo (ICpuBackend *pCheap, ICpuBackend *pOpt, CHAR8 CONST *pTracePath)
{
    std::printf ("\n== PGO inlining (recursive): inline a whole call tree (main -> outer -> inner) [6502]\n");

    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    std::memcpy (Ram, g_Nested, sizeof (g_Nested));
    CPU_STATE State;
    Init6502State (&State);

    ICpuArchitecture *pArch = Create6502 ();
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
        std::printf ("    JSR@0x%llx -> 0x%llx (count %llu)\n",
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
// True per-edge counters: a JSR inside a loop runs many times per region run, so
// propagating the REGION count to the edge under-counts it. An instrumented build
// counts the call site directly. Requires ICpuProfileEmitter (else SKIP).
//
static inline int
RunEdgeCountDemo (ICpuBackend *pCheap, ICpuBackend *pOpt)
{
    std::printf ("\n== True per-edge counters: a JSR in a loop (region count under-counts the edge) [6502]\n");

    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    std::memcpy (Ram, g_Loop, sizeof (g_Loop));
    CPU_STATE State;
    Init6502State (&State);

    ICpuArchitecture *pArch = Create6502 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));
    CPU_ADDR const Entry = 0, End = (CPU_ADDR) sizeof (g_Loop);
    UINT32 const Runs = 200;

    // (a) TRUE counts: instrumented profile. Emitted through ICpuProfileEmitter,
    // which only the in-process 'interp' backend implements; elsewhere
    // GenerateAotCfgProfiling returns E_NOTIMPL -- a capability gap, report SKIP so
    // a cross-backend sweep stays clean.
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

    std::printf ("  region ran %u times; the loop JSRs sub 5x per run\n", Runs);
    std::printf ("  edge count: TRUE = %llu (instrumented)   vs   PROPAGATED = %llu (region count)\n",
                 (unsigned long long) TrueEdge, (unsigned long long) PropEdge);

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
// Code-size budget: two hot JSR sites of the same leaf (CLC; ADC; RTS = 3 instrs
// each). A budget of 3 fits only ONE copy (hottest site inlines; the other stays a
// real JSR); a budget of 0 inlines both. Results stay correct either way.
//
static inline int
RunBudgetDemo (ICpuBackend *pCheap, ICpuBackend *pOpt)
{
    std::printf ("\n== Code-size budget: cap inlining; only the hottest site fits a small budget [6502]\n");

    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    std::memcpy (Ram, g_Call, sizeof (g_Call));
    CPU_STATE State;
    Init6502State (&State);

    ICpuArchitecture *pArch = Create6502 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));
    CPU_ADDR const Entry = 0, End = (CPU_ADDR) sizeof (g_Call);

    PerfTrace Trace;
    {
        CPU_TIER One[1] = { { pCheap, pCheap->GetName () } };
        TieredEngine Profiler (pArch, One, 1, ~(UINT64) 0);
        for (int i = 0; i < 200; i++) { Profiler.Run (Entry, End, Ram, &State, nullptr); }
        Profiler.ExportTrace (Trace);
    }

    UINT32 Budgets[2] = { 3, 0 };   // 3 = room for one 3-instr copy; 0 = unlimited
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

    std::printf ("  budget 3 (one copy):  inlined %d site(s); results %d/%d (exp 8/15)\n", Inl[0], Res1[0], Res2[0]);
    std::printf ("  budget 0 (unlimited): inlined %d site(s); results %d/%d (exp 8/15)\n", Inl[1], Res1[1], Res2[1]);

    bool Ok = Inl[0] == 1 && Inl[1] == 2
              && Res1[0] == 8 && Res2[0] == 15 && Res1[1] == 8 && Res2[1] == 15;
    std::printf ("RESULT: %s  (budget capped inlining to the hottest site; results correct with the mix)\n",
                 Ok ? "PASS" : "FAIL");

    pArch->Release ();
    return Ok ? 0 : 1;
}

//
// Recursion via the dispatcher: f(3) adds 2 three times = 6, in ONE translation
// (every return address is a known block). A recursive callee is NOT inlined.
//
static inline int
RunRecursionDemo (ICpuBackend *pBackend)
{
    std::printf ("\n== Recursion (via the dispatcher): f(3) = 2+2+2 = 6 in one translation [6502]\n");

    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));
    std::memcpy (Ram, g_Rec, sizeof (g_Rec));
    CPU_STATE State;
    Init6502State (&State);

    ICpuArchitecture *pArch = Create6502 ();
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

#endif // LIBCPU_RUNTIERED6502_H
