/** @file
  System-level emulation: a minimal, CPU-NEUTRAL emulated machine.

  Where user-level emulation reaches the host through system calls, system-level emulation gives
  the guest a MACHINE: a flat physical memory, a bus of emulated devices reached by port I/O, a
  translate/run loop, and a hardware-interrupt path. System understands only CPU-neutral traps
  (port IN/OUT, HALT, interrupt enable/disable) and the generic notion "a device raised an IRQ".

  Everything CPU-architecture-specific -- how FLAGS are laid out, how interrupts vector, the IRET
  stack frame, BCD/string privileged ops -- belongs to a pluggable CPU "personality", not here. A
  privileged trap whose reason is >= CPU_IO_ARCH_BASE is forwarded to the personality's handler
  (SetArchTrap); a pending IRQ is delivered by the personality's deliver hook (SetIrqDeliver). The
  x86/8086 personality lives in X86System.{h,cpp} (InstallX86System); a different CPU family would
  supply its own. System itself carries no knowledge of any particular instruction set.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_SYSTEM_H
#define LIBCPU_SYSTEM_H

#include "LibCPU/ICpu.h"
#include "LibCPU/CpuState.h"
#include "Dispatch.h"
#include "CodeCache.h"
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace LibCPU {

//
// An emulated device on the machine. It claims a set of I/O ports and may, each time
// the machine polls it, request a hardware interrupt (returning an IRQ number, or -1).
//
class Device {
public:
    virtual ~Device () {}
    virtual CHAR8 CONST *Name () CONST = 0;
    virtual bool   HandlesPort (UINT16 Port) CONST { return false; }
    virtual UINT16 ReadPort (UINT16 Port, UINT32 Width) { return 0; }
    virtual void   WritePort (UINT16 Port, UINT32 Width, UINT16 Value) {}
    virtual int    Poll () { return -1; }     // returns an IRQ to raise, or -1
    virtual void   Clock (UINT64 Cycles) {}   // feed the machine time base to time-driven devices
};

//
// Outcome of a machine run.
//
typedef struct _LC_SYS_RESULT {
    enum { Shutdown, HaltedIdle, StepBudget, Fault } Reason;
    UINT64 Steps;          // execution bursts run
    UINT64 Interrupts;     // hardware interrupts delivered
    UINT64 PortWrites;
    UINT64 PortReads;
    CPU_ADDR FinalPc;      // linear PC where the machine stopped (for post-mortem)
} LC_SYS_RESULT;

class System {
public:
    System (ICpuArchitecture *pArch, ICpuBackend *pBackend, UINT8 *pRAM, UINT64 RamSize);
    ~System ();

    void AddDevice (Device *pDevice);                // borrowed; caller keeps it alive
    void RequestShutdown () { m_Shutdown = true; }

    // Install a pump callback invoked on every execution-window boundary and periodically while
    // the CPU is halted. A console bridge uses it to stream the framebuffer out and feed injected
    // keystrokes in, without System knowing anything about the console protocol.
    void SetPump (std::function<void ()> Pump) { m_Pump = std::move (Pump); }

    // --- CPU personality hook -------------------------------------------------------------------
    // A trap whose reason is >= CPU_IO_ARCH_BASE is CPU-architecture-specific; System does not
    // interpret it but forwards (this, reason, IoData, return-pc) to the installed handler, which
    // returns the PC to resume at. This is how the x86 personality (InstallX86System) implements
    // IRET/PUSHF/POPF/INTO/BOUND/INS/OUTS without System carrying any x86 knowledge. The accessors
    // below are the CPU-neutral primitives such a handler works through.
    void SetArchTrap (std::function<CPU_ADDR (System &, UINT32 Reason, UINT32 Value, CPU_ADDR ReturnPc)> Fn) { m_ArchTrap = std::move (Fn); }
    // Install how a pending hardware IRQ is delivered. The generic loop calls this with the raw IRQ
    // number; the personality maps it to a vector and enters the handler, returning the resume PC.
    // Without it, the machine cannot vector interrupts (a halted guest just idles).
    void SetIrqDeliver (std::function<CPU_ADDR (System &, UINT32 Irq, CPU_ADDR ReturnPc)> Fn) { m_DeliverIrq = std::move (Fn); }
    // Install how a software interrupt (INT n) is delivered. A full machine vectors it in-guest
    // through the IVT so the guest's own BIOS/DOS handlers run (the BIOS provides INT 10h/13h/16h/...
    // itself). The personality returns the resume PC. Without it, an INT is inert (resumes after it),
    // which is what the host-intercept (knowledge-library) execution path relies on instead.
    void SetSyscallDeliver (std::function<CPU_ADDR (System &, UINT32 Vector, CPU_ADDR ReturnPc)> Fn) { m_DeliverSyscall = std::move (Fn); }

    UINT8   *PhysMem () { return m_pRAM; }
    UINT64   PhysSize () CONST { return m_RamSize; }
    bool     InterruptsEnabled () CONST { return m_If; }
    void     SetInterruptsEnabled (bool On) { m_If = On; }
    UINT16   PortRead (UINT16 Port, UINT32 Width) { Device *p = FindPort (Port); return p ? p->ReadPort (Port, Width) : (UINT16) 0; }
    void     PortWrite (UINT16 Port, UINT32 Width, UINT16 Val) { if (Device *p = FindPort (Port)) { p->WritePort (Port, Width, Val); } }

    // Enable tiered execution: cold immutable-code regions run on the construction backend (tier 0,
    // typically the interpreter -- instant to translate); a region executed at least Threshold times
    // is recompiled once with pHotBackend (tier 1, an optimizing JIT) and cached, so hot loops reach
    // native speed while a JIT's per-region compile cost is paid only where it amortizes. pHotBackend
    // is borrowed (the caller keeps it alive for the run). No effect on writable RAM code.
    // pHotArch is a SECOND, dedicated frontend instance (over the same RAM) used ONLY by the background
    // compile queue, so a tier-1 recompile runs on a worker thread without racing the execution thread's
    // frontend. When given, hot recompiles happen ASYNCHRONOUSLY: the machine keeps running tier 0 (the
    // interpreter) and the run loop hot-swaps the optimized artifact in once the worker has produced it,
    // so the slow optimizing compile never stalls execution. Pass nullptr for the legacy inline (blocking)
    // promotion. Both pHotBackend and pHotArch are borrowed (kept alive by the caller for the run).
    void SetHotBackend (ICpuBackend *pHotBackend, UINT64 Threshold, ICpuArchitecture *pHotArch = nullptr);

    // Multi-tier ladder. SetHotArch installs the dedicated background frontend (and the worker queue) once;
    // each AddTier appends a progressively more-optimizing tier (call them in ascending Threshold order),
    // optimized at OptLevel (honored by backends implementing ICpuBackendOptimize; LC_OPT_DEFAULT leaves
    // the backend's own setting). A hot region then climbs interp -> tier 1 -> tier 2 -> ... in the
    // background. (SetHotBackend is the one-tier shorthand built on these.)
    void SetHotArch (ICpuArchitecture *pHotArch);
    void AddTier (ICpuBackend *pBackend, UINT64 Threshold, UINT32 OptLevel);

    // Fallback backend for the shadow path: a native-CFG backend (the interpreter) used to
    // translate blocks the shadow layer cannot lower on its own (e.g. the V20 REP string loop).
    // The block runs entirely in this backend; the rest of the machine keeps using the shadow
    // backend. Without a fallback set, such a block faults. Held with an owned reference.
    void SetFallback (ICpuBackend *pBackend);

    // Map a device-owned host buffer over a guest physical region. The guest's flat RAM and the
    // device buffer are kept in sync at execution-window boundaries, so the region is genuinely
    // the device's memory (a card's video RAM, a bankable aperture), not a slice of main RAM.
    // pHost is borrowed; the caller keeps the device (and its buffer) alive for the run.
    void MapDeviceMemory (UINT32 Base, UINT32 Size, UINT8 *pHost, bool ReadOnly = false);

    // Mark a region that no component backs ("no card, no memory"): it reads as open bus (0xFF)
    // and writes to it are discarded. Enforced at execution-window boundaries.
    void MarkOpenBus (UINT32 Base, UINT32 Size);

    // Mark a guest-physical range as immutable code (a ROM/BIOS image): translations whose entry
    // lands here are cached and reused, since the bytes can never change. Read-only device-memory
    // regions are treated as immutable automatically; this is for firmware loaded into flat RAM.
    void MarkCodeImmutable (UINT64 Base, UINT64 Size);

    CPU_STATE *State () { return &m_State; }

    // Run from CodeEntry until a device requests shutdown, the guest halts with no
    // interrupt source left, or MaxSteps bursts elapse. CodeEnd bounds each
    // translation window.
    LC_SYS_RESULT Run (CPU_ADDR CodeEntry, CPU_ADDR CodeEnd, UINT64 MaxSteps);

private:
    Device *FindPort (UINT16 Port) CONST;
    int       PollDevices ();                          // first device asserting an IRQ
    void      SyncDeviceMemoryIn ();                   // device buffers -> flat RAM (before a window)
    void      SyncDeviceMemoryOut ();                  // flat RAM -> device buffers (after a window)

    // A device-owned memory region mapped into the guest address space (see MapDeviceMemory).
    struct DEVICE_MEMORY { UINT32 Base; UINT32 Size; UINT8 *pHost; bool ReadOnly; };
    std::vector<DEVICE_MEMORY> m_DeviceMemory;

    // Unbacked ("open bus") regions: read 0xFF, writes discarded (see MarkOpenBus).
    struct OPEN_BUS { UINT32 Base; UINT32 Size; };
    std::vector<OPEN_BUS> m_OpenBus;

    ICpuArchitecture       *m_pArch;
    ICpuBackend            *m_pBackend;
    UINT8                  *m_pRAM;
    UINT64                  m_RamSize;
    CPU_STATE               m_State;
    std::vector<Device *> m_Devices;
    // Memoizes FindPort: a port-bound guest (e.g. a BIOS polling a status register) hits the same
    // few ports millions of times; caching port -> owning device turns the per-access linear scan
    // over every device into an O(1) lookup. The device set is fixed during a run; AddDevice clears
    // it. Mutable so the const FindPort can fill it.
    mutable std::unordered_map<UINT16, Device *> m_PortCache;
    bool                    m_If;          // interrupt-enable flag (8086 IF)
    bool                    m_Halted;      // executed HLT, waiting for an interrupt
    bool                    m_Shutdown;
    bool                    m_ShadowMode = false;   // base backend lacks native CFG -> shadow translate + dispatch
    ICpuBackend            *m_pFallback  = nullptr;  // native-CFG backend for blocks the shadow path can't lower

    // Optional persistent translation cache ($LIBCPU_CODECACHE): compiled artifacts are
    // serialized and stored keyed by (address-folded content hash, producing backend, host
    // fingerprint, shadow/native variant), so a rerun reloads them instead of recompiling --
    // the speed win for compiler/script backends whose cold compile is expensive.
    CodeCache              *m_pCodeCache = nullptr;
    std::string  PersistKey (ICpuBackend *pBe, UINT64 LinAddr, CPU_ADDR Pc, CPU_ADDR EffEnd, bool Shadow) CONST;
    bool         PersistLoad (ICpuBackend *pBe, std::string CONST &Key, OUT ComPtr<ICpuCode> &Out);
    void         PersistSave (ICpuBackend *pBe, std::string CONST &Key, ICpuCode *pCode);
    std::function<void ()>  m_Pump;        // console bridge boundary callback (see SetPump)
    std::function<CPU_ADDR (System &, UINT32, UINT32, CPU_ADDR)> m_ArchTrap;    // CPU personality trap (SetArchTrap)
    std::function<CPU_ADDR (System &, UINT32, CPU_ADDR)>         m_DeliverIrq;  // CPU personality IRQ delivery (SetIrqDeliver)
    std::function<CPU_ADDR (System &, UINT32, CPU_ADDR)>         m_DeliverSyscall; // software-INT delivery (SetSyscallDeliver)

    // In-memory translation cache: compiled code keyed by the burst's linear entry address. Without
    // it the run loop re-translates every burst, which is cheap for the interpreter but ruinous for a
    // JIT (an LLVM module compiled per burst). Invalidated when the guest writes to watched code (SMC).
    // Tier: which tier's artifact pCode currently is -- 0 = tier 0 (the base/interpreter backend),
    // t in 1..N = m_Tiers[t-1]. Compiling: a background promotion to a higher tier is in flight; the run
    // loop keeps using the current pCode until the worker delivers the next one.
    // IsShadow: this artifact was built by GenerateShadowUnit (a non-CFG backend), so after it runs
    // the outcome is read from the scratch registers; a native artifact uses the CPU_STATE fields.
    struct CACHED_CODE { ICpuCode *pCode; UINT64 Runs; UINT32 Tier; bool Compiling; bool IsShadow; };
    std::unordered_map<UINT64, CACHED_CODE> m_CodeCache;
    UINT64 m_StatCompiles = 0;   // diagnostic (LCX_CACHE_STATS): translations performed
    UINT64 m_StatHits     = 0;   // diagnostic: cache hits (translations avoided)
    std::vector<std::pair<UINT64, UINT64>> m_ImmutableCode;   // (base, end) ranges safe to cache
    void ClearCodeCache ();
    bool IsImmutableCode (UINT64 Addr) CONST;                 // in a ROM / marked-immutable region?

    // The MULTI-TIER ladder. A region climbs interp(0) -> m_Tiers[0] -> m_Tiers[1] -> ... as its run count
    // crosses each tier's Threshold: progressively more optimizing (and slower-to-compile) backends, each
    // told to optimize at OptLevel (ICpuBackendOptimize). Ordered by ascending Threshold.
    struct HOT_TIER { ICpuBackend *pBackend; UINT64 Threshold; UINT32 OptLevel; };
    std::vector<HOT_TIER> m_Tiers;

    // Background (asynchronous) recompilation. m_pQueue runs every tier promotion on a worker that uses
    // m_pHotArch (a dedicated frontend over the same RAM, shared because there is one worker), so the
    // optimizing compile never stalls the execution thread. Finished artifacts are posted to m_Done under
    // m_DoneMutex; the run loop swaps them in at a safe point (so the cache stays single-threaded).
    ICpuArchitecture *m_pHotArch = nullptr;
    DispatchQueue    *m_pQueue   = nullptr;
    std::mutex        m_DoneMutex;
    struct DONE_JOB { UINT64 Key; ICpuCode *pCode; UINT32 Tier; };
    std::vector<DONE_JOB> m_Done;
    void DrainCompiled ();                                    // swap in completed background compiles
};

} // namespace LibCPU

#endif // LIBCPU_SYSTEM_H
