/** @file
  IBM/Xebec ST-506 fixed-disk controller (PC/XT hard-disk adapter) -- a LibCPU component bundle.

  The XT fixed-disk adapter (a Xebec 1210 design) presents four I/O ports at 0x320-0x323: the data
  register (0x320, the command/result byte stream), the controller status register (0x321: REQ/IO/
  C-D/BUSY), the controller-select register (0x322) and the DMA/interrupt mask register (0x323).

  The controller is just a BUS ADAPTER: the actual storage is a generic block medium (IBlockMedium)
  mounted by the machine builder from the node's "disks = <&drive>" phandle. The host writes a six-
  byte Device Control Block (command, head/drive, cyl/sector, cyl-low, count, control) to the data
  port; READ (0x08) / WRITE (0x0A) convert C/H/S to an LBA via the medium's geometry and transfer
  count*512 bytes over DMA channel 3, reading or writing the medium. Completion posts a status byte
  and raises IRQ5. Other commands (test-ready, recalibrate, init-params, ...) complete successfully.

  A COM object: IDevice + IPortDevice, IOptionRomHost, IStorageController, IInterruptSource and
  IDmaPeripheral.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IDevice.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace LibCPU {

namespace {

enum { Hdc_Data = 0, Hdc_Status = 1, Hdc_Select = 2, Hdc_Mask = 3 };
enum { Sector_Size = 512, Dcb_Len = 6 };

// Controller status register (0x321) bits.
enum { Sts_Req = 0x01, Sts_InputOutput = 0x02, Sts_CmdData = 0x04, Sts_Busy = 0x08 };

// The controller is a small state machine; the status register is a pure function of its phase.
// A select pulse (write to 0x322) moves it from Idle to Command; the final completion-byte read
// (from 0x320) drops it back to Idle. The two BIOS poll loops watch opposite edges of BUSY --
// the reset path waits for command-ready (BUSY set, value 0x0D), the post-command path waits
// for BUSY to clear (Idle, value 0x00) -- so a single static status value cannot satisfy both.
enum HdcPhase {
    Phase_Idle,      // deselected: status 0x00 (BUSY clear)
    Phase_Command,   // selected, accepting the DCB: status 0x0D (REQ | C/D | BUSY)
    Phase_Execute,   // command/transfer running: status 0x08 (BUSY)
    Phase_Result,    // a completion byte is waiting: status 0x0F (REQ | I/O | C/D | BUSY)
};

// Xebec command opcodes (low 5 bits of DCB byte 0).
enum { Cmd_Read = 0x08, Cmd_Write = 0x0A };

class HardDisk506 : public IDevice, public IPortDevice, public IOptionRomHost,
                    public IStorageController, public IInterruptSource, public IDmaPeripheral {
public:
    HardDisk506 () : m_Ref (1) {}
    ~HardDisk506 () { if (m_pMedium != nullptr) { m_pMedium->Release (); } }

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        if (ppvObject == nullptr) { return E_POINTER; }
        if (CompareGuid (&riid, &IID_IUnknown) || CompareGuid (&riid, &IID_IDevice)) {
            *ppvObject = static_cast<IDevice *> (this);
        } else if (CompareGuid (&riid, &IID_IPortDevice)) {
            *ppvObject = static_cast<IPortDevice *> (this);
        } else if (CompareGuid (&riid, &IID_IOptionRomHost)) {
            *ppvObject = static_cast<IOptionRomHost *> (this);
        } else if (CompareGuid (&riid, &IID_IStorageController)) {
            *ppvObject = static_cast<IStorageController *> (this);
        } else if (CompareGuid (&riid, &IID_IInterruptSource)) {
            *ppvObject = static_cast<IInterruptSource *> (this);
        } else if (CompareGuid (&riid, &IID_IDmaPeripheral)) {
            *ppvObject = static_cast<IDmaPeripheral *> (this);
        } else {
            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }
        AddRef ();
        return S_OK;
    }

    // IStorageController: the machine builder mounts the disk named by "disks = <&drive>". The
    // ST-506 adapter supports one fixed disk (unit 0); it reports the mounted drive's geometry.
    HRESULT STDMETHODCALLTYPE AttachMedium (UINT32 Unit, IBlockMedium *pMedium) override
    {
        if (pMedium == nullptr || Unit != 0) { return E_INVALIDARG; }
        if (m_pMedium != nullptr) { m_pMedium->Release (); }
        m_pMedium = pMedium;
        m_pMedium->AddRef ();
        m_pMedium->GetGeometry (&m_Cyls, &m_Heads, &m_Spt, &m_SecSize);
        UINT32 Mb = (UINT32) ((m_pMedium->GetSectorCount () * m_SecSize) / (1024 * 1024));
        CHAR8 Buf[80];
        std::snprintf (Buf, sizeof (Buf), "Xebec ST-506 HDC (%u MB: %u/%u/%u)", Mb, m_Cyls, m_Heads, m_Spt);
        m_Name = Buf;
        return S_OK;
    }

    UINT32 STDMETHODCALLTYPE GetRomAddress (THIS) override { return 0xC8000; }   // hard-disk option ROM

    UINT32 STDMETHODCALLTYPE AddRef (THIS) override { return (UINT32) ++m_Ref; }
    UINT32 STDMETHODCALLTYPE Release (THIS) override
    {
        UINT32 Count = (UINT32) --m_Ref;
        if (Count == 0) { delete this; }
        return Count;
    }

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override { return m_Name.c_str (); }

    HRESULT STDMETHODCALLTYPE Configure (IN IDeviceNode *pNode) override
    {
        UINT32 Base = 0x320;
        // The drive (if any) is mounted later via AttachMedium from the "disks" phandle; the
        // controller itself only needs its I/O base here.
        if (pNode != nullptr) { pNode->GetPropertyCell ("reg", 0, &Base); }
        m_Base = (UINT16) Base;
        return Reset ();
    }

    HRESULT STDMETHODCALLTYPE Reset (THIS) override
    {
        m_Mask = 0; m_DcbLen = 0; m_Phase = Phase_Idle; m_DmaPending = FALSE; m_IrqPending = FALSE;
        return S_OK;
    }

    BOOLEAN STDMETHODCALLTYPE OwnsPort (UINT16 Port) override
    {
        return (Port >= m_Base && Port < (UINT16) (m_Base + 4)) ? TRUE : FALSE;
    }

    HRESULT STDMETHODCALLTYPE ReadPort (UINT16 Port, UINT32 /*Width*/, UINT32 *pValue) override
    {
        if (pValue == nullptr) { return E_POINTER; }
        switch (Port - m_Base) {
            case Hdc_Status:
                // Pure function of the controller phase (see HdcPhase). Newer M24 BIOSes (>= 1.36)
                // reset the controller and spin here until it reports command-ready (0x0D) before
                // issuing a command, then -- after reading the completion byte -- spin again until
                // BUSY clears (0x00); older BIOSes (1.21) just write the command blind.
                switch (m_Phase) {
                    case Phase_Command: *pValue = Sts_Req | Sts_CmdData | Sts_Busy; break;
                    case Phase_Execute: *pValue = Sts_Busy; break;
                    case Phase_Result:  *pValue = Sts_Req | Sts_InputOutput | Sts_CmdData | Sts_Busy; break;
                    default:            *pValue = 0; break;   // Phase_Idle: deselected
                }
                break;
            case Hdc_Data:
                // The completion/status byte; reading it in the result phase ends the command and
                // returns the controller to Idle, which is the BUSY-clear edge the BIOS waits on.
                // It is also the service that drops the IRQ line.
                if (m_Phase == Phase_Result) {
                    *pValue = m_Completion;
                    m_Phase = Phase_Idle;
                    m_IrqPending = FALSE;
                } else {
                    *pValue = 0;
                }
                break;
            case Hdc_Mask: *pValue = m_Mask; break;
            default:       *pValue = 0; break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE WritePort (UINT16 Port, UINT32 /*Width*/, UINT32 Value) override
    {
        UINT8 V = (UINT8) Value;
        switch (Port - m_Base) {
            case Hdc_Data:                                    // accumulate the 6-byte Device Control Block
                // A select pulse normally precedes the command, but accept the first byte as an
                // implicit selection too (the 1.21 BIOS clocks the DCB in without polling first).
                if (m_Phase == Phase_Idle) { m_Phase = Phase_Command; m_DcbLen = 0; }
                if (m_Phase == Phase_Command) {
                    if (m_DcbLen < Dcb_Len) { m_Dcb[m_DcbLen++] = V; }
                    if (m_DcbLen == Dcb_Len) { m_DcbLen = 0; Execute (); }
                }
                break;
            case Hdc_Status:                                              // 0x321 write: controller reset
                m_Phase = Phase_Idle; m_DcbLen = 0; m_IrqPending = FALSE; break;
            case Hdc_Select:                                              // 0x322 write: select pulse -> command-ready
                m_Phase = Phase_Command; m_DcbLen = 0; m_IrqPending = FALSE; break;
            case Hdc_Mask:   m_Mask = V; break;                           // DMA/IRQ enable latch
            default:         break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE PollInterrupt (OUT UINT32 *pIrq) override
    {
        if (pIrq == nullptr) { return E_POINTER; }
        // A completion raises IRQ5 as a single edge, consumed here once. The M24 BIOS keeps IRQ5
        // masked while it polls the controller status (see the phase machine in ReadPort), so this
        // edge lands on a masked line -- the 8259 IRR latches it and delivers it only when the BIOS
        // later unmasks IRQ5, which is how the disk is detected. A held line would re-fire after
        // every EOI; the single edge + the PIC's IRR latch is the correct model.
        if (m_IrqPending) { m_IrqPending = FALSE; *pIrq = 5; return S_OK; }   // hard disk -> IRQ5
        return S_FALSE;
    }

    // --- IDmaPeripheral: the machine's DMA bridge moves the data between m_Buf and guest memory ---
    HRESULT STDMETHODCALLTYPE GetDmaRequest (UINT32 *pChannel, BOOLEAN *pToMemory, UINT8 **ppBuffer, UINT32 *pLength) override
    {
        if (!m_DmaPending) { return S_FALSE; }
        if (pChannel)  { *pChannel  = 3; }                    // the XT wires the fixed disk to DMA channel 3
        if (pToMemory) { *pToMemory = m_DmaToMem; }
        if (ppBuffer)  { *ppBuffer  = m_Buf.data (); }
        if (pLength)   { *pLength   = (UINT32) m_Buf.size (); }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CompleteDma (THIS) override
    {
        if (!m_DmaToMem && m_pMedium != nullptr) {            // WRITE: flush the DMA'd bytes to the medium
            m_pMedium->Write (m_Lba, m_Buf.data (), (UINT32) (m_Buf.size () / Sector_Size));
        }
        m_DmaPending  = FALSE;
        m_Completion  = 0x00;                                 // success
        m_Phase       = Phase_Result;                        // a completion byte is now waiting
        m_IrqPending  = TRUE;                                 // transfer done -> IRQ5
        return S_OK;
    }

private:
    VOID Execute ()
    {
        UINT8  Op    = (UINT8) (m_Dcb[0] & 0x1F);
        UINT8  Head  = (UINT8) (m_Dcb[1] & 0x1F);
        UINT16 Cyl   = (UINT16) (((UINT16) (m_Dcb[2] & 0xC0) << 2) | m_Dcb[3]);
        UINT8  Sect  = (UINT8) (m_Dcb[2] & 0x3F);
        UINT32 Count = m_Dcb[4] ? m_Dcb[4] : 1;

        if ((Op == Cmd_Read || Op == Cmd_Write) && m_pMedium != nullptr && m_Heads != 0 && m_Spt != 0) {
            m_Lba = (UINT64) ((Cyl * m_Heads + Head) * m_Spt + Sect);
            m_Buf.assign ((size_t) Count * Sector_Size, 0);
            m_DmaToMem = (Op == Cmd_Read) ? TRUE : FALSE;
            if (Op == Cmd_Read) { m_pMedium->Read (m_Lba, m_Buf.data (), Count); }   // stage for DMA-out
            m_DmaPending = TRUE;                              // the DMA bridge moves it, then CompleteDma
            m_Phase      = Phase_Execute;                     // BUSY while the transfer is in flight
            return;
        }
        // Non-transfer commands (test-ready, recalibrate, init drive params, sense, ...): succeed now.
        m_Completion  = 0x00;
        m_Phase       = Phase_Result;                        // a completion byte is now waiting
        m_IrqPending  = TRUE;
    }

    std::atomic<INT32> m_Ref;
    UINT16             m_Base    = 0x320;
    UINT8              m_Mask    = 0;
    IBlockMedium      *m_pMedium = nullptr;                  // mounted fixed disk (unit 0), or none
    std::string        m_Name    = "Xebec ST-506 HDC";
    UINT32             m_Cyls = 0, m_Heads = 0, m_Spt = 0, m_SecSize = Sector_Size;

    UINT8              m_Dcb[Dcb_Len] = { 0 };               // the Device Control Block being assembled
    UINT32             m_DcbLen = 0;
    std::vector<UINT8> m_Buf;                                // the in-flight DMA transfer buffer
    UINT64             m_Lba = 0;
    BOOLEAN            m_DmaToMem   = FALSE;
    BOOLEAN            m_DmaPending = FALSE;
    BOOLEAN            m_IrqPending = FALSE;
    HdcPhase           m_Phase = Phase_Idle;
    UINT8              m_Completion = 0;
};

} // anonymous namespace

IDevice *
CreateHardDisk506 ()
{
    return new HardDisk506 ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreateHardDisk506)
