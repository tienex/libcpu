/** @file
  LibCPU console protocol -- the wire contract between an emulated machine and a console
  (keyboard + display) front end.

  The machine and the console exchange only the framed messages defined here, carried over an
  IConsoleChannel endpoint (see IConsole.h). Because nothing but these bytes crosses the seam,
  the console can run in-process today and split out into a standalone application tomorrow by
  swapping the channel's transport (in-process queue -> pipe -> socket) with no change to either
  side's logic.

  Wire framing (little-endian): every message is

      [u32 Length][u16 Type][u16 Flags][payload ... Length-8 bytes]

  where Length counts the whole message including this 8-byte header. The header-only helpers
  below pack/unpack messages to/from a byte buffer so both ends -- and a future out-of-process
  front end -- share one definition.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_CONSOLEPROTOCOL_H
#define LIBCPU_CONSOLEPROTOCOL_H

#include "LibCPU/Base.h"
#include <vector>

namespace LibCPU {

// Protocol version, bumped on any incompatible change; exchanged in the HELLO handshake.
enum { ConsoleProtocolVersion = 1 };

// The 8-byte message header that prefixes every message on the wire.
enum { ConsoleMsgHeaderSize = 8 };

// Message kinds. Machine->Console describe what to draw; Console->Machine deliver input.
typedef enum _CONSOLE_MSG_TYPE {
    ConsoleMsgHello    = 1,    // either direction: version handshake
    ConsoleMsgHelloAck = 2,    // console -> machine: handshake reply
    ConsoleMsgMode     = 3,    // machine -> console: display geometry / pixel format
    ConsoleMsgFrame    = 4,    // machine -> console: a full framebuffer snapshot
    ConsoleMsgCursor   = 5,    // machine -> console: text cursor position / visibility
    ConsoleMsgBell     = 6,    // machine -> console: audible bell / beep
    ConsoleMsgKey      = 7,    // console -> machine: a key make/break (XT scan code)
    ConsoleMsgControl  = 8     // console -> machine: session control (reset/pause/quit)
} CONSOLE_MSG_TYPE;

// Framebuffer formats carried by MODE/FRAME. Text is a grid of (character, attribute) cells;
// graphics is a packed pixel bitmap whose interpretation follows the bits-per-pixel field.
typedef enum _CONSOLE_FB_FORMAT {
    ConsoleFbText     = 0,     // Width=columns, Height=rows, 2 bytes/cell (char, attribute)
    ConsoleFbGraphics = 1      // Width x Height pixels, Bpp bits each, row-major
} CONSOLE_FB_FORMAT;

// Session-control actions (CONTROL message payload).
typedef enum _CONSOLE_CONTROL {
    ConsoleControlReset  = 1,
    ConsoleControlPause  = 2,
    ConsoleControlResume = 3,
    ConsoleControlQuit   = 4
} CONSOLE_CONTROL;

// Decoded display mode (MODE payload). For text, Width/Height are columns/rows and Bpp is 0.
typedef struct _CONSOLE_MODE {
    UINT16 Format;             // CONSOLE_FB_FORMAT
    UINT16 Width;
    UINT16 Height;
    UINT16 Bpp;                // bits per pixel (graphics); 0 for text
} CONSOLE_MODE;

// A decoded message handed back by ConsoleMsgDecode: the type plus whichever fields apply, and
// for FRAME a view onto the payload bytes (which alias the source buffer -- copy if retained).
typedef struct _CONSOLE_MSG {
    UINT16        Type;
    UINT16        Flags;
    CONSOLE_MODE  Mode;        // valid for MODE
    UINT32        Seq;         // valid for FRAME
    UINT16        X, Y, Vis;   // valid for CURSOR
    UINT16        ScanCode, Down;  // valid for KEY
    UINT16        Control;     // valid for CONTROL (CONSOLE_CONTROL)
    UINT16        Version;     // valid for HELLO / HELLO_ACK
    UINT8 CONST  *pPayload;    // FRAME pixel/cell bytes (aliases the input buffer)
    UINT32        PayloadLen;
} CONSOLE_MSG;

//
// Little-endian field writers/readers over a growable byte buffer. A message stream is a
// dynamic structure, so it is serialized field-by-field here rather than by struct overlay.
//
inline VOID ConsolePutU16 (std::vector<UINT8> &Buf, UINT16 V)
{
    Buf.push_back ((UINT8) (V & 0xFF));
    Buf.push_back ((UINT8) (V >> 8));
}

inline VOID ConsolePutU32 (std::vector<UINT8> &Buf, UINT32 V)
{
    Buf.push_back ((UINT8) (V & 0xFF));
    Buf.push_back ((UINT8) ((V >> 8) & 0xFF));
    Buf.push_back ((UINT8) ((V >> 16) & 0xFF));
    Buf.push_back ((UINT8) ((V >> 24) & 0xFF));
}

inline UINT16 ConsoleGetU16 (UINT8 CONST *p)
{
    return (UINT16) (p[0] | (p[1] << 8));
}

inline UINT32 ConsoleGetU32 (UINT8 CONST *p)
{
    return (UINT32) p[0] | ((UINT32) p[1] << 8) | ((UINT32) p[2] << 16) | ((UINT32) p[3] << 24);
}

// Stamp the [Length][Type][Flags] header at the front of a buffer whose payload is already
// appended. Call after building the payload so Length is known.
inline VOID ConsoleFinishHeader (std::vector<UINT8> &Buf, UINT16 Type, UINT16 Flags)
{
    UINT32 Len = (UINT32) Buf.size ();
    Buf[0] = (UINT8) (Len & 0xFF);
    Buf[1] = (UINT8) ((Len >> 8) & 0xFF);
    Buf[2] = (UINT8) ((Len >> 16) & 0xFF);
    Buf[3] = (UINT8) ((Len >> 24) & 0xFF);
    Buf[4] = (UINT8) (Type & 0xFF);
    Buf[5] = (UINT8) (Type >> 8);
    Buf[6] = (UINT8) (Flags & 0xFF);
    Buf[7] = (UINT8) (Flags >> 8);
}

// Begin a message: reserve the 8-byte header (filled in by ConsoleFinishHeader).
inline VOID ConsoleBeginMsg (std::vector<UINT8> &Buf)
{
    Buf.clear ();
    for (int I = 0; I < ConsoleMsgHeaderSize; I++) { Buf.push_back (0); }
}

//
// Message builders. Each returns a complete framed message in Buf.
//
inline VOID ConsoleEncodeHello (std::vector<UINT8> &Buf, UINT16 Type, UINT16 Version)
{
    ConsoleBeginMsg (Buf);
    ConsolePutU16 (Buf, Version);
    ConsoleFinishHeader (Buf, Type, 0);
}

inline VOID ConsoleEncodeMode (std::vector<UINT8> &Buf, CONSOLE_MODE CONST &Mode)
{
    ConsoleBeginMsg (Buf);
    ConsolePutU16 (Buf, Mode.Format);
    ConsolePutU16 (Buf, Mode.Width);
    ConsolePutU16 (Buf, Mode.Height);
    ConsolePutU16 (Buf, Mode.Bpp);
    ConsoleFinishHeader (Buf, ConsoleMsgMode, 0);
}

inline VOID ConsoleEncodeFrame (std::vector<UINT8> &Buf, UINT32 Seq, UINT8 CONST *pPixels, UINT32 Len)
{
    ConsoleBeginMsg (Buf);
    ConsolePutU32 (Buf, Seq);
    for (UINT32 I = 0; I < Len; I++) { Buf.push_back (pPixels[I]); }
    ConsoleFinishHeader (Buf, ConsoleMsgFrame, 0);
}

inline VOID ConsoleEncodeCursor (std::vector<UINT8> &Buf, UINT16 X, UINT16 Y, UINT16 Vis)
{
    ConsoleBeginMsg (Buf);
    ConsolePutU16 (Buf, X);
    ConsolePutU16 (Buf, Y);
    ConsolePutU16 (Buf, Vis);
    ConsoleFinishHeader (Buf, ConsoleMsgCursor, 0);
}

inline VOID ConsoleEncodeBell (std::vector<UINT8> &Buf)
{
    ConsoleBeginMsg (Buf);
    ConsoleFinishHeader (Buf, ConsoleMsgBell, 0);
}

inline VOID ConsoleEncodeKey (std::vector<UINT8> &Buf, UINT16 ScanCode, bool Down)
{
    ConsoleBeginMsg (Buf);
    ConsolePutU16 (Buf, ScanCode);
    ConsolePutU16 (Buf, Down ? 1 : 0);
    ConsoleFinishHeader (Buf, ConsoleMsgKey, 0);
}

inline VOID ConsoleEncodeControl (std::vector<UINT8> &Buf, UINT16 Control)
{
    ConsoleBeginMsg (Buf);
    ConsolePutU16 (Buf, Control);
    ConsoleFinishHeader (Buf, ConsoleMsgControl, 0);
}

// Decode one framed message from a complete buffer (Len bytes, header included). Returns FALSE
// if the buffer is too short or internally inconsistent. pPayload (FRAME) aliases pData.
inline bool ConsoleMsgDecode (UINT8 CONST *pData, UINT32 Len, CONSOLE_MSG *pMsg)
{
    if (pData == nullptr || pMsg == nullptr || Len < ConsoleMsgHeaderSize) { return false; }
    UINT32 Declared = ConsoleGetU32 (pData);
    if (Declared != Len) { return false; }
    *pMsg = CONSOLE_MSG ();
    pMsg->Type  = ConsoleGetU16 (pData + 4);
    pMsg->Flags = ConsoleGetU16 (pData + 6);
    UINT8 CONST *p = pData + ConsoleMsgHeaderSize;
    UINT32       n = Len - ConsoleMsgHeaderSize;
    switch (pMsg->Type) {
        case ConsoleMsgHello:
        case ConsoleMsgHelloAck:
            if (n < 2) { return false; }
            pMsg->Version = ConsoleGetU16 (p);
            break;
        case ConsoleMsgMode:
            if (n < 8) { return false; }
            pMsg->Mode.Format = ConsoleGetU16 (p);
            pMsg->Mode.Width  = ConsoleGetU16 (p + 2);
            pMsg->Mode.Height = ConsoleGetU16 (p + 4);
            pMsg->Mode.Bpp    = ConsoleGetU16 (p + 6);
            break;
        case ConsoleMsgFrame:
            if (n < 4) { return false; }
            pMsg->Seq        = ConsoleGetU32 (p);
            pMsg->pPayload   = p + 4;
            pMsg->PayloadLen = n - 4;
            break;
        case ConsoleMsgCursor:
            if (n < 6) { return false; }
            pMsg->X   = ConsoleGetU16 (p);
            pMsg->Y   = ConsoleGetU16 (p + 2);
            pMsg->Vis = ConsoleGetU16 (p + 4);
            break;
        case ConsoleMsgBell:
            break;
        case ConsoleMsgKey:
            if (n < 4) { return false; }
            pMsg->ScanCode = ConsoleGetU16 (p);
            pMsg->Down     = ConsoleGetU16 (p + 2);
            break;
        case ConsoleMsgControl:
            if (n < 2) { return false; }
            pMsg->Control = ConsoleGetU16 (p);
            break;
        default:
            return false;
    }
    return true;
}

} // namespace LibCPU

#endif // LIBCPU_CONSOLEPROTOCOL_H
