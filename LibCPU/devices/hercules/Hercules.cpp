/** @file
  Hercules Graphics Card (HGC) -- a LibCPU hardware-component bundle.

  The Hercules card is MDA-compatible: an MC6845 CRTC at 0x3B0-0x3BB driving an 80x25 monochrome
  text page at 0xB0000. It adds a 720x348 monochrome graphics mode held in two 32 KiB pages -- page
  0 at 0xB0000 and page 1 at 0xB8000 -- selected by mode-register bit 7. Two extra registers govern
  it: the mode-control register (0x3B8) bit 1 enables graphics, and the configuration register
  (0x3BF) is a safety switch -- bit 0 must be set before graphics mode is allowed, and bit 1 before
  the second page is enabled. The status register (0x3BA) exposes a vertical-retrace bit (bit 7)
  that software toggles to tell a Hercules apart from a plain MDA.

  This component models those programmer-visible registers; RenderText draws the 80x25 text page.

  A COM object: IDevice + the IPortDevice and IDisplayDevice capabilities.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IDevice.h"
#include <atomic>
#include <cstdio>
#include <cstring>

namespace LibCPU {

namespace {

enum { Crtc_Index = 0x4, Crtc_Data = 0x5, Hgc_Mode = 0x8, Hgc_Status = 0xA, Hgc_Config = 0xF };
enum { Hgc_FbBase = 0xB0000, Hgc_Cols = 80, Hgc_Rows = 25 };   // monochrome text page
enum { Hgc_VramSize = 0x10000 };                               // 64 KiB: both 32 KiB graphics pages

// Mode-control (0x3B8) and configuration (0x3BF) register bits.
enum { Mode_Graphics = 0x02, Mode_Video = 0x08, Mode_Page1 = 0x80 };
enum { Cfg_AllowGraphics = 0x01, Cfg_AllowPage1 = 0x02 };

class Hercules : public IDevice, public IPortDevice, public IDisplayDevice, public IMemoryDevice {
public:
    Hercules () : m_Ref (1) {}

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override
    {
        if (ppvObject == nullptr) { return E_POINTER; }
        if (CompareGuid (&riid, &IID_IUnknown) || CompareGuid (&riid, &IID_IDevice)) {
            *ppvObject = static_cast<IDevice *> (this);
        } else if (CompareGuid (&riid, &IID_IPortDevice)) {
            *ppvObject = static_cast<IPortDevice *> (this);
        } else if (CompareGuid (&riid, &IID_IDisplayDevice)) {
            *ppvObject = static_cast<IDisplayDevice *> (this);
        } else if (CompareGuid (&riid, &IID_IMemoryDevice)) {
            *ppvObject = static_cast<IMemoryDevice *> (this);
        } else {
            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }
        AddRef ();
        return S_OK;
    }

    // --- IMemoryDevice: the card's own video RAM mapped into the address space ---
    UINT32  STDMETHODCALLTYPE GetBase (THIS) override { return Hgc_FbBase; }
    UINT32  STDMETHODCALLTYPE GetSize (THIS) override { return Hgc_VramSize; }
    BOOLEAN STDMETHODCALLTYPE IsReadOnly (THIS) override { return FALSE; }

    UINT32 STDMETHODCALLTYPE AddRef (THIS) override { return (UINT32) ++m_Ref; }
    UINT32 STDMETHODCALLTYPE Release (THIS) override
    {
        UINT32 Count = (UINT32) --m_Ref;
        if (Count == 0) { delete this; }
        return Count;
    }

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override { return "Hercules Graphics Card"; }

    HRESULT STDMETHODCALLTYPE Configure (IN IDeviceNode *pNode) override
    {
        UINT32 Base = 0x3B0;
        if (pNode != nullptr) { pNode->GetPropertyCell ("reg", 0, &Base); }
        m_Base = (UINT16) Base;
        return Reset ();
    }

    HRESULT STDMETHODCALLTYPE Reset (THIS) override
    {
        std::memset (m_Crtc, 0, sizeof (m_Crtc));
        m_Index  = 0;
        m_Mode   = 0;
        m_Config = 0;
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
            case Hgc_Status:  m_Status ^= 0x81; *pValue = m_Status; break;   // toggle h-retrace + v-sync(bit7)
            default:          *pValue = 0; break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE WritePort (UINT16 Port, UINT32 /*Width*/, UINT32 Value) override
    {
        UINT8 V = (UINT8) Value;
        switch (Port - m_Base) {
            case Crtc_Index:  m_Index = (UINT8) (V & 0x1F); break;            // select CRTC register
            case Crtc_Data:   if (m_Index < 18) { m_Crtc[m_Index] = V; } break;
            case Hgc_Mode:                                                    // graphics/page gated by config
                if ((m_Config & Cfg_AllowGraphics) == 0) { V &= (UINT8) ~Mode_Graphics; }
                if ((m_Config & Cfg_AllowPage1)    == 0) { V &= (UINT8) ~Mode_Page1; }
                m_Mode = V;
                break;
            case Hgc_Config:  m_Config = V; break;                           // configuration safety switch
            default:          break;
        }
        return S_OK;
    }

    // --- IDisplayDevice: the character framebuffer and how to draw it ---
    UINT32 STDMETHODCALLTYPE GetFramebufferBase (THIS) override { return Hgc_FbBase; }
    UINT32 STDMETHODCALLTYPE GetFramebufferSize (THIS) override { return Hgc_Cols * Hgc_Rows * 2; }

    HRESULT STDMETHODCALLTYPE RenderText (UINT8 CONST *pFb, UINT32 Len) override
    {
        if (pFb == nullptr) { return E_POINTER; }
        std::printf ("   +");
        for (int C = 0; C < Hgc_Cols; C++) { std::printf ("-"); }
        std::printf ("+\n");
        for (int R = 0; R < Hgc_Rows; R++) {
            std::printf ("   |");
            for (int C = 0; C < Hgc_Cols; C++) {
                UINT32 Off = (UINT32) (R * Hgc_Cols + C) * 2;        // char byte, attribute byte
                UINT8 Ch = (Off < Len) ? pFb[Off] : 0;
                std::printf ("%c", (Ch >= 0x20 && Ch < 0x7F) ? (char) Ch : ' ');
            }
            std::printf ("|\n");
        }
        std::printf ("   +");
        for (int C = 0; C < Hgc_Cols; C++) { std::printf ("-"); }
        std::printf ("+\n");
        return S_OK;
    }

private:
    std::atomic<INT32> m_Ref;
    UINT16             m_Base = 0x3B0;
    UINT8              m_Crtc[18] = { 0 };
    UINT8              m_Index  = 0;
    UINT8              m_Mode   = 0;
    UINT8              m_Config = 0;
    UINT8              m_Status = 0;
};

} // anonymous namespace

IDevice *
CreateHercules ()
{
    return new Hercules ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreateHercules)
