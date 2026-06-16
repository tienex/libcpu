/** @file
  NEC uPD765 / Intel 8272A floppy disk controller -- a LibCPU hardware-component bundle.

  The PC/XT floppy adapter sits at I/O ports 0x3F0-0x3F7: the digital output register (0x3F2,
  drive/motor/reset/DMA select), the main status register (0x3F4, read: RQM/DIO/busy), the data
  register (0x3F5, the command/result FIFO) and the configuration/digital-input register (0x3F7).
  This component models the programmer-visible registers; with no media attached it reports the
  controller ready (RQM set) so a polling BIOS does not block on a missing FIFO byte. It performs
  no actual disk transfers yet.

  A COM object: IDevice + the IPortDevice capability.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IDevice.h"
#include <atomic>

namespace LibCPU {

namespace {

enum { Fdc_Dor = 2, Fdc_Msr = 4, Fdc_Data = 5, Fdc_Ccr = 7 };
enum { Msr_Rqm = 0x80, Msr_Dio = 0x40, Msr_Busy = 0x10 };

class Fdc8272 : public IDevice, public IPortDevice {
public:
    Fdc8272 () : m_Ref (1) {}

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        if (ppvObject == nullptr) { return E_POINTER; }
        if (CompareGuid (&riid, &IID_IUnknown) || CompareGuid (&riid, &IID_IDevice)) {
            *ppvObject = static_cast<IDevice *> (this);
        } else if (CompareGuid (&riid, &IID_IPortDevice)) {
            *ppvObject = static_cast<IPortDevice *> (this);
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
        if (pNode != nullptr) { pNode->GetPropertyCell ("reg", 0, &Base); }
        m_Base = (UINT16) Base;
        return Reset ();
    }

    HRESULT STDMETHODCALLTYPE Reset (THIS) override { m_Dor = 0; return S_OK; }

    BOOLEAN STDMETHODCALLTYPE OwnsPort (UINT16 Port) override
    {
        return (Port >= m_Base && Port < (UINT16) (m_Base + 8)) ? TRUE : FALSE;
    }

    HRESULT STDMETHODCALLTYPE ReadPort (UINT16 Port, UINT32 /*Width*/, UINT32 *pValue) override
    {
        if (pValue == nullptr) { return E_POINTER; }
        switch (Port - m_Base) {
            case Fdc_Dor:  *pValue = m_Dor; break;
            case Fdc_Msr:  *pValue = Msr_Rqm; break;          // ready for a command byte (no media)
            case Fdc_Data: *pValue = 0; break;                // result FIFO empty
            default:       *pValue = 0; break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE WritePort (UINT16 Port, UINT32 /*Width*/, UINT32 Value) override
    {
        if ((Port - m_Base) == Fdc_Dor) { m_Dor = (UINT8) Value; }   // drive/motor/reset/DMA select
        // Command-FIFO (0x3F5) and CCR (0x3F7) writes are accepted and discarded.
        return S_OK;
    }

private:
    std::atomic<INT32> m_Ref;
    UINT16             m_Base = 0x3F0;
    UINT8              m_Dor  = 0;
};

} // anonymous namespace

IDevice *
CreateFdc8272 ()
{
    return new Fdc8272 ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreateFdc8272)
