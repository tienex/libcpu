/** @file
  Intel 8259A Programmable Interrupt Controller -- a LibCPU hardware-component bundle.

  The PC/XT has a single 8259A (master only) at I/O ports 0x20-0x21. This component models the
  programmer-visible behaviour: the ICW1..ICW4 initialization sequence (which sets the vector
  base), and the OCW1 interrupt-mask register. It is a COM object: it implements IDevice and,
  by also implementing IPortDevice, advertises that it decodes I/O ports -- the machine learns
  that capability through QueryInterface.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IDevice.h"
#include <atomic>

namespace LibCPU {

namespace {

// Initialization-word sequence state.
enum { InitIdle = 0, InitIcw2, InitIcw3, InitIcw4 };

class Pic8259 : public IDevice, public IPortDevice, public IInterruptController {
public:
    Pic8259 () : m_Ref (1) {}

    // --- IUnknown (shared across the interface vtables) ---
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        if (ppvObject == nullptr) { return E_POINTER; }
        if (CompareGuid (&riid, &IID_IUnknown) || CompareGuid (&riid, &IID_IDevice)) {
            *ppvObject = static_cast<IDevice *> (this);
        } else if (CompareGuid (&riid, &IID_IPortDevice)) {
            *ppvObject = static_cast<IPortDevice *> (this);
        } else if (CompareGuid (&riid, &IID_IInterruptController)) {
            *ppvObject = static_cast<IInterruptController *> (this);
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

    // --- IDevice ---
    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override { return "Intel 8259A PIC"; }

    HRESULT STDMETHODCALLTYPE Configure (IN IDeviceNode *pNode) override
    {
        UINT32 Base = 0x20;
        if (pNode != nullptr) { pNode->GetPropertyCell ("reg", 0, &Base); }   // I/O base from "reg"
        m_Base = (UINT16) Base;
        return Reset ();
    }

    HRESULT STDMETHODCALLTYPE Reset (THIS) override
    {
        m_Imr        = 0xFF;          // all lines masked at power-on
        m_VectorBase = 0x08;
        m_Init       = InitIdle;
        m_Single     = FALSE;
        m_NeedIcw4   = FALSE;
        return S_OK;
    }

    // --- IPortDevice ---
    BOOLEAN STDMETHODCALLTYPE OwnsPort (UINT16 Port) override
    {
        return (Port == m_Base || Port == (UINT16) (m_Base + 1)) ? TRUE : FALSE;
    }

    HRESULT STDMETHODCALLTYPE ReadPort (UINT16 Port, UINT32 /*Width*/, UINT32 *pValue) override
    {
        if (pValue == nullptr) { return E_POINTER; }
        // Command port reads IRR/ISR (none pending in this model); data port reads the IMR.
        *pValue = (Port == (UINT16) (m_Base + 1)) ? m_Imr : 0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE WritePort (UINT16 Port, UINT32 /*Width*/, UINT32 Value) override
    {
        UINT8 V = (UINT8) Value;
        if (Port == m_Base) {
            if (V & 0x10) {                                  // ICW1: begin initialization
                m_Single   = (V & 0x02) ? TRUE : FALSE;      // SNGL: no slave (the XT case)
                m_NeedIcw4 = (V & 0x01) ? TRUE : FALSE;      // IC4
                m_Imr      = 0;
                m_Init     = InitIcw2;
            }
            // Otherwise OCW2/OCW3 (EOI, read-register select): no observable state here yet.
            return S_OK;
        }
        // Data port (m_Base + 1): an ICW during init, otherwise OCW1 = the interrupt mask.
        switch (m_Init) {
            case InitIcw2:
                m_VectorBase = (UINT8) (V & 0xF8);           // ICW2: interrupt vector base
                m_Init = m_Single ? (m_NeedIcw4 ? InitIcw4 : InitIdle) : InitIcw3;
                break;
            case InitIcw3:
                m_Init = m_NeedIcw4 ? InitIcw4 : InitIdle;   // ICW3: cascade map (unused on XT)
                break;
            case InitIcw4:
                m_Init = InitIdle;                           // ICW4: mode (8086, EOI) -- accepted
                break;
            default:
                m_Imr = V;                                   // OCW1: interrupt mask register
                break;
        }
        return S_OK;
    }

    // --- IInterruptController: arbitrate a raised IRQ line ---
    HRESULT STDMETHODCALLTYPE AcceptInterrupt (UINT32 Irq, UINT32 *pVector) override
    {
        if (pVector == nullptr) { return E_POINTER; }
        if (Irq > 7 || (m_Imr & (1u << Irq)) != 0) { return S_FALSE; }   // masked (or out of range)
        *pVector = (UINT32) m_VectorBase + Irq;                          // ICW2 base + line
        return S_OK;
    }

private:
    std::atomic<INT32> m_Ref;
    UINT16             m_Base       = 0x20;
    UINT8              m_Imr        = 0xFF;
    UINT8              m_VectorBase = 0x08;
    int                m_Init       = InitIdle;
    BOOLEAN            m_Single     = FALSE;
    BOOLEAN            m_NeedIcw4   = FALSE;
};

} // anonymous namespace

IDevice *
CreatePic8259 ()
{
    return new Pic8259 ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreatePic8259)
