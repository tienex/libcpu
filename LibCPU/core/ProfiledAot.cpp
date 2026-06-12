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

HRESULT
LcProfiledAot::Build (LcPerfTrace CONST &Trace)
{
    HRESULT Result = S_OK;
    for (UINT32 I = 0; I < Trace.RegionCount (); I++) {
        CPU_TRACE_REGION R = Trace.Region (I);
        UINT32 Tier = PickTier (R.Count);

        ICpuCode *pCode = nullptr;
        HRESULT hr = GenerateAotCfg (m_pArch, m_Tiers[Tier].pBackend, R.Entry, R.End, &pCode, nullptr);
        if (FAILED (hr) || pCode == nullptr) {
            Result = FAILED (hr) ? hr : E_FAIL;
            continue;   // a failed region is simply absent; Run reports it
        }
        Built &B = m_Built[R.Entry];
        if (B.pCode != nullptr) {
            B.pCode->Release ();   // rebuild: drop the prior code
        }
        B.pCode = pCode;           // adopt GenerateAotCfg's +1 ref
        B.Tier  = Tier;
    }
    return Result;
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
