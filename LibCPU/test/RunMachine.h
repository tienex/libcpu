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
#include "../core/DeviceTree.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
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

// Aggregates every interrupt source and routes a raised IRQ through the 8259 cascade: lines 0-7
// go to the master, 8-15 to the slave (which is itself wired into the master's IRQ2, so the slave
// can only get through when the master's cascade line is unmasked). Each PIC masks its own lines
// (IMR) and supplies the vector from its ICW2 base. System dispatches INT 8+result, so the arbiter
// returns (Vector - 8): 8 + (Vector - 8) reconstructs the exact PIC vector for either controller,
// no matter what ICW2 base each was programmed with.
class InterruptArbiter final : public Device {
public:
    InterruptArbiter (std::vector<IInterruptSource *> Sources,
                      IInterruptController *pMaster, IInterruptController *pSlave)
        : m_Sources (std::move (Sources)), m_pMaster (pMaster), m_pSlave (pSlave) {}
    ~InterruptArbiter () override
    {
        for (IInterruptSource *p : m_Sources) { p->Release (); }
        if (m_pMaster != nullptr) { m_pMaster->Release (); }
        if (m_pSlave  != nullptr) { m_pSlave->Release (); }
    }

    CHAR8 CONST *Name () CONST override { return "8259 interrupt arbiter"; }

    int Poll () override
    {
        for (IInterruptSource *p : m_Sources) {
            UINT32 Irq = 0;
            if (p->PollInterrupt (&Irq) != S_OK) { continue; }
            UINT32 Vector = 0;
            if (Irq < 8) {                                          // master line
                if (m_pMaster != nullptr && m_pMaster->AcceptInterrupt (Irq, &Vector) == S_OK) {
                    return (int) (Vector - 8);
                }
            } else if (m_pSlave != nullptr) {                       // slave line (8-15), cascaded on IRQ2
                UINT32 Cascade = 0;
                if (m_pMaster != nullptr && m_pMaster->AcceptInterrupt (2, &Cascade) != S_OK) { continue; }
                if (m_pSlave->AcceptInterrupt (Irq - 8, &Vector) == S_OK) {
                    return (int) (Vector - 8);
                }
            }
        }
        return -1;
    }

private:
    std::vector<IInterruptSource *> m_Sources;
    IInterruptController           *m_pMaster;
    IInterruptController           *m_pSlave;
};

// Performs DMA transfers on behalf of peripherals. Each time the machine polls the bus, the bridge
// asks every DMA peripheral whether it has a pending request; if so it reads the channel's address
// and count from the DMA controller and moves the bytes between the peripheral's buffer and guest
// memory, then tells the peripheral the transfer is done (so it can post its result and raise its
// IRQ). It never asserts an interrupt itself -- it just moves bytes, like the real 8237 cycle.
class DmaBridge final : public Device {
public:
    DmaBridge (UINT8 *pRam, UINT64 RamSize, std::vector<IDmaPeripheral *> Peripherals, IDmaController *pDma)
        : m_pRam (pRam), m_RamSize (RamSize), m_Peripherals (std::move (Peripherals)), m_pDma (pDma) {}
    ~DmaBridge () override
    {
        for (IDmaPeripheral *p : m_Peripherals) { p->Release (); }
        if (m_pDma != nullptr) { m_pDma->Release (); }
    }

    CHAR8 CONST *Name () CONST override { return "8237 DMA bridge"; }

    int Poll () override
    {
        for (IDmaPeripheral *p : m_Peripherals) {
            UINT32  Channel = 0, Length = 0;
            BOOLEAN ToMemory = FALSE;
            UINT8  *pBuf = nullptr;
            if (p->GetDmaRequest (&Channel, &ToMemory, &pBuf, &Length) != S_OK) { continue; }
            UINT32 Addr = 0, Count = 0, Mode = 0;
            if (m_pDma != nullptr && m_pDma->GetChannel (Channel, &Addr, &Count, &Mode) == S_OK) {
                UINT32 N = Count + 1;                       // the 8237 holds count-minus-one
                if (N > Length) { N = Length; }
                if ((UINT64) Addr + N <= m_RamSize && pBuf != nullptr) {
                    if (ToMemory) { std::memcpy (m_pRam + Addr, pBuf, N); }   // device -> memory
                    else          { std::memcpy (pBuf, m_pRam + Addr, N); }   // memory -> device
                }
            }
            p->CompleteDma ();
        }
        return -1;                                          // the bridge moves bytes, raises no IRQ
    }

private:
    UINT8                          *m_pRam;
    UINT64                          m_RamSize;
    std::vector<IDmaPeripheral *>   m_Peripherals;
    IDmaController                 *m_pDma;
};

