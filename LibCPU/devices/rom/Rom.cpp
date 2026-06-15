/** @file
  Read-only memory (BIOS ROM) -- a LibCPU hardware-component bundle. A read-only region of the
  physical address space, base and size from the device-tree node's "reg". The image that backs
  it is supplied at run time (e.g. a BIOS dump loaded by the machine runner). A COM object
  exposing IMemoryDevice; IsReadOnly() is TRUE.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IDevice.h"
#include <atomic>

namespace LibCPU {

namespace {

class Rom : public IDevice, public IMemoryDevice {
public:
    Rom () : m_Ref (1) {}

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        if (ppvObject == nullptr) { return E_POINTER; }
        if (CompareGuid (&riid, &IID_IUnknown) || CompareGuid (&riid, &IID_IDevice)) {
            *ppvObject = static_cast<IDevice *> (this);
        } else if (CompareGuid (&riid, &IID_IMemoryDevice)) {
            *ppvObject = static_cast<IMemoryDevice *> (this);
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

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override { return "BIOS ROM"; }

    HRESULT STDMETHODCALLTYPE Configure (IN IDeviceNode *pNode) override
    {
        m_Base = 0;
        m_Size = 0;
        if (pNode != nullptr) {
            pNode->GetPropertyCell ("reg", 0, &m_Base);
            pNode->GetPropertyCell ("reg", 1, &m_Size);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Reset (THIS) override { return S_OK; }

    UINT32  STDMETHODCALLTYPE GetBase (THIS) override { return m_Base; }
    UINT32  STDMETHODCALLTYPE GetSize (THIS) override { return m_Size; }
    BOOLEAN STDMETHODCALLTYPE IsReadOnly (THIS) override { return TRUE; }

private:
    std::atomic<INT32> m_Ref;
    UINT32             m_Base = 0;
    UINT32             m_Size = 0;
};

} // anonymous namespace

IDevice *
CreateRom ()
{
    return new Rom ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreateRom)
