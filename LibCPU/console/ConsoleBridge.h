/** @file
  ConsoleBridge -- the machine side of the console seam.

  Installed as the System pump (System::SetPump), it runs at every execution-window boundary and
  while the CPU idles. Outbound, it reads the active display's mode and framebuffer (via
  IDisplayMode) and emits MODE/FRAME messages when they change. Inbound, it drains KEY/CONTROL
  messages from the channel, injecting scan codes into the keyboard controller (IKeyboardSink) and
  asking the machine to stop on CONTROL_QUIT. It speaks only the console protocol over an
  IConsoleChannel, so the peer can be in-process today or a separate process tomorrow.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_CONSOLEBRIDGE_H
#define LIBCPU_CONSOLEBRIDGE_H

#include "LibCPU/IConsole.h"
#include "LibCPU/IDevice.h"
#include "LibCPU/ConsoleProtocol.h"
#include <vector>

namespace LibCPU {

class System;   // forward: the bridge asks it to shut down on CONTROL_QUIT

class ConsoleBridge {
public:
    // pChannel/pDisplay/pKbd are borrowed-then-retained (AddRef'd here, Released in the dtor);
    // pSystem is borrowed and outlives the bridge. pKbd may be null (a machine with no keyboard).
    ConsoleBridge (IConsoleChannel *pChannel, IDisplayMode *pDisplay, IKeyboardSink *pKbd, System *pSystem);
    ~ConsoleBridge ();

    // The System pump entry point: feed injected keys in, then stream the framebuffer out.
    VOID Pump ();

private:
    VOID PumpIn ();
    VOID PumpOut ();

    IConsoleChannel   *m_pChannel;
    IDisplayMode      *m_pDisplay;
    IKeyboardSink     *m_pKbd;
    System            *m_pSystem;

    std::vector<UINT8> m_Scratch;       // outbound message encode buffer
    std::vector<UINT8> m_RxBuf;         // inbound message buffer
    std::vector<UINT8> m_LastFrame;     // previous framebuffer bytes, to send only on change
    UINT32             m_LastFormat;
    UINT32             m_LastWidth;
    UINT32             m_LastHeight;
    UINT32             m_LastBpp;
    UINT32             m_Seq;
    bool               m_HaveMode;
};

} // namespace LibCPU

#endif // LIBCPU_CONSOLEBRIDGE_H
