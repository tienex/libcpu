/** @file
  Swift textual-emit backend. See SwiftBackend.h.

  Each ICpuValue is a Swift Int constant (tN) masked to its width. The marshalled
  state file is read into the Foundation Data buffer d (RAM bytes then CPU_STATE
  bytes); the emitted body mutates it and it is written back. Comparison operators
  are emitted with surrounding spaces because Swift lexes "t4!=t5" as the
  force-unwrap "t4!" "= t5" rather than "!=".
**/
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

static CONST UINT32 RAM_SIZE = 0x10000;   // assumed guest RAM (6502 / CHIP-8)

//
// Fixed harness: read the state file into the Data buffer d, define
// register/memory/flag accessors, run the emitted body (%B%), then write d back.
//
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

static UINT64
Mask (UINT64 Value, UINT32 Bits)
{
    return (Bits >= 64) ? Value : (Value & (((UINT64) 1 << Bits) - 1));
}

class SwiftValue final : public ComObject<ICpuValue> {
public:
    SwiftValue (UINT32 Id, UINT32 Bits) : m_Id (Id), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    UINT32 m_Id;
    UINT32 m_Bits;
};

class SwiftBlock final : public ComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
};

static UINT32 IdOf   (ICpuValue *pValue) { return static_cast<SwiftValue *> (pValue)->m_Id; }
static UINT32 BitsOf (ICpuValue *pValue) { return static_cast<SwiftValue *> (pValue)->m_Bits; }

class SwiftCode final : public ComObject<ICpuCode> {
public:
    SwiftCode (std::string Dir) : m_Dir (std::move (Dir)) {}

    ~SwiftCode () override {
        if (!m_Dir.empty ()) {
            unlink ((m_Dir + "/insn.swift").c_str ());
            unlink ((m_Dir + "/state.bin").c_str ());
            rmdir (m_Dir.c_str ());
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuCode, ppvObject);
    }

    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID * /*pFRF*/) override {
        std::string State = m_Dir + "/state.bin";

        FILE *pFile = std::fopen (State.c_str (), "wb");
        if (pFile == nullptr) {
            return ExecTrap;
        }
        std::fwrite (pRAM, 1, RAM_SIZE, pFile);
        std::fwrite (pGRF, 1, sizeof (CPU_STATE), pFile);
        std::fclose (pFile);

        std::string Cmd = "swift '" + m_Dir + "/insn.swift' '" + State + "' 2>/dev/null";
        if (std::system (Cmd.c_str ()) != 0) {
            return ExecTrap;
        }

        pFile = std::fopen (State.c_str (), "rb");
        if (pFile == nullptr) {
            return ExecOk;
        }
        if (std::fread (pRAM, 1, RAM_SIZE, pFile) != RAM_SIZE) {
            std::fclose (pFile);
            return ExecOk;
        }
        (void) !std::fread (pGRF, 1, sizeof (CPU_STATE), pFile);
        std::fclose (pFile);
        return ExecOk;
    }

private:
    std::string m_Dir;
};

