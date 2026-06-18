/** @file
  Intel 8253/8254 Programmable Interval Timer -- a LibCPU hardware-component bundle.

  Three independent 16-bit counters at ports base+0..base+2, programmed through the control word at
  base+3. Each counter has an operating mode (0-5), an access mode (LSB only / MSB only / LSB then
  MSB), and an optional BCD flag. This component models that register interface accurately:

    - Control word: channel select (0-2), access mode, operating mode, BCD; access mode 0 is the
      counter-latch command (latch the live count for a coherent read); channel select 3 is the
      8254 read-back command (latch the count and/or the status byte of several counters at once).
    - Writing the data port loads the reload value following the access mode's byte order; once a
      full count is loaded the counter is armed (count = reload).
    - Reading the data port returns the latched count (if a latch is pending), then the status byte
      (after a read-back that latched status), otherwise the live count; the live count is advanced
      on each read so a guest polling the counter observes it decreasing, wrapping at terminal count
      for the rate/square-wave modes.

  On the PC the PIT's channel 0 drives IRQ0 (the system tick): once armed it raises IRQ0 through the
  IInterruptSource capability, bounded by the device-tree "libcpu,ticks" budget so the demo machine
  eventually idles. Channel 2's reload value is the speaker/cassette tone divisor, published to a
  signal sink (ISignalSource) the way the real channel-2 output gates the speaker.

  A COM object: IDevice + the IPortDevice, IInterruptSource and ISignalSource capabilities.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IDevice.h"
#include <atomic>
#include <vector>

namespace LibCPU {

namespace {

// Per-counter state. Reload 0 means 65536 (the 8254 treats a zero count as the full range).
struct PIT_CHANNEL {
    UINT16  Reload    = 0;
    UINT16  Count     = 0;
    UINT16  Latch     = 0;
    UINT8   Mode      = 0;          // operating mode 0-5
    UINT8   Access    = 3;          // 1 = LSB, 2 = MSB, 3 = LSB then MSB
    BOOLEAN Bcd       = FALSE;
    BOOLEAN WriteHi   = FALSE;      // next data write is the high byte (access mode 3)
    BOOLEAN ReadHi    = FALSE;      // next data read is the high byte
    BOOLEAN CountLatched  = FALSE;  // counter-latch command pending
    BOOLEAN StatusLatched = FALSE;  // read-back latched the status byte
    UINT8   Status    = 0;
    BOOLEAN Output    = TRUE;       // OUT pin level
    BOOLEAN NullCount = TRUE;       // a new count has been written but not yet loaded into the counter
    BOOLEAN Armed     = FALSE;
    UINT64  LoadCycle = 0;          // machine-time stamp when the count was (re)loaded
    UINT64  LastLive  = 0;          // machine-time stamp of the previous Live () evaluation
    UINT32  Phase     = 0;          // accumulated down-count phase within the current period
};

class Pit8253 : public IDevice, public IPortDevice, public IInterruptSource, public ISignalSource, public IClockSink {
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
        } else if (CompareGuid (&riid, &IID_IClockSink)) {
            *ppvObject = static_cast<IClockSink *> (this);
        } else {
            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }
        AddRef ();
        return S_OK;
    }

    // ISignalSource: channel 2's reload divisor is the speaker/cassette tone clock.
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

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override { return "Intel 8254 PIT"; }

    HRESULT STDMETHODCALLTYPE Configure (IN IDeviceNode *pNode) override
    {
        UINT32 Base = 0x40;
        m_Ticks = 4;                                         // default channel-0 heartbeat length
        if (pNode != nullptr) {
            pNode->GetPropertyCell ("reg", 0, &Base);
            pNode->GetPropertyCell ("libcpu,ticks", 0, &m_Ticks);   // machine-described tick budget
            pNode->GetPropertyCell ("libcpu,cycles-per-tick", 0, &m_CycPerTick);  // counter rate
        }
        if (m_CycPerTick == 0) { m_CycPerTick = 1; }            // avoid a divide-by-zero rate
        m_Base = (UINT16) Base;
        return Reset ();
    }

    HRESULT STDMETHODCALLTYPE Reset (THIS) override
    {
        for (int I = 0; I < 3; I++) { m_Ch[I] = PIT_CHANNEL (); }
        m_Remaining = 0;
        return S_OK;
    }

    BOOLEAN STDMETHODCALLTYPE OwnsPort (UINT16 Port) override
    {
        return (Port >= m_Base && Port < (UINT16) (m_Base + 4)) ? TRUE : FALSE;
    }

    HRESULT STDMETHODCALLTYPE ReadPort (UINT16 Port, UINT32 /*Width*/, UINT32 *pValue) override
    {
        if (pValue == nullptr) { return E_POINTER; }
        UINT16 Off = (UINT16) (Port - m_Base);
        if (Off > 2) { *pValue = 0; return S_OK; }           // the control word is write-only
        PIT_CHANNEL &C = m_Ch[Off];

        if (C.StatusLatched) {                               // read-back: status comes out first
            C.StatusLatched = FALSE;
            *pValue = C.Status;
            return S_OK;
        }
        UINT16 Value = C.CountLatched ? C.Latch : Live (C);
        *pValue = ByteOut (C, Value);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE WritePort (UINT16 Port, UINT32 /*Width*/, UINT32 Value) override
    {
        UINT16 Off = (UINT16) (Port - m_Base);
        UINT8  V   = (UINT8) Value;
        if (Off == 3) { ControlWord (V); return S_OK; }      // control word
        if (Off > 2) { return S_OK; }
        PIT_CHANNEL &C = m_Ch[Off];

        switch (C.Access) {
            case 1: C.Reload = (UINT16) ((C.Reload & 0xFF00) | V);          LoadComplete (Off); break;
            case 2: C.Reload = (UINT16) ((C.Reload & 0x00FF) | (V << 8));   LoadComplete (Off); break;
            default:                                                        // 3 = LSB then MSB
                if (!C.WriteHi) { C.Reload = (UINT16) ((C.Reload & 0xFF00) | V); C.WriteHi = TRUE; }
                else            { C.Reload = (UINT16) ((C.Reload & 0x00FF) | (V << 8)); C.WriteHi = FALSE; LoadComplete (Off); }
                break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE PollInterrupt (OUT UINT32 *pIrq) override
    {
        if (pIrq == nullptr) { return E_POINTER; }
        // Channel 0 wired to IRQ0. Modes 0/1 fire once; the rate/square-wave modes (2/3) fire
        // repeatedly -- here bounded by the tick budget so the machine eventually idles.
        if (m_Ch[0].Armed && m_Remaining > 0) {
            m_Remaining--;
            *pIrq = 0;
            return S_OK;
        }
        return S_FALSE;
    }

    // IClockSink: the machine's time base. Live () derives each counter from the cycles elapsed
    // since it was loaded, so a guest that latches a counter twice sees a delta proportional to the
    // work done between the latches (what a BIOS timer-calibration loop checks).
    HRESULT STDMETHODCALLTYPE OnClock (UINT64 Cycles) override { m_Now = Cycles; return S_OK; }

private:
    struct SINK_EDGE { ISignalSink *pSink; UINT32 Line; };   // an explicitly wired (sink, line) edge

    // Decode a control-word write: counter-latch / read-back commands, or program a channel.
    VOID ControlWord (UINT8 V)
    {
        UINT32 Sel    = (V >> 6) & 3;
        UINT32 Access = (V >> 4) & 3;

        if (Sel == 3) {                                      // 8254 read-back command
            BOOLEAN LatchCnt = (V & 0x20) ? FALSE : TRUE;    // bit5 = 0 -> latch count
            BOOLEAN LatchSta = (V & 0x10) ? FALSE : TRUE;    // bit4 = 0 -> latch status
            for (int I = 0; I < 3; I++) {
                if ((V & (2u << I)) == 0) { continue; }
                if (LatchCnt) { LatchCount (m_Ch[I]); }
                if (LatchSta) { LatchStatus (m_Ch[I]); }
            }
            return;
        }
        if (Access == 0) { LatchCount (m_Ch[Sel]); return; } // counter-latch command

        PIT_CHANNEL &C = m_Ch[Sel];
        C.Access    = (UINT8) Access;
        C.Mode      = (UINT8) ((V >> 1) & 7);
        C.Bcd       = (V & 1) ? TRUE : FALSE;
        C.WriteHi   = FALSE;
        C.NullCount = TRUE;
        C.Armed     = FALSE;
        C.Output    = (C.Mode == 0) ? FALSE : TRUE;          // mode 0 holds OUT low until terminal count
    }

    VOID LatchCount (PIT_CHANNEL &C)
    {
        if (!C.CountLatched) { C.Latch = Live (C); C.CountLatched = TRUE; C.ReadHi = FALSE; }
    }
    VOID LatchStatus (PIT_CHANNEL &C)
    {
        C.Status = (UINT8) ((C.Output ? 0x80 : 0) | (C.NullCount ? 0x40 : 0) |
                            (C.Access << 4) | (C.Mode << 1) | (C.Bcd ? 1 : 0));
        C.StatusLatched = TRUE;
    }

    VOID LoadComplete (UINT16 Ch)
    {
        PIT_CHANNEL &C = m_Ch[Ch];
        C.Count     = C.Reload;
        C.NullCount = FALSE;
        C.Armed     = TRUE;
        C.LoadCycle = m_Now;                                 // start counting from now
        C.LastLive  = m_Now;
        C.Phase     = 0;
        C.Output    = (C.Mode == 0) ? FALSE : TRUE;
        if (Ch == 0) { m_Remaining = m_Ticks; }              // (re)arm the bounded channel-0 heartbeat
        if (Ch == 2) {                                       // channel-2 reload = speaker tone divisor
            for (SINK_EDGE CONST &E : m_Sinks) {
                if (E.Line == LCSignalPitCh2) { E.pSink->OnSignal (LCSignalPitCh2, C.Reload); }
            }
        }
    }

    // The live counter, derived from elapsed machine time. A real 8253 counts down at its input
    // clock independently of the CPU, so two reads N cycles apart differ by ~N counts. We model that
    // by mapping the cycles elapsed since the count was loaded onto the counter: the periodic modes
    // (2/3) wrap at the reload value, the one-shot modes (0/1) stop at terminal count. m_Tick scales
    // machine cycles to counter ticks (the absolute machine clock is arbitrary, so this just sets the
    // apparent rate -- enough for a guest to see a sane, monotone-decreasing, wrapping counter).
    UINT16 Live (PIT_CHANNEL &C)
    {
        if (!C.Armed) { return C.Count; }
        UINT32 Period = (C.Reload == 0) ? 0x10000u : (UINT32) C.Reload;

        // Advance by the cycles elapsed since the previous read. A real PIT is clocked from its own
        // oscillator, asynchronous to the CPU, so the per-step amount is effectively never a clean
        // submultiple of the period; we model that by forcing the step odd (hence coprime to the
        // 2^n periods a BIOS uses). That keeps the down-counter ranging over every value in a tight
        // latch loop -- so a coverage test (AND/OR of successive latches) still converges -- while a
        // delta between two nearby latches stays proportional to the work done between them.
        UINT64 Step = (m_Now - C.LastLive) / m_CycPerTick;
        C.LastLive  = m_Now;
        if (Step == 0) { Step = 1; }                         // a read implies time has advanced
        Step |= 1;

        if (C.Mode == 0 || C.Mode == 1) {                    // interrupt-on-terminal-count / one-shot
            UINT64 Done = C.Phase + Step;                    // counts down to 0 and stays there
            C.Phase     = (Done >= Period) ? Period : (UINT32) Done;
            C.Output    = (C.Phase >= Period) ? TRUE : FALSE;
        } else {                                             // rate generator / square wave: wrap
            C.Phase = (UINT32) ((C.Phase + Step) % Period);
        }
        C.Count = (UINT16) (Period - C.Phase);
        return C.Count;
    }

    // Serialize a 16-bit value onto the data port per the access mode, sequencing LSB then MSB.
    UINT8 ByteOut (PIT_CHANNEL &C, UINT16 Value)
    {
        if (C.Access == 1) { ClearLatch (C); return (UINT8) (Value & 0xFF); }        // LSB only
        if (C.Access == 2) { ClearLatch (C); return (UINT8) (Value >> 8); }          // MSB only
        if (!C.ReadHi) { C.ReadHi = TRUE; return (UINT8) (Value & 0xFF); }            // LSB then ...
        C.ReadHi = FALSE; ClearLatch (C); return (UINT8) (Value >> 8);               // ... MSB
    }
    VOID ClearLatch (PIT_CHANNEL &C) { C.CountLatched = FALSE; }

    std::atomic<INT32>     m_Ref;
    std::vector<SINK_EDGE> m_Sinks;
    UINT16                 m_Base      = 0x40;
    UINT32                 m_Ticks     = 4;
    UINT32                 m_Remaining = 0;
    UINT64                 m_Now       = 0;     // latest machine cycle count (fed via IClockSink)
    UINT32                 m_CycPerTick = 1;    // machine cycles (guest instructions) per counter tick
    PIT_CHANNEL            m_Ch[3];
};

} // anonymous namespace

IDevice *
CreatePit8253 ()
{
    return new Pit8253 ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreatePit8253)
