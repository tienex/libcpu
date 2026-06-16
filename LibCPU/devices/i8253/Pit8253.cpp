/** @file
  Intel 8253 Programmable Interval Timer -- a LibCPU hardware-component bundle.

  The PC/XT wires the PIT's channel 0 to IRQ0 (the system timer tick). This component models
  the programmer-visible path: a control-word write (port base+3) selecting channel 0 begins a
  count load, the following writes to channel 0 (port base+0) load the reload count, and once
  loaded the channel is armed. An armed channel 0 raises IRQ0 through the IInterruptSource
  capability.

  A real PIT fires continuously at a rate set by its reload count; for a terminating demo this
  component fires a bounded number of ticks, taken from the device-tree node's optional
  "libcpu,ticks" property (so the machine description controls the heartbeat) -- after which the
  guest, finding no interrupt source, halts idle.

  It is a COM object: IDevice, plus the IPortDevice and IInterruptSource capabilities.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IDevice.h"
#include <atomic>
#include <vector>

namespace LibCPU {

namespace {

class Pit8253 : public IDevice, public IPortDevice, public IInterruptSource, public ISignalSource {
public:
    Pit8253 () : m_Ref (1) {}
    ~Pit8253 () { for (SINK_EDGE CONST &E : m_Sinks) { E.pSink->Release (); } }

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        if (ppvObject == nullptr) { return E_POINTER; }
        if (CompareGuid (&riid, &IID_IUnknown) || CompareGuid (&riid, &IID_IDevice)) {
            *ppvObject = static_cast<IDevice *> (this);
        } else if (CompareGuid (&riid, &IID_IPortDevice)) {
            *ppvObject = static_cast<IPortDevice *> (this);
        } else if (CompareGuid (&riid, &IID_IInterruptSource)) {
            *ppvObject = static_cast<IInterruptSource *> (this);
        } else if (CompareGuid (&riid, &IID_ISignalSource)) {
            *ppvObject = static_cast<ISignalSource *> (this);
        } else {
            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }
        AddRef ();
        return S_OK;
    }

    // ISignalSource: channel 2's reload divisor is the speaker/cassette tone clock. A sink is wired
    // to a specific line (here only LCSignalPitCh2); changes are pushed only on the wired line.
    HRESULT STDMETHODCALLTYPE ConnectSink (ISignalSink *pSink, UINT32 Line) override
    {
        if (pSink == nullptr) { return E_POINTER; }
        pSink->AddRef ();
        m_Sinks.push_back (SINK_EDGE { pSink, Line });
        return S_OK;
    }

    UINT32 STDMETHODCALLTYPE AddRef (THIS) override { return (UINT32) ++m_Ref; }
    UINT32 STDMETHODCALLTYPE Release (THIS) override
    {
        UINT32 Count = (UINT32) --m_Ref;
        if (Count == 0) { delete this; }
        return Count;
    }

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override { return "Intel 8253 PIT"; }

    HRESULT STDMETHODCALLTYPE Configure (IN IDeviceNode *pNode) override
    {
        UINT32 Base = 0x40;
        m_Ticks = 4;                                         // default heartbeat length
        if (pNode != nullptr) {
            pNode->GetPropertyCell ("reg", 0, &Base);
            pNode->GetPropertyCell ("libcpu,ticks", 0, &m_Ticks);   // machine-described tick budget
        }
        m_Base = (UINT16) Base;
        return Reset ();
    }

    HRESULT STDMETHODCALLTYPE Reset (THIS) override
    {
        m_Armed     = FALSE;
        m_Expect    = 0;
        m_Remaining = 0;
        m_Ch2Expect = 0;
        m_Ch2Count  = 0;
        return S_OK;
    }

    BOOLEAN STDMETHODCALLTYPE OwnsPort (UINT16 Port) override
    {
        return (Port >= m_Base && Port < (UINT16) (m_Base + 4)) ? TRUE : FALSE;
    }

    HRESULT STDMETHODCALLTYPE ReadPort (UINT16 /*Port*/, UINT32 /*Width*/, UINT32 *pValue) override
    {
        if (pValue == nullptr) { return E_POINTER; }
        *pValue = 0;                                         // count-read latch not modelled
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE WritePort (UINT16 Port, UINT32 /*Width*/, UINT32 Value) override
    {
        UINT16 Off = (UINT16) (Port - m_Base);
        if (Off == 3) {                                      // control word
            UINT32 Channel = (Value >> 6) & 0x3;
            if (Channel == 0) { m_Expect = 2; m_Armed = FALSE; }        // ch0: expect lo+hi count bytes
            else if (Channel == 2) { m_Ch2Expect = 2; m_Ch2Count = 0; } // ch2: lo+hi -> tone divisor
        } else if (Off == 0) {                               // channel-0 count load
            if (m_Expect > 0 && --m_Expect == 0) {
                m_Armed     = TRUE;
                m_Remaining = m_Ticks;                       // arm the bounded heartbeat
            }
        } else if (Off == 2) {                               // channel-2 count load (speaker/cassette clock)
            if (m_Ch2Expect == 2) { m_Ch2Count = Value & 0xFF; m_Ch2Expect = 1; }       // low byte
            else if (m_Ch2Expect == 1) {
                m_Ch2Count |= (Value & 0xFF) << 8;                                       // high byte
                m_Ch2Expect = 0;
                for (SINK_EDGE CONST &E : m_Sinks) {
                    if (E.Line == LCSignalPitCh2) { E.pSink->OnSignal (LCSignalPitCh2, m_Ch2Count); }
                }
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE PollInterrupt (OUT UINT32 *pIrq) override
    {
        if (pIrq == nullptr) { return E_POINTER; }
        if (m_Armed && m_Remaining > 0) {
            m_Remaining--;
            *pIrq = 0;                                       // channel 0 -> IRQ0
            return S_OK;
        }
        return S_FALSE;                                      // heartbeat exhausted
    }

private:
    struct SINK_EDGE { ISignalSink *pSink; UINT32 Line; };   // an explicitly wired (sink, line) edge

    std::atomic<INT32>         m_Ref;
    std::vector<SINK_EDGE>     m_Sinks;
    UINT16                     m_Base      = 0x40;
    UINT32                     m_Ticks     = 4;
    UINT32                     m_Remaining = 0;
    int                        m_Expect    = 0;
    BOOLEAN                    m_Armed     = FALSE;
    int                        m_Ch2Expect = 0;
    UINT32                     m_Ch2Count  = 0;
};

} // anonymous namespace

IDevice *
CreatePit8253 ()
{
    return new Pit8253 ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreatePit8253)
