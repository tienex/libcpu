/** @file
  Run a machine assembled from COM hardware-component bundles. The components matched by
  MachineBuilder are COM objects; this harness bridges their capabilities onto core/System and
  runs them on the V20 CPU:

    - IPortDevice  -> ComDeviceAdapter routes port I/O to the component.
    - IMemoryDevice -> the physical memory map (RAM/ROM regions) is read from the components,
      so the address space comes from the device tree; RAM is sized to cover it.
    - IInterruptSource + IInterruptController -> InterruptArbiter polls each source and routes
      its IRQ through the 8259 PIC, which masks it (IMR) and supplies the vector base. So an
      interrupt only fires if the guest has programmed and unmasked the PIC.

  With no image, a built-in power-on program initializes the PIC, programs the timer, and idles;
  each timer tick (gated by the PIC) writes '.' to the serial console. With --image, the given
  binary is loaded and run instead. The CPU is the V20 (the i8088 node maps onto it for now).

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
#include <string>
#include <vector>

namespace LibCPU {

// Bridges a COM component's port-mapped I/O onto the System device bus. Interrupts are handled
// centrally by the InterruptArbiter, so this adapter never raises one.
class ComDeviceAdapter final : public Device {
public:
    explicit ComDeviceAdapter (IDevice *pDev) : m_pDev (pDev)
    {
        m_pDev->AddRef ();
        m_pDev->QueryInterface (IID_IPortDevice, (VOID **) &m_pPort);
    }
    ~ComDeviceAdapter () override
    {
        if (m_pPort != nullptr) { m_pPort->Release (); }
        m_pDev->Release ();
    }

    CHAR8 CONST *Name () CONST override { return m_pDev->GetName (); }
    bool HandlesPort (UINT16 Port) CONST override { return m_pPort != nullptr && m_pPort->OwnsPort (Port); }
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

private:
    IDevice     *m_pDev;
    IPortDevice *m_pPort = nullptr;
};

// Aggregates every interrupt source and routes a raised IRQ through the PIC: the PIC masks it
// (IMR) and gives the vector base, so an interrupt reaches the CPU only when the guest has
// programmed and unmasked the controller. (System dispatches INT 8+IRQ, which equals the PIC's
// vector when the conventional ICW2 base of 8 is in effect.)
class InterruptArbiter final : public Device {
public:
    InterruptArbiter (std::vector<IInterruptSource *> Sources, IInterruptController *pPic)
        : m_Sources (std::move (Sources)), m_pPic (pPic) {}
    ~InterruptArbiter () override
    {
        for (IInterruptSource *p : m_Sources) { p->Release (); }
        if (m_pPic != nullptr) { m_pPic->Release (); }
    }

    CHAR8 CONST *Name () CONST override { return "8259 interrupt arbiter"; }

    int Poll () override
    {
        for (IInterruptSource *p : m_Sources) {
            UINT32 Irq = 0;
            if (p->PollInterrupt (&Irq) != S_OK) { continue; }
            if (m_pPic != nullptr) {
                UINT32 Vector = 0;
                if (m_pPic->AcceptInterrupt (Irq, &Vector) != S_OK) { continue; }   // masked by the PIC
            }
            return (int) Irq;
        }
        return -1;
    }

private:
    std::vector<IInterruptSource *> m_Sources;
    IInterruptController           *m_pPic;
};

static inline int
RunMachineDemo (MachineBuilder &Builder, ICpuBackend *pBackend, CHAR8 CONST *pImagePath, UINT32 LoadAddr)
{
    // Discover capabilities of every matched component.
    std::vector<IInterruptSource *> Sources;
    IInterruptController           *pPic = nullptr;
    UINT64                          MemEnd = 0x100000;       // at least the 1 MiB real-mode space
    std::printf ("\n== power-on: assembling the address space and interrupt path\n");
    std::printf ("   memory map:\n");
    for (MATCHED_DEVICE CONST &D : Builder.Devices ()) {
        IInterruptSource *pSrc = nullptr;
        D.pDevice->QueryInterface (IID_IInterruptSource, (VOID **) &pSrc);
        if (pSrc != nullptr) { Sources.push_back (pSrc); }
        if (pPic == nullptr) { D.pDevice->QueryInterface (IID_IInterruptController, (VOID **) &pPic); }
        IMemoryDevice *pMem = nullptr;
        D.pDevice->QueryInterface (IID_IMemoryDevice, (VOID **) &pMem);
        if (pMem != nullptr) {
            UINT32 Base = pMem->GetBase (), Size = pMem->GetSize ();
            std::printf ("     0x%08x..0x%08x  %s  %s\n", Base, Base + Size - 1,
                         pMem->IsReadOnly () ? "RO" : "RW", D.pDevice->GetName ());
            if ((UINT64) Base + Size > MemEnd) { MemEnd = (UINT64) Base + Size; }
            pMem->Release ();
        }
    }

    std::vector<UINT8> Ram ((size_t) MemEnd, 0);
    ICpuArchitecture *pArch = CreateV20 ();
    pArch->SetCodeMemory (Ram.data (), Ram.size ());
    System Machine (pArch, pBackend, Ram.data (), Ram.size ());

    // Port devices onto the bus; one arbiter for interrupts.
    std::vector<std::unique_ptr<ComDeviceAdapter>> Adapters;
    for (MATCHED_DEVICE CONST &D : Builder.Devices ()) {
        Adapters.push_back (std::unique_ptr<ComDeviceAdapter> (new ComDeviceAdapter (D.pDevice)));
        Machine.AddDevice (Adapters.back ().get ());
    }
    InterruptArbiter Arbiter (std::move (Sources), pPic);
    Machine.AddDevice (&Arbiter);

    // Map device-owned memory (a display card's video RAM) over its region: the machine keeps the
    // flat RAM and the card's own buffer in sync at window boundaries, so the framebuffer is truly
    // the card's memory rather than a slice of main RAM.
    for (MATCHED_DEVICE CONST &D : Builder.Devices ()) {
        IMemoryDevice *pMem  = nullptr;
        IHostMemory   *pHost = nullptr;
        D.pDevice->QueryInterface (IID_IMemoryDevice, (VOID **) &pMem);
        D.pDevice->QueryInterface (IID_IHostMemory, (VOID **) &pHost);
        if (pMem != nullptr && pHost != nullptr) {
            Machine.MapDeviceMemory (pMem->GetBase (), pMem->GetSize (), pHost->GetHostBuffer ());
        }
        if (pMem  != nullptr) { pMem->Release (); }
        if (pHost != nullptr) { pHost->Release (); }
    }

    // The board-level signal interconnect (speaker/cassette to the PPI/PIT lines) is declared in
    // the device tree ("signals = <&ppi ...>") and was resolved by MachineBuilder::Build.
    Machine.State ()->Reg[4]  = 0x1000;                      // SP
    Machine.State ()->Reg[10] = 0x0000;                      // SS

    CPU_ADDR Entry;
    CPU_ADDR End;
    if (pImagePath != nullptr) {
        // Load and run a supplied image (e.g. a BIOS dump) at LoadAddr.
        std::FILE *pF = std::fopen (pImagePath, "rb");
        if (pF == nullptr) { std::printf ("   cannot open image '%s'\n", pImagePath); pArch->Release (); return 2; }
        std::fseek (pF, 0, SEEK_END);
        long Size = std::ftell (pF);
        std::fseek (pF, 0, SEEK_SET);
        if (Size <= 0 || (UINT64) LoadAddr + (UINT64) Size > Ram.size ()) {
            std::fclose (pF); std::printf ("   image does not fit at 0x%x\n", LoadAddr); pArch->Release (); return 2;
        }
        if (std::fread (Ram.data () + LoadAddr, 1, (size_t) Size, pF) != (size_t) Size) {
            std::fclose (pF); std::printf ("   short read of image\n"); pArch->Release (); return 2;
        }
        std::fclose (pF);
        std::printf ("   loaded image '%s' (%ld bytes) at 0x%05x\n", pImagePath, Size, LoadAddr);
        Entry = LoadAddr;
        End   = LoadAddr + (CPU_ADDR) Size;
    } else {
        // Built-in power-on self test: init the PIC, program the timer, idle. Each PIC-gated
        // timer tick (INT 8) runs the ISR at 0:0x0500, which writes '.' to the UART.
        UINT8 const Isr[] = { 0xBA, 0xF8, 0x03, 0xB0, 0x2E, 0xEE, 0xCF };   // mov dx,3F8; mov al,'.'; out; iret
        std::memcpy (Ram.data () + 0x0500, Isr, sizeof (Isr));
        Machine.SetIvt (0x08, 0x0000, 0x0500);

        std::vector<UINT8> Prog = {
            0xB0, 0x13, 0xE6, 0x20,  // mov al,0x13 ; out 0x20,al   ICW1 (edge, single, ICW4)
            0xB0, 0x08, 0xE6, 0x21,  // mov al,0x08 ; out 0x21,al   ICW2 (vector base 8)
            0xB0, 0x01, 0xE6, 0x21,  // mov al,0x01 ; out 0x21,al   ICW4 (8086 mode) -> IRQs unmasked
            0xBA, 0xF8, 0x03         // mov dx, 0x3F8
        };
        CHAR8 CONST *pMsg = "PC/XT OK\n";
        for (CHAR8 CONST *p = pMsg; *p != '\0'; ++p) {
            Prog.push_back (0xB0); Prog.push_back ((UINT8) *p); Prog.push_back (0xEE);
        }
        // Sign-on banner to the text framebuffer of whatever display card is fitted: DS = the
        // framebuffer segment (MDA 0xB000, CGA/EGA/VGA 0xB800), then write each char+attr word to
        // DS:[col*2] -- a memory-mapped write that lands at the card's physical framebuffer.
        UINT16 FbSeg = 0xB000;
        for (MATCHED_DEVICE CONST &D : Builder.Devices ()) {
            IDisplayDevice *pDisp = nullptr;
            D.pDevice->QueryInterface (IID_IDisplayDevice, (VOID **) &pDisp);
            if (pDisp != nullptr) { FbSeg = (UINT16) (pDisp->GetFramebufferBase () >> 4); pDisp->Release (); break; }
        }
        Prog.push_back (0xB8); Prog.push_back ((UINT8) (FbSeg & 0xFF)); Prog.push_back ((UINT8) (FbSeg >> 8));  // mov ax, FbSeg
        Prog.push_back (0x8E); Prog.push_back (0xD8);                          // mov ds, ax
        CHAR8 CONST *pScreen = "LIBCPU PC/XT 5160";
        for (UINT16 Off = 0; *pScreen != '\0'; ++pScreen, Off = (UINT16) (Off + 2)) {
            Prog.push_back (0xB8); Prog.push_back ((UINT8) *pScreen); Prog.push_back (0x07);   // mov ax, 0x07<<8|ch
            Prog.push_back (0x89); Prog.push_back (0x06);                      // mov [disp16], ax
            Prog.push_back ((UINT8) (Off & 0xFF)); Prog.push_back ((UINT8) (Off >> 8));
        }
        // Beep the speaker and cycle the cassette motor through the board signal lines: program PIT
        // channel 2 to ~896 Hz (divisor 0x0533), then drive PPI port B -- gate+data sound the
        // speaker, bit 3 (active low) runs the cassette motor. The PPI/PIT push these onto whatever
        // signal sinks (speaker, cassette) the machine wired up.
        UINT8 const Beep[] = {
            0xB0, 0xB6, 0xE6, 0x43,  // mov al,0xB6 ; out 0x43,al   PIT ch2, lo/hi, mode 3
            0xB0, 0x33, 0xE6, 0x42,  // mov al,0x33 ; out 0x42,al   divisor low
            0xB0, 0x05, 0xE6, 0x42,  // mov al,0x05 ; out 0x42,al   divisor high -> tone set
            0xB0, 0x0B, 0xE6, 0x61,  // mov al,0x0B ; out 0x61,al   gate+data+motor-off -> speaker ON
            0xB0, 0x03, 0xE6, 0x61,  // mov al,0x03 ; out 0x61,al   clear bit3 -> cassette motor ON
            0xB0, 0x00, 0xE6, 0x61,  // mov al,0x00 ; out 0x61,al   gate/data low -> speaker off
            0xB0, 0x08, 0xE6, 0x61   // mov al,0x08 ; out 0x61,al   set bit3 -> cassette motor OFF
        };
        Prog.insert (Prog.end (), Beep, Beep + sizeof (Beep));

        UINT8 const PitAndIdle[] = {
            0xB0, 0x36, 0xE6, 0x43,  // mov al,0x36 ; out 0x43,al   PIT ch0 mode 3
            0xB0, 0xFF, 0xE6, 0x40,  // count low
            0xB0, 0xFF, 0xE6, 0x40,  // count high -> armed
            0xFB, 0xF4, 0xEB, 0xFD   // sti ; hlt ; jmp $-1
        };
        Prog.insert (Prog.end (), PitAndIdle, PitAndIdle + sizeof (PitAndIdle));
        Entry = 0x0600;
        std::memcpy (Ram.data () + Entry, Prog.data (), Prog.size ());
        End = Entry + (CPU_ADDR) Prog.size ();
    }

    std::printf ("   --- serial console (CPU: V20, backend: %s) ---\n   ", pBackend->GetName ());
    LC_SYS_RESULT R = Machine.Run (Entry, End, 2000000);
    std::printf ("\n   --- machine halted (reason=%s, interrupts=%llu, port writes=%llu) ---\n",
                 R.Reason == LC_SYS_RESULT::HaltedIdle ? "halted-idle" :
                 R.Reason == LC_SYS_RESULT::Shutdown   ? "shutdown" :
                 R.Reason == LC_SYS_RESULT::StepBudget ? "step-budget" : "fault",
                 (unsigned long long) R.Interrupts, (unsigned long long) R.PortWrites);

    // Render any text display from ITS OWN video RAM. A card that owns its memory (IHostMemory)
    // is scanned out of the card's buffer at the text page's offset within the aperture; the
    // sync at window boundaries has already captured the CPU's writes into it.
    for (MATCHED_DEVICE CONST &D : Builder.Devices ()) {
        IDisplayDevice *pDisp = nullptr;
        D.pDevice->QueryInterface (IID_IDisplayDevice, (VOID **) &pDisp);
        if (pDisp == nullptr) { continue; }
        UINT32 Base = pDisp->GetFramebufferBase (), Size = pDisp->GetFramebufferSize ();
        std::printf ("   --- %s, framebuffer 0x%05x ---\n", D.pDevice->GetName (), Base);

        IMemoryDevice *pMem  = nullptr;
        IHostMemory   *pHost = nullptr;
        D.pDevice->QueryInterface (IID_IMemoryDevice, (VOID **) &pMem);
        D.pDevice->QueryInterface (IID_IHostMemory, (VOID **) &pHost);
        if (pMem != nullptr && pHost != nullptr && Base >= pMem->GetBase ()) {
            pDisp->RenderText (pHost->GetHostBuffer () + (Base - pMem->GetBase ()), Size);   // the card's own RAM
        } else if ((UINT64) Base + Size <= Ram.size ()) {
            pDisp->RenderText (&Ram[Base], Size);                                            // fallback: flat RAM
        }
        if (pMem  != nullptr) { pMem->Release (); }
        if (pHost != nullptr) { pHost->Release (); }
        pDisp->Release ();
    }

    pArch->Release ();
    return 0;
}

} // namespace LibCPU

#endif // LIBCPU_RUNMACHINE_H
