/** @file
  ConsoleBridge implementation: see ConsoleBridge.h.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "ConsoleBridge.h"
#include "../core/System.h"
#include <cstring>

namespace LibCPU {

ConsoleBridge::ConsoleBridge (IConsoleChannel *pChannel, IDisplayMode *pDisplay, IKeyboardSink *pKbd, System *pSystem)
    : m_pChannel (pChannel), m_pDisplay (pDisplay), m_pKbd (pKbd), m_pSystem (pSystem),
      m_LastFormat (0xFFFFFFFF), m_LastWidth (0), m_LastHeight (0), m_LastBpp (0), m_Seq (0), m_HaveMode (false)
{
    if (m_pChannel) { m_pChannel->AddRef (); }
    if (m_pDisplay) { m_pDisplay->AddRef (); }
    if (m_pKbd)     { m_pKbd->AddRef (); }
    m_RxBuf.resize (8192);
}

ConsoleBridge::~ConsoleBridge ()
{
    if (m_pKbd)     { m_pKbd->Release (); }
    if (m_pDisplay) { m_pDisplay->Release (); }
    if (m_pChannel) { m_pChannel->Release (); }
}

VOID
ConsoleBridge::Pump ()
{
    PumpIn ();      // inject any keys the user typed before running the next slice
    PumpOut ();     // publish the current screen
}

VOID
ConsoleBridge::PumpIn ()
{
    if (m_pChannel == nullptr) { return; }
    for (;;) {
        UINT32 MsgLen = 0;
        HRESULT hr = m_pChannel->Poll (m_RxBuf.data (), (UINT32) m_RxBuf.size (), &MsgLen);
        if (hr != S_OK || MsgLen == 0) { break; }              // S_FALSE (empty) or error: done
        if (MsgLen > m_RxBuf.size ()) { m_RxBuf.resize (MsgLen); continue; }   // grow and retry
        CONSOLE_MSG Msg;
        if (!ConsoleMsgDecode (m_RxBuf.data (), MsgLen, &Msg)) { continue; }
        if (Msg.Type == ConsoleMsgKey) {
            // XT set-1: a make code as written, a break code with bit 7 set.
            UINT8 Code = (UINT8) (Msg.ScanCode & 0x7F) | (Msg.Down ? 0x00 : 0x80);
            if (m_pKbd) { m_pKbd->PushScanCode (Code); }
        } else if (Msg.Type == ConsoleMsgControl) {
            if (Msg.Control == ConsoleControlQuit && m_pSystem != nullptr) { m_pSystem->RequestShutdown (); }
        }
    }
}

VOID
ConsoleBridge::PumpOut ()
{
    if (m_pChannel == nullptr || m_pDisplay == nullptr) { return; }

    UINT32 Format = 0, Width = 0, Height = 0, Bpp = 0;
    if (FAILED (m_pDisplay->GetMode (&Format, &Width, &Height, &Bpp))) { return; }

    UINT8 CONST *pBytes = nullptr;
    UINT32       Len    = 0;
    if (FAILED (m_pDisplay->GetFramebuffer (&pBytes, &Len)) || pBytes == nullptr) { return; }

    // Announce a mode change (first frame, or geometry/format changed) before the frame itself.
    if (!m_HaveMode || Format != m_LastFormat || Width != m_LastWidth || Height != m_LastHeight || Bpp != m_LastBpp) {
        CONSOLE_MODE Mode;
        Mode.Format = (UINT16) Format;
        Mode.Width  = (UINT16) Width;
        Mode.Height = (UINT16) Height;
        Mode.Bpp    = (UINT16) Bpp;
        ConsoleEncodeMode (m_Scratch, Mode);
        m_pChannel->Send (m_Scratch.data (), (UINT32) m_Scratch.size ());
        m_LastFormat = Format; m_LastWidth = Width; m_LastHeight = Height; m_LastBpp = Bpp;
        m_HaveMode   = true;
        m_LastFrame.clear ();                       // force the next frame to be sent
    }

    // Send the framebuffer only when it actually changed since the last one.
    bool Changed = (m_LastFrame.size () != Len) ||
                   (Len != 0 && std::memcmp (m_LastFrame.data (), pBytes, Len) != 0);
    if (!Changed) { return; }
    m_LastFrame.assign (pBytes, pBytes + Len);
    ConsoleEncodeFrame (m_Scratch, m_Seq++, pBytes, Len);
    m_pChannel->Send (m_Scratch.data (), (UINT32) m_Scratch.size ());
}

} // namespace LibCPU
