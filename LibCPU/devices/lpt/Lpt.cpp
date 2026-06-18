/** @file
  Standard Parallel Port (SPP / Centronics) -- a LibCPU hardware-component bundle.

  The PC parallel port presents three registers at its base (LPT1 = 0x378):

    +0  data    (R/W): the 8 data lines latched to the printer.
    +1  status  (read): nBUSY(7) nACK(6) PE(5) SELECT(4) nERROR(3); idle = ready/online/no-error.
    +2  control (R/W): STROBE(0) AUTOFEED(1) nINIT(2) SELECTIN(3) IRQEN(4).

  A byte is "printed" on the rising edge of STROBE: the latched data byte is appended to the
  captured output and echoed to the host console, so guest printer output is observable. This is
  the classic unidirectional SPP (no EPP/ECP); it models the programmer-visible registers and the
  strobe handshake, reporting the printer permanently ready.

  A COM object: IDevice + the IPortDevice capability.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IDevice.h"
#include <atomic>
#include <cstdio>
#include <vector>

namespace LibCPU {

namespace {

enum { Lpt_Data = 0, Lpt_Status = 1, Lpt_Control = 2 };

// Status register (read): a ready, selected, error-free printer. nBUSY=1 (not busy), nACK=1,
// PE=0 (paper present), SELECT=1 (online), nERROR=1 (no fault); low three bits read 0.
enum { Lpt_StatusReady = 0xD8 };
// Control register bits.
enum { Ctl_Strobe = 0x01 };

class Lpt : public IDevice, public IPortDevice {
public:
    Lpt () : m_Ref (1) {}

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

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override { return "Parallel Port (SPP)"; }

    HRESULT STDMETHODCALLTYPE Configure (IN IDeviceNode *pNode) override
    {
        UINT32 Base = 0x378;                                 // LPT1
        if (pNode != nullptr) { pNode->GetPropertyCell ("reg", 0, &Base); }
        m_Base = (UINT16) Base;
        return Reset ();
    }

    HRESULT STDMETHODCALLTYPE Reset (THIS) override
    {
        m_Data = 0; m_Control = 0; m_Printed.clear ();
        return S_OK;
    }

    BOOLEAN STDMETHODCALLTYPE OwnsPort (UINT16 Port) override
    {
        return (Port >= m_Base && Port < (UINT16) (m_Base + 3)) ? TRUE : FALSE;
    }

    HRESULT STDMETHODCALLTYPE ReadPort (UINT16 Port, UINT32 /*Width*/, UINT32 *pValue) override
    {
        if (pValue == nullptr) { return E_POINTER; }
        switch (Port - m_Base) {
            case Lpt_Data:    *pValue = m_Data; break;        // data lines read back
            case Lpt_Status:  *pValue = Lpt_StatusReady; break;
            case Lpt_Control: *pValue = m_Control; break;
            default:          *pValue = 0; break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE WritePort (UINT16 Port, UINT32 /*Width*/, UINT32 Value) override
    {
        UINT8 V = (UINT8) Value;
        switch (Port - m_Base) {
            case Lpt_Data:
                m_Data = V;
                break;
            case Lpt_Control: {
                bool WasStrobe = (m_Control & Ctl_Strobe) != 0;
                m_Control = V;
                if (!WasStrobe && (V & Ctl_Strobe)) { Print (m_Data); }   // rising edge of STROBE
                break;
            }
            default:
                break;
        }
        return S_OK;
    }

private:
    VOID Print (UINT8 Byte)
    {
        m_Printed.push_back (Byte);
        std::printf ("[lpt] %c", (Byte >= 0x20 && Byte < 0x7F) ? (char) Byte : '.');
        std::fflush (stdout);
    }

    std::atomic<INT32> m_Ref;
    UINT16             m_Base    = 0x378;
    UINT8              m_Data    = 0;
    UINT8              m_Control = 0;
    std::vector<UINT8> m_Printed;                            // captured printer output
};

} // anonymous namespace

IDevice *
CreateLpt ()
{
    return new Lpt ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreateLpt)
