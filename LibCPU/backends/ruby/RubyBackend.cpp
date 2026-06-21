/** @file
  Ruby textual-emit backend. See RubyBackend.h.

  Each ICpuValue is a Ruby integer variable (tN) masked to its width. The
  marshalled state file is read into the global byte array $d (RAM bytes then
  CPU_STATE bytes); the emitted body mutates it and it is written back.
**/
#include "RubyBackend.h"
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
// Fixed harness: read the state file into $d, define register/memory/flag
// accessors, run the emitted body (%B%), then write $d back.
//
static CHAR8 CONST *kTemplate =
    "$d=File.binread(ARGV[0]).bytes\n"
    "def gR(i,b) v=0;(0...(b/8)).each{|k| v|=$d[65536+i*8+k]<<(8*k)};v end\n"
    "def pR(i,v,b) (0...8).each{|k| $d[65536+i*8+k]=(k<b/8) ? ((v>>(8*k))&0xff):0} end\n"
    "def rM(a,b) v=0;(0...(b/8)).each{|k| v|=$d[a+k]<<(8*k)};v end\n"
    "def wM(a,v,b) (0...(b/8)).each{|k| $d[a+k]=(v>>(8*k))&0xff} end\n"
    "def gF(f) $d[65536+256+f]&1 end\n"
    "def sF(f,v) $d[65536+256+f]=v&1 end\n"
    "def sP(p) (0...8).each{|k| $d[65536+264+k]=(p>>(8*k))&0xff} end\n"
    "def sx(v,s) (v&(1<<(s-1)))!=0 ? v-(1<<s):v end\n"
    "%B%"
    "File.binwrite(ARGV[0],$d.pack('C*'))\n";

static UINT64
Mask (UINT64 Value, UINT32 Bits)
{
    return (Bits >= 64) ? Value : (Value & (((UINT64) 1 << Bits) - 1));
}

class RubyValue final : public ComObject<ICpuValue> {
public:
    RubyValue (UINT32 Id, UINT32 Bits) : m_Id (Id), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    UINT32 m_Id;
    UINT32 m_Bits;
};

class RubyBlock final : public ComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
};

static UINT32 IdOf   (ICpuValue *pValue) { return static_cast<RubyValue *> (pValue)->m_Id; }
static UINT32 BitsOf (ICpuValue *pValue) { return static_cast<RubyValue *> (pValue)->m_Bits; }

class RubyCode final : public ComObject<ICpuCode> {
public:
    RubyCode (std::string Dir) : m_Dir (std::move (Dir)) {}

    ~RubyCode () override {
        if (!m_Dir.empty ()) {
            unlink ((m_Dir + "/insn.rb").c_str ());
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

        std::string Cmd = "ruby '" + m_Dir + "/insn.rb' '" + State + "' 2>/dev/null";
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

class RubyEmitter final : public ComObject<ICpuEmitter> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }

    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        Line ("t%u=%llu", Dest, (unsigned long long) Mask (Value, Bits));
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        Line ("t%u=gR(%u,%u)", Dest, Index, Bits);
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 Bits, BOOLEAN /*Sext*/) override {
        Line ("pR(%u,t%u,%u)", Index, IdOf (pValue), Bits ? Bits : BitsOf (pValue));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        Line ("t%u=rM(t%u,%u)", Dest, IdOf (pAddr), Bits);
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
        Line ("t%u=(t%u%st%u)&%llu", Dest, IdOf (pA), pOp, IdOf (pB),
              (unsigned long long) Mask (~0ull, Bits));
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        UINT32 Dest = Fresh ();
        unsigned long long FullMask = (unsigned long long) Mask (~0ull, Bits);
        switch (Op) {
        case UnNeg:
            Line ("t%u=(-t%u)&%llu", Dest, IdOf (pA), FullMask);
            return Make (Dest, Bits, ppValue);
        case UnCom:
            Line ("t%u=(~t%u)&%llu", Dest, IdOf (pA), FullMask);
            return Make (Dest, Bits, ppValue);
        case UnNot:
            Line ("t%u=(t%u==0) ? 1:0", Dest, IdOf (pA));
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
        Line ("t%u=(t%u%st%u) ? 1:0", Dest, IdOf (pA), pOp, IdOf (pB));
        return Make (Dest, 1, ppValue);
    }

    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA);
        UINT32 Dest = Fresh ();
        unsigned long long FullMask = (unsigned long long) Mask (~0ull, Bits);
        switch (Op) {
        case CastTrunc:
            Line ("t%u=t%u&%llu", Dest, IdOf (pA), FullMask);
            break;
        case CastZExt:
            Line ("t%u=t%u", Dest, IdOf (pA));
            break;
        case CastSExt:
            Line ("t%u=sx(t%u,%u)&%llu", Dest, IdOf (pA), SrcBits, FullMask);
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
        Line ("t%u=gF(%u)", Dest, (UINT32) Flag);
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
        *ppBlock = new RubyBlock ();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    ICpuCode *Build () {
        char DirTemplate[] = "/tmp/libcpu_rb_XXXXXX";
        if (mkdtemp (DirTemplate) == nullptr) {
            return nullptr;
        }
        std::string Dir = DirTemplate;
        std::string Source = Dir + "/insn.rb";

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
        return new RubyCode (Dir);
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
        *ppValue = new RubyValue (Id, Bits);
        return S_OK;
    }

    std::string m_Body;
    UINT32      m_Next = 0;
};

class RubyBackend final : public ComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBackend, ppvObject);
    }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "ruby"; }

    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override {
        *ppEmitter = new RubyEmitter ();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<RubyEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *
CreateRubyBackend (VOID)
{
    return new RubyBackend ();
}

} // namespace LibCPU
