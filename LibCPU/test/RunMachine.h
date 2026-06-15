/** @file
  Run a machine assembled from COM hardware-component bundles. The components matched by
  MachineBuilder are COM objects (IDevice + IPortDevice / IInterruptSource); ComDeviceAdapter
  bridges each onto core/System's device bus so the existing V20 + System run loop can drive
  them. A small power-on program writes a banner to the 8250 UART (port 0x3F8), so a correct
  run produces visible output through the real component chain:

      device tree -> bundle match -> COM component -> CPU port I/O -> device effect (stdout)

  The CPU is the V20 frontend (the i8088 node maps onto it for now).

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/
#ifndef LIBCPU_RUNMACHINE_H
#define LIBCPU_RUNMACHINE_H

#include "CpuV20.h"
#include "LibCPU/CpuState.h"
#include "LibCPU/IDevice.h"
#include "../core/System.h"
#include "../core/MachineBuilder.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace LibCPU {

// Bridges a COM hardware component onto the System device bus: port reads/writes go to the
// component's IPortDevice, interrupt polling to its IInterruptSource (if it has one).
class ComDeviceAdapter final : public Device {
public:
    explicit ComDeviceAdapter (IDevice *pDev) : m_pDev (pDev)
    {
        m_pDev->AddRef ();
        m_pDev->QueryInterface (IID_IPortDevice, (VOID **) &m_pPort);
        m_pDev->QueryInterface (IID_IInterruptSource, (VOID **) &m_pIrq);
    }
    ~ComDeviceAdapter () override
    {
        if (m_pPort != nullptr) { m_pPort->Release (); }
        if (m_pIrq != nullptr) { m_pIrq->Release (); }
        m_pDev->Release ();
    }

    CHAR8 CONST *Name () CONST override { return m_pDev->GetName (); }

    bool HandlesPort (UINT16 Port) CONST override
    {
        return m_pPort != nullptr && m_pPort->OwnsPort (Port);
    }

    UINT16 ReadPort (UINT16 Port, UINT32 Width) override
    {
        UINT32 Value = 0;
        if (m_pPort != nullptr) { m_pPort->ReadPort (Port, Width, &Value); }
        return (UINT16) Value;
    }

    void WritePort (UINT16 Port, UINT32 Width, UINT16 Value) override
    {
        if (m_pPort != nullptr) { m_pPort->WritePort (Port, Width, Value); }
    }

    int Poll () override
    {
        if (m_pIrq == nullptr) { return -1; }
        UINT32 Irq = 0;
        return (m_pIrq->PollInterrupt (&Irq) == S_OK) ? (int) Irq : -1;
    }

private:
    IDevice          *m_pDev;
    IPortDevice      *m_pPort = nullptr;
    IInterruptSource *m_pIrq  = nullptr;
};

static inline int
RunMachineDemo (MachineBuilder &Builder, ICpuBackend *pBackend)
{
    std::printf ("\n== power-on: running the assembled machine on '%s' (CPU: V20)\n", pBackend->GetName ());

    std::vector<UINT8> Ram (0x100000, 0);                    // 1 MiB real-mode address space

    // Power-on program at 0x0600: write "PC/XT OK\n" to the UART (COM1, 0x3F8), then halt.
    std::vector<UINT8> Prog = { 0xBA, 0xF8, 0x03 };          // mov dx, 0x3F8
    CHAR8 CONST *pMsg = "PC/XT OK\n";
    for (CHAR8 CONST *p = pMsg; *p != '\0'; ++p) {
        Prog.push_back (0xB0);                               // mov al, <char>
        Prog.push_back ((UINT8) *p);
        Prog.push_back (0xEE);                               // out dx, al
    }
    Prog.push_back (0xF4);                                   // hlt
    Prog.push_back (0xEB); Prog.push_back (0xFD);            // jmp $ (idle once halted)
    CPU_ADDR Entry = 0x0600;
    std::memcpy (Ram.data () + Entry, Prog.data (), Prog.size ());

    ICpuArchitecture *pArch = CreateV20 ();
    pArch->SetCodeMemory (Ram.data (), Ram.size ());
    System Machine (pArch, pBackend, Ram.data (), Ram.size ());

    // Bridge every matched component onto the device bus.
    std::vector<std::unique_ptr<ComDeviceAdapter>> Adapters;
    for (MATCHED_DEVICE CONST &D : Builder.Devices ()) {
        Adapters.push_back (std::unique_ptr<ComDeviceAdapter> (new ComDeviceAdapter (D.pDevice)));
        Machine.AddDevice (Adapters.back ().get ());
    }

    Machine.State ()->Reg[4]  = 0x1000;                      // SP
    Machine.State ()->Reg[10] = 0x0000;                      // SS

    std::printf ("   --- serial console ---\n   ");
    LC_SYS_RESULT R = Machine.Run (Entry, (CPU_ADDR) (Entry + Prog.size ()), 100000);
    std::printf ("\n   --- machine halted (reason=%s, port writes=%llu) ---\n",
                 R.Reason == LC_SYS_RESULT::HaltedIdle ? "halted-idle" :
                 R.Reason == LC_SYS_RESULT::Shutdown   ? "shutdown" :
                 R.Reason == LC_SYS_RESULT::StepBudget ? "step-budget" : "fault",
                 (unsigned long long) R.PortWrites);

    pArch->Release ();
    return 0;
}

} // namespace LibCPU

#endif // LIBCPU_RUNMACHINE_H
