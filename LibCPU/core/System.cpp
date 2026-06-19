/** @file  System-level emulation: the machine run loop. See System.h. */

#include "System.h"
#include "../aot/AotGenerator.h"
#include "../aot/ShadowCfg.h"
#include "LibCPU/PCom.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace LibCPU {

// 8086 register-file indices (must match the V20 frontend's RegV20* layout).
enum { S_AX = 0, S_CS = 9 };   // the only registers the generic loop touches (AX for port-IN, CS for the trace)

// Representative machine cycles credited to a burst a JIT ran natively without counting (see Run).
// Sized so a few back-to-back port traps (a BIOS timer-calibration loop) advance the PIT counter by
// the same order of magnitude the interpreter's per-micro-op count does for the same loop.
static CONST UINT64 CYCLES_PER_BURST = 32;

// Apply a tier's optimization level to its backend, if the backend can vary it (ICpuBackendOptimize).
// LC_OPT_DEFAULT leaves the backend's own setting alone; backends that do not optimize are unaffected.
static void
ApplyOptimization (ICpuBackend *pBackend, UINT32 Level)
{
    if (Level == LC_OPT_DEFAULT || pBackend == nullptr) {
        return;
    }
    ICpuBackendOptimize *pOpt = nullptr;
    if (SUCCEEDED (pBackend->QueryInterface (IID_ICpuBackendOptimize, (VOID **) &pOpt)) && pOpt != nullptr) {
        pOpt->SetOptimization (Level);
        pOpt->Release ();
    }
}

System::System (ICpuArchitecture *pArch, ICpuBackend *pBackend, UINT8 *pRAM, UINT64 RamSize)
    : m_pArch (pArch), m_pBackend (pBackend), m_pRAM (pRAM), m_RamSize (RamSize),
      m_If (false), m_Halted (false), m_Shutdown (false)
{
    std::memset (&m_State, 0, sizeof (m_State));
    m_State.RamSize = RamSize;

    // A backend with no native control flow drives the machine through the shadow path: we
    // translate one basic block at a time with GenerateShadowUnit and read the outcome from the
    // scratch registers after each burst. Probed once here (it is a property of the base backend).
    // LCX_SHADOW forces the path on even for a CFG-capable backend, to exercise it on a fast one.
    m_ShadowMode = (m_pBackend != nullptr)
                   && (std::getenv ("LCX_SHADOW") != nullptr || !BackendHasNativeCfg (m_pArch, m_pBackend));
}

System::~System ()
{
    // Join the background queue FIRST so no worker is mid-compile while we free the cache/arch it uses;
    // then release any artifacts it had already delivered but the run loop had not yet swapped in.
    delete m_pQueue;
    m_pQueue = nullptr;
    for (auto CONST &D : m_Done) {
        if (D.pCode != nullptr) { D.pCode->Release (); }
    }
    m_Done.clear ();
    ClearCodeCache ();
    if (m_pHotArch != nullptr) { m_pHotArch->Release (); m_pHotArch = nullptr; }
}

void
System::ClearCodeCache ()
{
    for (auto CONST &Entry : m_CodeCache) {
        if (Entry.second.pCode != nullptr) { Entry.second.pCode->Release (); }
    }
    m_CodeCache.clear ();
}

void
System::SetHotArch (ICpuArchitecture *pHotArch)
{
    // Own the hot arch: the background worker uses it, and ~System joins the worker BEFORE releasing it,
    // so the worker never touches a freed frontend regardless of the caller's own lifetime ordering. One
    // worker means the single hot arch is never used concurrently.
    if (m_pHotArch != nullptr) { m_pHotArch->Release (); }
    m_pHotArch = pHotArch;
    if (m_pHotArch != nullptr) {
        m_pHotArch->AddRef ();
        if (m_pQueue == nullptr) { m_pQueue = new DispatchQueue (1); }
    }
}

void
System::AddTier (ICpuBackend *pBackend, UINT64 Threshold, UINT32 OptLevel)
{
    m_Tiers.push_back (HOT_TIER { pBackend, Threshold, OptLevel });
}

