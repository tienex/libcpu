/** @file
  System RAM -- a LibCPU hardware-component bundle. A read/write region of the physical address
  space, its base and size taken from the device-tree node's "reg". It is a COM object exposing
  the IMemoryDevice capability, by which the machine learns the memory map from the device tree.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IDevice.h"
#include <atomic>

namespace LibCPU {

namespace {

class Ram : public IDevice, public IMemoryDevice {
public:
    Ram () : m_Ref (1) {}

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

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override { return "System RAM"; }

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
    BOOLEAN STDMETHODCALLTYPE IsReadOnly (THIS) override { return FALSE; }

private:
    std::atomic<INT32> m_Ref;
    UINT32             m_Base = 0;
    UINT32             m_Size = 0;
};

} // anonymous namespace

IDevice *
CreateRam ()
{
    return new Ram ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreateRam)
