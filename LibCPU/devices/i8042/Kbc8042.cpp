/** @file
  Intel 8042 keyboard controller (KBC) -- a LibCPU hardware-component bundle.

  On the PC/AT and PS/2 the 8042 replaces the XT's 8255 PPI for keyboard duties. It presents two
  ports: the data port 0x60 and the command/status port 0x64 (read = status register, write =
  controller command). The status register's bit 0 (OBF, output-buffer full) tells the CPU a byte
  is waiting at 0x60; bit 1 (IBF) and bit 3 (the command/data flag) complete the handshake.

  This component models the controller command set the BIOS exercises at power-on: self-test
  (0xAA -> 0x55), interface test (0xAB -> 0x00), read/write the controller command byte (0x20 /
  0x60), enable/disable the keyboard (0xAE / 0xAD), and read/write the output port (0xD0 / 0xD1) --
  whose bit 1 is the famous A20 gate. Bytes written to 0x60 are either command data (when a command
  is pending) or keyboard commands, which the device acknowledges with 0xFA. A queued scan code
  (optionally seeded from the device-tree "libcpu,keystroke" property) is delivered through the
  IInterruptSource capability as IRQ1.

  A COM object: IDevice + the IPortDevice and IInterruptSource capabilities.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IDevice.h"
#include "LibCPU/CpuState.h"
#include <atomic>
#include <deque>
#include <mutex>

namespace LibCPU {

namespace {

enum { Kbc_Data = 0x60, Kbc_Cmd = 0x64 };
// Status register (0x64 read) bits.
enum { Sts_Obf = 0x01, Sts_Ibf = 0x02, Sts_Sys = 0x04, Sts_Cmd = 0x08, Sts_Inh = 0x10 };

class Kbc8042 : public IDevice, public IPortDevice, public IInterruptSource, public IKeyboardSink {
public:
    Kbc8042 () : m_Ref (1) {}

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        if (ppvObject == nullptr) { return E_POINTER; }
        if (CompareGuid (&riid, &IID_IUnknown) || CompareGuid (&riid, &IID_IDevice)) {
            *ppvObject = static_cast<IDevice *> (this);
        } else if (CompareGuid (&riid, &IID_IPortDevice)) {
            *ppvObject = static_cast<IPortDevice *> (this);
        } else if (CompareGuid (&riid, &IID_IInterruptSource)) {
            *ppvObject = static_cast<IInterruptSource *> (this);
        } else if (CompareGuid (&riid, &IID_IKeyboardSink)) {
            *ppvObject = static_cast<IKeyboardSink *> (this);
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

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override { return "Intel 8042 KBC"; }

    HRESULT STDMETHODCALLTYPE Configure (IN IDeviceNode *pNode) override
    {
        UINT32 Seed = 0;
        if (pNode != nullptr) { pNode->GetPropertyCell ("libcpu,keystroke", 0, &Seed); }   // demo scan code
        if (Seed != 0) { PushScanCode ((UINT8) Seed); }                                    // queue it like a keypress
        return Reset ();
    }

    HRESULT STDMETHODCALLTYPE Reset (THIS) override
    {
        m_Status  = Sts_Sys;                                 // system flag set after a successful POST
        m_Output  = 0;
        m_Pending = 0;
        m_CmdByte = 0x01;                                    // IRQ1 enabled (typical post-init default)
        m_OutPort = 0x02;                                    // bit1 = A20 gate enabled
        return S_OK;
    }

    BOOLEAN STDMETHODCALLTYPE OwnsPort (UINT16 Port) override
    {
        return (Port == Kbc_Data || Port == Kbc_Cmd) ? TRUE : FALSE;
    }

    HRESULT STDMETHODCALLTYPE ReadPort (UINT16 Port, UINT32 /*Width*/, UINT32 *pValue) override
    {
        if (pValue == nullptr) { return E_POINTER; }
        if (Port == Kbc_Cmd) {
            *pValue = m_Status;                              // status register
        } else {                                             // data port: hand over the output byte
            *pValue = m_Output;
            m_Status &= (UINT8) ~Sts_Obf;                    // reading clears output-buffer-full
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE WritePort (UINT16 Port, UINT32 /*Width*/, UINT32 Value) override
    {
        UINT8 V = (UINT8) Value;
        if (Port == Kbc_Cmd) {
            m_Status |= Sts_Cmd;                             // a command (not data) was written
            switch (V) {
                case 0xAA: SetOutput (0x55); break;          // controller self-test -> 0x55
                case 0xAB: SetOutput (0x00); break;          // keyboard-interface test -> 0x00 (ok)
                case 0x20: SetOutput (m_CmdByte); break;     // read controller command byte
                case 0x60: m_Pending = 0x60; break;          // write controller command byte (data next)
                case 0xD0: SetOutput (m_OutPort); break;     // read output port
                case 0xD1: m_Pending = 0xD1; break;          // write output port (A20 etc., data next)
                case 0xAD: m_Status |= Sts_Inh; break;       // disable keyboard
                case 0xAE: m_Status &= (UINT8) ~Sts_Inh; break;  // enable keyboard
                default:   break;                            // 0xFE (pulse reset) and others: no-op here
            }
        } else {                                             // write to the data port
            m_Status &= (UINT8) ~Sts_Cmd;
            if (m_Pending == 0x60)      { m_CmdByte = V; m_Pending = 0; }   // controller command byte
            else if (m_Pending == 0xD1) { m_OutPort = V; m_Pending = 0; }   // output port (bit1 = A20)
            else                        { SetOutput (0xFA); }               // keyboard command -> ACK
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE PollInterrupt (OUT UINT32 *pIrq) override
    {
        if (pIrq == nullptr) { return E_POINTER; }
        // Deliver the next queued scan code: load it into the output buffer and raise IRQ1. While
        // the buffer is full (CPU has not read it) or the queue is empty, assert nothing -- bounded,
        // so the machine can go idle when nobody is typing.
        if ((m_Status & Sts_Obf) != 0) { return S_FALSE; }
        std::lock_guard<std::mutex> Lock (m_QueueMtx);
        if (m_Queue.empty ()) { return S_FALSE; }
        SetOutput (m_Queue.front ());
        m_Queue.pop_front ();
        *pIrq = 1;                                           // keyboard -> IRQ1
        return S_OK;
    }

    // IKeyboardSink: a console front end queues a raw scan code as the user types. Thread-safe so
    // it may be called from a console thread while the machine thread polls.
    HRESULT STDMETHODCALLTYPE PushScanCode (UINT8 ScanCode) override
    {
        std::lock_guard<std::mutex> Lock (m_QueueMtx);
        m_Queue.push_back (ScanCode);
        return S_OK;
    }

private:
    VOID SetOutput (UINT8 V) { m_Output = V; m_Status |= Sts_Obf; }

    std::atomic<INT32> m_Ref;
    std::mutex         m_QueueMtx;                           // guards m_Queue (console vs machine thread)
    std::deque<UINT8>  m_Queue;                              // pending scan codes awaiting delivery
    UINT8              m_Status  = 0;
    UINT8              m_Output  = 0;
    UINT8              m_Pending = 0;                        // controller command awaiting its data byte
    UINT8              m_CmdByte = 0x01;
    UINT8              m_OutPort = 0x02;
};

} // anonymous namespace

IDevice *
CreateKbc8042 ()
{
    return new Kbc8042 ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreateKbc8042)
