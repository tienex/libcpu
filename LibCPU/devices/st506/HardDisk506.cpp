/** @file
  IBM/Xebec ST-506 fixed-disk controller (PC/XT hard-disk adapter) -- a LibCPU component bundle.

  The XT fixed-disk adapter (a Xebec 1210 design) presents four I/O ports at 0x320-0x323: the data
  register (0x320, the command/status/sense byte stream), the controller status register (0x321,
  read: BUSY/BSY, C/D, I/O, REQ; write: controller reset), the controller-select / DIP register
  (0x322) and the DMA / interrupt mask register (0x323). This component models the programmer-visible
  registers; with no drive attached it reports the controller idle so a polling BIOS does not block.
  It performs no actual disk transfers yet.

  A COM object: IDevice + the IPortDevice capability.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IDevice.h"
#include <atomic>

namespace LibCPU {

namespace {

enum { Hdc_Data = 0, Hdc_Status = 1, Hdc_Select = 2, Hdc_Mask = 3 };

// Controller status register (0x321) bit field.
enum { Sts_Req = 0x01, Sts_InputOutput = 0x02, Sts_CmdData = 0x04, Sts_Busy = 0x08, Sts_Drq = 0x10 };

class HardDisk506 : public IDevice, public IPortDevice {
public:
    HardDisk506 () : m_Ref (1) {}

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

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override { return "Xebec ST-506 HDC"; }

    HRESULT STDMETHODCALLTYPE Configure (IN IDeviceNode *pNode) override
    {
        UINT32 Base = 0x320;
        if (pNode != nullptr) { pNode->GetPropertyCell ("reg", 0, &Base); }
        m_Base = (UINT16) Base;
        return Reset ();
    }

    HRESULT STDMETHODCALLTYPE Reset (THIS) override { m_Mask = 0; return S_OK; }

    BOOLEAN STDMETHODCALLTYPE OwnsPort (UINT16 Port) override
    {
        return (Port >= m_Base && Port < (UINT16) (m_Base + 4)) ? TRUE : FALSE;
    }

    HRESULT STDMETHODCALLTYPE ReadPort (UINT16 Port, UINT32 /*Width*/, UINT32 *pValue) override
    {
        if (pValue == nullptr) { return E_POINTER; }
        switch (Port - m_Base) {
            case Hdc_Status: *pValue = 0; break;              // idle: not BUSY, no REQ (no drive)
            case Hdc_Data:   *pValue = 0; break;              // sense/result stream empty
            case Hdc_Mask:   *pValue = m_Mask; break;
            default:         *pValue = 0; break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE WritePort (UINT16 Port, UINT32 /*Width*/, UINT32 Value) override
    {
        if ((Port - m_Base) == Hdc_Mask) { m_Mask = (UINT8) Value; }   // DMA/IRQ enable latch
        // Data (0x320), reset (0x321) and controller-select (0x322) writes are accepted and discarded.
        return S_OK;
    }

private:
    std::atomic<INT32> m_Ref;
    UINT16             m_Base = 0x320;
    UINT8              m_Mask = 0;
};

} // anonymous namespace

IDevice *
CreateHardDisk506 ()
{
    return new HardDisk506 ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreateHardDisk506)
