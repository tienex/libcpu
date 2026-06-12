/** @file
  Profile-guided AOT builder (LibCPU core).

  The AOT counterpart of LcTieredEngine. Where the JIT engine decides tiers AT RUN
  TIME (count, then background-recompile), this decides them AHEAD OF TIME from a
  persisted performance trace: Build() walks the trace and compiles each region once
  at the tier its recorded hotness justifies (hot -> optimizing backend, cold ->
  cheap backend). Run() is then pure execution -- no counting, no recompiling, no
  background threads -- so a hot region is already native on its very first run.

  Workflow: a profiling run emits a trace (LcTieredEngine::ExportTrace or any
  counting pass) -> LcPerfTrace::Save -> a later run LcPerfTrace::Load -> Build ->
  Run. The expensive optimizing compiles happen once, up front, reused thereafter.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_PROFILEDAOT_H
#define LIBCPU_PROFILEDAOT_H

#include "LibCPU/ICpu.h"
#include "LibCPU/CpuState.h"
#include "TieredEngine.h"   // CPU_TIER
#include "PerfTrace.h"
#include <map>
#include <set>
#include <vector>

namespace LibCPU {

// Profile the call edges of [Entry, End) with TRUE runtime counts: build an
// instrumented version, run it Runs times (reusing pState, whose EdgeCount[] holds
// the counters), and fill Trace with each edge's real frequency plus the region.
// The backend must expose ICpuProfileEmitter (the interpreter does).
HRESULT LcCollectEdgeProfile (ICpuArchitecture *pArch, ICpuBackend *pBackend,
                              CPU_ADDR Entry, CPU_ADDR End,
                              VOID *pRAM, CPU_STATE *pState, UINT32 Runs, LcPerfTrace &Trace);

class LcProfiledAot {
public:
    // InlineBudget caps the total INSTRUCTIONS Build() may duplicate by inlining, per
    // region (0 = unlimited). The hottest call edges are inlined first, until the
    // budget is spent -- so a region never blows up in size chasing every hot call.
    LcProfiledAot (ICpuArchitecture *pArch, CPU_TIER CONST *pTiers, UINT32 TierCount,
                   UINT64 HotThreshold, UINT32 InlineBudget = 0);
    ~LcProfiledAot ();

    LcProfiledAot (LcProfiledAot CONST &)            = delete;
    LcProfiledAot &operator= (LcProfiledAot CONST &) = delete;

    // Compile every region in the trace at the tier its recorded count justifies.
    HRESULT Build (LcPerfTrace CONST &Trace);

    // Execute a region that Build() compiled. Pure execution -- no profiling.
    CPU_EXEC_STATUS Run (CPU_ADDR Entry, VOID *pRAM, VOID *pGRF, VOID *pFRF);

    UINT32 TierOf      (CPU_ADDR Entry) CONST;
    UINT32 RegionCount () CONST;
    UINT32 InlinedCount () CONST;           // how many callees Build() inlined

private:
    struct Built { ICpuCode *pCode; UINT32 Tier; };

    UINT32  PickTier (UINT64 Count) CONST;  // tier the recorded count warrants
    // True if Callee's whole call tree is inlinable: finite (acyclic) and within the
    // depth cap. Recursive calls (a cycle) and over-deep trees are rejected.
    BOOLEAN InlinableTree (CPU_ADDR Callee, UINT32 Depth, std::set<CPU_ADDR> &Active) CONST;
    // Number of instances Callee's tree expands to (itself + all nested callees).
    UINT32  CountTree (CPU_ADDR Callee, UINT32 Depth) CONST;
    // Total INSTRUCTIONS Callee's tree duplicates when inlined (sum over all copies).
    UINT32  CountTreeInstrs (CPU_ADDR Callee, UINT32 Depth) CONST;

    ICpuArchitecture        *m_pArch;       // borrowed
    std::vector<CPU_TIER>     m_Tiers;
    UINT64                    m_HotThreshold;
    UINT32                    m_InlineBudget;
    std::map<CPU_ADDR, Built> m_Built;
    UINT32                    m_Inlined = 0;
};

} // namespace LibCPU

#endif // LIBCPU_PROFILEDAOT_H
