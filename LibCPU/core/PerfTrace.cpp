/** @file
  Performance trace implementation. See PerfTrace.h.
**/
#include "PerfTrace.h"
#include <cstdio>

namespace LibCPU {

VOID
PerfTrace::Record (CPU_ADDR Entry, CPU_ADDR End, UINT64 Count)
{
    Sample &R = m_Regions[Entry];
    R.End    = End;
    R.Count += Count;
}

VOID
PerfTrace::RecordEdge (CPU_ADDR Site, CPU_ADDR Callee, CPU_ADDR ReturnPoint, UINT64 Count)
{
    EdgeData &E = m_Edges[Site];
    E.Callee       = Callee;
    E.ReturnPoint  = ReturnPoint;
    E.Count       += Count;
}

UINT32
PerfTrace::RegionCount () CONST
{
    return (UINT32) m_Regions.size ();
}

UINT32
PerfTrace::EdgeCount () CONST
{
    return (UINT32) m_Edges.size ();
}

CPU_TRACE_EDGE
PerfTrace::Edge (UINT32 Index) CONST
{
    CPU_TRACE_EDGE Out = { 0, 0, 0, 0 };
    UINT32 I = 0;
    for (auto CONST &Pair : m_Edges) {
        if (I++ == Index) {
            Out.Site        = Pair.first;
            Out.Callee      = Pair.second.Callee;
            Out.ReturnPoint = Pair.second.ReturnPoint;
            Out.Count       = Pair.second.Count;
            break;
        }
    }
    return Out;
}

CPU_TRACE_REGION
PerfTrace::Region (UINT32 Index) CONST
{
    CPU_TRACE_REGION Out = { 0, 0, 0 };
    UINT32 I = 0;
    for (auto CONST &Pair : m_Regions) {
        if (I++ == Index) {
            Out.Entry = Pair.first;
            Out.End   = Pair.second.End;
            Out.Count = Pair.second.Count;
            break;
        }
    }
    return Out;
}

UINT64
PerfTrace::CountOf (CPU_ADDR Entry) CONST
{
    auto It = m_Regions.find (Entry);
    return (It != m_Regions.end ()) ? It->second.Count : 0;
}

BOOLEAN
PerfTrace::Save (CHAR8 CONST *pPath) CONST
{
    FILE *pF = std::fopen (pPath, "w");
    if (pF == nullptr) {
        return FALSE;
    }
    std::fprintf (pF, "# libcpu perf-trace v1\n");
    std::fprintf (pF, "# region: <entry> <end> <count>   edge: E <site> <callee> <return> <count>\n");
    for (auto CONST &Pair : m_Regions) {
        std::fprintf (pF, "0x%llx 0x%llx %llu\n",
                      (unsigned long long) Pair.first,
                      (unsigned long long) Pair.second.End,
                      (unsigned long long) Pair.second.Count);
    }
    for (auto CONST &Pair : m_Edges) {
        std::fprintf (pF, "E 0x%llx 0x%llx 0x%llx %llu\n",
                      (unsigned long long) Pair.first,
                      (unsigned long long) Pair.second.Callee,
                      (unsigned long long) Pair.second.ReturnPoint,
                      (unsigned long long) Pair.second.Count);
    }
    std::fclose (pF);
    return TRUE;
}

BOOLEAN
PerfTrace::Load (CHAR8 CONST *pPath)
{
    FILE *pF = std::fopen (pPath, "r");
    if (pF == nullptr) {
        return FALSE;
    }
    m_Regions.clear ();
    m_Edges.clear ();
    char Line[256];
    while (std::fgets (Line, sizeof (Line), pF) != nullptr) {
        if (Line[0] == '#' || Line[0] == '\n') {
            continue;
        }
        unsigned long long Site = 0, Callee = 0, Ret = 0, Count = 0;
        if (Line[0] == 'E') {
            if (std::sscanf (Line + 1, "%llx %llx %llx %llu", &Site, &Callee, &Ret, &Count) == 4) {
                RecordEdge ((CPU_ADDR) Site, (CPU_ADDR) Callee, (CPU_ADDR) Ret, (UINT64) Count);
            }
            continue;
        }
        unsigned long long Entry = 0, End = 0;
        if (std::sscanf (Line, "%llx %llx %llu", &Entry, &End, &Count) == 3) {
            Record ((CPU_ADDR) Entry, (CPU_ADDR) End, (UINT64) Count);
        }
    }
    std::fclose (pF);
    return TRUE;
}

VOID
PerfTrace::Clear ()
{
    m_Regions.clear ();
    m_Edges.clear ();
}

} // namespace LibCPU