class SwiftEmitter final : public ComObject<ICpuEmitter> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }

    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        Line ("let t%u=%llu", Dest, (unsigned long long) Mask (Value, Bits));
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        Line ("let t%u=gR(%u,%u)", Dest, Index, Bits);
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 Bits, BOOLEAN /*Sext*/) override {
        Line ("pR(%u,t%u,%u)", Index, IdOf (pValue), Bits ? Bits : BitsOf (pValue));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        Line ("let t%u=rM(t%u,%u)", Dest, IdOf (pAddr), Bits);
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        Line ("wM(t%u,t%u,%u)", IdOf (pAddr), IdOf (pValue), Bits);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        CONST CHAR8 *pOp;
        switch (Op) {
        case BinAdd:  pOp = "+";  break;
        case BinSub:  pOp = "-";  break;
        case BinMul:  pOp = "*";  break;
        case BinAnd:  pOp = "&";  break;
        case BinOr:   pOp = "|";  break;
        case BinXor:  pOp = "^";  break;
        case BinShl:  pOp = "<<"; break;
        case BinLShr: pOp = ">>"; break;
        case BinAShr: pOp = ">>"; break;
        case BinUDiv: pOp = "/";  break;
        case BinURem: pOp = "%";  break;
        default:      pOp = "+";  break;
        }
        UINT32 Dest = Fresh ();
        Line ("let t%u=(t%u%st%u)&%llu", Dest, IdOf (pA), pOp, IdOf (pB),
              (unsigned long long) Mask (~0ull, Bits));
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        UINT32 Dest = Fresh ();
        unsigned long long FullMask = (unsigned long long) Mask (~0ull, Bits);
        switch (Op) {
        case UnNeg:
            Line ("let t%u=(0-t%u)&%llu", Dest, IdOf (pA), FullMask);
            return Make (Dest, Bits, ppValue);
        case UnCom:
            Line ("let t%u=(~t%u)&%llu", Dest, IdOf (pA), FullMask);
            return Make (Dest, Bits, ppValue);
        case UnNot:
            Line ("let t%u=(t%u==0) ? 1:0", Dest, IdOf (pA));
            return Make (Dest, 1, ppValue);
        // floating-point ops: unsupported by this backend
        default: return E_INVALIDARG;
        }
        return E_INVALIDARG;
    }

    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        CONST CHAR8 *pOp;
        switch (Pred) {
        case CmpEq:  pOp = "==";  break;
        case CmpNe:  pOp = "!=";  break;
        case CmpULt: case CmpSLt: pOp = "<";  break;
        case CmpULe: case CmpSLe: pOp = "<="; break;
        case CmpUGt: case CmpSGt: pOp = ">";  break;
        case CmpUGe: case CmpSGe: pOp = ">="; break;
        default:     pOp = "==";  break;
        }
        UINT32 Dest = Fresh ();
        // Spaces required: Swift would otherwise lex "t!=u" as force-unwrap "t!".
        Line ("let t%u=(t%u %s t%u) ? 1:0", Dest, IdOf (pA), pOp, IdOf (pB));
        return Make (Dest, 1, ppValue);
    }

    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA);
        UINT32 Dest = Fresh ();
        unsigned long long FullMask = (unsigned long long) Mask (~0ull, Bits);
        switch (Op) {
        case CastTrunc:
            Line ("let t%u=t%u&%llu", Dest, IdOf (pA), FullMask);
            break;
        case CastZExt:
            Line ("let t%u=t%u", Dest, IdOf (pA));
            break;
        case CastSExt:
            Line ("let t%u=sx(t%u,%u)&%llu", Dest, IdOf (pA), SrcBits, FullMask);
            break;
        // floating-point ops: unsupported by this backend
        default: return E_INVALIDARG;
        }
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE Select (ICpuValue *, ICpuValue *, ICpuValue *, ICpuValue **ppValue) override {
        *ppValue = nullptr;
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        Line ("let t%u=gF(%u)", Dest, (UINT32) Flag);
        return Make (Dest, 1, ppValue);
    }

    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        Line ("sF(%u,t%u)", (UINT32) Flag, IdOf (pValue));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override {
        Line ("sP(%llu)", (unsigned long long) Pc);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override {
        *ppBlock = new SwiftBlock ();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    ICpuCode *Build () {
        char DirTemplate[] = "/tmp/libcpu_sw_XXXXXX";
        if (mkdtemp (DirTemplate) == nullptr) {
            return nullptr;
        }
        std::string Dir = DirTemplate;
        std::string Source = Dir + "/insn.swift";

        std::string Full = kTemplate;
        Subst (Full, "%B%", m_Body);

        int Fd = open (Source.c_str (), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600);
        if (Fd < 0) {
            rmdir (Dir.c_str ());
            return nullptr;
        }
        FILE *pFile = fdopen (Fd, "w");
        if (pFile == nullptr) {
            close (Fd);
            unlink (Source.c_str ());
            rmdir (Dir.c_str ());
            return nullptr;
        }
        std::fwrite (Full.data (), 1, Full.size (), pFile);
        std::fclose (pFile);
        return new SwiftCode (Dir);
    }

private:
    UINT32 Fresh () { return m_Next++; }

    void Line (CONST CHAR8 *pFmt, ...) {
        char Buf[256];
        va_list Args;
        va_start (Args, pFmt);
        std::vsnprintf (Buf, sizeof (Buf), pFmt, Args);
        va_end (Args);
        m_Body += Buf;
        m_Body += "\n";
    }

    static VOID Subst (std::string &Str, CHAR8 CONST *pKey, std::string CONST &Val) {
        size_t Pos;
        while ((Pos = Str.find (pKey)) != std::string::npos) {
            Str.replace (Pos, std::strlen (pKey), Val);
        }
    }

    HRESULT Make (UINT32 Id, UINT32 Bits, ICpuValue **ppValue) {
        *ppValue = new SwiftValue (Id, Bits);
        return S_OK;
    }

    std::string m_Body;
    UINT32      m_Next = 0;
};

class SwiftBackend final : public ComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBackend, ppvObject);
    }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "swift"; }

    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override {
        *ppEmitter = new SwiftEmitter ();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<SwiftEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *
CreateSwiftBackend (VOID)
{
    return new SwiftBackend ();
}

} // namespace LibCPU
