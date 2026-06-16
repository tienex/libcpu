/** @file
  PC speaker -- a LibCPU hardware-component bundle.

  The PC speaker is not addressed through an I/O port of its own. It is wired to two chips: the
  8253 PIT channel 2 produces a square wave whose frequency is 1193182 / divisor, and the 8255 PPI
  port B gates it -- bit 0 is the timer-2 gate (enable counting) and bit 1 is the speaker data line
  (route the timer output, or a steady level, to the cone). The cone is driven only when both bits
  are set. This component observes those lines through the ISignalSink capability (the PPI and PIT
  are ISignalSource) and reports the audible state; with no audio device it logs the tone instead.

  A COM object: IDevice + the ISignalSink capability.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IDevice.h"
#include <atomic>
#include <cstdio>

namespace LibCPU {

namespace {

enum { PortB_Gate = 0x01, PortB_Data = 0x02 };
enum { PitInputHz = 1193182 };                               // 8253 input clock (1.193182 MHz)

class Speaker : public IDevice, public ISignalSink {
public:
    Speaker () : m_Ref (1) {}

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        if (ppvObject == nullptr) { return E_POINTER; }
        if (CompareGuid (&riid, &IID_IUnknown) || CompareGuid (&riid, &IID_IDevice)) {
            *ppvObject = static_cast<IDevice *> (this);
        } else if (CompareGuid (&riid, &IID_ISignalSink)) {
            *ppvObject = static_cast<ISignalSink *> (this);
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

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override { return "PC Speaker"; }

    HRESULT STDMETHODCALLTYPE Configure (IN IDeviceNode * /*pNode*/) override { return Reset (); }

    HRESULT STDMETHODCALLTYPE Reset (THIS) override
    {
        m_Enabled = FALSE;
        m_Divisor = 0;
        return S_OK;
    }

    // ISignalSink: react to the gate/data lines (PPI port B) and the tone clock (PIT channel 2).
    HRESULT STDMETHODCALLTYPE OnSignal (UINT32 Line, UINT32 Value) override
    {
        if (Line == LCSignalPpiPortB) {
            BOOLEAN On = ((Value & PortB_Gate) && (Value & PortB_Data)) ? TRUE : FALSE;
            if (On != m_Enabled) {
                m_Enabled = On;
                Announce ();
            }
        } else if (Line == LCSignalPitCh2) {
            m_Divisor = Value;
            if (m_Enabled) { Announce (); }                  // re-tune while sounding
        }
        return S_OK;
    }

private:
    VOID Announce ()
    {
        if (m_Enabled) {
            UINT32 Div = (m_Divisor != 0) ? m_Divisor : 0x10000;   // a zero count means 65536 on the 8253
            std::printf ("[speaker] ON  ~%u Hz\n", (UINT32) PitInputHz / Div);
        } else {
            std::printf ("[speaker] off\n");
        }
    }

    std::atomic<INT32> m_Ref;
    BOOLEAN            m_Enabled = FALSE;
    UINT32            m_Divisor = 0;
};

} // anonymous namespace

IDevice *
CreateSpeaker ()
{
    return new Speaker ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreateSpeaker)
