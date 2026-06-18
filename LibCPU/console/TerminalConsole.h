/** @file
  TerminalConsole -- the console side of the seam: it turns protocol messages into a drawn screen
  and host keystrokes into KEY messages. It holds only an IConsoleChannel endpoint, so it can run
  in the machine's process (in-proc channel) or, unchanged, as a standalone `lcx console` over a
  pipe/socket.

  The channel/render/screen-model logic is headless and deterministic (Service, FeedKey,
  ScreenText) so it can be driven and asserted from a test without a terminal. The raw-TTY
  interactive loop (RunInteractive) is the only part that needs a real terminal.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_TERMINALCONSOLE_H
#define LIBCPU_TERMINALCONSOLE_H

#include "LibCPU/IConsole.h"
#include "LibCPU/ConsoleProtocol.h"
#include <cstdio>
#include <string>
#include <vector>

namespace LibCPU {

class TerminalConsole {
public:
    // pChannel is borrowed-then-retained. If pOut is non-null, Service() draws frames to it
    // (an ANSI terminal); pass null for a headless renderer that only updates the screen model.
    explicit TerminalConsole (IConsoleChannel *pChannel, std::FILE *pOut = nullptr);
    ~TerminalConsole ();

    // Drain all pending messages, updating the screen model and (if pOut set) drawing. Returns the
    // number of frames consumed this call.
    UINT32 Service ();

    // Translate a host ASCII byte into a key make+break and send it (the common case: one glyph).
    // Returns false for a byte we have no scan code for (nothing sent).
    bool FeedKey (char Ascii);
    // Send a raw XT scan code make+break (for keys with no ASCII, e.g. function keys later).
    VOID SendScan (UINT16 ScanCode);
    VOID SendControl (UINT16 Control);

    // The current text screen as one string (rows joined by '\n'); for headless assertions.
    std::string ScreenText () CONST;

    UINT32 Cols ()   CONST { return m_Cols; }
    UINT32 Rows ()   CONST { return m_Rows; }
    UINT32 Format () CONST { return m_Format; }

    // Raw-terminal interactive loop: put stdin in raw mode, render frames, forward keystrokes,
    // until CONTROL_QUIT or the user presses the quit key. Needs a real TTY (no-op return on none).
    VOID RunInteractive ();

private:
    VOID ApplyMessage (CONSOLE_MSG CONST &Msg);
    VOID Draw () CONST;

    IConsoleChannel    *m_pChannel;
    std::FILE          *m_pOut;
    std::vector<UINT8>  m_RxBuf;
    std::vector<UINT8>  m_Cells;        // text: cols*rows*2 (char, attr); or raw graphics bytes
    UINT32              m_Format;       // CONSOLE_FB_FORMAT
    UINT32              m_Cols, m_Rows; // text geometry (or graphics width/height)
    UINT32              m_Bpp;
    bool                m_Quit;
};

} // namespace LibCPU

#endif // LIBCPU_TERMINALCONSOLE_H
