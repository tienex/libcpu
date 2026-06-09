/** @file  Swift textual-emit backend. See SwiftBackend.h. Values are Swift Int
  (tN) masked to width; d is the marshalled state Data buffer (RAM then state). **/
#include "SwiftBackend.h"
#include "LibCPU/CpuState.h"
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <string>

namespace LibCPU {
namespace {

static CONST UINT32 RAM_SIZE = 0x10000;

static CHAR8 CONST *kTemplate =
    "import Foundation\n"
    "let url=URL(fileURLWithPath:CommandLine.arguments[1])\n"
    "var d=try! Data(contentsOf:url)\n"
    "func gR(_ i:Int,_ b:Int)->Int{var v=0;for k in 0..<(b/8){v|=Int(d[65536+i*8+k])<<(8*k)};return v}\n"
    "func pR(_ i:Int,_ v:Int,_ b:Int){for k in 0..<8{d[65536+i*8+k]=k<(b/8) ? UInt8((v>>(8*k))&0xff):0}}\n"
    "func rM(_ a:Int,_ b:Int)->Int{var v=0;for k in 0..<(b/8){v|=Int(d[a+k])<<(8*k)};return v}\n"
    "func wM(_ a:Int,_ v:Int,_ b:Int){for k in 0..<(b/8){d[a+k]=UInt8((v>>(8*k))&0xff)}}\n"
    "func gF(_ f:Int)->Int{return Int(d[65536+256+f])&1}\n"
    "func sF(_ f:Int,_ v:Int){d[65536+256+f]=UInt8(v&1)}\n"
    "func sP(_ p:Int){for k in 0..<8{d[65536+264+k]=UInt8((p>>(8*k))&0xff)}}\n"
    "func sx(_ v:Int,_ s:Int)->Int{return (v&(1<<(s-1))) != 0 ? v-(1<<s):v}\n"
    "%B%"
    "try! d.write(to:url)\n";

static UINT64 Mask (UINT64 V, UINT32 Bits) { return (Bits >= 64) ? V : (V & (((UINT64) 1 << Bits) - 1)); }

class SwValue final : public LcComObject<ICpuValue> {
public:
    SwValue (UINT32 Id, UINT32 Bits) : m_Id (Id), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuValue, ppvObject); }
    UINT32 m_Id, m_Bits;
};
class SwBlock final : public LcComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuBlock, ppvObject); }
};
static UINT32 IdOf   (ICpuValue *pV) { return static_cast<SwValue *> (pV)->m_Id; }
static UINT32 BitsOf (ICpuValue *pV) { return static_cast<SwValue *> (pV)->m_Bits; }

class SwCode final : public LcComObject<ICpuCode> {
public:
    SwCode (std::string Dir) : m_Dir (std::move (Dir)) {}
    ~SwCode () override {
        if (!m_Dir.empty ()) { unlink ((m_Dir + "/insn.swift").c_str ()); unlink ((m_Dir + "/state.bin").c_str ()); rmdir (m_Dir.c_str ()); }
    }
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuCode, ppvObject); }
    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID * /*pFRF*/) override {
        std::string St = m_Dir + "/state.bin";
        FILE *pF = std::fopen (St.c_str (), "wb"); if (!pF) return ExecTrap;
        std::fwrite (pRAM, 1, RAM_SIZE, pF); std::fwrite (pGRF, 1, sizeof (CPU_STATE), pF); std::fclose (pF);
        std::string Cmd = "swift '" + m_Dir + "/insn.swift' '" + St + "' 2>/dev/null";
        if (std::system (Cmd.c_str ()) != 0) return ExecTrap;
        pF = std::fopen (St.c_str (), "rb"); if (!pF) return ExecOk;
        if (std::fread (pRAM, 1, RAM_SIZE, pF) != RAM_SIZE) { std::fclose (pF); return ExecOk; }
        (void) !std::fread (pGRF, 1, sizeof (CPU_STATE), pF); std::fclose (pF);
        return ExecOk;
    }
private:
    std::string m_Dir;
};

