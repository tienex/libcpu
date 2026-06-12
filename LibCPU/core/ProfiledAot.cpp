/** @file
  Profile-guided AOT builder implementation. See ProfiledAot.h.
**/
#include "ProfiledAot.h"
#include "../aot/AotGenerator.h"
#include <cstring>
#include <set>

namespace LibCPU {

HRESULT
LcCollectEdgeProfile (ICpuArchitecture *pArch, ICpuBackend *pBackend,
                      CPU_ADDR Entry, CPU_ADDR End,
                      VOID *pRAM, CPU_STATE *pState, UINT32 Runs, LcPerfTrace &Trace)
{
    CPU_CALL_EDGE Sites[CPU_PROFILE_SLOTS];
    UINT32        SiteCount = 0;
    ICpuCode     *pCode     = nullptr;
    HRESULT hr = GenerateAotCfgProfiling (pArch, pBackend, Entry, End, &pCode,
                                          Sites, CPU_PROFILE_SLOTS, &SiteCount);
    if (FAILED (hr) || pCode == nullptr) {
        return FAILED (hr) ? hr : E_FAIL;
    }

    std::memset (pState->EdgeCount, 0, sizeof (pState->EdgeCount));
    for (UINT32 R = 0; R < Runs; R++) {
        pState->TrapPc = CPU_SMC_NO_TRAP;
        pCode->Execute (pRAM, pState, nullptr);
    }
    pCode->Release ();

    Trace.Record (Entry, End, (UINT64) Runs);   // region ran Runs times
    for (UINT32 I = 0; I < SiteCount; I++) {
        Trace.RecordEdge (Sites[I].Site, Sites[I].Callee, Sites[I].ReturnPoint,
                          pState->EdgeCount[I]);   // TRUE per-edge count
    }
    return S_OK;
}

} // namespace LibCPU

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

static UINT32 const kMaxInlineDepth = 4;

BOOLEAN
LcProfiledAot::InlinableTree (CPU_ADDR Callee, UINT32 Depth, std::set<CPU_ADDR> &Active) CONST
{
    if (Depth > kMaxInlineDepth) {
        return FALSE;                 // too deep
    }
    if (Active.count (Callee) != 0) {
        return FALSE;                 // recursion (a cycle on the active chain)
    }
    Active.insert (Callee);

    // Walk the callee's own blocks (following internal branches, stopping at RET). A
    // nested CALL is OK only if its callee's tree is also inlinable.
    BOOLEAN               Ok = TRUE;
    std::set<CPU_ADDR>    Seen;
    std::vector<CPU_ADDR> Work;
    Work.push_back (Callee);
    while (Ok && !Work.empty ()) {
        CPU_ADDR Pc = Work.back ();
        Work.pop_back ();
        if (Seen.count (Pc) != 0) {
            continue;
        }
        Seen.insert (Pc);
        if (Seen.size () > 256) {
            Ok = FALSE;
            break;
        }
        UINT32   Tag;
        CPU_ADDR NewPc;
        CPU_ADDR NextPc;
        if (FAILED (m_pArch->TagInstr (Pc, &Tag, &NewPc, &NextPc))) {
            Ok = FALSE;
            break;
        }
        if (Tag & TagCall) {
            if (!InlinableTree (NewPc, Depth + 1, Active)) { Ok = FALSE; break; }
            Work.push_back (NextPc);   // continue past the (inlinable) nested call
            continue;
        }
        if (Tag & TagReturn) {
            continue;                  // an exit
        }
        if (Tag & (TagContinue | TagConditional)) { Work.push_back (NextPc); }
        if (Tag & (TagBranch | TagConditional))   { Work.push_back (NewPc); }
    }

    Active.erase (Callee);
    return Ok;
}

UINT32
LcProfiledAot::CountTree (CPU_ADDR Callee, UINT32 Depth) CONST
{
    // 1 for this callee + the trees of its nested callees.
    UINT32 N = 1;
    if (Depth > kMaxInlineDepth) {
        return N;
    }
    std::set<CPU_ADDR>    Seen;
    std::vector<CPU_ADDR> Work;
    Work.push_back (Callee);
    while (!Work.empty ()) {
        CPU_ADDR Pc = Work.back ();
        Work.pop_back ();
        if (Seen.count (Pc) != 0) {
            continue;
        }
        Seen.insert (Pc);
        UINT32   Tag;
        CPU_ADDR NewPc;
        CPU_ADDR NextPc;
        if (FAILED (m_pArch->TagInstr (Pc, &Tag, &NewPc, &NextPc))) {
            continue;
        }
        if (Tag & TagCall) {
            N += CountTree (NewPc, Depth + 1);
            Work.push_back (NextPc);
            continue;
        }
        if (Tag & TagReturn) {
            continue;
        }
        if (Tag & (TagContinue | TagConditional)) { Work.push_back (NextPc); }
        if (Tag & (TagBranch | TagConditional))   { Work.push_back (NewPc); }
    }
    return N;
}

HRESULT
LcProfiledAot::Build (LcPerfTrace CONST &Trace)
{
    HRESULT Result = S_OK;
    for (UINT32 I = 0; I < Trace.RegionCount (); I++) {
        CPU_TRACE_REGION R = Trace.Region (I);
        UINT32 Tier = PickTier (R.Count);

        // Gather an inline plan: the region's TOP-LEVEL (direct) call sites that are
        // hot and whose whole callee tree is inlinable. The AOT driver recursively
        // inlines each tree, so nested calls are NOT listed here.
        CPU_CALL_EDGE Direct[64];
        UINT32 Nd = CollectDirectCallEdges (m_pArch, R.Entry, R.End, Direct, 64);
        if (Nd > 64) { Nd = 64; }

        std::vector<CPU_INLINE_SITE> Plan;
        for (UINT32 D = 0; D < Nd; D++) {
            UINT64 EdgeCnt = 0;
            for (UINT32 E = 0; E < Trace.EdgeCount (); E++) {
                if (Trace.Edge (E).Site == Direct[D].Site) { EdgeCnt = Trace.Edge (E).Count; break; }
            }
            if (EdgeCnt < m_HotThreshold) {
                continue;
            }
            std::set<CPU_ADDR> Active;
            if (InlinableTree (Direct[D].Callee, 0, Active)) {
                CPU_INLINE_SITE Site = { Direct[D].Site, Direct[D].Callee, Direct[D].ReturnPoint };
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
        for (CPU_INLINE_SITE CONST &S : Plan) {
            m_Inlined += CountTree (S.Callee, 0);   // each top-level site expands to a tree
        }
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
