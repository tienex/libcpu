/** @file
  TerminalConsole implementation: see TerminalConsole.h.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "TerminalConsole.h"
#include <cstring>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

namespace LibCPU {

namespace {

// US-keyboard ASCII -> XT set-1 make code, for the keys a terminal can deliver as a single byte.
// 0 = no mapping. Shifted glyphs share their base key's code (the guest's BIOS applies shift state;
// for v1 we forward the base code, which is enough to drive the keyboard ISR and type lower case).
UINT8 AsciiToScan (char C)
{
    switch (C) {
        case 27:   return 0x01;   // Esc
        case '1':  return 0x02;   case '2': return 0x03;  case '3': return 0x04;  case '4': return 0x05;
        case '5':  return 0x06;   case '6': return 0x07;  case '7': return 0x08;  case '8': return 0x09;
        case '9':  return 0x0A;   case '0': return 0x0B;  case '-': return 0x0C;  case '=': return 0x0D;
        case '\b': case 127: return 0x0E;  // Backspace
        case '\t': return 0x0F;
        case 'q':  return 0x10;   case 'w': return 0x11;  case 'e': return 0x12;  case 'r': return 0x13;
        case 't':  return 0x14;   case 'y': return 0x15;  case 'u': return 0x16;  case 'i': return 0x17;
        case 'o':  return 0x18;   case 'p': return 0x19;  case '[': return 0x1A;  case ']': return 0x1B;
        case '\r': case '\n': return 0x1C;  // Enter
        case 'a':  return 0x1E;   case 's': return 0x1F;  case 'd': return 0x20;  case 'f': return 0x21;
        case 'g':  return 0x22;   case 'h': return 0x23;  case 'j': return 0x24;  case 'k': return 0x25;
        case 'l':  return 0x26;   case ';': return 0x27;  case '\'': return 0x28; case '`': return 0x29;
        case '\\': return 0x2B;
        case 'z':  return 0x2C;   case 'x': return 0x2D;  case 'c': return 0x2E;  case 'v': return 0x2F;
        case 'b':  return 0x30;   case 'n': return 0x31;  case 'm': return 0x32;  case ',': return 0x33;
        case '.':  return 0x34;   case '/': return 0x35;  case ' ': return 0x39;
        default:   return 0x00;
    }
}

} // anonymous namespace

TerminalConsole::TerminalConsole (IConsoleChannel *pChannel, std::FILE *pOut)
    : m_pChannel (pChannel), m_pOut (pOut), m_Format (ConsoleFbText),
      m_Cols (0), m_Rows (0), m_Bpp (0), m_Quit (false)
{
    if (m_pChannel) { m_pChannel->AddRef (); }
    m_RxBuf.resize (1 << 18);                            // big enough for a 64 KiB graphics frame
}

TerminalConsole::~TerminalConsole ()
{
    if (m_pChannel) { m_pChannel->Release (); }
}

UINT32
TerminalConsole::Service ()
{
    if (m_pChannel == nullptr) { return 0; }
    UINT32 Frames = 0;
    for (;;) {
        UINT32 MsgLen = 0;
        HRESULT hr = m_pChannel->Poll (m_RxBuf.data (), (UINT32) m_RxBuf.size (), &MsgLen);
        if (hr != S_OK || MsgLen == 0) { break; }
        if (MsgLen > m_RxBuf.size ()) { m_RxBuf.resize (MsgLen); continue; }
        CONSOLE_MSG Msg;
        if (!ConsoleMsgDecode (m_RxBuf.data (), MsgLen, &Msg)) { continue; }
        ApplyMessage (Msg);
        if (Msg.Type == ConsoleMsgFrame) { Frames++; }
    }
    if (Frames != 0 && m_pOut != nullptr) { Draw (); }
    return Frames;
}

VOID
TerminalConsole::ApplyMessage (CONSOLE_MSG CONST &Msg)
{
    switch (Msg.Type) {
        case ConsoleMsgMode:
            m_Format = Msg.Mode.Format;
            m_Cols   = Msg.Mode.Width;
            m_Rows   = Msg.Mode.Height;
            m_Bpp    = Msg.Mode.Bpp;
            m_Cells.clear ();
            break;
        case ConsoleMsgFrame:
            m_Cells.assign (Msg.pPayload, Msg.pPayload + Msg.PayloadLen);
            break;
        default:
            break;
    }
}

std::string
TerminalConsole::ScreenText () CONST
{
    std::string Out;
    if (m_Format == ConsoleFbText) {
        for (UINT32 R = 0; R < m_Rows; R++) {
            for (UINT32 C = 0; C < m_Cols; C++) {
                UINT32 Off = (R * m_Cols + C) * 2;       // (char, attribute)
                UINT8  Ch  = (Off < m_Cells.size ()) ? m_Cells[Off] : ' ';
                Out.push_back ((Ch >= 0x20 && Ch < 0x7F) ? (char) Ch : ' ');
            }
            Out.push_back ('\n');
        }
    } else {
        // Graphics: downsample the bitmap to ~80 columns of a brightness ramp, so a terminal shows
        // a recognizable picture; a GUI console would render the full bitmap from the same frame.
        static CHAR8 CONST *Ramp = " .:-=+*#%@";
        UINT32 OutCols = (m_Cols < 80) ? m_Cols : 80;
        UINT32 OutRows = (m_Rows < 40) ? m_Rows : 40;
        if (OutCols == 0 || OutRows == 0) { return Out; }
        for (UINT32  R = 0; R < OutRows; R++) {
            for (UINT32 C = 0; C < OutCols; C++) {
                UINT32 Sx = C * m_Cols / OutCols;
                UINT32 Sy = R * m_Rows / OutRows;
                UINT32 Off = Sy * m_Cols + Sx;           // 8 bpp: one byte per pixel
                UINT8  V   = (Off < m_Cells.size ()) ? m_Cells[Off] : 0;
                Out.push_back (Ramp[(V * 9) / 255]);
            }
            Out.push_back ('\n');
        }
    }
    return Out;
}

VOID
TerminalConsole::Draw () CONST
{
    if (m_pOut == nullptr) { return; }
    std::fprintf (m_pOut, "\x1b[H\x1b[2J");               // home + clear
    std::string Text = ScreenText ();
    std::fwrite (Text.data (), 1, Text.size (), m_pOut);
    std::fflush (m_pOut);
}

bool
TerminalConsole::FeedKey (char Ascii)
{
    UINT8 Scan = AsciiToScan (Ascii);
    if (Scan == 0) { return false; }
    SendScan (Scan);
    return true;
}

VOID
TerminalConsole::SendScan (UINT16 ScanCode)
{
    if (m_pChannel == nullptr) { return; }
    std::vector<UINT8> Buf;
    ConsoleEncodeKey (Buf, ScanCode, true);              // make
    m_pChannel->Send (Buf.data (), (UINT32) Buf.size ());
    ConsoleEncodeKey (Buf, ScanCode, false);             // break
    m_pChannel->Send (Buf.data (), (UINT32) Buf.size ());
}

VOID
TerminalConsole::SendControl (UINT16 Control)
{
    if (m_pChannel == nullptr) { return; }
    std::vector<UINT8> Buf;
    ConsoleEncodeControl (Buf, Control);
    m_pChannel->Send (Buf.data (), (UINT32) Buf.size ());
    if (Control == ConsoleControlQuit) { m_Quit = true; }
}

VOID
TerminalConsole::RunInteractive ()
{
    if (!isatty (STDIN_FILENO)) { return; }              // needs a real terminal

    struct termios Old;
    if (tcgetattr (STDIN_FILENO, &Old) != 0) { return; }
    struct termios Raw = Old;
    Raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO | ISIG);   // raw: no line buffering, echo, or signals
    Raw.c_cc[VMIN]  = 0;
    Raw.c_cc[VTIME] = 0;
    tcsetattr (STDIN_FILENO, TCSANOW, &Raw);
    int Flags = fcntl (STDIN_FILENO, F_GETFL, 0);
    fcntl (STDIN_FILENO, F_SETFL, Flags | O_NONBLOCK);

    std::fprintf (m_pOut ? m_pOut : stdout, "\x1b[2J[console: Ctrl-] to quit]\n");
    while (!m_Quit) {
        Service ();
        char Buf[64];
        ssize_t N = read (STDIN_FILENO, Buf, sizeof (Buf));
        for (ssize_t I = 0; I < N; I++) {
            if (Buf[I] == 0x1D) { SendControl (ConsoleControlQuit); break; }   // Ctrl-]
            FeedKey (Buf[I]);
        }
        usleep (5000);                                   // ~200 Hz service tick
    }

    fcntl (STDIN_FILENO, F_SETFL, Flags);
    tcsetattr (STDIN_FILENO, TCSANOW, &Old);
}

} // namespace LibCPU
