/** @file
  IBM PC (5150) cassette interface -- a LibCPU hardware-component bundle.

  The original IBM PC stored programs on audio cassette; the XT (5160) dropped it. Like the speaker
  the cassette has no I/O port of its own -- it is wired to the 8255 PPI and the 8253 PIT: port B
  bit 3 drives the cassette-motor relay (active low, 0 = motor running), PIT channel 2 clocks the
  write-data line, and the read-data comparator returns on PPI port C bit 4. This component observes
  the motor-relay line through the ISignalSink capability (the PPI is ISignalSource) and reports the
  transport state; it does not yet read or write a tape image.

  A COM object: IDevice + the ISignalSink capability.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IDevice.h"
#include <atomic>
#include <cstdio>

namespace LibCPU {

namespace {

enum { PortB_MotorOff = 0x08 };                              // port B bit 3: 1 = motor off, 0 = motor on

class Cassette : public IDevice, public ISignalSink {
public:
    Cassette () : m_Ref (1) {}

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

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override { return "Cassette interface"; }

    HRESULT STDMETHODCALLTYPE Configure (IN IDeviceNode * /*pNode*/) override { return Reset (); }

    HRESULT STDMETHODCALLTYPE Reset (THIS) override { m_MotorOn = FALSE; return S_OK; }

    // ISignalSink: the cassette-motor relay is PPI port B bit 3 (active low).
    HRESULT STDMETHODCALLTYPE OnSignal (UINT32 Line, UINT32 Value) override
    {
        if (Line == LCSignalPpiPortB) {
            BOOLEAN On = (Value & PortB_MotorOff) ? FALSE : TRUE;
            if (On != m_MotorOn) {
                m_MotorOn = On;
                std::printf ("[cassette] motor %s\n", m_MotorOn ? "ON" : "OFF");
            }
        }
        return S_OK;
    }

private:
    std::atomic<INT32> m_Ref;
    BOOLEAN            m_MotorOn = FALSE;
};

} // anonymous namespace

IDevice *
CreateCassette ()
{
    return new Cassette ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreateCassette)
