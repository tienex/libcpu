/** @file
  Profile-guided AOT builder implementation. See ProfiledAot.h.
**/
#include "ProfiledAot.h"
#include "../aot/AotGenerator.h"

namespace LibCPU {

LcProfiledAot::LcProfiledAot (ICpuArchitecture *pArch, CPU_TIER CONST *pTiers, UINT32 TierCount, UINT64 HotThreshold)
    : m_pArch (pArch), m_Tiers (pTiers, pTiers + TierCount), m_HotThreshold (HotThreshold)
{
}

LcProfiledAot::~LcProfiledAot ()
{
    for (auto CONST &Pair : m_Built) {
        if (Pair.second.pCode != nullptr) {
            Pair.second.pCode->Release ();
        }
    }
}

UINT32
LcProfiledAot::PickTier (UINT64 Count) CONST
{
    // Highest tier whose threshold the recorded count meets (tier i needs
    // HotThreshold * i). With one threshold this is simply hot -> top, cold -> 0.
    UINT32 Tier = 0;
    for (UINT32 I = 1; I < (UINT32) m_Tiers.size (); I++) {
        if (Count >= m_HotThreshold * (UINT64) I) {
            Tier = I;
        }
    }
    return Tier;
}

BOOLEAN
LcProfiledAot::LeafCalleeRange (CPU_ADDR Callee, CPU_ADDR *pEnd) CONST
{
    // Walk straight through the callee: it must be a simple leaf -- no nested call,
    // no branch -- ending in a RET. (A callee with internal control flow is not
    // inlined by this slice.)
    CPU_ADDR Pc = Callee;
    for (UINT32 Guard = 0; Guard < 64; Guard++) {
        UINT32   Tag;
        CPU_ADDR NewPc;
        CPU_ADDR NextPc;
        if (FAILED (m_pArch->TagInstr (Pc, &Tag, &NewPc, &NextPc))) {
            return FALSE;
        }
        if (Tag & (TagCall | TagBranch | TagConditional)) {
            return FALSE;   // not a straight-line leaf
        }
        if (Tag & TagReturn) {
            *pEnd = NextPc;   // address just past the RET
            return TRUE;
        }
        Pc = NextPc;
    }
    return FALSE;
}

HRESULT
LcProfiledAot::Build (LcPerfTrace CONST &Trace)
{
    HRESULT Result = S_OK;
    for (UINT32 I = 0; I < Trace.RegionCount (); I++) {
        CPU_TRACE_REGION R = Trace.Region (I);
        UINT32 Tier = PickTier (R.Count);

        // Gather an inline plan for this region: every hot call edge whose site is in
        // [R.Entry, R.End) and whose callee is a single-site straight-line leaf.
        std::vector<CPU_INLINE_SITE> Plan;
        for (UINT32 E = 0; E < Trace.EdgeCount (); E++) {
            CPU_TRACE_EDGE Edge = Trace.Edge (E);
            if (Edge.Site < R.Entry || Edge.Site >= R.End || Edge.Count < m_HotThreshold) {
                continue;
            }
            // Single call site for this callee across the whole program?
            UINT32 Sites = 0;
            for (UINT32 J = 0; J < Trace.EdgeCount (); J++) {
                if (Trace.Edge (J).Callee == Edge.Callee) { Sites++; }
            }
            CPU_ADDR CalleeEnd = 0;
            if (Sites == 1 && LeafCalleeRange (Edge.Callee, &CalleeEnd)) {
                CPU_INLINE_SITE Site = { Edge.Callee, CalleeEnd, Edge.ReturnPoint };
                Plan.push_back (Site);
            }
        }

        ICpuCode *pCode = nullptr;
        HRESULT hr = GenerateAotCfgInlined (m_pArch, m_Tiers[Tier].pBackend, R.Entry, R.End,
                                            Plan.empty () ? nullptr : Plan.data (), (UINT32) Plan.size (),
                                            &pCode, nullptr);
        if (FAILED (hr) || pCode == nullptr) {
            Result = FAILED (hr) ? hr : E_FAIL;
            continue;   // a failed region is simply absent; Run reports it
        }
        Built &B = m_Built[R.Entry];
        if (B.pCode != nullptr) {
            B.pCode->Release ();   // rebuild: drop the prior code
        }
        B.pCode = pCode;           // adopt GenerateAotCfgInlined's +1 ref
        B.Tier  = Tier;
        m_Inlined += (UINT32) Plan.size ();
    }
    return Result;
}

UINT32
LcProfiledAot::InlinedCount () CONST
{
    return m_Inlined;
}

CPU_EXEC_STATUS
LcProfiledAot::Run (CPU_ADDR Entry, VOID *pRAM, VOID *pGRF, VOID *pFRF)
{
    auto It = m_Built.find (Entry);
    if (It == m_Built.end () || It->second.pCode == nullptr) {
        return ExecFuncNotFound;   // not in the trace / failed to build
    }
    return It->second.pCode->Execute (pRAM, pGRF, pFRF);
}

UINT32
LcProfiledAot::TierOf (CPU_ADDR Entry) CONST
{
    auto It = m_Built.find (Entry);
    return (It != m_Built.end ()) ? It->second.Tier : 0;
}

UINT32
LcProfiledAot::RegionCount () CONST
{
    return (UINT32) m_Built.size ();
}

} // namespace LibCPU
