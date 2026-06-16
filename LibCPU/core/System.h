/** @file
  System-level emulation: a minimal emulated machine.

  Where user-level emulation reaches the host through system calls, system-level
  emulation gives the guest a MACHINE: a flat physical memory, a bus of emulated
  devices reached by port I/O, and a hardware-interrupt path (a device raises an IRQ,
  the machine vectors the CPU through the interrupt table to a guest ISR, which
  returns with IRET). The CPU's privileged instructions -- IN/OUT, STI/CLI, HLT,
  IRET -- trap to System (via ICpuSystemEmitter), which drives the addressed device
  or performs the control action and resumes. This is the 8086/PC model (IRQ n ->
  INT 8+n, real-mode IVT at physical 0).

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_SYSTEM_H
#define LIBCPU_SYSTEM_H

#include "LibCPU/ICpu.h"
#include "LibCPU/CpuState.h"
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
} LC_SYS_RESULT;

class System {
public:
    System (ICpuArchitecture *pArch, ICpuBackend *pBackend, UINT8 *pRAM, UINT64 RamSize);

    void AddDevice (Device *pDevice);                // borrowed; caller keeps it alive
    void RequestShutdown () { m_Shutdown = true; }

    // Map a device-owned host buffer over a guest physical region. The guest's flat RAM and the
    // device buffer are kept in sync at execution-window boundaries, so the region is genuinely
    // the device's memory (a card's video RAM, a bankable aperture), not a slice of main RAM.
    // pHost is borrowed; the caller keeps the device (and its buffer) alive for the run.
    void MapDeviceMemory (UINT32 Base, UINT32 Size, UINT8 *pHost);

    // Mark a region that no component backs ("no card, no memory"): it reads as open bus (0xFF)
    // and writes to it are discarded. Enforced at execution-window boundaries.
    void MarkOpenBus (UINT32 Base, UINT32 Size);

    // Seed an interrupt-vector-table entry (real-mode IVT at physical 0): vector ->
    // Seg:Off. Used by the "BIOS" to install a default ISR before running.
    void SetIvt (UINT32 Vector, UINT16 Seg, UINT16 Off);

    CPU_STATE *State () { return &m_State; }

    // Run from CodeEntry until a device requests shutdown, the guest halts with no
    // interrupt source left, or MaxSteps bursts elapse. CodeEnd bounds each
    // translation window.
    LC_SYS_RESULT Run (CPU_ADDR CodeEntry, CPU_ADDR CodeEnd, UINT64 MaxSteps);

private:
    Device *FindPort (UINT16 Port) CONST;
    int       PollDevices ();                          // first device asserting an IRQ
    void      InjectInterrupt (UINT32 Vector, CPU_ADDR ReturnPc);
    void      SyncDeviceMemoryIn ();                   // device buffers -> flat RAM (before a window)
    void      SyncDeviceMemoryOut ();                  // flat RAM -> device buffers (after a window)

    // A device-owned memory region mapped into the guest address space (see MapDeviceMemory).
    struct DEVICE_MEMORY { UINT32 Base; UINT32 Size; UINT8 *pHost; };
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
    bool                    m_If;          // interrupt-enable flag (8086 IF)
    bool                    m_Halted;      // executed HLT, waiting for an interrupt
    bool                    m_Shutdown;
    CPU_ADDR                m_NextPc;      // set by InjectInterrupt
};

} // namespace LibCPU

#endif // LIBCPU_SYSTEM_H
