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
        m_Irr        = 0;             // no requests pending
        m_Isr        = 0;             // nothing in service
        m_VectorBase = 0x08;
        m_Init       = InitIdle;
        m_Single     = FALSE;
        m_NeedIcw4   = FALSE;
        m_ReadIsr    = FALSE;
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
        // Data port reads the IMR; command port reads the IRR or ISR per the last OCW3 selection.
        if (Port == (UINT16) (m_Base + 1)) { *pValue = m_Imr; }
        else                               { *pValue = m_ReadIsr ? m_Isr : m_Irr; }
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
            // OCW3 (bit3 set): select whether the command port reads the IRR or the ISR.
            if ((V & 0x18) == 0x08) { m_ReadIsr = (V & 0x03) == 0x03 ? TRUE : FALSE; return S_OK; }
            // OCW2: end-of-interrupt. Specific EOI (0x60 | level) clears that level; non-specific
            // EOI (0x20) clears the highest-priority (lowest-numbered) in-service line.
            if (V & 0x20) {
                if (V & 0x40) { m_Isr = (UINT8) (m_Isr & ~(1u << (V & 7))); }   // specific EOI
                else {                                                          // non-specific EOI
                    for (int I = 0; I < 8; I++) {
                        if (m_Isr & (1u << I)) { m_Isr = (UINT8) (m_Isr & ~(1u << I)); break; }
                    }
                }
            }
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
        m_Irr = (UINT8) (m_Irr | (1u << Irq));                          // line requests service
        // Priority: a line is acknowledged only if no equal-or-higher-priority line (IRQ0 is
        // highest) is already in service. Bits 0..Irq of the ISR cover those priorities.
        if ((m_Isr & (((1u << Irq) << 1) - 1)) != 0) { return S_FALSE; }
        m_Irr = (UINT8) (m_Irr & ~(1u << Irq));                         // request granted -> in service
        m_Isr = (UINT8) (m_Isr | (1u << Irq));
        *pVector = (UINT32) m_VectorBase + Irq;                         // ICW2 base + line
        return S_OK;
    }

    // Read-only test (no IRR/ISR side effect) of whether a line would be acknowledged right now --
    // not masked, and no equal-or-higher-priority line already in service. Used by the bus to gate
    // the slave's cascade onto the master's IRQ2 before committing the slave acknowledge.
    BOOLEAN STDMETHODCALLTYPE CanAccept (UINT32 Irq) override
    {
        if (Irq > 7 || (m_Imr & (1u << Irq)) != 0) { return FALSE; }
        if ((m_Isr & (((1u << Irq) << 1) - 1)) != 0) { return FALSE; }
        return TRUE;
    }

private:
    std::atomic<INT32> m_Ref;
    UINT16             m_Base       = 0x20;
    UINT8              m_Imr        = 0xFF;
    UINT8              m_Irr        = 0;
    UINT8              m_Isr        = 0;
    UINT8              m_VectorBase = 0x08;
    int                m_Init       = InitIdle;
    BOOLEAN            m_Single     = FALSE;
    BOOLEAN            m_NeedIcw4   = FALSE;
    BOOLEAN            m_ReadIsr    = FALSE;       // OCW3: command port reads ISR vs IRR
};

} // anonymous namespace

IDevice *
CreatePic8259 ()
{
    return new Pic8259 ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreatePic8259)