void
System::SetHotBackend (ICpuBackend *pHotBackend, UINT64 Threshold, ICpuArchitecture *pHotArch)
{
    // Convenience: a single optimizing tier above the interpreter (the common --jit case). LC_OPT_DEFAULT
    // leaves the backend's own optimization setting untouched, preserving legacy behavior.
    if (pHotArch != nullptr) { SetHotArch (pHotArch); }
    AddTier (pHotBackend, Threshold, LC_OPT_DEFAULT);
}

// Swap in any higher-tier artifacts the background worker has finished, at a run-loop safe point (the
// cache is otherwise touched only by the execution thread). The new artifact replaces the lower-tier one.
void
System::DrainCompiled ()
{
    if (m_pQueue == nullptr) {
        return;
    }
    std::unique_lock<std::mutex> Lk (m_DoneMutex);
    for (auto CONST &D : m_Done) {
        auto It = m_CodeCache.find (D.Key);
        if (It == m_CodeCache.end ()) {
            if (D.pCode != nullptr) { D.pCode->Release (); }   // entry evicted before delivery (not for ROM)
            continue;
        }
        It->second.Compiling = false;
        It->second.Tier      = D.Tier;                         // record the tier reached (even on failure)
        if (D.pCode != nullptr) {
            if (It->second.pCode != nullptr) { It->second.pCode->Release (); }   // drop the lower tier
            It->second.pCode = D.pCode;                                          // adopt the higher (owns ref)
        }
        // D.pCode == nullptr: the compile failed; keep the lower tier and don't retry this tier.
    }
    m_Done.clear ();
}

void
System::AddDevice (Device *pDevice)
{
    m_Devices.push_back (pDevice);
}

void
System::MapDeviceMemory (UINT32 Base, UINT32 Size, UINT8 *pHost, bool ReadOnly)
{
    if (pHost == nullptr || Size == 0) { return; }
    DEVICE_MEMORY M = { Base, Size, pHost, ReadOnly };
    m_DeviceMemory.push_back (M);
}

void
System::MarkOpenBus (UINT32 Base, UINT32 Size)
{
    if (Size == 0) { return; }
    OPEN_BUS M = { Base, Size };
    m_OpenBus.push_back (M);
}

void
System::MarkCodeImmutable (UINT64 Base, UINT64 Size)
{
    if (Size == 0) { return; }
    m_ImmutableCode.push_back (std::make_pair (Base, Base + Size));
}

bool
System::IsImmutableCode (UINT64 Addr) CONST
{
    for (auto CONST &R : m_ImmutableCode) {
        if (Addr >= R.first && Addr < R.second) { return true; }
    }
    for (DEVICE_MEMORY CONST &M : m_DeviceMemory) {           // read-only device memory is firmware too
        if (M.ReadOnly && Addr >= M.Base && Addr < (UINT64) M.Base + M.Size) { return true; }
    }
    return false;
}

// Bring each device's own buffer into the guest's flat RAM, so the CPU's direct accesses see the
// device's current memory (its initial contents, or a freshly switched bank). Unbacked regions
// are forced to 0xFF, so a slot with no card reads as open bus and any writes from the previous
// window are discarded -- "no card, no memory".
void
System::SyncDeviceMemoryIn ()
{
    for (OPEN_BUS CONST &M : m_OpenBus) {
        if ((UINT64) M.Base + M.Size <= m_RamSize) { std::memset (m_pRAM + M.Base, 0xFF, M.Size); }
    }
    for (DEVICE_MEMORY CONST &M : m_DeviceMemory) {
        if ((UINT64) M.Base + M.Size <= m_RamSize) { std::memcpy (m_pRAM + M.Base, M.pHost, M.Size); }
    }
}

// Capture the CPU's writes from the flat RAM back into each device's own buffer, so the device
// (e.g. a card scanning out its framebuffer) observes them.
void
System::SyncDeviceMemoryOut ()
{
    for (DEVICE_MEMORY CONST &M : m_DeviceMemory) {
        if (M.ReadOnly) { continue; }                        // ROM/firmware: writes are discarded
        if ((UINT64) M.Base + M.Size <= m_RamSize) { std::memcpy (M.pHost, m_pRAM + M.Base, M.Size); }
    }
}

