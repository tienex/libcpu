/** @file
  National/INS 8250 UART (PC/XT serial port, COM1 at 0x3F8) -- a LibCPU hardware-component
  bundle. A byte written to the transmit-holding register is emitted to host stdout, so a guest
  that drives this port produces visible output -- the simplest way to see the assembled machine
  actually execute. The line-status register always reports the transmitter ready.

  Like every component it is a COM object: IDevice plus the IPortDevice capability.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IDevice.h"
#include <atomic>
#include <cstdio>

namespace LibCPU {

namespace {

// 8250 register offsets from the I/O base.
enum { Reg_ThrRbr = 0, Reg_IerDlm = 1, Reg_IirFcr = 2, Reg_Lcr = 3, Reg_Mcr = 4, Reg_Lsr = 5 };
enum { Lcr_Dlab = 0x80, Lsr_ThrEmpty = 0x20, Lsr_TxEmpty = 0x40 };

class Uart8250 : public IDevice, public IPortDevice {
public:
    Uart8250 () : m_Ref (1) {}

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

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override { return "INS8250 UART (COM1)"; }

    HRESULT STDMETHODCALLTYPE Configure (IN IDeviceNode *pNode) override
    {
        UINT32 Base = 0x3F8;
        if (pNode != nullptr) { pNode->GetPropertyCell ("reg", 0, &Base); }
        m_Base = (UINT16) Base;
        return Reset ();
    }

    HRESULT STDMETHODCALLTYPE Reset (THIS) override { m_Lcr = 0; return S_OK; }

    BOOLEAN STDMETHODCALLTYPE OwnsPort (UINT16 Port) override
    {
        return (Port >= m_Base && Port < (UINT16) (m_Base + 8)) ? TRUE : FALSE;
    }

    HRESULT STDMETHODCALLTYPE ReadPort (UINT16 Port, UINT32 /*Width*/, UINT32 *pValue) override
    {
        if (pValue == nullptr) { return E_POINTER; }
        UINT16 Off = (UINT16) (Port - m_Base);
        // The transmitter is always ready, so a polled driver never blocks.
        *pValue = (Off == Reg_Lsr) ? (UINT32) (Lsr_ThrEmpty | Lsr_TxEmpty) : 0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE WritePort (UINT16 Port, UINT32 /*Width*/, UINT32 Value) override
    {
        UINT16 Off = (UINT16) (Port - m_Base);
        if (Off == Reg_Lcr) {
            m_Lcr = (UINT8) Value;                           // track DLAB (divisor-latch access)
        } else if (Off == Reg_ThrRbr && (m_Lcr & Lcr_Dlab) == 0) {
            std::fputc ((int) (Value & 0xFF), stdout);       // transmit holding register -> host stdout
            std::fflush (stdout);
        }
        // Divisor-latch and other register writes are accepted and ignored.
        return S_OK;
    }

private:
    std::atomic<INT32> m_Ref;
    UINT16             m_Base = 0x3F8;
    UINT8              m_Lcr  = 0;
};

} // anonymous namespace

IDevice *
CreateUart8250 ()
{
    return new Uart8250 ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreateUart8250)
