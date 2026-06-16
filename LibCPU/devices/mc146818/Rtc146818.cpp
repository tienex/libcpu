/** @file
  Motorola MC146818 real-time clock + CMOS RAM -- a LibCPU hardware-component bundle.

  The AT's RTC presents two ports: 0x70 selects one of the 64 CMOS RAM locations (bits 0-6; bit 7
  is the NMI-disable line), and 0x71 reads or writes the selected location. The first ten locations
  are the clock: seconds (0x00), minutes (0x02), hours (0x04), day-of-week (0x06), day-of-month
  (0x07), month (0x08) and year (0x09); 0x0A-0x0D are status registers A-D and 0x0E-0x3F are
  general battery-backed CMOS RAM (equipment byte, memory size, checksum, ...). This component seeds
  a fixed date/time in BCD (Status Register B bit 2 clear) and presents Status D's valid-RAM bit so
  the BIOS trusts the CMOS; reading Status Register C clears its interrupt flags.

  The periodic / alarm / update-ended interrupt is IRQ8, on the AT's second 8259 -- not yet wired
  here (there is no slave PIC component), so this models only the programmer-visible register file.

  A COM object: IDevice + the IPortDevice capability.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IDevice.h"
#include <atomic>
#include <cstring>

namespace LibCPU {

namespace {

enum { Rtc_Index = 0, Rtc_Data = 1 };                        // offsets from the 0x70 base
enum { Reg_StatusC = 0x0A + 2, Reg_StatusD = 0x0A + 3 };     // 0x0C, 0x0D

class Rtc146818 : public IDevice, public IPortDevice {
public:
    Rtc146818 () : m_Ref (1) {}

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

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override { return "MC146818 RTC/CMOS"; }

    HRESULT STDMETHODCALLTYPE Configure (IN IDeviceNode *pNode) override
    {
        UINT32 Base = 0x70;
        if (pNode != nullptr) { pNode->GetPropertyCell ("reg", 0, &Base); }
        m_Base = (UINT16) Base;
        return Reset ();
    }

    HRESULT STDMETHODCALLTYPE Reset (THIS) override
    {
        std::memset (m_Cmos, 0, sizeof (m_Cmos));
        m_Index = 0;
        m_Nmi   = FALSE;

        m_Cmos[0x00] = 0x00;                                 // seconds (BCD)
        m_Cmos[0x02] = 0x00;                                 // minutes
        m_Cmos[0x04] = 0x12;                                 // hours -> 12:00:00
        m_Cmos[0x06] = 0x03;                                 // day of week (Tue)
        m_Cmos[0x07] = 0x16;                                 // day of month -> 16
        m_Cmos[0x08] = 0x06;                                 // month -> June
        m_Cmos[0x09] = 0x26;                                 // year -> 26 (2026)
        m_Cmos[0x0A] = 0x26;                                 // Status A: UIP clear, default divider/rate
        m_Cmos[0x0B] = 0x02;                                 // Status B: 24-hour, BCD
        m_Cmos[0x0C] = 0x00;                                 // Status C: no interrupt flags
        m_Cmos[0x0D] = 0x80;                                 // Status D: VRT (CMOS battery/RAM valid)
        m_Cmos[0x10] = 0x40;                                 // floppy types: one 1.44 MB drive
        m_Cmos[0x14] = 0x2D;                                 // equipment byte
        m_Cmos[0x15] = 0x80; m_Cmos[0x16] = 0x02;            // base memory = 640 KiB (0x0280)
        m_Cmos[0x32] = 0x20;                                 // century (BCD) -> 20
        return S_OK;
    }

    BOOLEAN STDMETHODCALLTYPE OwnsPort (UINT16 Port) override
    {
        return (Port == (UINT16) (m_Base + Rtc_Index) || Port == (UINT16) (m_Base + Rtc_Data)) ? TRUE : FALSE;
    }

    HRESULT STDMETHODCALLTYPE ReadPort (UINT16 Port, UINT32 /*Width*/, UINT32 *pValue) override
    {
        if (pValue == nullptr) { return E_POINTER; }
        if (Port == (UINT16) (m_Base + Rtc_Index)) {
            *pValue = m_Index;                               // the index port reads back the selection
            return S_OK;
        }
        *pValue = m_Cmos[m_Index & 0x3F];
        if ((m_Index & 0x3F) == Reg_StatusC) { m_Cmos[Reg_StatusC] = 0; }   // reading Status C clears its flags
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE WritePort (UINT16 Port, UINT32 /*Width*/, UINT32 Value) override
    {
        if (Port == (UINT16) (m_Base + Rtc_Index)) {
            m_Index = (UINT8) (Value & 0x7F);                // bits 0-6 select the register
            m_Nmi   = (Value & 0x80) ? TRUE : FALSE;         // bit 7 disables NMI
            return S_OK;
        }
        if ((m_Index & 0x3F) != Reg_StatusD) {               // Status D (VRT) is read-only
            m_Cmos[m_Index & 0x3F] = (UINT8) Value;
        }
        return S_OK;
    }

private:
    std::atomic<INT32> m_Ref;
    UINT16             m_Base = 0x70;
    UINT8              m_Cmos[64] = { 0 };
    UINT8              m_Index = 0;
    BOOLEAN            m_Nmi   = FALSE;
};

} // anonymous namespace

IDevice *
CreateRtc146818 ()
{
    return new Rtc146818 ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreateRtc146818)