static inline int
RunMachineDemo (MachineBuilder &Builder, ICpuBackend *pBackend, CHAR8 CONST *pImagePath, UINT32 LoadAddr,
                int Demo = 0)                                // 0 none, 1 bank-switch, 2 keyboard IRQ1
{
    // Discover capabilities of every matched component.
    std::vector<IInterruptSource *> Sources;
    IInterruptController           *pMaster = nullptr;       // 8259 at the lower base (0x20)
    IInterruptController           *pSlave  = nullptr;       // cascaded 8259 (0xA0), IRQ8-15
    std::vector<IDmaPeripheral *>   DmaPeris;                // peripherals that transfer via DMA
    IDmaController                 *pDmaCtl = nullptr;       // the 8237
    UINT64                          MemEnd = 0x100000;       // at least the 1 MiB real-mode space
    std::vector<std::pair<UINT32, UINT32>> Mapped;          // (base, size) of every backed region
    std::printf ("\n== power-on: assembling the address space and interrupt path\n");
    std::printf ("   memory map:\n");
    for (MATCHED_DEVICE CONST &D : Builder.Devices ()) {
        IInterruptSource *pSrc = nullptr;
        D.pDevice->QueryInterface (IID_IInterruptSource, (VOID **) &pSrc);
        if (pSrc != nullptr) { Sources.push_back (pSrc); }
        IInterruptController *pIc = nullptr;
        D.pDevice->QueryInterface (IID_IInterruptController, (VOID **) &pIc);
        if (pIc != nullptr) {                                // master if at the low base, else slave
            std::string Key = "reg";
            DT_PROP *pReg = (D.pNode != nullptr) ? D.pNode->FindProp (Key) : nullptr;
            UINT32 RegBase = 0;                              // big-endian cell 0 of "reg" (the I/O base)
            if (pReg != nullptr && pReg->Value.size () >= 4) {
                UINT8 CONST *b = pReg->Value.data ();
                RegBase = ((UINT32) b[0] << 24) | ((UINT32) b[1] << 16) | ((UINT32) b[2] << 8) | b[3];
            }
            if (RegBase >= 0x80) { if (pSlave  == nullptr) { pSlave  = pIc; } else { pIc->Release (); } }
            else                 { if (pMaster == nullptr) { pMaster = pIc; } else { pIc->Release (); } }
        }
        IDmaPeripheral *pPeri = nullptr;
        D.pDevice->QueryInterface (IID_IDmaPeripheral, (VOID **) &pPeri);
        if (pPeri != nullptr) { DmaPeris.push_back (pPeri); }
        if (pDmaCtl == nullptr) { D.pDevice->QueryInterface (IID_IDmaController, (VOID **) &pDmaCtl); }
        IMemoryDevice *pMem = nullptr;
        D.pDevice->QueryInterface (IID_IMemoryDevice, (VOID **) &pMem);
        if (pMem != nullptr) {
            UINT32 Base = pMem->GetBase (), Size = pMem->GetSize ();
            std::printf ("     0x%08x..0x%08x  %s  %s\n", Base, Base + Size - 1,
                         pMem->IsReadOnly () ? "RO" : "RW", D.pDevice->GetName ());
            if ((UINT64) Base + Size > MemEnd) { MemEnd = (UINT64) Base + Size; }
            Mapped.push_back (std::make_pair (Base, Size));
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
    // The DMA bridge runs before the interrupt arbiter, so a transfer completes (and the peripheral
    // arms its IRQ) within the same poll the arbiter then sees.
    DmaBridge Bridge (Ram.data (), Ram.size (), std::move (DmaPeris), pDmaCtl);
    Machine.AddDevice (&Bridge);
    InterruptArbiter Arbiter (std::move (Sources), pMaster, pSlave);
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

    // "No card, no memory": every hole between backed regions, up to the top of the address space,
    // is open bus -- it reads 0xFF and discards writes. Walk the mapped regions in address order
    // and mark each gap.
    std::sort (Mapped.begin (), Mapped.end ());
    UINT64 Cursor = 0;
    for (std::pair<UINT32, UINT32> CONST &Rgn : Mapped) {
        if (Rgn.first > Cursor) { Machine.MarkOpenBus ((UINT32) Cursor, (UINT32) (Rgn.first - Cursor)); }
        UINT64 RegionEnd = (UINT64) Rgn.first + Rgn.second;
        if (RegionEnd > Cursor) { Cursor = RegionEnd; }
    }
    if (Cursor < MemEnd) { Machine.MarkOpenBus ((UINT32) Cursor, (UINT32) (MemEnd - Cursor)); }

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
    } else if (Demo == 2) {
        // Keyboard IRQ1 demo: a scan code seeded into the 8042 (its "libcpu,keystroke" property) is
        // delivered as IRQ1. The PIC is programmed with IRQ1 unmasked, an ISR at INT 9 (8 + IRQ1)
        // reads the scan code from 0x60, prints a marker, and acknowledges the PIC -- so the full
        // path 8042 -> arbiter -> PIC -> INT 9 -> ISR runs, then the machine idles.
        UINT8 const Isr[] = {
            0xE4, 0x60,              // in al, 0x60        read the scan code from the KBC
            0xA2, 0x52, 0x00,        // mov [0x0052], al   stash it in low RAM for the host to report
            0xBA, 0xF8, 0x03,        // mov dx, 0x3F8
            0xB0, 0x2A, 0xEE,        // mov al,'*' ; out dx,al    visible "a key arrived" marker
            0xB0, 0x20, 0xE6, 0x20,  // mov al,0x20 ; out 0x20,al  end-of-interrupt to the PIC
            0xCF                     // iret
        };
        std::memcpy (Ram.data () + 0x0500, Isr, sizeof (Isr));
        Machine.SetIvt (0x09, 0x0000, 0x0500);              // IRQ1 -> INT 9

        std::vector<UINT8> Prog = {
            0x31, 0xC0, 0x8E, 0xD8,  // xor ax,ax ; mov ds,ax      DS = 0 (so the ISR's store lands at 0x52)
            0xB0, 0x13, 0xE6, 0x20,  // mov al,0x13 ; out 0x20,al   ICW1
            0xB0, 0x08, 0xE6, 0x21,  // mov al,0x08 ; out 0x21,al   ICW2 (vector base 8 -> IRQ1 = INT 9)
            0xB0, 0x01, 0xE6, 0x21,  // mov al,0x01 ; out 0x21,al   ICW4
            0xB0, 0xFD, 0xE6, 0x21,  // mov al,0xFD ; out 0x21,al   IMR: unmask IRQ1 only
            0xBA, 0xF8, 0x03         // mov dx, 0x3F8
        };
        CHAR8 CONST *pMsg = "press a key... ";
        for (CHAR8 CONST *p = pMsg; *p != '\0'; ++p) {
            Prog.push_back (0xB0); Prog.push_back ((UINT8) *p); Prog.push_back (0xEE);
        }
        UINT8 const Idle[] = { 0xFB, 0xF4, 0xEB, 0xFD };    // sti ; hlt ; jmp $-1
        Prog.insert (Prog.end (), Idle, Idle + sizeof (Idle));
        Entry = 0x0600;
        std::memcpy (Ram.data () + Entry, Prog.data (), Prog.size ());
        End = Entry + (CPU_ADDR) Prog.size ();
    } else if (Demo == 3) {
        // RTC/CMOS read demo: select each clock register through 0x70 and read it from 0x71,
        // stashing the BCD bytes in low RAM for the host to print as a date/time.
        std::vector<UINT8> Prog = { 0x31, 0xC0, 0x8E, 0xD8 };       // xor ax,ax ; mov ds,ax  (DS = 0)
        auto ReadCmos = [&] (UINT8 Reg, UINT8 Off) {
            Prog.push_back (0xB0); Prog.push_back (Reg);            // mov al, Reg
            Prog.push_back (0xE6); Prog.push_back (0x70);          // out 0x70, al   select register
            Prog.push_back (0xE4); Prog.push_back (0x71);          // in  al, 0x71   read it
            Prog.push_back (0xA2); Prog.push_back (Off); Prog.push_back (0x00);   // mov [Off], al
        };
        ReadCmos (0x04, 0x60);  ReadCmos (0x02, 0x61);  ReadCmos (0x00, 0x62);    // hour, minute, second
        ReadCmos (0x09, 0x63);  ReadCmos (0x08, 0x64);  ReadCmos (0x07, 0x65);    // year, month, day
        Prog.push_back (0xF4);                                     // hlt
        Entry = 0x0600;
        std::memcpy (Ram.data () + Entry, Prog.data (), Prog.size ());
        End = Entry + (CPU_ADDR) Prog.size ();
    } else if (Demo == 5) {
        // 8254 counter-latch read-back: program channel 0 (mode 2, LSB/MSB) with reload 0x1234,
        // issue the counter-latch command, and read the latched count LSB-then-MSB into low RAM.
        std::vector<UINT8> Prog = {
            0x31, 0xC0, 0x8E, 0xD8,                          // xor ax,ax ; mov ds,ax
            0xB0, 0x34, 0xE6, 0x43,                          // mov al,0x34 ; out 0x43,al   ch0, LSB/MSB, mode 2
            0xB0, 0x34, 0xE6, 0x40,                          // mov al,0x34 ; out 0x40,al   reload LSB
            0xB0, 0x12, 0xE6, 0x40,                          // mov al,0x12 ; out 0x40,al   reload MSB -> 0x1234
            0xB0, 0x00, 0xE6, 0x43,                          // mov al,0x00 ; out 0x43,al   counter-latch ch0
            0xE4, 0x40, 0xA2, 0x70, 0x00,                    // in al,0x40 ; mov [0x70],al  latched LSB
            0xE4, 0x40, 0xA2, 0x71, 0x00,                    // in al,0x40 ; mov [0x71],al  latched MSB
            0xF4                                             // hlt
        };
        Entry = 0x0600;
        std::memcpy (Ram.data () + Entry, Prog.data (), Prog.size ());
        End = Entry + (CPU_ADDR) Prog.size ();
    } else if (Demo == 6) {
        // DMA-driven floppy read: program DMA channel 2 to land a sector at 0x2000, issue the FDC
        // Read Data command, and idle. The DMA bridge moves the sector into memory and the FDC
        // raises IRQ6; the INT 0x0E ISR prints '#' and sends EOI. The sector text ends up at 0x2000.
        UINT8 const Isr[] = {
            0xB0, 0x23, 0xBA, 0xF8, 0x03, 0xEE,   // mov al,'#' ; mov dx,0x3F8 ; out dx,al
            0xB0, 0x20, 0xE6, 0x20,               // mov al,0x20 ; out 0x20,al   EOI to the master PIC
            0xCF                                  // iret
        };
        std::memcpy (Ram.data () + 0x0500, Isr, sizeof (Isr));
        Machine.SetIvt (0x0E, 0x0000, 0x0500);              // IRQ6 -> INT 0x0E (8 + 6)

        std::vector<UINT8> Prog = { 0x31, 0xC0, 0x8E, 0xD8 };   // xor ax,ax ; mov ds,ax
        auto Out = [&] (UINT8 Port, UINT8 Val) {            // out imm8 (ports < 0x100)
            Prog.push_back (0xB0); Prog.push_back (Val); Prog.push_back (0xE6); Prog.push_back (Port);
        };
        Out (0x20, 0x13); Out (0x21, 0x08); Out (0x21, 0x01); Out (0x21, 0xBF);   // PIC: init, unmask IRQ6
        Out (0x0C, 0x00);                                   // clear the DMA byte-pointer flip-flop
        Out (0x0B, 0x46);                                   // ch2 mode: single, write-to-memory
        Out (0x04, 0x00); Out (0x04, 0x20);                 // ch2 base address = 0x2000
        Out (0x05, 0xFF); Out (0x05, 0x01);                 // ch2 count = 0x01FF (512 bytes)
        Out (0x0A, 0x02);                                   // unmask DMA channel 2
        Prog.push_back (0xBA); Prog.push_back (0xF5); Prog.push_back (0x03);   // mov dx, 0x3F5 (FDC FIFO)
        UINT8 const Cmd[] = { 0xE6, 0x00, 0x00, 0x00, 0x01, 0x02, 0x01, 0x1B, 0xFF };   // Read Data C0 H0 R1 N2
        for (UINT8 B : Cmd) { Prog.push_back (0xB0); Prog.push_back (B); Prog.push_back (0xEE); }   // mov al,B ; out dx,al
        UINT8 const Idle[] = { 0xFB, 0xF4, 0xEB, 0xFD };    // sti ; hlt ; jmp $-1
        Prog.insert (Prog.end (), Idle, Idle + sizeof (Idle));
        Entry = 0x0600;
        std::memcpy (Ram.data () + Entry, Prog.data (), Prog.size ());
        End = Entry + (CPU_ADDR) Prog.size ();
    } else if (Demo == 4) {
        // RTC IRQ8 through the slave 8259. The RTC's periodic interrupt is wired to IRQ8, which the
        // slave PIC vectors (ICW2 base 0x70 -> INT 0x70) and presents to the master on IRQ2; the
        // ISR reads Status C to acknowledge the RTC and sends EOI to both PICs.
        UINT8 const Isr[] = {
            0xB0, 0x0C, 0xE6, 0x70,  // mov al,0x0C ; out 0x70,al   select Status C
            0xE4, 0x71,              // in al, 0x71                 read it (clears the RTC's flags)
            0xB0, 0x21, 0xBA, 0xF8, 0x03, 0xEE,   // mov al,'!' ; mov dx,0x3F8 ; out dx,al
            0xB0, 0x20, 0xE6, 0xA0,  // mov al,0x20 ; out 0xA0,al   EOI to the slave
            0xB0, 0x20, 0xE6, 0x20,  // mov al,0x20 ; out 0x20,al   EOI to the master
            0xCF                     // iret
        };
        std::memcpy (Ram.data () + 0x0500, Isr, sizeof (Isr));
        Machine.SetIvt (0x70, 0x0000, 0x0500);              // slave IRQ8 -> INT 0x70

        std::vector<UINT8> Prog = {
            0x31, 0xC0, 0x8E, 0xD8,                          // xor ax,ax ; mov ds,ax
            0xB0, 0x11, 0xE6, 0x20,  0xB0, 0x08, 0xE6, 0x21, // master: ICW1 ; ICW2 (base 8)
            0xB0, 0x04, 0xE6, 0x21,  0xB0, 0x01, 0xE6, 0x21, // master: ICW3 (slave on IRQ2) ; ICW4
            0xB0, 0xFB, 0xE6, 0x21,                          // master IMR: unmask IRQ2 (cascade)
            0xB0, 0x11, 0xE6, 0xA0,  0xB0, 0x70, 0xE6, 0xA1, // slave:  ICW1 ; ICW2 (base 0x70)
            0xB0, 0x02, 0xE6, 0xA1,  0xB0, 0x01, 0xE6, 0xA1, // slave:  ICW3 (id 2) ; ICW4
            0xB0, 0xFE, 0xE6, 0xA1,                          // slave IMR: unmask IRQ8 (slave line 0)
            0xBA, 0xF8, 0x03                                 // mov dx, 0x3F8
        };
        CHAR8 CONST *pMsg = "RTC IRQ8: ";
        for (CHAR8 CONST *p = pMsg; *p != '\0'; ++p) {
            Prog.push_back (0xB0); Prog.push_back ((UINT8) *p); Prog.push_back (0xEE);
        }
        UINT8 const Arm[] = {
            0xB0, 0x0B, 0xE6, 0x70,  0xB0, 0x42, 0xE6, 0x71, // select Status B ; write PIE|24h (enable periodic)
            0xFB, 0xF4, 0xEB, 0xFD                            // sti ; hlt ; jmp $-1
        };
        Prog.insert (Prog.end (), Arm, Arm + sizeof (Arm));
        Entry = 0x0600;
        std::memcpy (Ram.data () + Entry, Prog.data (), Prog.size ());
        End = Entry + (CPU_ADDR) Prog.size ();
    } else if (Demo == 1) {
        // Bank-switch + open-bus demo. Writes two Hercules display pages, probes an unmapped
        // address (which reads back as open bus), then flips the displayed page via the mode
        // register -- so the rendered screen is the bank that was switched in.
        std::vector<UINT8> Prog;
        auto Emit = [&] (CHAR8 CONST *p) {                  // print a string to the UART (DX=0x3F8)
            for (; *p != '\0'; ++p) { Prog.push_back (0xB0); Prog.push_back ((UINT8) *p); Prog.push_back (0xEE); }
        };
        auto SetDs = [&] (UINT16 Seg) {                     // mov bx,Seg ; mov ds,bx (via BX, so AL survives)
            Prog.push_back (0xBB); Prog.push_back ((UINT8) (Seg & 0xFF)); Prog.push_back ((UINT8) (Seg >> 8));
            Prog.push_back (0x8E); Prog.push_back (0xDB);
        };
        auto Screen = [&] (CHAR8 CONST *p) {                // write a char+attr string at DS:0
            for (UINT16 Off = 0; *p != '\0'; ++p, Off = (UINT16) (Off + 2)) {
                Prog.push_back (0xB8); Prog.push_back ((UINT8) *p); Prog.push_back (0x07);   // mov ax,0x07<<8|ch
                Prog.push_back (0x89); Prog.push_back (0x06);                                 // mov [disp16],ax
                Prog.push_back ((UINT8) (Off & 0xFF)); Prog.push_back ((UINT8) (Off >> 8));
            }
        };

        Prog.push_back (0xBA); Prog.push_back (0xF8); Prog.push_back (0x03);   // mov dx, 0x3F8
        Emit ("HERCULES bank-switch + open-bus demo\n");

        // Probe an unmapped address (0xA0000): read it, stash the byte in low RAM for reporting.
        SetDs (0xA000);
        Prog.push_back (0xA0); Prog.push_back (0x00); Prog.push_back (0x00);   // mov al, [0x0000]  (ds:0 = 0xA0000)
        SetDs (0x0000);
        Prog.push_back (0xA2); Prog.push_back (0x50); Prog.push_back (0x00);   // mov [0x0050], al  (ds:0x50 = phys 0x50)

        // Write both Hercules pages: page 0 (initially shown) and page 1 (the bank to switch in).
        SetDs (0xB000); Screen ("PAGE 0 -- hidden after the flip");
        SetDs (0xB800); Screen ("PAGE 1 -- bank-switched in by mode bit7");

        // Enable and select page 1: config 0x3BF bit0|bit1, then mode 0x3B8 bit7.
        UINT8 const Flip[] = {
            0xBA, 0xBF, 0x03, 0xB0, 0x03, 0xEE,   // mov dx,0x3BF ; mov al,0x03 ; out dx,al  (allow gfx+page1)
            0xBA, 0xB8, 0x03, 0xB0, 0x80, 0xEE,   // mov dx,0x3B8 ; mov al,0x80 ; out dx,al  (display page1)
            0xF4, 0xEB, 0xFD                       // hlt ; jmp $-1
        };
        Prog.insert (Prog.end (), Flip, Flip + sizeof (Flip));

        Entry = 0x0600;
        std::memcpy (Ram.data () + Entry, Prog.data (), Prog.size ());
        End = Entry + (CPU_ADDR) Prog.size ();
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

    if (Demo == 1) {
        std::printf ("   open-bus probe: guest read of unmapped 0xA0000 -> 0x%02X (%s)\n",
                     Ram[0x0050], Ram[0x0050] == 0xFF ? "open bus: no card, no memory" : "backed");
    }
    if (Demo == 2) {
        std::printf ("   keyboard IRQ1: %llu interrupt(s) delivered; ISR read scan code 0x%02X from the 8042\n",
                     (unsigned long long) R.Interrupts, Ram[0x0052]);
    }
    if (Demo == 3) {
        std::printf ("   RTC/CMOS: %02X:%02X:%02X  20%02X-%02X-%02X (BCD, read from the MC146818)\n",
                     Ram[0x0060], Ram[0x0061], Ram[0x0062], Ram[0x0063], Ram[0x0064], Ram[0x0065]);
    }
    if (Demo == 4) {
        std::printf ("   RTC IRQ8: %llu interrupt(s) via the slave 8259 (INT 0x70)\n",
                     (unsigned long long) R.Interrupts);
    }
    if (Demo == 5) {
        std::printf ("   PIT ch0 latched count = 0x%02X%02X (8254 counter-latch read-back)\n",
                     Ram[0x0071], Ram[0x0070]);
    }
    if (Demo == 6) {
        CHAR8 Text[48];
        for (int I = 0; I < 47; I++) {
            UINT8 Ch = Ram[0x2000 + I];
            Text[I] = (Ch >= 0x20 && Ch < 0x7F) ? (CHAR8) Ch : ' ';
        }
        Text[47] = '\0';
        std::printf ("   floppy DMA: %llu IRQ6; sector at 0x2000 = \"%s\"\n",
                     (unsigned long long) R.Interrupts, Text);
    }

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
