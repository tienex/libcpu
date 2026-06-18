/** @file
  NEC uPD765 / Intel 8272A floppy disk controller -- a LibCPU hardware-component bundle.

  Ports 0x3F0-0x3F7: digital output register (0x3F2, drive/motor/reset/DMA enable), main status
  register (0x3F4, RQM/DIO/CB handshake), the command-and-result FIFO (0x3F5), and the
  configuration/control register (0x3F7). The controller works in three phases: the host writes a
  command and its parameters to the FIFO; the controller executes (a Read/Write Data command moves
  a sector over DMA channel 2 to or from guest memory); then the host reads back a result phase.

  This component models that flow for the Read Data and Write Data commands against a small backing
  image, plus the Specify / Recalibrate / Seek / Sense-Interrupt-Status housekeeping the BIOS uses.
  The sector transfer itself is performed by the machine's DMA bridge through the IDmaPeripheral
  capability; completion raises IRQ6 through IInterruptSource.

  A COM object: IDevice + the IPortDevice, IInterruptSource and IDmaPeripheral capabilities.

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

enum { Fdc_Dor = 2, Fdc_Msr = 4, Fdc_Data = 5, Fdc_Ccr = 7 };
enum { Msr_Rqm = 0x80, Msr_Dio = 0x40, Msr_Ndm = 0x20, Msr_Cb = 0x10 };
enum { Sector_Size = 512 };
// Default geometry is the 1.44 MB 3.5" drive (80 cyl x 2 heads x 18 spt); the device tree can
// override it per drive (e.g. a 360 KB 5.25" drive is 40 x 2 x 9).
enum { Geo_Cyls = 80, Geo_Spt = 18, Geo_Heads = 2 };

// Phases of a controller command.
enum PHASE { PhaseCommand, PhaseExec, PhaseResult };

// Parameter bytes (after the opcode) for the commands this controller decodes; -1 = unknown.
static int
CommandParams (UINT8 Opcode)
{
    switch (Opcode & 0x1F) {
        case 0x06: return 8;        // Read Data
        case 0x05: return 8;        // Write Data
        case 0x0A: return 1;        // Read ID
        case 0x03: return 2;        // Specify
        case 0x07: return 1;        // Recalibrate
        case 0x0F: return 2;        // Seek
        case 0x04: return 1;        // Sense Drive Status
        case 0x08: return 0;        // Sense Interrupt Status
        default:   return -1;
    }
}

class Fdc8272 : public IDevice, public IPortDevice, public IInterruptSource, public IDmaPeripheral {
public:
    Fdc8272 () : m_Ref (1) {}

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        if (ppvObject == nullptr) { return E_POINTER; }
        if (CompareGuid (&riid, &IID_IUnknown) || CompareGuid (&riid, &IID_IDevice)) {
            *ppvObject = static_cast<IDevice *> (this);
        } else if (CompareGuid (&riid, &IID_IPortDevice)) {
            *ppvObject = static_cast<IPortDevice *> (this);
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

    UINT32 STDMETHODCALLTYPE AddRef (THIS) override { return (UINT32) ++m_Ref; }
    UINT32 STDMETHODCALLTYPE Release (THIS) override
    {
        UINT32 Count = (UINT32) --m_Ref;
        if (Count == 0) { delete this; }
        return Count;
    }

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override { return "uPD765/8272A FDC"; }

    HRESULT STDMETHODCALLTYPE Configure (IN IDeviceNode *pNode) override
    {
        UINT32 Base = 0x3F0;
        UINT32 Cyls = Geo_Cyls, Heads = Geo_Heads, Spt = Geo_Spt;
        std::string ImagePath;
        if (pNode != nullptr) {
            pNode->GetPropertyCell ("reg", 0, &Base);
            pNode->GetPropertyCell ("libcpu,cylinders", 0, &Cyls);   // drive geometry (default 1.44 MB)
            pNode->GetPropertyCell ("libcpu,heads", 0, &Heads);
            pNode->GetPropertyCell ("libcpu,sectors", 0, &Spt);
            UINT8 CONST *pImg = nullptr; UINT32 ImgLen = 0;          // optional backing disk image
            if (SUCCEEDED (pNode->GetProperty ("libcpu,image", &pImg, &ImgLen)) && pImg != nullptr && ImgLen > 0) {
                ImagePath.assign ((CHAR8 CONST *) pImg, ImgLen);
                ImagePath = ImagePath.c_str ();                      // trim at the NUL
            }
        }
        m_Base  = (UINT16) Base;
        m_Heads = (UINT8) Heads;
        m_Spt   = (UINT8) Spt;

        m_Image.assign ((size_t) Cyls * Heads * Spt * Sector_Size, 0);
        if (!ImagePath.empty ()) {                           // a real diskette image: load it verbatim
            std::FILE *pF = std::fopen (ImagePath.c_str (), "rb");
            if (pF != nullptr) {
                std::fread (m_Image.data (), 1, m_Image.size (), pF);   // short images leave the tail zeroed
                std::fclose (pF);
            }
        } else {                                             // no media attached: seed a recognizable pattern
            CHAR8 CONST *pTag = "LIBCPU FLOPPY SECTOR 0 - DMA TRANSFER OK";
            std::memcpy (m_Image.data (), pTag, std::strlen (pTag));
            m_Image[Sector_Size - 2] = 0x55;                 // boot signature
            m_Image[Sector_Size - 1] = 0xAA;
        }
        return Reset ();
    }

    HRESULT STDMETHODCALLTYPE Reset (THIS) override
    {
        m_Dor        = 0;
        m_Phase      = PhaseCommand;
        m_CmdLen     = 0;
        m_CmdNeed    = 0;
        m_ResLen     = 0;
        m_ResIdx     = 0;
        m_DmaPending = FALSE;
        m_IrqPending = FALSE;
        return S_OK;
    }

    BOOLEAN STDMETHODCALLTYPE OwnsPort (UINT16 Port) override
    {
        return (Port >= m_Base && Port < (UINT16) (m_Base + 8)) ? TRUE : FALSE;
    }

    HRESULT STDMETHODCALLTYPE ReadPort (UINT16 Port, UINT32 /*Width*/, UINT32 *pValue) override
    {
        if (pValue == nullptr) { return E_POINTER; }
        switch (Port - m_Base) {
            case Fdc_Dor:  *pValue = m_Dor; break;
            case Fdc_Msr:  *pValue = Msr (); break;
            case Fdc_Data:                                   // result-phase FIFO read
                if (m_Phase == PhaseResult && m_ResIdx < m_ResLen) {
                    *pValue = m_Result[m_ResIdx++];
                    if (m_ResIdx >= m_ResLen) { m_Phase = PhaseCommand; }   // result drained -> idle
                } else {
                    *pValue = 0;
                }
                break;
            default:       *pValue = 0; break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE WritePort (UINT16 Port, UINT32 /*Width*/, UINT32 Value) override
    {
        UINT8 V = (UINT8) Value;
        switch (Port - m_Base) {
            case Fdc_Dor: {                                  // drive/motor/reset/DMA select
                UINT8 Old = m_Dor;
                m_Dor = V;
                // DOR bit 2 is the controller's active-low reset. When the BIOS releases reset
                // (0 -> 1), a real uPD765 finishes its reset and raises IRQ6; the BIOS's "reset disk
                // system" (INT 13h AH=0) waits for exactly that, then drains state with Sense
                // Interrupt Status. Without it the reset times out (status 0x80) and boot fails.
                if (!(Old & 0x04) && (V & 0x04)) {
                    m_Phase = PhaseCommand;
                    m_CmdLen = 0;
                    m_IrqPending = TRUE;
                }
                break;
            }
            case Fdc_Data:
                if (m_Phase != PhaseCommand) { break; }      // ignore data writes outside command phase
                if (m_CmdLen == 0) {                         // first byte: opcode sets the parameter count
                    int Params = CommandParams (V);
                    if (Params < 0) { break; }               // unknown command: drop it
                    m_CmdNeed = (UINT32) Params + 1;
                }
                if (m_CmdLen < sizeof (m_Cmd)) { m_Cmd[m_CmdLen++] = V; }
                if (m_CmdLen == m_CmdNeed) { Execute (); }
                break;
            default: break;                                  // CCR (0x3F7) and others accepted, discarded
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE PollInterrupt (OUT UINT32 *pIrq) override
    {
        if (pIrq == nullptr) { return E_POINTER; }
        if (m_IrqPending) { m_IrqPending = FALSE; *pIrq = 6; return S_OK; }   // floppy -> IRQ6
        return S_FALSE;
    }

    // --- IDmaPeripheral: the machine's DMA bridge moves the sector between m_Sector and memory ---
    HRESULT STDMETHODCALLTYPE GetDmaRequest (UINT32 *pChannel, BOOLEAN *pToMemory, UINT8 **ppBuffer, UINT32 *pLength) override
    {
        if (!m_DmaPending) { return S_FALSE; }
        if (pChannel) { *pChannel = 2; }                     // the PC wires the floppy to DMA channel 2
        if (pToMemory) { *pToMemory = m_DmaToMem; }
        if (ppBuffer) { *ppBuffer = m_Sector; }
        if (pLength) { *pLength = Sector_Size; }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CompleteDma (THIS) override
    {
        if (!m_DmaToMem) { WriteSector (m_Lba); }            // Write Data: flush the buffer to the image
        m_DmaPending = FALSE;
        BuildReadWriteResult ();
        m_Phase      = PhaseResult;
        m_IrqPending = TRUE;                                 // raise IRQ6 now that the transfer is done
        return S_OK;
    }

private:
    UINT8 Msr ()
    {
        UINT8 S = Msr_Rqm;                                   // always ready to accept/return a byte
        if (m_Phase == PhaseResult) { S |= Msr_Dio | Msr_Cb; }   // host reads the result
        else if (m_DmaPending)      { S |= Msr_Cb; }             // executing
        return S;
    }

    UINT32 Lba (UINT8 C, UINT8 H, UINT8 R) const
    {
        return ((UINT32) C * m_Heads + H) * m_Spt + (UINT32) (R > 0 ? R - 1 : 0);
    }

    VOID ReadSector (UINT32 Lba)
    {
        UINT64 Off = (UINT64) Lba * Sector_Size;
        for (UINT32 I = 0; I < Sector_Size; I++) {
            m_Sector[I] = (Off + I < m_Image.size ()) ? m_Image[(size_t) (Off + I)] : 0;
        }
    }
    VOID WriteSector (UINT32 Lba)
    {
        UINT64 Off = (UINT64) Lba * Sector_Size;
        for (UINT32 I = 0; I < Sector_Size && Off + I < m_Image.size (); I++) {
            m_Image[(size_t) (Off + I)] = m_Sector[I];
        }
    }

    VOID Execute ()
    {
        UINT8 Op = (UINT8) (m_Cmd[0] & 0x1F);
        if (Op == 0x06 || Op == 0x05) {                      // Read Data / Write Data
            m_Lba      = Lba (m_Cmd[2], m_Cmd[3], m_Cmd[4]);
            m_DmaToMem = (Op == 0x06) ? TRUE : FALSE;
            if (m_DmaToMem) { ReadSector (m_Lba); }          // disk -> buffer (bridge then copies to memory)
            m_DmaPending = TRUE;                             // bridge performs the transfer, then CompleteDma
            m_Phase      = PhaseExec;
            m_CmdLen     = 0;
            return;
        }
        if (Op == 0x08) {                                    // Sense Interrupt Status
            m_Result[0] = 0x20;                              // ST0: seek end
            m_Result[1] = 0x00;                              // present cylinder number
            m_ResLen = 2; m_ResIdx = 0; m_Phase = PhaseResult;
            m_CmdLen = 0;
            return;
        }
        if (Op == 0x07 || Op == 0x0F) {                      // Recalibrate / Seek: complete, raise IRQ6
            m_IrqPending = TRUE;
            m_CmdLen = 0; m_Phase = PhaseCommand;
            return;
        }
        // Specify / Read ID / Sense Drive Status and the rest: accept and return to idle.
        m_CmdLen = 0; m_Phase = PhaseCommand;
    }

    VOID BuildReadWriteResult ()
    {
        m_Result[0] = 0x00;                                  // ST0: normal termination, drive 0
        m_Result[1] = 0x00;                                  // ST1
        m_Result[2] = 0x00;                                  // ST2
        m_Result[3] = m_Cmd[2];                              // cylinder
        m_Result[4] = m_Cmd[3];                              // head
        m_Result[5] = (UINT8) (m_Cmd[4] + 1);               // next sector
        m_Result[6] = m_Cmd[5];                              // bytes-per-sector code
        m_ResLen = 7; m_ResIdx = 0;
    }

    std::atomic<INT32> m_Ref;
    UINT16             m_Base  = 0x3F0;
    UINT8              m_Dor   = 0;
    UINT8              m_Heads = Geo_Heads;                   // drive geometry (from the device tree)
    UINT8              m_Spt   = Geo_Spt;
    std::vector<UINT8> m_Image;
    UINT8              m_Sector[Sector_Size] = { 0 };        // the in-flight DMA sector buffer
    UINT8              m_Cmd[16] = { 0 };
    UINT8              m_Result[8] = { 0 };
    UINT32             m_CmdLen  = 0;
    UINT32             m_CmdNeed = 0;
    UINT32             m_ResLen  = 0;
    UINT32             m_ResIdx  = 0;
    UINT32             m_Lba     = 0;
    PHASE              m_Phase   = PhaseCommand;
    BOOLEAN            m_DmaPending = FALSE;
    BOOLEAN            m_DmaToMem   = FALSE;
    BOOLEAN            m_IrqPending = FALSE;
};

} // anonymous namespace

IDevice *
CreateFdc8272 ()
{
    return new Fdc8272 ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreateFdc8272)
