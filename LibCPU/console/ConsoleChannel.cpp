/** @file
  In-process IConsoleChannel transport: two cross-wired endpoints backed by a pair of
  message queues. This is the transport that lets the console run in the same process as the
  machine; replacing it with a pipe- or socket-backed endpoint (same IConsoleChannel contract)
  is all it takes to split the console into a standalone application.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "LibCPU/IConsole.h"
#include "LibCPU/ConsoleProtocol.h"
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace LibCPU {

namespace {

// A thread-safe FIFO of whole, framed messages. One direction of the link.
struct MsgQueue {
    std::mutex                       Mtx;
    std::deque<std::vector<UINT8>>   Q;
};

// One endpoint of the link: it Sends into m_Out and Polls from m_In. The peer endpoint holds
// the same two queues with the roles swapped, so the pair is cross-wired.
class InProcEndpoint final : public ComObject<IConsoleChannel> {
public:
    InProcEndpoint (std::shared_ptr<MsgQueue> Out, std::shared_ptr<MsgQueue> In)
        : m_Out (std::move (Out)), m_In (std::move (In)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_IConsoleChannel, ppvObject);
    }

    HRESULT STDMETHODCALLTYPE Send (UINT8 CONST *pData, UINT32 Len) override {
        if (pData == nullptr || Len < ConsoleMsgHeaderSize) { return E_INVALIDARG; }
        std::lock_guard<std::mutex> Lock (m_Out->Mtx);
        m_Out->Q.emplace_back (pData, pData + Len);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Poll (UINT8 *pBuf, UINT32 BufLen, UINT32 *pMsgLen) override {
        if (pMsgLen == nullptr) { return E_POINTER; }
        std::lock_guard<std::mutex> Lock (m_In->Mtx);
        if (m_In->Q.empty ()) { *pMsgLen = 0; return S_FALSE; }
        std::vector<UINT8> CONST &Front = m_In->Q.front ();
        *pMsgLen = (UINT32) Front.size ();
        if (pBuf == nullptr || BufLen < Front.size ()) { return S_OK; }   // caller must retry larger
        std::memcpy (pBuf, Front.data (), Front.size ());
        m_In->Q.pop_front ();
        return S_OK;
    }

private:
    std::shared_ptr<MsgQueue> m_Out;
    std::shared_ptr<MsgQueue> m_In;
};

} // anonymous namespace

HRESULT
CreateInProcConsolePair (IConsoleChannel **ppMachineEnd, IConsoleChannel **ppConsoleEnd)
{
    if (ppMachineEnd == nullptr || ppConsoleEnd == nullptr) { return E_POINTER; }
    auto MtoC = std::make_shared<MsgQueue> ();   // machine -> console
    auto CtoM = std::make_shared<MsgQueue> ();   // console -> machine
    *ppMachineEnd = new InProcEndpoint (MtoC, CtoM);
    *ppConsoleEnd = new InProcEndpoint (CtoM, MtoC);
    return S_OK;
}

} // namespace LibCPU
