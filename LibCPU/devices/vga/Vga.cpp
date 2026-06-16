/** @file
  IBM VGA (Video Graphics Array) -- a LibCPU hardware-component bundle.

  The VGA is a superset of the EGA register model with the same indexed groups in 0x3C0-0x3CF and
  the colour CRTC at 0x3D4/0x3D5, plus the RAMDAC colour palette:

    - Attribute controller (0x3C0): index/data via the 0x3DA-reset flip-flop (as on the EGA).
    - Misc output (write 0x3C2, read 0x3CC); sequencer (0x3C4/0x3C5, 5 regs).
    - DAC: write-index (0x3C8) then three successive 0x3C9 writes load one palette entry's R,G,B
      (6-bit each); read-index (0x3C7) then three reads return them. 256 entries.
    - Graphics controller (0x3CE/0x3CF, 9 regs); CRTC (0x3D4/0x3D5, 25 regs); status 0x3DA.

  Mode 13h linear graphics live at 0xA0000; the colour-text page is at 0xB8000, which this
  component renders as an 80x25 screen. It models the programmer-visible register files and DAC.

  A COM object: IDevice + the IPortDevice and IDisplayDevice capabilities.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IDevice.h"
#include <atomic>
#include <cstdio>
#include <cstring>

namespace LibCPU {

namespace {

enum {
    Vga_AttrAddr  = 0x3C0, Vga_AttrRead = 0x3C1, Vga_MiscOut = 0x3C2,
    Vga_SeqIndex  = 0x3C4, Vga_SeqData  = 0x3C5,
    Vga_DacReadIx = 0x3C7, Vga_DacWriteIx = 0x3C8, Vga_DacData = 0x3C9,
    Vga_MiscRead  = 0x3CC, Vga_GcIndex  = 0x3CE, Vga_GcData = 0x3CF,
    Vga_CrtcIndex = 0x3D4, Vga_CrtcData = 0x3D5, Vga_Status1 = 0x3DA
};
enum { Vga_FbBase = 0xB8000, Vga_Cols = 80, Vga_Rows = 25 };

class Vga : public IDevice, public IPortDevice, public IDisplayDevice {
public:
    Vga () : m_Ref (1) {}

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

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override { return "IBM VGA"; }

    HRESULT STDMETHODCALLTYPE Configure (IN IDeviceNode * /*pNode*/) override { return Reset (); }

    HRESULT STDMETHODCALLTYPE Reset (THIS) override
    {
        std::memset (m_Crtc, 0, sizeof (m_Crtc));
        std::memset (m_Seq, 0, sizeof (m_Seq));
        std::memset (m_Gc, 0, sizeof (m_Gc));
        std::memset (m_Attr, 0, sizeof (m_Attr));
        std::memset (m_Dac, 0, sizeof (m_Dac));
        m_CrtcIndex = m_SeqIndex = m_GcIndex = m_AttrIndex = 0;
        m_AttrFlip  = FALSE;
        m_MiscOut   = 0;
        m_Status    = 0;
        m_DacWrIx   = 0; m_DacRdIx = 0; m_DacComp = 0;
        return S_OK;
    }

    BOOLEAN STDMETHODCALLTYPE OwnsPort (UINT16 Port) override
    {
        return ((Port >= 0x3C0 && Port <= 0x3CF) || (Port >= 0x3D4 && Port <= 0x3DA)) ? TRUE : FALSE;
    }

    HRESULT STDMETHODCALLTYPE ReadPort (UINT16 Port, UINT32 /*Width*/, UINT32 *pValue) override
    {
        if (pValue == nullptr) { return E_POINTER; }
        switch (Port) {
            case Vga_AttrRead:  *pValue = (m_AttrIndex < 21) ? m_Attr[m_AttrIndex] : 0; break;
            case Vga_SeqData:   *pValue = (m_SeqIndex < 5)  ? m_Seq[m_SeqIndex]  : 0; break;
            case Vga_GcData:    *pValue = (m_GcIndex < 9)   ? m_Gc[m_GcIndex]    : 0; break;
            case Vga_CrtcData:  *pValue = (m_CrtcIndex < 25) ? m_Crtc[m_CrtcIndex] : 0; break;
            case Vga_MiscRead:  *pValue = m_MiscOut; break;
            case Vga_DacData:   *pValue = m_Dac[(m_DacRdIx % 256) * 3 + m_DacComp];   // R, G, then B
                                if (++m_DacComp == 3) { m_DacComp = 0; m_DacRdIx++; } break;
            case Vga_Status1:   m_AttrFlip = FALSE;                  // reading status resets the attr flip-flop
                                m_Status ^= 0x09; *pValue = m_Status; break;
            default:            *pValue = 0; break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE WritePort (UINT16 Port, UINT32 /*Width*/, UINT32 Value) override
    {
        UINT8 V = (UINT8) Value;
        switch (Port) {
            case Vga_AttrAddr:                                      // alternates index then data
                if (!m_AttrFlip) { m_AttrIndex = (UINT8) (V & 0x1F); }
                else if (m_AttrIndex < 21) { m_Attr[m_AttrIndex] = V; }
                m_AttrFlip = m_AttrFlip ? FALSE : TRUE;
                break;
            case Vga_MiscOut:    m_MiscOut = V; break;
            case Vga_SeqIndex:   m_SeqIndex = V; break;
            case Vga_SeqData:    if (m_SeqIndex < 5)  { m_Seq[m_SeqIndex]  = V; } break;
            case Vga_GcIndex:    m_GcIndex = V; break;
            case Vga_GcData:     if (m_GcIndex < 9)   { m_Gc[m_GcIndex]    = V; } break;
            case Vga_CrtcIndex:  m_CrtcIndex = V; break;
            case Vga_CrtcData:   if (m_CrtcIndex < 25) { m_Crtc[m_CrtcIndex] = V; } break;
            case Vga_DacWriteIx: m_DacWrIx = V; m_DacComp = 0; break;   // begin loading a palette entry
            case Vga_DacReadIx:  m_DacRdIx = V; m_DacComp = 0; break;
            case Vga_DacData:    m_Dac[(m_DacWrIx % 256) * 3 + m_DacComp] = (UINT8) (V & 0x3F);
                                 if (++m_DacComp == 3) { m_DacComp = 0; m_DacWrIx++; } break;
            default:             break;
        }
        return S_OK;
    }

    // --- IDisplayDevice: the colour-text framebuffer and how to draw it ---
    UINT32 STDMETHODCALLTYPE GetFramebufferBase (THIS) override { return Vga_FbBase; }
    UINT32 STDMETHODCALLTYPE GetFramebufferSize (THIS) override { return Vga_Cols * Vga_Rows * 2; }

    HRESULT STDMETHODCALLTYPE RenderText (UINT8 CONST *pFb, UINT32 Len) override
    {
        if (pFb == nullptr) { return E_POINTER; }
        std::printf ("   +");
        for (int C = 0; C < Vga_Cols; C++) { std::printf ("-"); }
        std::printf ("+\n");
        for (int R = 0; R < Vga_Rows; R++) {
            std::printf ("   |");
            for (int C = 0; C < Vga_Cols; C++) {
                UINT32 Off = (UINT32) (R * Vga_Cols + C) * 2;        // char byte, attribute byte
                UINT8 Ch = (Off < Len) ? pFb[Off] : 0;
                std::printf ("%c", (Ch >= 0x20 && Ch < 0x7F) ? (char) Ch : ' ');
            }
            std::printf ("|\n");
        }
        std::printf ("   +");
        for (int C = 0; C < Vga_Cols; C++) { std::printf ("-"); }
        std::printf ("+\n");
        return S_OK;
    }

private:
    std::atomic<INT32> m_Ref;
    UINT8              m_Crtc[25] = { 0 };
    UINT8              m_Seq[5]   = { 0 };
    UINT8              m_Gc[9]    = { 0 };
    UINT8              m_Attr[21] = { 0 };
    UINT8              m_Dac[256 * 3] = { 0 };        // 256 palette entries, R/G/B (6-bit) each
    UINT8              m_CrtcIndex = 0, m_SeqIndex = 0, m_GcIndex = 0, m_AttrIndex = 0;
    BOOLEAN            m_AttrFlip = FALSE;
    UINT8              m_MiscOut  = 0;
    UINT8              m_Status   = 0;
    UINT16             m_DacWrIx  = 0, m_DacRdIx = 0;
    UINT8              m_DacComp  = 0;                 // which of R/G/B comes next (0..2)
};

} // anonymous namespace

IDevice *
CreateVga ()
{
    return new Vga ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreateVga)
