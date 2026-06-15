/** @file
  Intel 8255 Programmable Peripheral Interface -- a LibCPU hardware-component bundle.

  On the PC/XT the 8255 (ports 0x60-0x63) reads the keyboard scan port (port A), drives
  keyboard/speaker control (port B), and presents the system configuration DIP switches
  (port C). Port B bit 3 selects which nibble of switch bank SW1 appears on port C, the way
  the XT BIOS reads the installed-equipment configuration.

  A COM object: IDevice + the IPortDevice capability.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IDevice.h"
#include <atomic>

namespace LibCPU {

namespace {

class Ppi8255 : public IDevice, public IPortDevice {
public:
    Ppi8255 () : m_Ref (1) {}

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

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override { return "Intel 8255 PPI"; }

    HRESULT STDMETHODCALLTYPE Configure (IN IDeviceNode *pNode) override
    {
        UINT32 Base = 0x60;
        m_Sw1 = 0x30;                                        // default: 80x25 MDA video, one floppy
        if (pNode != nullptr) {
            pNode->GetPropertyCell ("reg", 0, &Base);
            pNode->GetPropertyCell ("ibm,dip-sw1", 0, &m_Sw1);   // installed-equipment switches
        }
        m_Base = (UINT16) Base;
        return Reset ();
    }

    HRESULT STDMETHODCALLTYPE Reset (THIS) override { m_PortB = 0; m_Control = 0; return S_OK; }

    BOOLEAN STDMETHODCALLTYPE OwnsPort (UINT16 Port) override
    {
        return (Port >= m_Base && Port < (UINT16) (m_Base + 4)) ? TRUE : FALSE;
    }

    HRESULT STDMETHODCALLTYPE ReadPort (UINT16 Port, UINT32 /*Width*/, UINT32 *pValue) override
    {
        if (pValue == nullptr) { return E_POINTER; }
        switch (Port - m_Base) {
            case 0:  *pValue = m_KbScan; break;              // port A: keyboard scan code
            case 1:  *pValue = m_PortB;  break;              // port B: control read-back
            case 2:                                          // port C: DIP switches (B bit3 selects nibble)
                *pValue = (m_PortB & 0x08) ? (m_Sw1 >> 4) & 0x0F : m_Sw1 & 0x0F;
                break;
            default: *pValue = m_Control; break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE WritePort (UINT16 Port, UINT32 /*Width*/, UINT32 Value) override
    {
        switch (Port - m_Base) {
            case 1:  m_PortB   = (UINT8) Value; break;       // port B: keyboard enable / speaker / nibble select
            case 3:  m_Control = (UINT8) Value; break;       // control word
            default: break;                                  // ports A/C are inputs here
        }
        return S_OK;
    }

private:
    std::atomic<INT32> m_Ref;
    UINT16             m_Base    = 0x60;
    UINT32             m_Sw1     = 0x30;
    UINT8              m_KbScan  = 0;
    UINT8              m_PortB   = 0;
    UINT8              m_Control = 0;
};

} // anonymous namespace

IDevice *
CreatePpi8255 ()
{
    return new Ppi8255 ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreatePpi8255)
