/** @file
  Read-only memory (BIOS / option ROM) -- a LibCPU hardware-component bundle. A read-only region of
  the physical address space, base and size from the device-tree node's "reg". If the node carries a
  "libcpu,firmware" property (a file path), this component loads that image as the ROM's contents and
  presents it through IHostMemory, so the machine maps the firmware into the address space (a card's
  option ROM in the C0000-EFFFF window, or the system BIOS). Otherwise the region is described but
  left for the machine runner to fill (e.g. a BIOS dump loaded with --image).

  A COM object exposing IMemoryDevice (IsReadOnly() is TRUE) and, when firmware is loaded, IHostMemory.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IDevice.h"
#include <atomic>
#include <cstdio>
#include <vector>

namespace LibCPU {

namespace {

class Rom : public IDevice, public IMemoryDevice, public IHostMemory {
public:
    Rom () : m_Ref (1) {}

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        if (ppvObject == nullptr) { return E_POINTER; }
        if (CompareGuid (&riid, &IID_IUnknown) || CompareGuid (&riid, &IID_IDevice)) {
            *ppvObject = static_cast<IDevice *> (this);
        } else if (CompareGuid (&riid, &IID_IMemoryDevice)) {
            *ppvObject = static_cast<IMemoryDevice *> (this);
        } else if (CompareGuid (&riid, &IID_IHostMemory) && !m_Image.empty ()) {
            *ppvObject = static_cast<IHostMemory *> (this);   // only when firmware backs the region
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

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override
    {
        return m_Image.empty () ? "BIOS ROM" : "option ROM (firmware)";
    }

    HRESULT STDMETHODCALLTYPE Configure (IN IDeviceNode *pNode) override
    {
        m_Base = 0;
        m_Size = 0;
        m_Image.clear ();
        if (pNode != nullptr) {
            pNode->GetPropertyCell ("reg", 0, &m_Base);
            pNode->GetPropertyCell ("reg", 1, &m_Size);

            UINT8 CONST *pPath = nullptr;
            UINT32       PathLen = 0;
            if (SUCCEEDED (pNode->GetProperty ("libcpu,firmware", &pPath, &PathLen)) &&
                pPath != nullptr && PathLen > 0) {
                LoadFirmware ((CHAR8 CONST *) pPath);         // a NUL-terminated path string
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Reset (THIS) override { return S_OK; }

    // --- IMemoryDevice ---
    UINT32  STDMETHODCALLTYPE GetBase (THIS) override { return m_Base; }
    UINT32  STDMETHODCALLTYPE GetSize (THIS) override { return m_Image.empty () ? m_Size : (UINT32) m_Image.size (); }
    BOOLEAN STDMETHODCALLTYPE IsReadOnly (THIS) override { return TRUE; }

    // --- IHostMemory: the firmware bytes that back the region (present only when loaded) ---
    UINT8 * STDMETHODCALLTYPE GetHostBuffer (THIS) override { return m_Image.data (); }

private:
    VOID LoadFirmware (CHAR8 CONST *pPath)
    {
        std::FILE *pF = std::fopen (pPath, "rb");
        if (pF == nullptr) { return; }
        std::fseek (pF, 0, SEEK_END);
        long Len = std::ftell (pF);
        std::fseek (pF, 0, SEEK_SET);
        if (Len > 0) {
            m_Image.resize ((size_t) Len);
            if (std::fread (m_Image.data (), 1, (size_t) Len, pF) != (size_t) Len) { m_Image.clear (); }
        }
        std::fclose (pF);
    }

    std::atomic<INT32> m_Ref;
    UINT32             m_Base = 0;
    UINT32             m_Size = 0;
    std::vector<UINT8> m_Image;          // firmware contents, empty if none
};

} // anonymous namespace

IDevice *
CreateRom ()
{
    return new Rom ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreateRom)
