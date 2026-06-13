/** @file
  Performance trace (LibCPU core): a reusable per-region hotness profile.

  A run records how often each region (keyed by entry PC) executed; the trace is
  persisted to a file and re-loaded by a LATER run to drive profile-guided AOT:
  ahead of time, each region is compiled at the tier its recorded count justifies
  (hot -> optimizing backend, cold -> cheap backend). Unlike the JIT engine, no
  decision is made at run time -- the trace is the profile, fixed before execution.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_PERFTRACE_H
#define LIBCPU_PERFTRACE_H

#include "LibCPU/ICpu.h"
#include <map>
#include <vector>

namespace LibCPU {

//
// One profiled region: the code range [Entry, End) and how often it ran.
//
typedef struct _CPU_TRACE_REGION {
    CPU_ADDR Entry;
    CPU_ADDR End;
    UINT64   Count;
} CPU_TRACE_REGION;

//
// One profiled call edge: a CALL at Site to Callee (returning to ReturnPoint),
// taken Count times. Lets profile-guided AOT decide which callees to inline.
//
typedef struct _CPU_TRACE_EDGE {
    CPU_ADDR Site;
    CPU_ADDR Callee;
    CPU_ADDR ReturnPoint;
    UINT64   Count;
} CPU_TRACE_EDGE;

class PerfTrace {
public:
    // Merge a sample (adds Count to the region; remembers End).
    VOID Record (CPU_ADDR Entry, CPU_ADDR End, UINT64 Count);

    // Merge a call-edge sample (adds Count to the Site->Callee edge).
    VOID RecordEdge (CPU_ADDR Site, CPU_ADDR Callee, CPU_ADDR ReturnPoint, UINT64 Count);

    UINT32                  RegionCount () CONST;
    CPU_TRACE_REGION        Region (UINT32 Index) CONST;
    UINT64                  CountOf (CPU_ADDR Entry) CONST;

    UINT32                  EdgeCount () CONST;
    CPU_TRACE_EDGE          Edge (UINT32 Index) CONST;

    // Persist / restore the trace as text ("0xEntry 0xEnd Count" per line), so a
    // separate run can re-use the profile.
    BOOLEAN Save (CHAR8 CONST *pPath) CONST;
    BOOLEAN Load (CHAR8 CONST *pPath);
    VOID    Clear ();

private:
    struct Sample   { CPU_ADDR End; UINT64 Count; };
    struct EdgeData { CPU_ADDR Callee; CPU_ADDR ReturnPoint; UINT64 Count; };
    std::map<CPU_ADDR, Sample>   m_Regions;   // keyed by entry
    std::map<CPU_ADDR, EdgeData> m_Edges;     // keyed by call site
};

} // namespace LibCPU

#endif // LIBCPU_PERFTRACE_H
