/** @file
  XT-IDE 8-bit hard-disk controller -- a LibCPU hardware-component bundle.

  The XT-IDE adapts a 16-bit IDE/ATA drive onto the 8-bit XT bus. The ATA task-file registers sit
  at the controller base (commonly 0x300): data (offset 0), error/features (1), sector count (2),
  LBA bytes / sector-number, cylinder-low, cylinder-high (3-5), drive/head (6), and status/command
  (7). Because the bus is 8-bit but the drive's data register is 16-bit, the rev-1 hardware adds a
  high-byte latch at offset 8: reading the data register (0x300) returns the low byte and latches
  the high byte, which the driver then reads from 0x308; writing pairs a 0x308 (high) write with a
  0x300 (low) write. Transfers are programmed I/O -- the driver polls the status register's DRQ bit.

  This component models that task file and serves READ/WRITE SECTORS and IDENTIFY DEVICE against a
  small backing image (sector 0 seeded with a recognizable pattern). LBA addressing is used.

  A COM object: IDevice + the IPortDevice capability.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IDevice.h"
#include <atomic>
#include <cstring>
#include <vector>

namespace LibCPU {

namespace {

enum { Ata_Data = 0, Ata_Error = 1, Ata_SecCount = 2, Ata_Lba0 = 3, Ata_Lba1 = 4,
       Ata_Lba2 = 5, Ata_DriveHead = 6, Ata_Status = 7, Xtide_DataHi = 8 };
// Status register bits.
enum { St_Bsy = 0x80, St_Drdy = 0x40, St_Drq = 0x08, St_Err = 0x01 };
enum { Sector_Size = 512, Disk_Sectors = 32 };               // a small backing disk

class Xtide : public IDevice, public IPortDevice, public IOptionRomHost {
public:
    Xtide () : m_Ref (1) {}

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        if (ppvObject == nullptr) { return E_POINTER; }
        if (CompareGuid (&riid, &IID_IUnknown) || CompareGuid (&riid, &IID_IDevice)) {
            *ppvObject = static_cast<IDevice *> (this);
        } else if (CompareGuid (&riid, &IID_IPortDevice)) {
            *ppvObject = static_cast<IPortDevice *> (this);
        } else if (CompareGuid (&riid, &IID_IOptionRomHost)) {
            *ppvObject = static_cast<IOptionRomHost *> (this);
        } else {
            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }
        AddRef ();
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

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override { return "XT-IDE controller"; }

    HRESULT STDMETHODCALLTYPE Configure (IN IDeviceNode *pNode) override
    {
        UINT32 Base = 0x300;
        if (pNode != nullptr) { pNode->GetPropertyCell ("reg", 0, &Base); }
        m_Base = (UINT16) Base;

        m_Image.assign ((size_t) Disk_Sectors * Sector_Size, 0);
        CHAR8 CONST *pTag = "LIBCPU XT-IDE SECTOR 0 - PIO READ OK";
        std::memcpy (m_Image.data (), pTag, std::strlen (pTag));
        m_Image[Sector_Size - 2] = 0x55;
        m_Image[Sector_Size - 1] = 0xAA;
        return Reset ();
    }

    HRESULT STDMETHODCALLTYPE Reset (THIS) override
    {
        std::memset (m_Reg, 0, sizeof (m_Reg));
        m_Status   = St_Drdy;
        m_HighLatch = 0;
        m_Idx      = 0;
        m_Words    = 0;
        m_Writing  = FALSE;
        return S_OK;
    }

    BOOLEAN STDMETHODCALLTYPE OwnsPort (UINT16 Port) override
    {
        return (Port >= m_Base && Port < (UINT16) (m_Base + 0x10)) ? TRUE : FALSE;
    }

    HRESULT STDMETHODCALLTYPE ReadPort (UINT16 Port, UINT32 /*Width*/, UINT32 *pValue) override
    {
        if (pValue == nullptr) { return E_POINTER; }
        switch (Port - m_Base) {
            case Ata_Data:                                   // low byte; latch the high byte for 0x308
                if (m_Words > 0 && m_Idx + 1 < (UINT32) m_Buf.size ()) {
                    *pValue     = m_Buf[m_Idx];
                    m_HighLatch = m_Buf[m_Idx + 1];
                    AdvanceWord ();
                } else {
                    *pValue = 0;
                }
                break;
            case Xtide_DataHi: *pValue = m_HighLatch; break; // the latched high byte
            case Ata_Status:   *pValue = m_Status; break;
            default:           *pValue = m_Reg[Port - m_Base]; break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE WritePort (UINT16 Port, UINT32 /*Width*/, UINT32 Value) override
    {
        UINT8 V = (UINT8) Value;
        switch (Port - m_Base) {
            case Ata_Data:                                   // low byte; combine with the latched high byte
                if (m_Writing && m_Words > 0 && m_Idx + 1 < (UINT32) m_Buf.size ()) {
                    m_Buf[m_Idx]     = V;
                    m_Buf[m_Idx + 1] = m_HighLatch;
                    AdvanceWord ();
                }
                break;
            case Xtide_DataHi:   m_HighLatch = V; break;     // stash the high byte for the next 0x300 write
            case Ata_Status:     Command (V); break;         // status offset is the command register on write
            default:             m_Reg[Port - m_Base] = V; break;
        }
        return S_OK;
    }

private:
    UINT32 Lba () const
    {
        return ((UINT32) (m_Reg[Ata_DriveHead] & 0x0F) << 24) | ((UINT32) m_Reg[Ata_Lba2] << 16) |
               ((UINT32) m_Reg[Ata_Lba1] << 8) | m_Reg[Ata_Lba0];
    }

    VOID LoadSector (UINT32 Lba)
    {
        m_Buf.assign (Sector_Size, 0);
        UINT64 Off = (UINT64) Lba * Sector_Size;
        for (UINT32 I = 0; I < Sector_Size && Off + I < m_Image.size (); I++) { m_Buf[I] = m_Image[(size_t) (Off + I)]; }
        m_Idx = 0;
    }

    VOID AdvanceWord ()
    {
        m_Idx += 2;
        if (m_Idx >= Sector_Size) {                          // sector exhausted
            if (m_Writing) { StoreSector (); }
            m_Words = (m_Words >= 256) ? (m_Words - 256) : 0;
            if (m_Words > 0) { m_Lba++; if (m_Writing) { m_Buf.assign (Sector_Size, 0); m_Idx = 0; } else { LoadSector (m_Lba); } }
            else { m_Status = St_Drdy; m_Writing = FALSE; }  // transfer complete: DRQ clears
        }
    }

    VOID StoreSector ()
    {
        UINT64 Off = (UINT64) m_Lba * Sector_Size;
        for (UINT32 I = 0; I < Sector_Size && Off + I < m_Image.size (); I++) { m_Image[(size_t) (Off + I)] = m_Buf[I]; }
    }

    VOID Command (UINT8 Cmd)
    {
        UINT32 Count = (m_Reg[Ata_SecCount] == 0) ? 256 : m_Reg[Ata_SecCount];
        switch (Cmd) {
            case 0x20: case 0x21:                            // READ SECTORS
                m_Lba = Lba (); LoadSector (m_Lba);
                m_Words = Count * 256; m_Writing = FALSE;
                m_Status = St_Drdy | St_Drq;
                break;
            case 0x30: case 0x31:                            // WRITE SECTORS
                m_Lba = Lba (); m_Buf.assign (Sector_Size, 0); m_Idx = 0;
                m_Words = Count * 256; m_Writing = TRUE;
                m_Status = St_Drdy | St_Drq;
                break;
            case 0xEC:                                       // IDENTIFY DEVICE
                BuildIdentify ();
                m_Words = 256; m_Writing = FALSE;
                m_Status = St_Drdy | St_Drq;
                break;
            default:                                         // Recalibrate / Init params / etc.: ready
                m_Status = St_Drdy;
                break;
        }
    }

    VOID BuildIdentify ()
    {
        m_Buf.assign (Sector_Size, 0);
        UINT32 Total = Disk_Sectors;
        m_Buf[0] = 0x40;                                     // word 0: fixed device
        m_Buf[120] = (UINT8) (Total & 0xFF);                 // words 60-61: total LBA sectors
        m_Buf[121] = (UINT8) (Total >> 8);
        CHAR8 CONST *pModel = "LIBCPU XTIDE DISK";           // words 27-46: model string (byte-swapped pairs)
        for (UINT32 I = 0; pModel[I] != '\0' && I < 40; I += 2) {
            m_Buf[54 + I + 1] = (UINT8) pModel[I];
            m_Buf[54 + I]     = (pModel[I + 1] != '\0') ? (UINT8) pModel[I + 1] : (UINT8) ' ';
        }
        m_Idx = 0;
    }

    std::atomic<INT32> m_Ref;
    UINT16             m_Base = 0x300;
    std::vector<UINT8> m_Image;
    std::vector<UINT8> m_Buf;                                // the current PIO sector buffer
    UINT8              m_Reg[8] = { 0 };                     // ATA task-file registers
    UINT8              m_Status = St_Drdy;
    UINT8              m_HighLatch = 0;
    UINT32             m_Idx   = 0;                          // byte index within m_Buf
    UINT32             m_Words = 0;                          // 16-bit words remaining in the transfer
    UINT32             m_Lba   = 0;
    BOOLEAN            m_Writing = FALSE;
};

} // anonymous namespace

IDevice *
CreateXtide ()
{
    return new Xtide ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreateXtide)
