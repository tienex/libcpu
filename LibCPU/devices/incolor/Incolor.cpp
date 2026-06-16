/** @file
  Hercules InColor Card -- a LibCPU hardware-component bundle.

  The InColor card is a colour Hercules: the same MDA-compatible MC6845 register layout at
  0x3B0-0x3BF and 720x348 resolution, but with four bit planes giving 16 colours out of 64 KiB of
  video RAM at 0xB0000. It extends the CRTC index space (indices 0x10-0x1F) with the InColor
  registers, the most visible being the palette register at index 0x14: with the index set to 0x14,
  successive writes to the data port (0x3B5) load the 16 palette entries in turn (then wrap). The
  remaining extended registers (read/write masks, plane select, ...) live in the same index bank.

  This component models the extended register file and the 16-entry palette; RenderText draws the
  80x25 text page. Like the Hercules it toggles the status register's vertical-sync bit.

  A COM object: IDevice + the IPortDevice and IDisplayDevice capabilities.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IDevice.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

namespace LibCPU {

namespace {

enum { Crtc_Index = 0x4, Crtc_Data = 0x5, Inc_Mode = 0x8, Inc_Status = 0xA, Inc_Config = 0xF };
enum { Inc_FbBase = 0xB0000, Inc_Cols = 80, Inc_Rows = 25 };
enum { Inc_PaletteReg = 0x14 };                                // CRTC index of the palette register
enum { Inc_VramSize = 0x10000 };                               // 64 KiB across the four colour planes

class Incolor : public IDevice, public IPortDevice, public IDisplayDevice, public IMemoryDevice, public IHostMemory {
public:
    Incolor () : m_Ref (1) {}

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
        } else if (CompareGuid (&riid, &IID_IHostMemory)) {
            *ppvObject = static_cast<IHostMemory *> (this);
        } else {
            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }
        AddRef ();
        return S_OK;
    }

    // --- IMemoryDevice + IHostMemory: the card's own video RAM, mapped into the address space ---
    UINT32  STDMETHODCALLTYPE GetBase (THIS) override { return Inc_FbBase; }
    UINT32  STDMETHODCALLTYPE GetSize (THIS) override { return Inc_VramSize; }
    BOOLEAN STDMETHODCALLTYPE IsReadOnly (THIS) override { return FALSE; }
    UINT8 * STDMETHODCALLTYPE GetHostBuffer (THIS) override { return m_Vram.data (); }

    UINT32 STDMETHODCALLTYPE AddRef (THIS) override { return (UINT32) ++m_Ref; }
    UINT32 STDMETHODCALLTYPE Release (THIS) override
    {
        UINT32 Count = (UINT32) --m_Ref;
        if (Count == 0) { delete this; }
        return Count;
    }

    CHAR8 CONST * STDMETHODCALLTYPE GetName (THIS) override { return "Hercules InColor Card"; }

    HRESULT STDMETHODCALLTYPE Configure (IN IDeviceNode *pNode) override
    {
        UINT32 Base = 0x3B0;
        if (pNode != nullptr) { pNode->GetPropertyCell ("reg", 0, &Base); }
        m_Base = (UINT16) Base;
        m_Vram.assign (Inc_VramSize, 0);                // the card's own video RAM
        return Reset ();
    }

    HRESULT STDMETHODCALLTYPE Reset (THIS) override
    {
        std::memset (m_Crtc, 0, sizeof (m_Crtc));
        std::memset (m_Palette, 0, sizeof (m_Palette));
        m_Index    = 0;
        m_PalWrite = 0;
        m_Mode     = 0;
        m_Config   = 0;
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
        switch (Port - m_Base) {
            case Crtc_Data:   *pValue = (m_Index < 32) ? m_Crtc[m_Index] : 0; break;
            case Inc_Status:  m_Status ^= 0x81; *pValue = m_Status; break;   // toggle h-retrace + v-sync(bit7)
            default:          *pValue = 0; break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE WritePort (UINT16 Port, UINT32 /*Width*/, UINT32 Value) override
    {
        UINT8 V = (UINT8) Value;
        switch (Port - m_Base) {
            case Crtc_Index:  m_Index = (UINT8) (V & 0x1F);                   // 32-register extended index
                              if (m_Index == Inc_PaletteReg) { m_PalWrite = 0; }   // restart palette load
                              break;
            case Crtc_Data:
                if (m_Index == Inc_PaletteReg) {                             // load the 16-entry palette
                    m_Palette[m_PalWrite & 0x0F] = (UINT8) (V & 0x3F);
                    m_PalWrite = (UINT8) ((m_PalWrite + 1) & 0x0F);
                } else if (m_Index < 32) {
                    m_Crtc[m_Index] = V;
                }
                break;
            case Inc_Mode:    m_Mode   = V; break;
            case Inc_Config:  m_Config = V; break;
            default:          break;
        }
        return S_OK;
    }

    // --- IDisplayDevice: the character framebuffer and how to draw it ---
    UINT32 STDMETHODCALLTYPE GetFramebufferBase (THIS) override { return Inc_FbBase; }
    UINT32 STDMETHODCALLTYPE GetFramebufferSize (THIS) override { return Inc_Cols * Inc_Rows * 2; }

    HRESULT STDMETHODCALLTYPE RenderText (UINT8 CONST *pFb, UINT32 Len) override
    {
        if (pFb == nullptr) { return E_POINTER; }
        std::printf ("   +");
        for (int C = 0; C < Inc_Cols; C++) { std::printf ("-"); }
        std::printf ("+\n");
        for (int R = 0; R < Inc_Rows; R++) {
            std::printf ("   |");
            for (int C = 0; C < Inc_Cols; C++) {
                UINT32 Off = (UINT32) (R * Inc_Cols + C) * 2;        // char byte, attribute byte
                UINT8 Ch = (Off < Len) ? pFb[Off] : 0;
                std::printf ("%c", (Ch >= 0x20 && Ch < 0x7F) ? (char) Ch : ' ');
            }
            std::printf ("|\n");
        }
        std::printf ("   +");
        for (int C = 0; C < Inc_Cols; C++) { std::printf ("-"); }
        std::printf ("+\n");
        return S_OK;
    }

private:
    std::atomic<INT32> m_Ref;
    std::vector<UINT8> m_Vram;
    UINT16             m_Base = 0x3B0;
    UINT8              m_Crtc[32]    = { 0 };       // extended InColor CRTC register bank
    UINT8              m_Palette[16] = { 0 };       // 16-entry colour palette
    UINT8              m_Index    = 0;
    UINT8              m_PalWrite = 0;              // next palette entry to load (0..15)
    UINT8              m_Mode     = 0;
    UINT8              m_Config   = 0;
    UINT8              m_Status   = 0;
};

} // anonymous namespace

IDevice *
CreateIncolor ()
{
    return new Incolor ();
}

} // namespace LibCPU

LIBCPU_MODULE_CREATE_DEVICE (LibCPU::CreateIncolor)
