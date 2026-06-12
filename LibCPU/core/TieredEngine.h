/** @file
  Adaptive tiered execution engine (LibCPU core).

  A profiling driver that runs a guest through a CHEAP backend first, counts how
  often each region (keyed by entry PC) executes, and -- once a region crosses a
  heat threshold -- recompiles it with a BETTER backend on a background thread,
  then atomically hot-swaps the optimized code in. Subsequent runs of that region
  use the optimized version; cold regions stay on the cheap tier.

  This sits entirely on top of the existing AOT seam: each tier is just an
  ICpuBackend, and an upgrade is GenerateAotCfg() re-run with the next tier's
  backend. The frontend and the produced ICpuCode are unaware they are being
  retiered. Thread-safety rests on two things: a mutex guarding the slot table,
  and ICpuCode's atomic refcount, which keeps a code object alive for an in-flight
  Execute even if a background thread swaps it out mid-run.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_TIEREDENGINE_H
#define LIBCPU_TIEREDENGINE_H

#include "LibCPU/ICpu.h"
#include <map>
#include <vector>
#include <mutex>
#include <thread>

namespace LibCPU {

class LcPerfTrace;   // PerfTrace.h

//
// One compilation tier: a backend and a human-readable label. Tiers are ordered
// cheapest-to-best; the engine starts at tier 0 and promotes hot regions upward.
// The backend is borrowed (the caller keeps it alive for the engine's lifetime).
//
typedef struct _CPU_TIER {
    ICpuBackend *pBackend;
    CHAR8 CONST *pLabel;
} CPU_TIER;

class LcTieredEngine {
public:
    // HotThreshold is the run count at which a region is promoted to the next tier.
    LcTieredEngine (ICpuArchitecture *pArch, CPU_TIER CONST *pTiers, UINT32 TierCount, UINT64 HotThreshold);
    ~LcTieredEngine ();

    LcTieredEngine (LcTieredEngine CONST &)            = delete;
    LcTieredEngine &operator= (LcTieredEngine CONST &) = delete;

    // Execute the region [Entry, End) once. The first touch compiles tier 0
    // synchronously; crossing the heat threshold launches a background upgrade.
    CPU_EXEC_STATUS Run (CPU_ADDR Entry, CPU_ADDR End, VOID *pRAM, VOID *pGRF, VOID *pFRF);

    // Block until every in-flight background compile has finished and swapped in.
    VOID Drain ();

    // Per-region telemetry (for hot-spot inspection / tests).
    UINT32 TierOf     (CPU_ADDR Entry);
    UINT64 CountOf    (CPU_ADDR Entry);
    UINT32 UpgradesOf (CPU_ADDR Entry);

    // Emit the collected per-region counts as a performance trace, which can be
    // persisted and re-used to drive profile-guided AOT (see PerfTrace / ProfiledAot).
    VOID ExportTrace (LcPerfTrace &Trace);

private:
    struct Slot {
        ICpuCode *pCode     = nullptr;   // current tier's code (owns one ref)
        UINT64    Count     = 0;         // total runs
        UINT32    Tier      = 0;         // current tier index
        UINT32    Upgrades  = 0;         // completed background promotions
        CPU_ADDR  End       = 0;         // region end (for recompiles)
        bool      Compiling = false;     // a background upgrade is in flight
    };

    VOID LaunchUpgrade (CPU_ADDR Entry);   // spawn the background recompile thread

    ICpuArchitecture        *m_pArch;          // borrowed
    std::vector<CPU_TIER>    m_Tiers;
    UINT64                   m_HotThreshold;
    std::map<CPU_ADDR, Slot> m_Slots;
    std::mutex               m_Mutex;          // guards m_Slots + m_Threads
    std::mutex               m_CompileMutex;   // serializes GenerateAotCfg (shared arch)
    std::vector<std::thread> m_Threads;        // background upgrade threads
};

} // namespace LibCPU

#endif // LIBCPU_TIEREDENGINE_H
