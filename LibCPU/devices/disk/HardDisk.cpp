/** @file
  Generic hard-disk MEDIUM -- a LibCPU hardware-component bundle.

  This models the drive itself (platters + geometry), independent of the bus that reaches it. The
  same medium is mounted behind an ST-506, ESDI, SASI, IDE/ATA, SCSI, ... controller; each of those
  is only a different protocol in front of this component. The machine builder wires a medium to a
  controller through the controller's "disks = <&drive>" phandle, then the controller serves its
  wire protocol from the medium's sectors.

  Geometry comes from the device tree: "libcpu,cylinders", "libcpu,heads", "libcpu,sectors", and an
  optional "libcpu,sector-size" (default 512). The backing store is a flat byte image of the whole
  capacity; sector 0 is seeded with a boot signature so a controller/BIOS sees a formatted disk. An
  optional "libcpu,image" path preloads the image from a file (and is the obvious place to persist
  it later).

  A COM object: IDevice + the IBlockMedium capability.

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

class HardDisk : public IDevice, public IBlockMedium {
public:
    HardDisk () : m_Ref (1) {}

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        if (ppvObject == nullptr) { return E_POINTER; }
        if (CompareGuid (&riid, &IID_IUnknown) || CompareGuid (&riid, &IID_IDevice)) {
            *ppvObject = static_cast<IDevice *> (this);
        } else if (CompareGuid (&riid, &IID_IBlockMedium)) {
            *ppvObject = static_cast<IBlockMedium *> (this);
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

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override { return m_Name.c_str (); }

    HRESULT STDMETHODCALLTYPE Configure (IN IDeviceNode *pNode) override
    {
        UINT32 Cyls = 0, Heads = 0, Spt = 0, SecSize = 512;
        std::string ImagePath;
        if (pNode != nullptr) {
            pNode->GetPropertyCell ("libcpu,cylinders", 0, &Cyls);
            pNode->GetPropertyCell ("libcpu,heads", 0, &Heads);
            pNode->GetPropertyCell ("libcpu,sectors", 0, &Spt);
            pNode->GetPropertyCell ("libcpu,sector-size", 0, &SecSize);
            UINT8 CONST *pImg = nullptr;
            UINT32       ImgLen = 0;
            if (SUCCEEDED (pNode->GetProperty ("libcpu,image", &pImg, &ImgLen)) && pImg != nullptr && ImgLen > 0) {
                ImagePath.assign ((CHAR8 CONST *) pImg, ImgLen);   // DT string property (NUL-terminated)
                ImagePath = ImagePath.c_str ();                    // trim at the NUL
            }
        }
        if (Cyls == 0)  { Cyls = 615; }                      // sensible default: ~20 MB (XT type 2)
        if (Heads == 0) { Heads = 4; }
        if (Spt == 0)   { Spt = 17; }
        if (SecSize == 0) { SecSize = 512; }
        m_Cyls = Cyls; m_Heads = Heads; m_Spt = Spt; m_SectorSize = SecSize;
        m_Sectors = (UINT64) Cyls * Heads * Spt;

        m_Image.assign ((size_t) (m_Sectors * m_SectorSize), 0);
        if (!ImagePath.empty ()) { LoadImage (ImagePath.c_str ()); }
        if (m_Image.size () >= 512) {                        // seed a boot signature on sector 0
            CHAR8 CONST *pTag = "LIBCPU HARD DISK SECTOR 0";
            std::memcpy (m_Image.data (), pTag, std::strlen (pTag));
            m_Image[510] = 0x55; m_Image[511] = 0xAA;
        }

        UINT32 Mb = (UINT32) ((m_Sectors * m_SectorSize) / (1024 * 1024));
        CHAR8 Buf[80];
        std::snprintf (Buf, sizeof (Buf), "Hard Disk (%u MB: %u/%u/%u)", Mb, m_Cyls, m_Heads, m_Spt);
        m_Name = Buf;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Reset (THIS) override { return S_OK; }

    // --- IBlockMedium ---
    HRESULT STDMETHODCALLTYPE GetGeometry (UINT32 *pCylinders, UINT32 *pHeads, UINT32 *pSectors, UINT32 *pSectorSize) override
    {
        if (pCylinders)  { *pCylinders  = m_Cyls; }
        if (pHeads)      { *pHeads      = m_Heads; }
        if (pSectors)    { *pSectors    = m_Spt; }
        if (pSectorSize) { *pSectorSize = m_SectorSize; }
        return S_OK;
    }

    UINT64 STDMETHODCALLTYPE GetSectorCount (THIS) override { return m_Sectors; }

    HRESULT STDMETHODCALLTYPE Read (UINT64 Lba, UINT8 *pBuffer, UINT32 SectorCount) override
    {
        if (pBuffer == nullptr) { return E_POINTER; }
        for (UINT32 S = 0; S < SectorCount; S++) {
            UINT64 Off = (Lba + S) * m_SectorSize;
            for (UINT32 I = 0; I < m_SectorSize; I++) {
                pBuffer[S * m_SectorSize + I] = (Off + I < m_Image.size ()) ? m_Image[(size_t) (Off + I)] : 0;
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Write (UINT64 Lba, UINT8 CONST *pBuffer, UINT32 SectorCount) override
    {
        if (pBuffer == nullptr) { return E_POINTER; }
        for (UINT32 S = 0; S < SectorCount; S++) {
            UINT64 Off = (Lba + S) * m_SectorSize;
            for (UINT32 I = 0; I < m_SectorSize && Off + I < m_Image.size (); I++) {
                m_Image[(size_t) (Off + I)] = pBuffer[S * m_SectorSize + I];
            }
        }
        return S_OK;
    }

private:
    VOID LoadImage (CHAR8 CONST *pPath)
    {
        std::FILE *pF = std::fopen (pPath, "rb");
        if (pF == nullptr) { return; }
        std::fread (m_Image.data (), 1, m_Image.size (), pF);   // short images leave the tail zeroed
        std::fclose (pF);
    }

    std::atomic<INT32> m_Ref;
    UINT32             m_Cyls       = 0;
    UINT32             m_Heads      = 0;
    UINT32             m_Spt        = 0;
    UINT32             m_SectorSize = 512;
    UINT64             m_Sectors    = 0;
    std::vector<UINT8> m_Image;
    std::string        m_Name = "Hard Disk";
};

} // anonymous namespace

IDevice *
CreateHardDisk ()
{
    return new HardDisk ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreateHardDisk)
