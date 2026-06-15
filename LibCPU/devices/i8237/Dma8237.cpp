/** @file
  Intel 8237 DMA controller -- a LibCPU hardware-component bundle.

  The PC/XT's 8237 lives at ports 0x00-0x0F. This component models the programmer-visible
  register file: four channels each with a 16-bit base address and word count (ports 0x00-0x07,
  accessed a byte at a time through a low/high flip-flop), plus the command/status, mask, mode,
  flip-flop-clear and master-clear registers (0x08-0x0F). It performs no transfers yet (there is
  no DMA-driven peripheral wired in), but the registers read back consistently.

  A COM object: IDevice + the IPortDevice capability.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IDevice.h"
#include <atomic>
#include <cstring>

namespace LibCPU {

namespace {

class Dma8237 : public IDevice, public IPortDevice {
public:
    Dma8237 () : m_Ref (1) {}

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

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override { return "Intel 8237 DMA"; }

    HRESULT STDMETHODCALLTYPE Configure (IN IDeviceNode *pNode) override
    {
        UINT32 Base = 0x00;
        if (pNode != nullptr) { pNode->GetPropertyCell ("reg", 0, &Base); }
        m_Base = (UINT16) Base;
        return Reset ();
    }

    HRESULT STDMETHODCALLTYPE Reset (THIS) override
    {
        std::memset (m_Addr, 0, sizeof (m_Addr));
        std::memset (m_Count, 0, sizeof (m_Count));
        std::memset (m_Mode, 0, sizeof (m_Mode));
        m_FlipFlop = FALSE;
        m_Mask     = 0x0F;                                   // all channels masked after reset
        m_Command  = 0;
        m_Status   = 0;
        return S_OK;
    }

    BOOLEAN STDMETHODCALLTYPE OwnsPort (UINT16 Port) override
    {
        return (Port >= m_Base && Port < (UINT16) (m_Base + 0x10)) ? TRUE : FALSE;
    }

    HRESULT STDMETHODCALLTYPE ReadPort (UINT16 Port, UINT32 /*Width*/, UINT32 *pValue) override
    {
        if (pValue == nullptr) { return E_POINTER; }
        UINT16 Off = (UINT16) (Port - m_Base);
        if (Off < 0x08) {                                    // channel address/count, low then high byte
            UINT16 *pReg = (Off & 1) ? &m_Count[Off >> 1] : &m_Addr[Off >> 1];
            UINT8 B = m_FlipFlop ? (UINT8) (*pReg >> 8) : (UINT8) (*pReg & 0xFF);
            m_FlipFlop = (BOOLEAN) !m_FlipFlop;
            *pValue = B;
        } else if (Off == 0x08) {
            *pValue = m_Status;                              // status register
        } else {
            *pValue = 0;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE WritePort (UINT16 Port, UINT32 /*Width*/, UINT32 Value) override
    {
        UINT16 Off = (UINT16) (Port - m_Base);
        UINT8  V   = (UINT8) Value;
        if (Off < 0x08) {                                    // channel address/count, low then high byte
            UINT16 *pReg = (Off & 1) ? &m_Count[Off >> 1] : &m_Addr[Off >> 1];
            if (m_FlipFlop) { *pReg = (UINT16) ((*pReg & 0x00FF) | ((UINT16) V << 8)); }
            else            { *pReg = (UINT16) ((*pReg & 0xFF00) | V); }
            m_FlipFlop = (BOOLEAN) !m_FlipFlop;
            return S_OK;
        }
        switch (Off) {
            case 0x08: m_Command = V; break;                 // command register
            case 0x0A: if (V & 0x04) { m_Mask |= (1u << (V & 3)); }   // single channel mask
                       else          { m_Mask &= ~(1u << (V & 3)); } break;
            case 0x0B: m_Mode[V & 3] = V; break;             // mode register
            case 0x0C: m_FlipFlop = FALSE; break;            // clear byte-pointer flip-flop
            case 0x0D: Reset (); break;                      // master clear
            case 0x0E: m_Mask = 0x00; break;                 // clear mask register
            case 0x0F: m_Mask = (UINT8) (V & 0x0F); break;   // write all mask bits
            default:   break;
        }
        return S_OK;
    }

private:
    std::atomic<INT32> m_Ref;
    UINT16             m_Base = 0x00;
    UINT16             m_Addr[4]  = { 0, 0, 0, 0 };
    UINT16             m_Count[4] = { 0, 0, 0, 0 };
    UINT8              m_Mode[4]  = { 0, 0, 0, 0 };
    BOOLEAN            m_FlipFlop = FALSE;
    UINT8              m_Mask     = 0x0F;
    UINT8              m_Command  = 0;
    UINT8              m_Status   = 0;
};

} // anonymous namespace

IDevice *
CreateDma8237 ()
{
    return new Dma8237 ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreateDma8237)
