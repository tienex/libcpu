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
#include "LibCPU/IConsole.h"
#include "../core/System.h"
#include "../core/X86System.h"
#include "../core/MachineBuilder.h"
#include "../core/DeviceTree.h"
#include "../console/ConsoleBridge.h"
#include "../console/TerminalConsole.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
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
        m_pDev->QueryInterface (IID_IClockSink, (VOID **) &m_pClock);
    }
    ~ComDeviceAdapter () override
    {
        if (m_pClock != nullptr) { m_pClock->Release (); }
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
    void Clock (UINT64 Cycles) override
    {
        if (m_pClock != nullptr) { m_pClock->OnClock (Cycles); }
    }

private:
    IDevice     *m_pDev;
    IPortDevice *m_pPort  = nullptr;
    IClockSink  *m_pClock = nullptr;
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
                if (m_pMaster != nullptr && !m_pMaster->CanAccept (2)) { continue; }   // cascade gated by the master
                if (m_pSlave->AcceptInterrupt (Irq - 8, &Vector) == S_OK) {
                    if (m_pMaster != nullptr) {                     // the cascade goes in service on the master too
                        UINT32 Casc = 0;
                        m_pMaster->AcceptInterrupt (2, &Casc);
                    }
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
                m_pDma->SetTerminalCount (Channel);         // latch TC so the BIOS sees the op completed
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

// Build an interactive "typewriter" guest at 0x0600. An IRQ1 handler reads the keyboard scan
// code, translates it through a seeded XT-set-1 -> ASCII table, and writes the glyph to the text
// framebuffer (segment FbSeg), advancing a cursor word; main installs the vector, unmasks IRQ1 on
// the 8259, prints a "TYPE: " prompt, then idles with STI/HLT waiting for keystrokes. Returns the
// entry point. This is the guest that demonstrates the console seam end to end.
static inline CPU_ADDR
BuildTypewriter (std::vector<UINT8> &Ram, UINT16 FbSeg)
{
    enum { Table = 0x0500, Cursor = 0x04FE, Main = 0x0600, IsrAddr = 0x0700 };
    auto Lo = [] (UINT32 V) { return (UINT8) (V & 0xFF); };
    auto Hi = [] (UINT32 V) { return (UINT8) ((V >> 8) & 0xFF); };

    // Seed scancode -> ASCII for the printable XT set-1 keys the console forwards.
    static CHAR8 CONST *Row1 = "1234567890";          // 0x02..0x0B
    static CHAR8 CONST *Row2 = "qwertyuiop";           // 0x10..0x19
    static CHAR8 CONST *Row3 = "asdfghjkl";            // 0x1E..0x26
    static CHAR8 CONST *Row4 = "zxcvbnm";              // 0x2C..0x32
    for (int I = 0; Row1[I]; I++) { Ram[Table + 0x02 + I] = (UINT8) Row1[I]; }
    for (int I = 0; Row2[I]; I++) { Ram[Table + 0x10 + I] = (UINT8) Row2[I]; }
    for (int I = 0; Row3[I]; I++) { Ram[Table + 0x1E + I] = (UINT8) Row3[I]; }
    for (int I = 0; Row4[I]; I++) { Ram[Table + 0x2C + I] = (UINT8) Row4[I]; }
    Ram[Table + 0x39] = ' ';                           // space

    // --- ISR at 0x0700: translate one make code to a glyph and place it on screen ---
    std::vector<UINT8> Isr;
    auto E = [&] (std::initializer_list<UINT8> B) { for (UINT8 X : B) { Isr.push_back (X); } };
    E ({ 0x50, 0x53, 0x57 });                          // push ax, bx, di
    E ({ 0xE4, 0x60 });                                // in al, 0x60
    E ({ 0xA8, 0x80 });                                // test al, 0x80   (break code?)
    E ({ 0x75, 0x00 }); size_t Jbreak = Isr.size () - 1;   // jnz eoi
    E ({ 0x8A, 0xD8, 0x32, 0xFF });                    // mov bl,al ; xor bh,bh
    E ({ 0x8A, 0x87, Lo (Table), Hi (Table) });        // mov al, [bx+Table]
    E ({ 0x3C, 0x00 });                                // cmp al, 0
    E ({ 0x74, 0x00 }); size_t Jnull = Isr.size () - 1;    // jz eoi (unmapped)
    E ({ 0xB4, 0x07 });                                // mov ah, 0x07 (attribute)
    E ({ 0x8B, 0x3E, Lo (Cursor), Hi (Cursor) });      // mov di, [Cursor]
    E ({ 0x26, 0x89, 0x05 });                          // mov es:[di], ax
    E ({ 0x83, 0x06, Lo (Cursor), Hi (Cursor), 0x02 });// add word [Cursor], 2
    size_t Eoi = Isr.size ();
    E ({ 0xB0, 0x20, 0xE6, 0x20 });                    // mov al,0x20 ; out 0x20,al  (EOI)
    E ({ 0x5F, 0x5B, 0x58, 0xCF });                    // pop di, bx, ax ; iret
    Isr[Jbreak] = (UINT8) (Eoi - (Jbreak + 1));        // patch the two short forward jumps
    Isr[Jnull]  = (UINT8) (Eoi - (Jnull + 1));
    std::memcpy (Ram.data () + IsrAddr, Isr.data (), Isr.size ());

    // --- main at 0x0600 ---
    std::vector<UINT8> M;
    auto A = [&] (std::initializer_list<UINT8> B) { for (UINT8 X : B) { M.push_back (X); } };
    A ({ 0xFA });                                      // cli
    A ({ 0xB8, Lo (FbSeg), Hi (FbSeg), 0x8E, 0xC0 });  // mov ax, FbSeg ; mov es, ax
    A ({ 0x31, 0xC0, 0xA3, Lo (Cursor), Hi (Cursor) });// xor ax,ax ; mov [Cursor], ax
    A ({ 0xB8, Lo (IsrAddr), Hi (IsrAddr), 0xA3, 0x24, 0x00 });   // mov ax,Isr ; mov [0x0024],ax
    A ({ 0x31, 0xC0, 0xA3, 0x26, 0x00 });              // xor ax,ax ; mov [0x0026],ax  (IVT[9] seg 0)
    A ({ 0xB0, 0x13, 0xE6, 0x20, 0xB0, 0x08, 0xE6, 0x21, 0xB0, 0x01, 0xE6, 0x21, 0xB0, 0xFD, 0xE6, 0x21 });
    //   8259: ICW1=0x13, ICW2=0x08, ICW4=0x01, OCW1 mask=0xFD (IRQ1 enabled)
    CHAR8 CONST *pPrompt = "TYPE: ";
    UINT16 Off = 0;
    for (CHAR8 CONST *p = pPrompt; *p; ++p, Off = (UINT16) (Off + 2)) {
        A ({ 0xB8, (UINT8) *p, 0x07, 0x26, 0xA3, Lo (Off), Hi (Off) });   // mov ax,0x07<<8|ch ; mov es:[off],ax
    }
    A ({ 0xB8, Lo (Off), Hi (Off), 0xA3, Lo (Cursor), Hi (Cursor) });    // cursor = after prompt
    A ({ 0xFB });                                      // sti
    A ({ 0xF4 });                                      // hlt
    A ({ 0xEB, 0xFD });                                // jmp $-1 (back to hlt)
    std::memcpy (Ram.data () + Main, M.data (), M.size ());
    return Main;
}

// Build a minimal BIOS-style option-ROM bootstrap at 0x0600: walk the upper-memory area in 2 KiB
// steps looking for the 0x55 0xAA option-ROM signature, and far-CALL each found ROM's init entry
// (offset 3) -- exactly what a PC BIOS does during POST. Registers are saved around each call
// (PUSHA/POPA). Returns the entry point. The far pointer is staged at 0x0500.
static inline CPU_ADDR
BuildBiosBootstrap (std::vector<UINT8> &Ram)
{
    enum { Ptr = 0x0500, Main = 0x0600 };
    auto Lo = [] (UINT32 V) { return (UINT8) (V & 0xFF); };
    auto Hi = [] (UINT32 V) { return (UINT8) ((V >> 8) & 0xFF); };

    std::vector<UINT8> M;
    auto A = [&] (std::initializer_list<UINT8> B) { for (UINT8 X : B) { M.push_back (X); } };
    A ({ 0xFA, 0x31, 0xC0, 0x8E, 0xD8, 0xFB });        // cli ; xor ax,ax ; mov ds,ax ; sti
    A ({ 0xBB, 0x00, 0xC0 });                          // mov bx, 0xC000  (first option-ROM segment)
    size_t Scan = M.size ();
    A ({ 0x8E, 0xC3 });                                // mov es, bx
    A ({ 0x26, 0x81, 0x3E, 0x00, 0x00, 0x55, 0xAA });  // cmp word es:[0], 0xAA55
    A ({ 0x75, 0x00 }); size_t Jne = M.size () - 1;    // jne .next  (patched)
    A ({ 0xC7, 0x06, Lo (Ptr), Hi (Ptr), 0x03, 0x00 });// mov word [Ptr], 3      (ROM init offset)
    A ({ 0x89, 0x1E, Lo (Ptr + 2), Hi (Ptr + 2) });    // mov [Ptr+2], bx        (ROM segment)
    A ({ 0x60 });                                      // pusha
    A ({ 0xFF, 0x1E, Lo (Ptr), Hi (Ptr) });            // call far [Ptr]  -> ROM init (RETFs back)
    A ({ 0x61 });                                      // popa
    size_t Next = M.size ();
    A ({ 0x81, 0xC3, 0x80, 0x00 });                    // add bx, 0x0080  (advance 2 KiB)
    A ({ 0x81, 0xFB, 0x00, 0xF0 });                    // cmp bx, 0xF000
    A ({ 0x72, 0x00 }); size_t Jb = M.size () - 1;     // jb .scan   (patched, backward)
    A ({ 0xF4 });                                      // hlt
    M[Jne] = (UINT8) (Next - (Jne + 1));               // forward to .next
    M[Jb]  = (UINT8) (INT8) ((int) Scan - (int) (Jb + 1));   // backward to .scan
    std::memcpy (Ram.data () + Main, M.data (), M.size ());
    return Main;
}

static inline int
RunMachineDemo (MachineBuilder &Builder, ICpuBackend *pBackend, CHAR8 CONST *pImagePath, UINT32 LoadAddr,
                int Demo,                                    // 0 none, 1 bank-switch, 2 keyboard IRQ1, ...
                std::vector<std::pair<std::string, std::string>> CONST &CliRoms,
                bool BiosBoot = false,                       // load the image at the top of memory, boot the reset vector
                UINT64 BiosSteps = 4000,                     // bounded execution-burst budget for the BIOS attempt
                bool ConsoleMode = false,                    // run interactively through the console seam
                std::string CONST &ConsoleKeys = std::string (),  // headless: pre-injected keystrokes (else live TTY)
                bool BootScan = false,                       // run the option-ROM bootstrap (scan UMA + far-call inits)
                ICpuBackend *pHotBackend = nullptr)          // optional tier-1 JIT for hot regions (tiered execution)
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
    InstallX86System (Machine);                              // CPU personality: x86 privileged ops
    if (pHotBackend != nullptr) { Machine.SetHotBackend (pHotBackend, 50); }   // tiered: JIT regions run >=50x

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

    // Map device-owned memory over its region: a display card's video RAM (read/write, kept in
    // sync both ways) or a card's option-ROM firmware (read-only, loaded in and never written
    // back). So the framebuffer is truly the card's memory, and a controller's firmware appears in
    // the address space exactly where its ROM is mapped.
    for (MATCHED_DEVICE CONST &D : Builder.Devices ()) {
        IMemoryDevice *pMem  = nullptr;
        IHostMemory   *pHost = nullptr;
        D.pDevice->QueryInterface (IID_IMemoryDevice, (VOID **) &pMem);
        D.pDevice->QueryInterface (IID_IHostMemory, (VOID **) &pHost);
        if (pMem != nullptr && pHost != nullptr) {
            bool Ro = pMem->IsReadOnly () ? true : false;
            Machine.MapDeviceMemory (pMem->GetBase (), pMem->GetSize (), pHost->GetHostBuffer (), Ro);
            if (Ro) { std::printf ("   firmware mapped: %u bytes at 0x%05x (%s)\n",
                                   pMem->GetSize (), pMem->GetBase (), D.pDevice->GetName ()); }
        }
        if (pMem  != nullptr) { pMem->Release (); }
        if (pHost != nullptr) { pHost->Release (); }
    }

    // Command-line firmware: "--rom <device>=<path>" loads an option ROM for a named controller
    // into a free, 2 KiB-aligned slot of the option-ROM area (0xC8000 up), auto-assigning the
    // address around everything already mapped. The image is read-only, like any other option ROM.
    std::vector<std::vector<UINT8>> RomStore;
    RomStore.reserve (CliRoms.size ());
    UINT32 RomCursor = 0xC8000;
    for (std::pair<std::string, std::string> CONST &R : CliRoms) {
        std::FILE *pF = std::fopen (R.second.c_str (), "rb");
        if (pF == nullptr) { std::printf ("   --rom: cannot open '%s'\n", R.second.c_str ()); continue; }
        std::fseek (pF, 0, SEEK_END);
        long FileLen = std::ftell (pF);
        std::fseek (pF, 0, SEEK_SET);
        if (FileLen <= 0) { std::fclose (pF); continue; }
        std::vector<UINT8> Bytes ((size_t) FileLen);
        if (std::fread (Bytes.data (), 1, (size_t) FileLen, pF) != (size_t) FileLen) { std::fclose (pF); continue; }
        std::fclose (pF);

        // The option ROM occupies its declared length (header block count), else the whole file.
        UINT32 Size = (UINT32) Bytes.size ();
        if (Bytes.size () >= 3 && Bytes[0] == 0x55 && Bytes[1] == 0xAA) {
            UINT32 Decl = (UINT32) Bytes[2] * 512;
            if (Decl > 0 && Decl <= Bytes.size ()) { Size = Decl; }
        }
        // Find the lowest free 2 KiB-aligned address that doesn't overlap a mapped region.
        for (;;) {
            bool Clear = (UINT64) RomCursor + Size <= MemEnd;
            for (std::pair<UINT32, UINT32> CONST &M : Mapped) {
                if (RomCursor < M.first + M.second && M.first < RomCursor + Size) { Clear = false; break; }
            }
            if (Clear) { break; }
            RomCursor += 0x800;
            if ((UINT64) RomCursor + Size > MemEnd) { break; }
        }
        // Identify the target device by bundle or node name; if it is an option-ROM host with a
        // conventional address that's still free, place the ROM there instead of auto-assigning.
        std::string Dev = "?";
        bool Conventional = false;
        for (MATCHED_DEVICE CONST &D : Builder.Devices ()) {
            if (R.first.empty () || (D.BundleName.find (R.first) == std::string::npos &&
                                     D.NodeName.find (R.first) == std::string::npos)) { continue; }
            Dev = D.NodeName;
            IOptionRomHost *pHost = nullptr;
            D.pDevice->QueryInterface (IID_IOptionRomHost, (VOID **) &pHost);
            if (pHost != nullptr) {
                UINT32 Want = pHost->GetRomAddress ();
                pHost->Release ();
                bool Free = (Want != 0) && ((UINT64) Want + Size <= MemEnd);
                for (std::pair<UINT32, UINT32> CONST &M : Mapped) {
                    if (Want < M.first + M.second && M.first < Want + Size) { Free = false; break; }
                }
                if (Free) { RomCursor = Want; Conventional = true; }   // honor the card's conventional address
            }
            break;
        }
        RomStore.push_back (std::move (Bytes));
        Machine.MapDeviceMemory (RomCursor, Size, RomStore.back ().data (), true);
        Mapped.push_back (std::make_pair (RomCursor, Size));
        std::printf ("   --rom %s: loaded %u bytes for %s at 0x%05x (%s)\n",
                     R.first.empty () ? "(rom)" : R.first.c_str (), Size, Dev.c_str (), RomCursor,
                     Conventional ? "conventional" : "auto-assigned");
        RomCursor = (UINT32) ((RomCursor + Size + 0x7FF) & ~UINT32_C (0x7FF));   // next 2 KiB slot
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
    if (BootScan) {
        // Minimal BIOS POST: scan the UMA for option ROMs and far-call each one's init entry.
        Entry = BuildBiosBootstrap (Ram);
        End   = 0x0700;
    } else if (ConsoleMode) {
        // Interactive console: an IRQ1-driven typewriter that echoes typed keys onto the display.
        // Use whatever text framebuffer segment the fitted display card reports.
        UINT16 FbSeg = 0xB800;
        for (MATCHED_DEVICE CONST &D : Builder.Devices ()) {
            IDisplayDevice *pDisp = nullptr;
            D.pDevice->QueryInterface (IID_IDisplayDevice, (VOID **) &pDisp);
            if (pDisp != nullptr) { FbSeg = (UINT16) (pDisp->GetFramebufferBase () >> 4); pDisp->Release (); break; }
        }
        Entry = BuildTypewriter (Ram, FbSeg);
        End   = 0x0800;
    } else if (pImagePath != nullptr) {
        // Load and run a supplied image (e.g. a BIOS dump) at LoadAddr.
        std::FILE *pF = std::fopen (pImagePath, "rb");
        if (pF == nullptr) { std::printf ("   cannot open image '%s'\n", pImagePath); pArch->Release (); return 2; }
        std::fseek (pF, 0, SEEK_END);
        long Size = std::ftell (pF);
        std::fseek (pF, 0, SEEK_SET);
        if (Size <= 0 || (UINT64) Size > Ram.size ()) {
            std::fclose (pF); std::printf ("   image too large\n"); pArch->Release (); return 2;
        }
        // A BIOS maps at the top of memory so its reset vector lands at 0xFFFF0; otherwise load at
        // the given address.
        UINT32 La = BiosBoot ? (UINT32) (Ram.size () - (UINT64) Size) : LoadAddr;
        if ((UINT64) La + (UINT64) Size > Ram.size ()) {
            std::fclose (pF); std::printf ("   image does not fit at 0x%x\n", La); pArch->Release (); return 2;
        }
        if (std::fread (Ram.data () + La, 1, (size_t) Size, pF) != (size_t) Size) {
            std::fclose (pF); std::printf ("   short read of image\n"); pArch->Release (); return 2;
        }
        std::fclose (pF);
        std::printf ("   loaded image '%s' (%ld bytes) at 0x%05x%s\n", pImagePath, Size, La,
                     BiosBoot ? " (BIOS; booting reset vector 0xFFFF0)" : "");
        if (BiosBoot) { Machine.MarkCodeImmutable (La, (UINT64) Size); }   // firmware: cache its translations
        Entry = BiosBoot ? (CPU_ADDR) 0xFFFF0 : La;          // reset vector vs the load address
        End   = BiosBoot ? (CPU_ADDR) Ram.size () : (La + (CPU_ADDR) Size);
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
        X86SetIvt (Machine, 0x09, 0x0000, 0x0500);              // IRQ1 -> INT 9

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
    } else if (Demo == 10) {
        // Option-ROM firmware: the controller firmware is mapped read-only at its ROM address. The
        // guest reads the first two bytes (the 0x55 0xAA signature) at 0xC8000 through an ES segment
        // override, proving the firmware is present and CPU-readable in the address space.
        std::vector<UINT8> Prog = {
            0x31, 0xC0, 0x8E, 0xD8,                          // xor ax,ax ; mov ds,ax
            0xB8, 0x00, 0xC8, 0x8E, 0xC0,                    // mov ax,0xC800 ; mov es,ax
            0x26, 0x8A, 0x06, 0x00, 0x00, 0xA2, 0x00, 0x48,  // mov al,es:[0] ; mov [0x4800],al
            0x26, 0x8A, 0x06, 0x01, 0x00, 0xA2, 0x01, 0x48,  // mov al,es:[1] ; mov [0x4801],al
            0xF4                                             // hlt
        };
        Entry = 0x0600;
        std::memcpy (Ram.data () + Entry, Prog.data (), Prog.size ());
        End = Entry + (CPU_ADDR) Prog.size ();
    } else if (Demo == 9) {
        // XT-IDE PIO read: program the ATA task file for an LBA-0 read, issue READ SECTORS, then
        // read words from the data register -- low byte from 0x300 (which latches the high byte),
        // high byte from 0x308 -- into memory at 0x4000. No interrupts: pure programmed I/O.
        std::vector<UINT8> Prog = { 0x31, 0xC0, 0x8E, 0xD8 };   // xor ax,ax ; mov ds,ax
        auto OutDx = [&] (UINT16 Port, UINT8 Val) {            // mov dx,Port ; mov al,Val ; out dx,al
            Prog.push_back (0xBA); Prog.push_back ((UINT8) (Port & 0xFF)); Prog.push_back ((UINT8) (Port >> 8));
            Prog.push_back (0xB0); Prog.push_back (Val); Prog.push_back (0xEE);
        };
        OutDx (0x302, 0x01);                                // sector count = 1
        OutDx (0x303, 0x00); OutDx (0x304, 0x00); OutDx (0x305, 0x00);   // LBA = 0
        OutDx (0x306, 0xE0);                                // drive/head: LBA mode, drive 0
        OutDx (0x307, 0x20);                                // command: READ SECTORS

        for (UINT16 W = 0; W < 20; W++) {                   // read 20 words (40 bytes) of the sector
            UINT16 Dst = (UINT16) (0x4000 + W * 2);
            Prog.push_back (0xBA); Prog.push_back (0x00); Prog.push_back (0x03);   // mov dx,0x300
            Prog.push_back (0xEC);                                                  // in al,dx (low + latch high)
            Prog.push_back (0xA2); Prog.push_back ((UINT8) (Dst & 0xFF)); Prog.push_back ((UINT8) (Dst >> 8));
            Prog.push_back (0xBA); Prog.push_back (0x08); Prog.push_back (0x03);   // mov dx,0x308
            Prog.push_back (0xEC);                                                  // in al,dx (latched high)
            Prog.push_back (0xA2); Prog.push_back ((UINT8) ((Dst + 1) & 0xFF)); Prog.push_back ((UINT8) ((Dst + 1) >> 8));
        }
        Prog.push_back (0xF4);                              // hlt
        Entry = 0x0600;
        std::memcpy (Ram.data () + Entry, Prog.data (), Prog.size ());
        End = Entry + (CPU_ADDR) Prog.size ();
    } else if (Demo == 8) {
        // Priority PIC / in-service register: the IRQ0 handler reads the ISR through an OCW3 select
        // (out 0x0B to 0x20, then in from 0x20). With IRQ0 in service, bit 0 reads back set; the
        // handler stashes it and sends EOI so the next tick is delivered.
        UINT8 const Isr[] = {
            0xB0, 0x0B, 0xE6, 0x20,  // mov al,0x0B ; out 0x20,al   OCW3: select ISR for read-back
            0xE4, 0x20,              // in al, 0x20                 read the in-service register
            0xA2, 0x56, 0x00,        // mov [0x0056], al            stash it for the host
            0xB0, 0x20, 0xE6, 0x20,  // mov al,0x20 ; out 0x20,al   EOI
            0xCF                     // iret
        };
        std::memcpy (Ram.data () + 0x0500, Isr, sizeof (Isr));
        X86SetIvt (Machine, 0x08, 0x0000, 0x0500);              // IRQ0 -> INT 8

        std::vector<UINT8> Prog = {
            0x31, 0xC0, 0x8E, 0xD8,                          // xor ax,ax ; mov ds,ax
            0xB0, 0x13, 0xE6, 0x20,  0xB0, 0x08, 0xE6, 0x21, // ICW1 ; ICW2 (base 8)
            0xB0, 0x01, 0xE6, 0x21,  0xB0, 0xFE, 0xE6, 0x21, // ICW4 ; IMR: unmask IRQ0
            0xB0, 0x36, 0xE6, 0x43,  0xB0, 0xFF, 0xE6, 0x40, 0xB0, 0xFF, 0xE6, 0x40,   // PIT ch0 mode 3, count
            0xFB, 0xF4, 0xEB, 0xFD                           // sti ; hlt ; jmp $-1
        };
        Entry = 0x0600;
        std::memcpy (Ram.data () + Entry, Prog.data (), Prog.size ());
        End = Entry + (CPU_ADDR) Prog.size ();
    } else if (Demo == 7) {
        // DMA floppy WRITE round-trip: fill a buffer, DMA it out to the disk image via Write Data,
        // then DMA a Read Data of the same sector into a different buffer. If the bytes survive the
        // trip, the controller's write path works. IRQ6 stays masked (the PIC's reset state); STI
        // just lets the device bus -- and thus the DMA bridge -- run each window. A final HLT with
        // no deliverable interrupt ends the run.
        std::vector<UINT8> Prog = { 0x31, 0xC0, 0x8E, 0xD8, 0xFB };   // xor ax,ax ; mov ds,ax ; sti
        auto Out = [&] (UINT8 Port, UINT8 Val) {
            Prog.push_back (0xB0); Prog.push_back (Val); Prog.push_back (0xE6); Prog.push_back (Port);
        };
        auto Fdc = [&] (UINT8 const *p, size_t n) {         // DX must be 0x3F5
            for (size_t I = 0; I < n; I++) { Prog.push_back (0xB0); Prog.push_back (p[I]); Prog.push_back (0xEE); }
        };

        CHAR8 CONST *pPat = "DMA-WRITE-ROUNDTRIP-OK";        // pattern to write then read back
        for (UINT16 I = 0; pPat[I] != '\0'; I++) {
            Prog.push_back (0xB0); Prog.push_back ((UINT8) pPat[I]);              // mov al, ch
            Prog.push_back (0xA2); Prog.push_back ((UINT8) ((0x3000 + I) & 0xFF));
            Prog.push_back ((UINT8) ((0x3000 + I) >> 8));                         // mov [0x3000+I], al
        }

        Prog.push_back (0xBA); Prog.push_back (0xF5); Prog.push_back (0x03);      // mov dx, 0x3F5
        Out (0x0C, 0x00); Out (0x0B, 0x4A);                 // ch2 mode: single, read-from-memory
        Out (0x04, 0x00); Out (0x04, 0x30); Out (0x81, 0x00);   // address 0x3000, page 0
        Out (0x05, 0xFF); Out (0x05, 0x01); Out (0x0A, 0x02);   // count 0x1FF, unmask ch2
        UINT8 const Write[] = { 0x45, 0x00, 0x00, 0x00, 0x01, 0x02, 0x01, 0x1B, 0xFF };   // Write Data
        Fdc (Write, sizeof (Write));
        for (int I = 0; I < 7; I++) { Prog.push_back (0xEC); }   // drain the result bytes (in al,dx) -> ready

        Out (0x0C, 0x00); Out (0x0B, 0x46);                 // ch2 mode: single, write-to-memory
        Out (0x04, 0x00); Out (0x04, 0x50); Out (0x81, 0x00);   // address 0x5000, page 0
        Out (0x05, 0xFF); Out (0x05, 0x01); Out (0x0A, 0x02);
        Prog.push_back (0xBA); Prog.push_back (0xF5); Prog.push_back (0x03);      // mov dx, 0x3F5
        UINT8 const Read[] = { 0xE6, 0x00, 0x00, 0x00, 0x01, 0x02, 0x01, 0x1B, 0xFF };    // Read Data
        Fdc (Read, sizeof (Read));
        for (int I = 0; I < 7; I++) { Prog.push_back (0xEC); }   // let the read transfer + drain its result
        UINT8 const Idle[] = { 0xF4, 0xEB, 0xFD };          // hlt ; jmp $-1
        Prog.insert (Prog.end (), Idle, Idle + sizeof (Idle));

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
        X86SetIvt (Machine, 0x0E, 0x0000, 0x0500);              // IRQ6 -> INT 0x0E (8 + 6)

        std::vector<UINT8> Prog = { 0x31, 0xC0, 0x8E, 0xD8 };   // xor ax,ax ; mov ds,ax
        auto Out = [&] (UINT8 Port, UINT8 Val) {            // out imm8 (ports < 0x100)
            Prog.push_back (0xB0); Prog.push_back (Val); Prog.push_back (0xE6); Prog.push_back (Port);
        };
        Out (0x20, 0x13); Out (0x21, 0x08); Out (0x21, 0x01); Out (0x21, 0xBF);   // PIC: init, unmask IRQ6
        Out (0x0C, 0x00);                                   // clear the DMA byte-pointer flip-flop
        Out (0x0B, 0x46);                                   // ch2 mode: single, write-to-memory
        Out (0x04, 0x00); Out (0x04, 0x20);                 // ch2 base address = 0x2000
        Out (0x81, 0x01);                                   // ch2 page = 1 -> physical 0x12000 (>64 KiB)
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
    } else if (Demo == 12) {
        // DMA-driven hard-disk read: program DMA channel 3 to land sector 0 at physical 0x12000,
        // write the ST-506 six-byte READ control block to 0x320, and idle. The DMA bridge moves the
        // sector (read from the mounted medium) into memory and the controller raises IRQ5; the
        // INT 0x0D ISR prints '#' and sends EOI. The medium's sector-0 text ends up at 0x12000.
        UINT8 const Isr[] = {
            0xB0, 0x23, 0xBA, 0xF8, 0x03, 0xEE,   // mov al,'#' ; mov dx,0x3F8 ; out dx,al
            0xB0, 0x20, 0xE6, 0x20,               // mov al,0x20 ; out 0x20,al   EOI to the master PIC
            0xCF                                  // iret
        };
        std::memcpy (Ram.data () + 0x0500, Isr, sizeof (Isr));
        X86SetIvt (Machine, 0x0D, 0x0000, 0x0500);              // IRQ5 -> INT 0x0D (8 + 5)

        std::vector<UINT8> Prog = { 0x31, 0xC0, 0x8E, 0xD8 };   // xor ax,ax ; mov ds,ax
        auto Out = [&] (UINT8 Port, UINT8 Val) {
            Prog.push_back (0xB0); Prog.push_back (Val); Prog.push_back (0xE6); Prog.push_back (Port);
        };
        Out (0x20, 0x13); Out (0x21, 0x08); Out (0x21, 0x01); Out (0x21, 0xDF);   // PIC: init, unmask IRQ5
        Out (0x0C, 0x00);                                   // clear the DMA byte-pointer flip-flop
        Out (0x0B, 0x47);                                   // ch3 mode: single, write-to-memory
        Out (0x06, 0x00); Out (0x06, 0x20);                 // ch3 base address = 0x2000
        Out (0x82, 0x01);                                   // ch3 page = 1 -> physical 0x12000
        Out (0x07, 0xFF); Out (0x07, 0x01);                 // ch3 count = 0x01FF (512 bytes)
        Out (0x0A, 0x03);                                   // unmask DMA channel 3
        // Write the 6-byte READ Device Control Block to the data port 0x320 (>= 0x100, so via DX).
        Prog.push_back (0xBA); Prog.push_back (0x20); Prog.push_back (0x03);   // mov dx, 0x0320
        UINT8 const Dcb[] = { 0x08, 0x00, 0x00, 0x00, 0x01, 0x00 };   // READ cyl0 head0 sect0 count1
        for (UINT8 B : Dcb) { Prog.push_back (0xB0); Prog.push_back (B); Prog.push_back (0xEE); }   // mov al,B ; out dx,al
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
            0xB0, 0x0B, 0xE6, 0x20,  // mov al,0x0B ; out 0x20,al   OCW3: select master ISR for read-back
            0xE4, 0x20, 0xA2, 0x57, 0x00,   // in al,0x20 ; mov [0x57],al   master ISR (cascade bit 2 set?)
            0xB0, 0x21, 0xBA, 0xF8, 0x03, 0xEE,   // mov al,'!' ; mov dx,0x3F8 ; out dx,al
            0xB0, 0x20, 0xE6, 0xA0,  // mov al,0x20 ; out 0xA0,al   EOI to the slave
            0xB0, 0x20, 0xE6, 0x20,  // mov al,0x20 ; out 0x20,al   EOI to the master (clears cascade ISR bit 2)
            0xCF                     // iret
        };
        std::memcpy (Ram.data () + 0x0500, Isr, sizeof (Isr));
        X86SetIvt (Machine, 0x70, 0x0000, 0x0500);              // slave IRQ8 -> INT 0x70

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
        // mov dx,0x3F8 ; mov al,'.' ; out dx,al ; mov al,0x20 ; out 0x20,al (EOI) ; iret
        UINT8 const Isr[] = { 0xBA, 0xF8, 0x03, 0xB0, 0x2E, 0xEE, 0xB0, 0x20, 0xE6, 0x20, 0xCF };
        std::memcpy (Ram.data () + 0x0500, Isr, sizeof (Isr));
        X86SetIvt (Machine, 0x08, 0x0000, 0x0500);

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

    if (ConsoleMode) {
        // Find the display (mode + framebuffer) and the keyboard sink among the matched components.
        IDisplayMode  *pDispMode = nullptr;
        IKeyboardSink *pKbd      = nullptr;
        for (MATCHED_DEVICE CONST &D : Builder.Devices ()) {
            if (pDispMode == nullptr) { D.pDevice->QueryInterface (IID_IDisplayMode, (VOID **) &pDispMode); }
            if (pKbd      == nullptr) { D.pDevice->QueryInterface (IID_IKeyboardSink, (VOID **) &pKbd); }
        }
        if (pDispMode == nullptr) {
            std::printf ("   console: the fitted display exposes no IDisplayMode\n");
            if (pKbd) { pKbd->Release (); }
            pArch->Release (); return 2;
        }

        // Wire the console seam: machine endpoint -> ConsoleBridge (the System pump); console
        // endpoint -> TerminalConsole. The two talk only through the in-process channel, so the
        // console could be split into a separate process by swapping the channel transport.
        IConsoleChannel *pMachEnd = nullptr, *pConEnd = nullptr;
        CreateInProcConsolePair (&pMachEnd, &pConEnd);
        ConsoleBridge Bridge (pMachEnd, pDispMode, pKbd, &Machine);
        Machine.SetPump ([&Bridge] () { Bridge.Pump (); });

        bool Headless = !ConsoleKeys.empty ();
        TerminalConsole Console (pConEnd, Headless ? nullptr : stdout);

        std::printf ("   --- console (%s; CPU: V20, backend: %s) ---\n",
                     Headless ? "headless/scripted" : "interactive", pBackend->GetName ());

        std::thread Worker ([&] () { Machine.Run (Entry, End, ~(UINT64) 0); });
        if (Headless) {
            // Scripted: type the given keys, let them round-trip, print the resulting screen.
            for (char Ch : ConsoleKeys) { Console.FeedKey (Ch); }
            for (int I = 0; I < 200; I++) {                  // poll up to ~2 s for the echo to land
                Console.Service ();
                std::string S = Console.ScreenText ();
                if (S.find (ConsoleKeys) != std::string::npos) { break; }
                std::this_thread::sleep_for (std::chrono::milliseconds (10));
            }
            Console.Service ();
            Console.SendControl (ConsoleControlQuit);
            std::string Screen = Console.ScreenText ();
            std::printf ("   screen %ux%u:\n", Console.Cols (), Console.Rows ());
            std::printf ("%s", Screen.c_str ());
        } else {
            Console.RunInteractive ();                       // blocks until Ctrl-]; sends CONTROL_QUIT
        }
        Worker.join ();
        pConEnd->Release ();
        pMachEnd->Release ();
        if (pKbd) { pKbd->Release (); }
        pDispMode->Release ();
        pArch->Release ();
        return 0;
    }

    std::printf ("   --- serial console (CPU: V20, backend: %s) ---\n   ", pBackend->GetName ());
    // A real BIOS POST does thousands of port accesses, each forcing a whole-window re-translation
    // here (there is no block cache), so it is slow -- a bounded budget keeps the attempt observable.
    UINT64 Budget = BiosBoot ? BiosSteps : 2000000;
    LC_SYS_RESULT R = Machine.Run (Entry, End, Budget);
    std::printf ("\n   --- machine halted (reason=%s, interrupts=%llu, port writes=%llu) ---\n",
                 R.Reason == LC_SYS_RESULT::HaltedIdle ? "halted-idle" :
                 R.Reason == LC_SYS_RESULT::Shutdown   ? "shutdown" :
                 R.Reason == LC_SYS_RESULT::StepBudget ? "step-budget" : "fault",
                 (unsigned long long) R.Interrupts, (unsigned long long) R.PortWrites);
    if (BiosBoot) {
        // Post-mortem: report where POST stopped as a linear address and an F000:offset pair, plus
        // the I/O activity, so we can see how far the real firmware advanced through power-on tests.
        UINT32 Lin = (UINT32) R.FinalPc;
        std::printf ("   BIOS POST stopped at linear 0x%05X (F000:%04X); port reads=%llu writes=%llu, steps=%llu\n",
                     Lin, (UINT16) (Lin - 0xF0000), (unsigned long long) R.PortReads,
                     (unsigned long long) R.PortWrites, (unsigned long long) R.Steps);
    }

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
        std::printf ("   RTC IRQ8: %llu interrupt(s) via the slave 8259 (INT 0x70); master ISR in handler = 0x%02X (cascade IRQ2)\n",
                     (unsigned long long) R.Interrupts, Ram[0x0057]);
    }
    if (Demo == 5) {
        std::printf ("   PIT ch0 latched count = 0x%02X%02X (8254 counter-latch read-back)\n",
                     Ram[0x0071], Ram[0x0070]);
    }
    if (Demo == 8) {
        std::printf ("   priority PIC: %llu IRQ0; in-service register read inside the handler = 0x%02X\n",
                     (unsigned long long) R.Interrupts, Ram[0x0056]);
    }
    if (Demo == 10 || BootScan) {
        if (Demo == 10) {
            std::printf ("   guest read option-ROM signature at 0xc8000: 0x%02X 0x%02X\n",
                         Ram[0x4800], Ram[0x4801]);
        }
        // BIOS-style option-ROM scan: walk the UMA in 2 KiB steps looking for the 0x55 0xAA marker.
        for (UINT32 Seg = 0xC0000; Seg < 0xF0000 && Seg + 2 < Ram.size (); Seg += 0x800) {
            if (Ram[Seg] != 0x55 || Ram[Seg + 1] != 0xAA) { continue; }
            UINT32 Len = (UINT32) Ram[Seg + 2] * 512;
            UINT32 Sum = 0;
            for (UINT32 I = 0; I < Len && Seg + I < Ram.size (); I++) { Sum += Ram[Seg + I]; }
            std::printf ("   option ROM found at 0x%05x: %u bytes, checksum %s\n",
                         Seg, Len, (Sum & 0xFF) == 0 ? "OK" : "bad");
        }
    }
    if (Demo == 9) {
        CHAR8 Text[40];
        for (int I = 0; I < 39; I++) {
            UINT8 Ch = Ram[0x4000 + I];
            Text[I] = (Ch >= 0x20 && Ch < 0x7F) ? (CHAR8) Ch : ' ';
        }
        Text[39] = '\0';
        std::printf ("   XT-IDE PIO read: sector 0 at 0x4000 = \"%s\"\n", Text);
    }
    if (Demo == 7) {
        CHAR8 Text[32];
        for (int I = 0; I < 31; I++) {
            UINT8 Ch = Ram[0x5000 + I];                     // sector read back into 0x5000
            Text[I] = (Ch >= 0x20 && Ch < 0x7F) ? (CHAR8) Ch : ' ';
        }
        Text[31] = '\0';
        std::printf ("   floppy write round-trip: wrote then read back from disk = \"%s\"\n", Text);
    }
    if (Demo == 6) {
        CHAR8 Text[48];
        for (int I = 0; I < 47; I++) {
            UINT8 Ch = Ram[0x12000 + I];                    // page 1 + offset 0x2000
            Text[I] = (Ch >= 0x20 && Ch < 0x7F) ? (CHAR8) Ch : ' ';
        }
        Text[47] = '\0';
        std::printf ("   floppy DMA: %llu IRQ6; sector at 0x12000 (page 1) = \"%s\"\n",
                     (unsigned long long) R.Interrupts, Text);
    }
    if (Demo == 12) {
        CHAR8 Text[40];
        for (int I = 0; I < 39; I++) {
            UINT8 Ch = Ram[0x12000 + I];                    // ch3 page 1 + offset 0x2000
            Text[I] = (Ch >= 0x20 && Ch < 0x7F) ? (CHAR8) Ch : ' ';
        }
        Text[39] = '\0';
        std::printf ("   hard-disk DMA: %llu IRQ5; sector 0 at 0x12000 = \"%s\"\n",
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
