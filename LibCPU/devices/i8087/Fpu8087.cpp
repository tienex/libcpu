/** @file
  Intel 8087 numeric coprocessor (FPU) -- a LibCPU hardware-component bundle.

  The 8087 sits on the local bus beside the 8086/8088 (here the V20/8086 frontend) and executes the
  ESC (0xD8-0xDF) / WAIT (0x9B) instruction group. It is not reached through the I/O port bus: the
  CPU and the 8087 share the instruction stream. This component models the coprocessor's PRESENCE
  -- so a machine that fits an 8087 reports it, and the CPU decodes the coprocessor opcodes (as
  no-ops, since floating-point execution is not yet modelled). It claims no ports and no memory.

  A COM object: IDevice only (a present-but-passive part).

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IDevice.h"
#include <atomic>

namespace LibCPU {

namespace {

class Fpu8087 : public IDevice {
public:
    Fpu8087 () : m_Ref (1) {}

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        if (ppvObject == nullptr) { return E_POINTER; }
        if (CompareGuid (&riid, &IID_IUnknown) || CompareGuid (&riid, &IID_IDevice)) {
            *ppvObject = static_cast<IDevice *> (this);
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

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override { return "Intel 8087 FPU"; }
    HRESULT STDMETHODCALLTYPE Configure (IN IDeviceNode * /*pNode*/) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Reset (THIS) override { return S_OK; }

private:
    std::atomic<INT32> m_Ref;
};

} // anonymous namespace

IDevice *
CreateFpu8087 ()
{
    return new Fpu8087 ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreateFpu8087)