Device *
System::FindPort (UINT16 Port) CONST
{
    for (Device *p : m_Devices) {
        if (p->HandlesPort (Port)) {
            return p;
        }
    }
    return nullptr;
}

int
System::PollDevices ()
{
    for (Device *p : m_Devices) {
        int Irq = p->Poll ();
        if (Irq >= 0) {
            return Irq;
        }
    }
    return -1;
}

LC_SYS_RESULT
System::Run (CPU_ADDR CodeEntry, CPU_ADDR CodeEnd, UINT64 MaxSteps)
{
    LC_SYS_RESULT R;
    R.Reason = LC_SYS_RESULT::StepBudget;
    R.Steps = R.Interrupts = R.PortWrites = R.PortReads = 0;
    R.FinalPc = CodeEntry;

    bool Trace = std::getenv ("LCX_TRACE") != nullptr;  // per-step PC trace for post-mortem debugging
    // A segmented frontend (8086/V20) fetches code from a CS base + IP offset. Pc here is the IP
    // offset; we re-point the decode base at the guest CS each window and report the linear PC.
    ComPtr<ICpuSegmentedCode> Seg;
    m_pArch->QueryInterface (IID_ICpuSegmentedCode, (VOID **) &Seg);   // null for flat-address archs

    CPU_ADDR Pc = CodeEntry;
    for (UINT64 Step = 0; Step < MaxSteps; Step++) {
        UINT64 SegBase = (Seg != nullptr) ? Seg->SegmentBase ((UINT32) (UINT16) m_State.Reg[S_CS]) : 0;
        R.FinalPc = SegBase + Pc;                       // linear PC for a post-mortem report
        if (Trace) {
            UINT64 Lin = SegBase + Pc;
            std::fprintf (stderr, "[%6llu] pc=%05llX cs=%04X ax=%04X op=%02X %02X %02X "
                          "S%d O%d Z%d C%d P%d halt=%d if=%d\n",
                          (unsigned long long) Step, (unsigned long long) Lin, (UINT16) m_State.Reg[S_CS],
                          (UINT16) m_State.Reg[S_AX],
                          Lin < m_RamSize ? m_pRAM[Lin] : 0, Lin + 1 < m_RamSize ? m_pRAM[Lin + 1] : 0,
                          Lin + 2 < m_RamSize ? m_pRAM[Lin + 2] : 0,
                          m_State.Flag[FlagNegative], m_State.Flag[FlagOverflow], m_State.Flag[FlagZero],
                          m_State.Flag[FlagCarry], m_State.Flag[FlagParity], m_Halted ? 1 : 0, m_If ? 1 : 0);
        }
        // Adopt any tier-1 artifacts the background queue finished since the last burst (safe point: the
        // cache is touched only here, on the execution thread).
        DrainCompiled ();

        // Service the console (stream the framebuffer out, feed injected keys in) and honour a
        // shutdown the pump may have requested (e.g. the console sent CTRL_QUIT).
        if (m_Pump) { m_Pump (); }
        if (m_Shutdown) { R.Reason = LC_SYS_RESULT::Shutdown; break; }

        // Halted (HLT): the CPU idles until a device raises an interrupt. A halt with interrupts
        // disabled is terminal. Otherwise wait for an IRQ: bounded by MaxSteps without a console,
        // or indefinitely (pacing the idle wait) with one, so a keystroke can still wake the guest.
        if (m_Halted) {
            if (!m_If) { R.Reason = LC_SYS_RESULT::HaltedIdle; break; }
            int Irq = -1;
            for (UINT64 Spin = 0; Irq < 0 && (m_Pump != nullptr || Spin < MaxSteps); Spin++) {
                if (m_Pump) { m_Pump (); if (m_Shutdown) { break; } }
                Irq = PollDevices ();
                if (Irq < 0 && m_Pump) { std::this_thread::sleep_for (std::chrono::milliseconds (1)); }
            }
            if (m_Shutdown) { R.Reason = LC_SYS_RESULT::Shutdown; break; }
            if (Irq < 0 || !m_DeliverIrq) { R.Reason = LC_SYS_RESULT::HaltedIdle; break; }
            Pc = m_DeliverIrq (*this, (UINT32) Irq, Pc);  // CPU personality vectors the IRQ
            m_Halted = false;                             // an interrupt wakes a halted CPU
            R.Interrupts++;
            continue;
        }
        // Running with interrupts enabled: deliver a pending IRQ at this boundary.
        if (m_If && m_DeliverIrq) {
            int Irq = PollDevices ();
            if (Irq >= 0) {
                Pc = m_DeliverIrq (*this, (UINT32) Irq, Pc);
                R.Interrupts++;
                continue;
            }
        }

        // Translate a window from Pc and run it until the next trap. For a segmented frontend,
        // re-point the decode base at the guest CS and bound the window to the bytes physically
        // above that base so CFG discovery cannot read past RAM.
        CPU_ADDR EffEnd = CodeEnd;
        if (Seg != nullptr) {
            Seg->SetCodeSegment ((UINT32) (UINT16) m_State.Reg[S_CS]);
            UINT64 Avail = (SegBase < m_RamSize) ? (m_RamSize - SegBase) : 0;
            if ((UINT64) EffEnd > Avail) { EffEnd = (CPU_ADDR) Avail; }
        }
        // Reuse a cached translation when this burst enters immutable code (a ROM/BIOS image); the
        // bytes there can never change, so the compiled artifact stays valid. Code in writable RAM
        // is re-translated each burst (so the JIT always sees current bytes -- correct for SMC).
        UINT64           CacheKey  = SegBase + (UINT64) Pc;
        bool             Cacheable = IsImmutableCode (CacheKey);
        ICpuCode        *pCode     = nullptr;
        ComPtr<ICpuCode> Fresh;                         // owns a ref only on a cache miss
        if (Cacheable) {
            auto It = m_CodeCache.find (CacheKey);
            if (It != m_CodeCache.end ()) {
                CACHED_CODE &C = It->second;
                C.Runs++;
                // Multi-tier promotion: pick the HIGHEST tier whose run threshold this region has crossed;
                // if it is above the region's current tier, (re)compile to it with that tier's backend at
                // its optimization level. As Runs grows the region climbs interp -> ... -> the top tier;
                // if it heats up during a compile it may jump straight to the highest justified tier.
                if (!C.Compiling && !m_Tiers.empty ()) {
                    UINT32 Target = C.Tier;             // 0 = tier 0 (the base backend); t = m_Tiers[t-1]
                    for (UINT32 t = 0; t < (UINT32) m_Tiers.size (); t++) {
                        if (C.Runs >= m_Tiers[t].Threshold) { Target = t + 1; }
                    }
                    if (Target > C.Tier) {
                        ICpuBackend *pTierBe = m_Tiers[Target - 1].pBackend;
                        UINT32       Opt     = m_Tiers[Target - 1].OptLevel;
                        if (m_pQueue != nullptr && m_pHotArch != nullptr) {
                            // Asynchronous: queue the recompile on the background worker and keep running the
                            // current tier; DrainCompiled swaps it in once ready, so it never stalls us.
                            C.Compiling   = true;
                            UINT64   Key  = CacheKey;
                            CPU_ADDR JEnt = Pc;
                            CPU_ADDR JEnd = EffEnd;
                            UINT16   JCs  = (UINT16) m_State.Reg[S_CS];
                            m_pQueue->Async ([this, Key, JEnt, JEnd, JCs, pTierBe, Opt, Target] {
                                ApplyOptimization (pTierBe, Opt);
                                ComPtr<ICpuSegmentedCode> Seg2;
                                m_pHotArch->QueryInterface (IID_ICpuSegmentedCode, (VOID **) &Seg2);
                                if (Seg2 != nullptr) { Seg2->SetCodeSegment (JCs); }
                                ICpuCode *Hot = nullptr;
                                GenerateAotCfg (m_pHotArch, pTierBe, JEnt, JEnd, &Hot, nullptr, TRUE);
                                std::unique_lock<std::mutex> Lk (m_DoneMutex);
                                m_Done.push_back (DONE_JOB { Key, Hot, Target });   // Hot==null on failure
                            });
                        } else {
                            // Inline (blocking) promotion: no background queue configured.
                            ApplyOptimization (pTierBe, Opt);
                            ComPtr<ICpuCode> Hot;
                            if (SUCCEEDED (GenerateAotCfg (m_pArch, pTierBe, Pc, EffEnd, &Hot, nullptr, TRUE)) && Hot != nullptr) {
                                C.pCode->Release ();
                                Hot->AddRef (); C.pCode = Hot.Get ();
                            }
                            C.Tier = Target;            // record the tier (even on failure: do not retry it)
                        }
                    }
                }
                pCode = C.pCode;                        // borrowed; the cache owns the ref
                m_StatHits++;
            }
        }
        if (pCode == nullptr) {
            HRESULT Thr = m_ShadowMode
                              ? GenerateShadowUnit (m_pArch, m_pBackend, Pc, EffEnd, &Fresh)
                              : GenerateAotCfg (m_pArch, m_pBackend, Pc, EffEnd, &Fresh, nullptr, TRUE);
            if (FAILED (Thr) || Fresh == nullptr) {
                R.Reason = LC_SYS_RESULT::Fault;
                break;
            }
            m_StatCompiles++;
            pCode = Fresh.Get ();
            if (Cacheable) { pCode->AddRef (); m_CodeCache[CacheKey] = CACHED_CODE { pCode, 1, 0, false }; }
        }
        m_State.TrapPc = CPU_SMC_NO_TRAP;
        m_State.IoCtrl = CPU_IO_NONE;
        m_State.SyscallVector = CPU_NO_SYSCALL;
        m_State.CodeBase = Pc;                          // PIC base: this unit's load address (entry offset),
                                                        // so the artifact reconstructs code addresses as
                                                        // CodeBase + (target - Entry) -- valid at any load
        SyncDeviceMemoryIn ();                          // device-owned memory -> flat RAM
        UINT64 CyclesBefore = m_State.Cycles;
        pCode->Execute (m_pRAM, &m_State, nullptr);
        SyncDeviceMemoryOut ();                         // flat RAM -> device-owned memory
        R.Steps++;

        // A shadow unit could not write the dedicated dispatch fields; it left its outcome in
        // the scratch registers. Translate that into the native fields and fall through to the
        // shared dispatch below, so port I/O, INT, and control transfer reuse one code path.
        if (m_ShadowMode) {
            UINT64 St      = m_State.Reg[SHADOW_REG_STATUS];
            UINT32 ShReason = (UINT32) (St & 0xFF);
            UINT32 Width    = (UINT32) ((St >> 8) & 0xFF);
            m_State.TrapPc        = (UINT64) (m_State.Reg[SHADOW_REG_NEXTPC] & UINT64_C (0xFFFFFFFF));
            m_State.IoCtrl        = CPU_IO_NONE;
            m_State.SyscallVector = CPU_NO_SYSCALL;
            switch (ShReason) {
            case SHADOW_ST_PORTOUT:
                m_State.IoCtrl = CPU_IO_OUT | ((UINT64) Width << 8);
                m_State.IoPort = m_State.Reg[SHADOW_REG_A];
                m_State.IoData = m_State.Reg[SHADOW_REG_B];
                break;
            case SHADOW_ST_PORTIN:
                m_State.IoCtrl = CPU_IO_IN | ((UINT64) Width << 8);
                m_State.IoPort = m_State.Reg[SHADOW_REG_A];
                break;
            case SHADOW_ST_SYSCALL:
                m_State.SyscallVector = m_State.Reg[SHADOW_REG_A];
                break;
            case SHADOW_ST_SYSTRAP:
                m_State.IoCtrl = m_State.Reg[SHADOW_REG_A];   // a CPU_IO_* reason (HLT/STI/CLI/arch)
                m_State.IoData = m_State.Reg[SHADOW_REG_B];
                break;
            default:                                          // SHADOW_ST_NEXT: plain transfer to TrapPc
                break;
            }
        }

        // Advance the machine time base, then hand it to time-driven devices (the PIT) before
        // servicing this burst's trap, so a port access that latches a timer reads a count
        // consistent with the work just performed. The interpreter counts every executed micro-op
        // into Cycles; a JIT runs the whole burst natively with no per-instruction hook, so when the
        // backend left Cycles untouched we credit the burst a representative cost -- enough that the
        // counter still advances between the back-to-back port traps a BIOS timer loop issues.
        if (m_State.Cycles == CyclesBefore) { m_State.Cycles += CYCLES_PER_BURST; }
        for (Device *p : m_Devices) { p->Clock (m_State.Cycles); }

        if (m_State.TrapPc == CPU_SMC_NO_TRAP) {
            R.Reason = LC_SYS_RESULT::HaltedIdle;       // ran off the code window
            break;
        }
        // A software interrupt (INT n) records its vector and traps to the instruction after it.
        // A full machine vectors it in-guest through the IVT so the guest's own handlers run; the
        // personality returns the resume PC (and may re-point CS). Without a deliverer it stays inert.
        if (m_State.SyscallVector != CPU_NO_SYSCALL && m_DeliverSyscall) {
            Pc = m_DeliverSyscall (*this, (UINT32) m_State.SyscallVector, (CPU_ADDR) m_State.TrapPc);
            continue;
        }
        if (m_State.IoCtrl == CPU_IO_NONE) {
            Pc = (CPU_ADDR) m_State.TrapPc;             // a non-system trap (far jump)
            continue;
        }

        // A system trap: drive the machine.
        UINT32 Reason = CPU_IO_REASON (m_State.IoCtrl);
        UINT32 Width  = CPU_IO_WIDTH (m_State.IoCtrl);
        CPU_ADDR Return = (CPU_ADDR) m_State.TrapPc;
        switch (Reason) {
        case CPU_IO_OUT: {
            UINT16 Port = (UINT16) m_State.IoPort;
            UINT16 Data = (UINT16) (m_State.IoData & (Width == 8 ? 0xFF : 0xFFFF));
            if (Device *p = FindPort (Port)) { p->WritePort (Port, Width, Data); }
            R.PortWrites++;
            Pc = Return;
            break;
        }
        case CPU_IO_IN: {
            UINT16 Port = (UINT16) m_State.IoPort;
            UINT16 Val  = 0;
            if (Device *p = FindPort (Port)) { Val = p->ReadPort (Port, Width); }
            if (Width == 8) {
                m_State.Reg[S_AX] = (m_State.Reg[S_AX] & 0xFF00) | (Val & 0xFF);
            } else {
                m_State.Reg[S_AX] = Val;
            }
            R.PortReads++;
            Pc = Return;
            break;
        }
        case CPU_IO_STI: m_If = true;  Pc = Return; break;
        case CPU_IO_CLI: m_If = false; Pc = Return; break;
        case CPU_IO_HLT: m_Halted = true; Pc = Return; break;
        default:
            // Any other reason is CPU-architecture-specific (>= CPU_IO_ARCH_BASE): hand it to the
            // installed CPU personality (e.g. InstallX86System for the x86 IRET/PUSHF/POPF/INTO/
            // BOUND/INS/OUTS), which works through the generic accessors and returns the resume PC.
            // The generic machine itself has no knowledge of any specific CPU's privileged ops.
            if ((UINT64) Reason >= CPU_IO_ARCH_BASE && m_ArchTrap) {
                Pc = m_ArchTrap (*this, Reason, (UINT32) m_State.IoData, Return);
            } else {
                Pc = Return;
            }
            break;
        }

        if (m_Shutdown) {
            R.Reason = LC_SYS_RESULT::Shutdown;
            break;
        }
    }
    if (std::getenv ("LCX_CACHE_STATS") != nullptr) {
        std::fprintf (stderr, "[cache] compiles=%llu hits=%llu cached=%zu\n",
                      (unsigned long long) m_StatCompiles, (unsigned long long) m_StatHits, m_CodeCache.size ());
    }
    return R;
}

} // namespace LibCPU
