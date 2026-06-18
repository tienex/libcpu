/** @file
  IConsoleChannel -- the transport seam between an emulated machine and its console front end.

  Each side of the console link holds one IConsoleChannel ENDPOINT and only ever Sends framed
  messages (see ConsoleProtocol.h) to the peer and Polls for messages from it. The two endpoints
  are cross-wired by a transport: today CreateInProcConsolePair gives an in-process pair backed by
  two queues; a pipe- or socket-backed pair would let the console run as a separate process with
  no change to either side. Send/Poll are message-atomic (a Poll yields exactly one whole message
  or nothing) and safe to call from one machine thread and one console thread.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_ICONSOLE_H
#define LIBCPU_ICONSOLE_H

#include "LibCPU/PCom.h"

namespace LibCPU {

DECLARE_INTERFACE_ (IConsoleChannel, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    // Send one complete framed message (pData includes the 8-byte header; Len == its Length field).
    STDMETHOD (Send)(THIS_ IN UINT8 CONST *pData, UINT32 Len) PURE;
    // Poll for one message from the peer. S_OK + *pMsgLen set if one was copied into pBuf; S_FALSE
    // if none is waiting; E_* on error. If the message exceeds BufLen, *pMsgLen is the needed size
    // and the message is left queued (caller retries with a larger buffer).
    STDMETHOD (Poll)(THIS_ OUT UINT8 *pBuf, UINT32 BufLen, OUT UINT32 *pMsgLen) PURE;
};

// {1C9A0003-0001-4C50-9A00-000000000001}  console interface family.
inline constexpr IID IID_IConsoleChannel =
    { 0x1C9A0003, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 } };

// Create a cross-wired in-process endpoint pair: messages Sent on *ppMachineEnd arrive on
// *ppConsoleEnd's Poll and vice versa. Each returned endpoint is owned (Release when done).
HRESULT CreateInProcConsolePair (OUT IConsoleChannel **ppMachineEnd, OUT IConsoleChannel **ppConsoleEnd);

} // namespace LibCPU

#endif // LIBCPU_ICONSOLE_H
