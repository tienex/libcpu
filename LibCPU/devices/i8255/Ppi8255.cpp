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
#include <vector>

namespace LibCPU {

namespace {

class Ppi8255 : public IDevice, public IPortDevice, public ISignalSource {
public:
    Ppi8255 () : m_Ref (1) {}
    ~Ppi8255 () { for (SINK_EDGE CONST &E : m_Sinks) { E.pSink->Release (); } }

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        if (ppvObject == nullptr) { return E_POINTER; }
        if (CompareGuid (&riid, &IID_IUnknown) || CompareGuid (&riid, &IID_IDevice)) {
            *ppvObject = static_cast<IDevice *> (this);
        } else if (CompareGuid (&riid, &IID_IPortDevice)) {
            *ppvObject = static_cast<IPortDevice *> (this);
        } else if (CompareGuid (&riid, &IID_ISignalSource)) {
            *ppvObject = static_cast<ISignalSource *> (this);
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

    // ISignalSource: the speaker and cassette tap port B's gate/data/motor bits. A sink is wired to
    // a specific line (here only LCSignalPpiPortB); changes are pushed only on the wired line.
    HRESULT STDMETHODCALLTYPE ConnectSink (ISignalSink *pSink, UINT32 Line) override
    {
        if (pSink == nullptr) { return E_POINTER; }
        pSink->AddRef ();
        m_Sinks.push_back (SINK_EDGE { pSink, Line });
        return S_OK;
    }

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
            case 1:                                          // port B: keyboard enable / speaker / motor / nibble select
                m_PortB = (UINT8) Value;
                for (SINK_EDGE CONST &E : m_Sinks) {
                    if (E.Line == LCSignalPpiPortB) { E.pSink->OnSignal (LCSignalPpiPortB, m_PortB); }
                }
                break;
            case 3:  m_Control = (UINT8) Value; break;       // control word
            default: break;                                  // ports A/C are inputs here
        }
        return S_OK;
    }

private:
    struct SINK_EDGE { ISignalSink *pSink; UINT32 Line; };   // an explicitly wired (sink, line) edge

    std::atomic<INT32>        m_Ref;
    std::vector<SINK_EDGE>    m_Sinks;
    UINT16                    m_Base    = 0x60;
    UINT32                    m_Sw1     = 0x30;
    UINT8                     m_KbScan  = 0;
    UINT8                     m_PortB   = 0;
    UINT8                     m_Control = 0;
};

} // anonymous namespace

IDevice *
CreatePpi8255 ()
{
    return new Ppi8255 ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreatePpi8255)
