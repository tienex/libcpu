/** @file
  Motorola MC6845 CRTC as wired for the IBM Color Graphics Adapter (CGA) -- a LibCPU
  hardware-component bundle.

  The CGA control ports live at 0x3D0-0x3DF: an index register (0x3D4) selects one of the CRTC's
  internal registers, read/written through the data register (0x3D5); the mode-control register is
  0x3D8, the colour-select register is 0x3D9, and the status register is 0x3DA. Unlike the MDA the
  CGA is colour and its character/attribute framebuffer lives at physical 0xB8000 (a 16 KiB page,
  of which an 80x25 text screen uses 4000 bytes). This component models that register file and
  toggles the status register's display-enable/retrace bits so BIOS retrace-wait loops progress.

  A COM object: IDevice + the IPortDevice and IDisplayDevice capabilities.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IDevice.h"
#include <atomic>
#include <cstdio>
#include <cstring>

namespace LibCPU {

namespace {

enum { Crtc_Index = 0x4, Crtc_Data = 0x5, Cga_Mode = 0x8, Cga_Color = 0x9, Cga_Status = 0xA };
enum { Cga_FbBase = 0xB8000, Cga_Cols = 80, Cga_Rows = 25 };   // colour text page

class Cga6845 : public IDevice, public IPortDevice, public IDisplayDevice {
public:
    Cga6845 () : m_Ref (1) {}

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        if (ppvObject == nullptr) { return E_POINTER; }
        if (CompareGuid (&riid, &IID_IUnknown) || CompareGuid (&riid, &IID_IDevice)) {
            *ppvObject = static_cast<IDevice *> (this);
        } else if (CompareGuid (&riid, &IID_IPortDevice)) {
            *ppvObject = static_cast<IPortDevice *> (this);
        } else if (CompareGuid (&riid, &IID_IDisplayDevice)) {
            *ppvObject = static_cast<IDisplayDevice *> (this);
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

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override { return "MC6845 CRTC (CGA)"; }

    HRESULT STDMETHODCALLTYPE Configure (IN IDeviceNode *pNode) override
    {
        UINT32 Base = 0x3D0;
        if (pNode != nullptr) { pNode->GetPropertyCell ("reg", 0, &Base); }
        m_Base = (UINT16) Base;
        return Reset ();
    }

    HRESULT STDMETHODCALLTYPE Reset (THIS) override
    {
        std::memset (m_Crtc, 0, sizeof (m_Crtc));
        m_Index  = 0;
        m_Mode   = 0;
        m_Color  = 0;
        m_Status = 0;
        return S_OK;
    }

    BOOLEAN STDMETHODCALLTYPE OwnsPort (UINT16 Port) override
    {
        return (Port >= m_Base && Port < (UINT16) (m_Base + 0x10)) ? TRUE : FALSE;
    }

    HRESULT STDMETHODCALLTYPE ReadPort (UINT16 Port, UINT32 /*Width*/, UINT32 *pValue) override
    {
        if (pValue == nullptr) { return E_POINTER; }
        switch (Port - m_Base) {
            case Crtc_Data:   *pValue = (m_Index < 18) ? m_Crtc[m_Index] : 0; break;
            case Cga_Status:  m_Status ^= 0x09; *pValue = m_Status; break;   // toggle retrace + video bits
            default:          *pValue = 0; break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE WritePort (UINT16 Port, UINT32 /*Width*/, UINT32 Value) override
    {
        switch (Port - m_Base) {
            case Crtc_Index:  m_Index = (UINT8) (Value & 0x1F); break;        // select CRTC register
            case Crtc_Data:   if (m_Index < 18) { m_Crtc[m_Index] = (UINT8) Value; } break;
            case Cga_Mode:    m_Mode  = (UINT8) Value; break;                 // mode control
            case Cga_Color:   m_Color = (UINT8) Value; break;                 // colour / palette select
            default:          break;
        }
        return S_OK;
    }

    // --- IDisplayDevice: the character framebuffer and how to draw it ---
    UINT32 STDMETHODCALLTYPE GetFramebufferBase (THIS) override { return Cga_FbBase; }
    UINT32 STDMETHODCALLTYPE GetFramebufferSize (THIS) override { return Cga_Cols * Cga_Rows * 2; }

    HRESULT STDMETHODCALLTYPE RenderText (UINT8 CONST *pFb, UINT32 Len) override
    {
        if (pFb == nullptr) { return E_POINTER; }
        std::printf ("   +");
        for (int C = 0; C < Cga_Cols; C++) { std::printf ("-"); }
        std::printf ("+\n");
        for (int R = 0; R < Cga_Rows; R++) {
            std::printf ("   |");
            for (int C = 0; C < Cga_Cols; C++) {
                UINT32 Off = (UINT32) (R * Cga_Cols + C) * 2;        // char byte, attribute byte
                UINT8 Ch = (Off < Len) ? pFb[Off] : 0;
                std::printf ("%c", (Ch >= 0x20 && Ch < 0x7F) ? (char) Ch : ' ');
            }
            std::printf ("|\n");
        }
        std::printf ("   +");
        for (int C = 0; C < Cga_Cols; C++) { std::printf ("-"); }
        std::printf ("+\n");
        return S_OK;
    }

private:
    std::atomic<INT32> m_Ref;
    UINT16             m_Base = 0x3D0;
    UINT8              m_Crtc[18] = { 0 };
    UINT8              m_Index  = 0;
    UINT8              m_Mode   = 0;
    UINT8              m_Color  = 0;
    UINT8              m_Status = 0;
};

} // anonymous namespace

IDevice *
CreateCga6845 ()
{
    return new Cga6845 ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreateCga6845)