class SwEmitter final : public LcComObject<ICpuEmitter> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuEmitter, ppvObject); }

    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("let t%u=%llu", D, (unsigned long long) Mask (Value, Bits)); return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("let t%u=gR(%u,%u)", D, Index, Bits); return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 Bits, BOOLEAN) override {
        Line ("pR(%u,t%u,%u)", Index, IdOf (pValue), Bits ? Bits : BitsOf (pValue)); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("let t%u=rM(t%u,%u)", D, IdOf (pAddr), Bits); return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        Line ("wM(t%u,t%u,%u)", IdOf (pAddr), IdOf (pValue), Bits); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        CONST CHAR8 *o;
        switch (Op) {
        case BinAdd:o="+";break; case BinSub:o="-";break; case BinMul:o="*";break;
        case BinAnd:o="&";break; case BinOr:o="|";break; case BinXor:o="^";break;
        case BinShl:o="<<";break; case BinLShr:o=">>";break; case BinAShr:o=">>";break;
        case BinUDiv:o="/";break; case BinURem:o="%";break; default:o="+";break;
        }
        UINT32 D = Fresh ();
        Line ("let t%u=(t%u%st%u)&%llu", D, IdOf (pA), o, IdOf (pB), (unsigned long long) Mask (~0ull, Bits));
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA), D = Fresh ();
        switch (Op) {
        case UnNeg: Line ("let t%u=(0-t%u)&%llu", D, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); return Make (D, Bits, ppValue);
        case UnCom: Line ("let t%u=(~t%u)&%llu", D, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); return Make (D, Bits, ppValue);
        case UnNot: Line ("let t%u=(t%u==0) ? 1:0", D, IdOf (pA)); return Make (D, 1, ppValue);
        }
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        CONST CHAR8 *o;
        switch (Pred) {
        case CmpEq:o="==";break; case CmpNe:o="!=";break;
        case CmpULt:case CmpSLt:o="<";break; case CmpULe:case CmpSLe:o="<=";break;
        case CmpUGt:case CmpSGt:o=">";break; case CmpUGe:case CmpSGe:o=">=";break; default:o="==";break;
        }
        UINT32 D = Fresh (); Line ("let t%u=(t%u %s t%u) ? 1:0", D, IdOf (pA), o, IdOf (pB)); return Make (D, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 Src = BitsOf (pA), D = Fresh ();
        switch (Op) {
        case CastTrunc: Line ("let t%u=t%u&%llu", D, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); break;
        case CastZExt:  Line ("let t%u=t%u", D, IdOf (pA)); break;
        case CastSExt:  Line ("let t%u=sx(t%u,%u)&%llu", D, IdOf (pA), Src, (unsigned long long) Mask (~0ull, Bits)); break;
        }
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *, ICpuValue *, ICpuValue *, ICpuValue **ppValue) override { *ppValue = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("let t%u=gF(%u)", D, (UINT32) Flag); return Make (D, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override { Line ("sF(%u,t%u)", (UINT32) Flag, IdOf (pValue)); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override { Line ("sP(%llu)", (unsigned long long) Pc); return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override { *ppBlock = new SwBlock (); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    ICpuCode *Build () {
        char DirTmpl[] = "/tmp/libcpu_sw_XXXXXX";
        if (mkdtemp (DirTmpl) == nullptr) return nullptr;
        std::string Dir = DirTmpl, Src = Dir + "/insn.swift", Full = kTemplate;
        Subst (Full, "%B%", m_Body);
        int Fd = open (Src.c_str (), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600);
        if (Fd < 0) { rmdir (Dir.c_str ()); return nullptr; }
        FILE *pF = fdopen (Fd, "w"); if (!pF) { close (Fd); unlink (Src.c_str ()); rmdir (Dir.c_str ()); return nullptr; }
        std::fwrite (Full.data (), 1, Full.size (), pF); std::fclose (pF);
        return new SwCode (Dir);
    }
private:
    UINT32 Fresh () { return m_Next++; }
    void Line (CONST CHAR8 *pFmt, ...) {
        char Buf[256]; va_list A; va_start (A, pFmt); std::vsnprintf (Buf, sizeof (Buf), pFmt, A); va_end (A);
        m_Body += Buf; m_Body += "\n";
    }
    static VOID Subst (std::string &S, CHAR8 CONST *K, std::string CONST &V) { size_t P; while ((P = S.find (K)) != std::string::npos) S.replace (P, std::strlen (K), V); }
    HRESULT Make (UINT32 Id, UINT32 Bits, ICpuValue **ppValue) { *ppValue = new SwValue (Id, Bits); return S_OK; }
    std::string m_Body; UINT32 m_Next = 0;
};

class SwiftBackend final : public LcComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuBackend, ppvObject); }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "swift"; }
    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override { *ppEmitter = new SwEmitter (); return S_OK; }
    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<SwEmitter *> (pEmitter)->Build (); *ppCode = pCode; return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *CreateSwiftBackend (VOID) { return new SwiftBackend (); }

} // namespace LibCPU
