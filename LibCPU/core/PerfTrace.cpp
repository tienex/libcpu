/** @file
  Performance trace implementation. See PerfTrace.h.
**/
#include "PerfTrace.h"
#include <cstdio>

namespace LibCPU {

VOID
LcPerfTrace::Record (CPU_ADDR Entry, CPU_ADDR End, UINT64 Count)
{
    Sample &R = m_Regions[Entry];
    R.End    = End;
    R.Count += Count;
}

UINT32
LcPerfTrace::RegionCount () CONST
{
    return (UINT32) m_Regions.size ();
}

CPU_TRACE_REGION
LcPerfTrace::Region (UINT32 Index) CONST
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
LcPerfTrace::CountOf (CPU_ADDR Entry) CONST
{
    auto It = m_Regions.find (Entry);
    return (It != m_Regions.end ()) ? It->second.Count : 0;
}

BOOLEAN
LcPerfTrace::Save (CHAR8 CONST *pPath) CONST
{
    FILE *pF = std::fopen (pPath, "w");
    if (pF == nullptr) {
        return FALSE;
    }
    std::fprintf (pF, "# libcpu perf-trace v1: <entry> <end> <count>\n");
    for (auto CONST &Pair : m_Regions) {
        std::fprintf (pF, "0x%llx 0x%llx %llu\n",
                      (unsigned long long) Pair.first,
                      (unsigned long long) Pair.second.End,
                      (unsigned long long) Pair.second.Count);
    }
    std::fclose (pF);
    return TRUE;
}

BOOLEAN
LcPerfTrace::Load (CHAR8 CONST *pPath)
{
    FILE *pF = std::fopen (pPath, "r");
    if (pF == nullptr) {
        return FALSE;
    }
    m_Regions.clear ();
    char Line[256];
    while (std::fgets (Line, sizeof (Line), pF) != nullptr) {
        if (Line[0] == '#' || Line[0] == '\n') {
            continue;
        }
        unsigned long long Entry = 0, End = 0, Count = 0;
        if (std::sscanf (Line, "%llx %llx %llu", &Entry, &End, &Count) == 3) {
            Record ((CPU_ADDR) Entry, (CPU_ADDR) End, (UINT64) Count);
        }
    }
    std::fclose (pF);
    return TRUE;
}

VOID
LcPerfTrace::Clear ()
{
    m_Regions.clear ();
}

} // namespace LibCPU
