/** @file
  Adaptive tiered execution engine implementation. See TieredEngine.h.
**/
#include "TieredEngine.h"
#include "../aot/AotGenerator.h"

namespace LibCPU {

LcTieredEngine::LcTieredEngine (ICpuArchitecture *pArch, CPU_TIER CONST *pTiers, UINT32 TierCount, UINT64 HotThreshold)
    : m_pArch (pArch), m_Tiers (pTiers, pTiers + TierCount), m_HotThreshold (HotThreshold)
{
}

LcTieredEngine::~LcTieredEngine ()
{
    Drain ();   // no thread may touch a Slot after this
    for (auto CONST &Pair : m_Slots) {
        if (Pair.second.pCode != nullptr) {
            Pair.second.pCode->Release ();
        }
    }
}

CPU_EXEC_STATUS
LcTieredEngine::Run (CPU_ADDR Entry, CPU_ADDR End, VOID *pRAM, VOID *pGRF, VOID *pFRF)
{
    ICpuCode *pExec = nullptr;
    bool      Launch = false;
    {
        std::lock_guard<std::mutex> Lk (m_Mutex);
        Slot &S = m_Slots[Entry];
        S.End = End;

        if (S.pCode == nullptr) {
            // First touch: compile tier 0 synchronously (it is the cheap backend).
            ICpuCode *pCold = nullptr;
            if (SUCCEEDED (GenerateAotCfg (m_pArch, m_Tiers[0].pBackend, Entry, End, &pCold, nullptr)) && pCold != nullptr) {
                S.pCode = pCold;   // adopt GenerateAotCfg's +1 ref
                S.Tier  = 0;
            }
        }

        S.Count++;
        pExec = S.pCode;
        if (pExec != nullptr) {
            pExec->AddRef ();   // keep alive across Execute even if a swap happens

            // Promote once the region is hot and a better tier exists. The Compiling
            // flag keeps the many runs between threshold and swap from re-launching.
            if (!S.Compiling && (UINT32) (S.Tier + 1) < (UINT32) m_Tiers.size () && S.Count >= m_HotThreshold) {
                S.Compiling = true;
                Launch      = true;
            }
        }
    }

    if (pExec == nullptr) {
        return ExecFuncNotFound;   // even tier 0 failed to compile
    }
    if (Launch) {
        LaunchUpgrade (Entry);
    }

    CPU_EXEC_STATUS Status = pExec->Execute (pRAM, pGRF, pFRF);   // outside the lock
    pExec->Release ();
    return Status;
}

VOID
LcTieredEngine::LaunchUpgrade (CPU_ADDR Entry)
{
    UINT32   NextTier;
    CPU_ADDR End;
    {
        std::lock_guard<std::mutex> Lk (m_Mutex);
        NextTier = m_Slots[Entry].Tier + 1;
        End      = m_Slots[Entry].End;

        m_Threads.emplace_back ([this, Entry, End, NextTier] () {
            ICpuCode *pNew = nullptr;
            {
                // Serialize compiles: the frontend (arch) is shared, and only one
                // GenerateAotCfg should walk it at a time.
                std::lock_guard<std::mutex> CLk (m_CompileMutex);
                GenerateAotCfg (m_pArch, m_Tiers[NextTier].pBackend, Entry, End, &pNew, nullptr);
            }

            std::lock_guard<std::mutex> Lk (m_Mutex);
            Slot &S = m_Slots[Entry];
            if (pNew != nullptr) {
                if (S.pCode != nullptr) {
                    S.pCode->Release ();   // drop the old tier (atomic; an in-flight
                                           // Execute still holds its own ref)
                }
                S.pCode = pNew;            // adopt the new tier's +1 ref
                S.Tier  = NextTier;
                S.Upgrades++;
            }
            S.Compiling = false;
        });
    }
}

VOID
LcTieredEngine::Drain ()
{
    std::vector<std::thread> Pending;
    {
        std::lock_guard<std::mutex> Lk (m_Mutex);
        Pending.swap (m_Threads);   // take ownership, then join WITHOUT the lock held
    }                               // (a thread's swap needs m_Mutex to finish)
    for (std::thread &T : Pending) {
        if (T.joinable ()) {
            T.join ();
        }
    }
}

UINT32
LcTieredEngine::TierOf (CPU_ADDR Entry)
{
    std::lock_guard<std::mutex> Lk (m_Mutex);
    auto It = m_Slots.find (Entry);
    return (It != m_Slots.end ()) ? It->second.Tier : 0;
}

UINT64
LcTieredEngine::CountOf (CPU_ADDR Entry)
{
    std::lock_guard<std::mutex> Lk (m_Mutex);
    auto It = m_Slots.find (Entry);
    return (It != m_Slots.end ()) ? It->second.Count : 0;
}

UINT32
LcTieredEngine::UpgradesOf (CPU_ADDR Entry)
{
    std::lock_guard<std::mutex> Lk (m_Mutex);
    auto It = m_Slots.find (Entry);
    return (It != m_Slots.end ()) ? It->second.Upgrades : 0;
}

} // namespace LibCPU
