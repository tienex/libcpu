/** @file
  IBM Enhanced Graphics Adapter (EGA) -- a LibCPU hardware-component bundle.

  The EGA replaces the discrete MC6845 with several indexed register groups in the 0x3C0-0x3CF
  range, plus a colour CRTC at 0x3D4/0x3D5 (the same address pair the CGA uses):

    - Attribute controller (0x3C0): a single port that alternates between an index write and a data
      write, governed by an internal flip-flop. Reading the input-status-1 register (0x3DA) resets
      the flip-flop so the next 0x3C0 write is an index -- this component models that handshake.
    - Misc output register (write 0x3C2, read 0x3CC): selects the CRTC address and clock.
    - Sequencer (index 0x3C4, data 0x3C5): 5 registers (reset, clocking, map mask, ...).
    - Graphics controller (index 0x3CE, data 0x3CF): 9 registers (set/reset, mode, bit mask, ...).
    - CRTC (index 0x3D4, data 0x3D5): 25 registers; status at 0x3DA toggles the retrace bits.

  The framebuffer is at 0xA0000 for the planar graphics modes and 0xB8000 for the colour-text page;
  this component renders the 80x25 text page. It models the programmer-visible register files.

  A COM object: IDevice + the IPortDevice and IDisplayDevice capabilities.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IDevice.h"
#include <atomic>
#include <cstdio>
#include <cstring>

namespace LibCPU {

namespace {

// Absolute port addresses of the EGA register groups (independent of the CRTC base).
enum {
    Ega_AttrAddr  = 0x3C0,      // attribute controller index/data (flip-flop selected)
    Ega_AttrRead  = 0x3C1,
    Ega_MiscOut   = 0x3C2,      // miscellaneous output (write)
    Ega_SeqIndex  = 0x3C4,      // sequencer index
    Ega_SeqData   = 0x3C5,      // sequencer data
    Ega_MiscRead  = 0x3CC,      // miscellaneous output (read)
    Ega_GcIndex   = 0x3CE,      // graphics-controller index
    Ega_GcData    = 0x3CF,      // graphics-controller data
    Ega_CrtcIndex = 0x3D4,      // CRTC index
    Ega_CrtcData  = 0x3D5,      // CRTC data
    Ega_Status1   = 0x3DA       // input status 1 (read) / feature control (write)
};
enum { Ega_FbBase = 0xB8000, Ega_Cols = 80, Ega_Rows = 25 };
enum { Ega_VramBase = 0xA0000, Ega_VramSize = 0x20000 };       // 128 KiB planar window 0xA0000-0xBFFFF

class Ega : public IDevice, public IPortDevice, public IDisplayDevice, public IMemoryDevice {
public:
    Ega () : m_Ref (1) {}

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
    UINT32  STDMETHODCALLTYPE GetBase (THIS) override { return Ega_VramBase; }
    UINT32  STDMETHODCALLTYPE GetSize (THIS) override { return Ega_VramSize; }
    BOOLEAN STDMETHODCALLTYPE IsReadOnly (THIS) override { return FALSE; }

    UINT32 STDMETHODCALLTYPE AddRef (THIS) override { return (UINT32) ++m_Ref; }
    UINT32 STDMETHODCALLTYPE Release (THIS) override
    {
        UINT32 Count = (UINT32) --m_Ref;
        if (Count == 0) { delete this; }
        return Count;
    }

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override { return "IBM EGA"; }

    HRESULT STDMETHODCALLTYPE Configure (IN IDeviceNode * /*pNode*/) override { return Reset (); }

    HRESULT STDMETHODCALLTYPE Reset (THIS) override
    {
        std::memset (m_Crtc, 0, sizeof (m_Crtc));
        std::memset (m_Seq, 0, sizeof (m_Seq));
        std::memset (m_Gc, 0, sizeof (m_Gc));
        std::memset (m_Attr, 0, sizeof (m_Attr));
        m_CrtcIndex = m_SeqIndex = m_GcIndex = m_AttrIndex = 0;
        m_AttrFlip  = FALSE;
        m_MiscOut   = 0;
        m_Status    = 0;
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
            case Ega_AttrRead:  *pValue = (m_AttrIndex < 21) ? m_Attr[m_AttrIndex] : 0; break;
            case Ega_SeqData:   *pValue = (m_SeqIndex < 5)  ? m_Seq[m_SeqIndex]  : 0; break;
            case Ega_GcData:    *pValue = (m_GcIndex < 9)   ? m_Gc[m_GcIndex]    : 0; break;
            case Ega_CrtcData:  *pValue = (m_CrtcIndex < 25) ? m_Crtc[m_CrtcIndex] : 0; break;
            case Ega_MiscRead:  *pValue = m_MiscOut; break;
            case Ega_Status1:   m_AttrFlip = FALSE;                  // reading status resets the attr flip-flop
                                m_Status ^= 0x09; *pValue = m_Status; break;
            default:            *pValue = 0; break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE WritePort (UINT16 Port, UINT32 /*Width*/, UINT32 Value) override
    {
        UINT8 V = (UINT8) Value;
        switch (Port) {
            case Ega_AttrAddr:                                      // alternates index then data
                if (!m_AttrFlip) { m_AttrIndex = (UINT8) (V & 0x1F); }
                else if (m_AttrIndex < 21) { m_Attr[m_AttrIndex] = V; }
                m_AttrFlip = m_AttrFlip ? FALSE : TRUE;
                break;
            case Ega_MiscOut:   m_MiscOut = V; break;
            case Ega_SeqIndex:  m_SeqIndex = V; break;
            case Ega_SeqData:   if (m_SeqIndex < 5)  { m_Seq[m_SeqIndex]  = V; } break;
            case Ega_GcIndex:   m_GcIndex = V; break;
            case Ega_GcData:    if (m_GcIndex < 9)   { m_Gc[m_GcIndex]    = V; } break;
            case Ega_CrtcIndex: m_CrtcIndex = V; break;
            case Ega_CrtcData:  if (m_CrtcIndex < 25) { m_Crtc[m_CrtcIndex] = V; } break;
            default:            break;
        }
        return S_OK;
    }

    // --- IDisplayDevice: the colour-text framebuffer and how to draw it ---
    UINT32 STDMETHODCALLTYPE GetFramebufferBase (THIS) override { return Ega_FbBase; }
    UINT32 STDMETHODCALLTYPE GetFramebufferSize (THIS) override { return Ega_Cols * Ega_Rows * 2; }

    HRESULT STDMETHODCALLTYPE RenderText (UINT8 CONST *pFb, UINT32 Len) override
    {
        if (pFb == nullptr) { return E_POINTER; }
        std::printf ("   +");
        for (int C = 0; C < Ega_Cols; C++) { std::printf ("-"); }
        std::printf ("+\n");
        for (int R = 0; R < Ega_Rows; R++) {
            std::printf ("   |");
            for (int C = 0; C < Ega_Cols; C++) {
                UINT32 Off = (UINT32) (R * Ega_Cols + C) * 2;        // char byte, attribute byte
                UINT8 Ch = (Off < Len) ? pFb[Off] : 0;
                std::printf ("%c", (Ch >= 0x20 && Ch < 0x7F) ? (char) Ch : ' ');
            }
            std::printf ("|\n");
        }
        std::printf ("   +");
        for (int C = 0; C < Ega_Cols; C++) { std::printf ("-"); }
        std::printf ("+\n");
        return S_OK;
    }

private:
    std::atomic<INT32> m_Ref;
    UINT8              m_Crtc[25] = { 0 };
    UINT8              m_Seq[5]   = { 0 };
    UINT8              m_Gc[9]    = { 0 };
    UINT8              m_Attr[21] = { 0 };
    UINT8              m_CrtcIndex = 0, m_SeqIndex = 0, m_GcIndex = 0, m_AttrIndex = 0;
    BOOLEAN            m_AttrFlip = FALSE;
    UINT8              m_MiscOut  = 0;
    UINT8              m_Status   = 0;
};

} // anonymous namespace

IDevice *
CreateEga ()
{
    return new Ega ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreateEga)
