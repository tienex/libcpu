/** @file
  Python textual-emit backend. See PythonBackend.h.

  Each ICpuValue is a Python int masked to its width. Build() emits a Python
  script; ICpuCode::Execute marshals the guest state through a temp file and runs
  it on python3 (mutating the buffer in place via memoryviews).
**/
#include "PythonBackend.h"
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

static CONST UINT32 RAM_SIZE = 0x10000;   // assumed guest RAM (6502)

// %S% is sizeof(CPU_STATE), %B% the body. RAM/ST are memoryviews into the file
// buffer, so element writes mutate it in place; the buffer is written back.
static CHAR8 CONST *kTemplate =
    "import sys\n"
    "d=bytearray(open(sys.argv[1],'rb').read())\n"
    "RAM=memoryview(d)[0:65536]\n"
    "ST=memoryview(d)[65536:65536+%S%]\n"
    "def gR(i,b):\n v=0\n for k in range(b//8):v|=ST[i*8+k]<<(8*k)\n return v\n"
    "def pR(i,v,b):\n for k in range(8):ST[i*8+k]=((v>>(8*k))&0xff) if k<b//8 else 0\n"
    "def rM(a,b):\n v=0\n for k in range(b//8):v|=RAM[a+k]<<(8*k)\n return v\n"
    "def wM(a,v,b):\n for k in range(b//8):RAM[a+k]=(v>>(8*k))&0xff\n"
    "def gF(f):return ST[256+f]&1\n"
    "def sF(f,v):ST[256+f]=v&1\n"
    "def sP(p):\n for k in range(8):ST[264+k]=(p>>(8*k))&0xff\n"
    "def sx(v,s):\n if v&(1<<(s-1)):v-=(1<<s)\n return v\n"
    "%B%"
    "RAM.release();ST.release()\n"
    "open(sys.argv[1],'wb').write(d)\n";

static UINT64 Mask (UINT64 V, UINT32 Bits) { return (Bits >= 64) ? V : (V & (((UINT64) 1 << Bits) - 1)); }

class PyValue final : public ComObject<ICpuValue> {
public:
    PyValue (UINT32 Id, UINT32 Bits) : m_Id (Id), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuValue, ppvObject); }
    UINT32 m_Id, m_Bits;
};
class PyBlock final : public ComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuBlock, ppvObject); }
};
static UINT32 IdOf   (ICpuValue *pV) { return static_cast<PyValue *> (pV)->m_Id; }
static UINT32 BitsOf (ICpuValue *pV) { return static_cast<PyValue *> (pV)->m_Bits; }

class PyCode final : public ComObject<ICpuCode> {
public:
    PyCode (std::string Dir) : m_Dir (std::move (Dir)) {}
    ~PyCode () override {
        if (!m_Dir.empty ()) {
            unlink ((m_Dir + "/insn.py").c_str ());
            unlink ((m_Dir + "/state.bin").c_str ());
            rmdir (m_Dir.c_str ());
        }
    }
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuCode, ppvObject); }
    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID * /*pFRF*/) override {
        std::string State = m_Dir + "/state.bin";
        FILE *pF = std::fopen (State.c_str (), "wb");
        if (!pF) return ExecTrap;
        std::fwrite (pRAM, 1, RAM_SIZE, pF);
        std::fwrite (pGRF, 1, sizeof (CPU_STATE), pF);
        std::fclose (pF);

        std::string Cmd = "python3 '" + m_Dir + "/insn.py' '" + State + "' 2>/dev/null";
        if (std::system (Cmd.c_str ()) != 0) return ExecTrap;

        pF = std::fopen (State.c_str (), "rb");
        if (!pF) return ExecOk;
        if (std::fread (pRAM, 1, RAM_SIZE, pF) != RAM_SIZE) { std::fclose (pF); return ExecOk; }
        (void) !std::fread (pGRF, 1, sizeof (CPU_STATE), pF);
        std::fclose (pF);
        return ExecOk;
    }
private:
    std::string m_Dir;
};

class PyEmitter final : public ComObject<ICpuEmitter> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuEmitter, ppvObject); }

    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("t%u=%llu", D, (unsigned long long) Mask (Value, Bits)); return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("t%u=gR(%u,%u)", D, Index, Bits); return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 Bits, BOOLEAN /*Sext*/) override {
        Line ("pR(%u,t%u,%u)", Index, IdOf (pValue), Bits ? Bits : BitsOf (pValue)); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("t%u=rM(t%u,%u)", D, IdOf (pAddr), Bits); return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        Line ("wM(t%u,t%u,%u)", IdOf (pAddr), IdOf (pValue), Bits); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        CONST CHAR8 *pOp;
        switch (Op) {
        case BinAdd: pOp = "+"; break;  case BinSub: pOp = "-"; break;  case BinMul: pOp = "*"; break;
        case BinAnd: pOp = "&"; break;  case BinOr: pOp = "|"; break;   case BinXor: pOp = "^"; break;
        case BinShl: pOp = "<<"; break; case BinLShr: pOp = ">>"; break; case BinAShr: pOp = ">>"; break;
        case BinUDiv: pOp = "//"; break; case BinURem: pOp = "%"; break;
        default: pOp = "+"; break;
        }
        UINT32 D = Fresh ();
        Line ("t%u=(t%u%st%u)&%llu", D, IdOf (pA), pOp, IdOf (pB), (unsigned long long) Mask (~0ull, Bits));
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA), D = Fresh ();
        switch (Op) {
        case UnNeg: Line ("t%u=(-t%u)&%llu", D, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); return Make (D, Bits, ppValue);
        case UnCom: Line ("t%u=(~t%u)&%llu", D, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); return Make (D, Bits, ppValue);
        case UnNot: Line ("t%u=1 if t%u==0 else 0", D, IdOf (pA)); return Make (D, 1, ppValue);
        }
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        CONST CHAR8 *pOp;
        switch (Pred) {
        case CmpEq: pOp = "=="; break; case CmpNe: pOp = "!="; break;
        case CmpULt: case CmpSLt: pOp = "<"; break; case CmpULe: case CmpSLe: pOp = "<="; break;
        case CmpUGt: case CmpSGt: pOp = ">"; break; case CmpUGe: case CmpSGe: pOp = ">="; break;
        default: pOp = "=="; break;
        }
        UINT32 D = Fresh ();
        Line ("t%u=1 if (t%u%st%u) else 0", D, IdOf (pA), pOp, IdOf (pB));
        return Make (D, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA), D = Fresh ();
        switch (Op) {
        case CastTrunc: Line ("t%u=t%u&%llu", D, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); break;
        case CastZExt:  Line ("t%u=t%u", D, IdOf (pA)); break;
        case CastSExt:  Line ("t%u=sx(t%u,%u)&%llu", D, IdOf (pA), SrcBits, (unsigned long long) Mask (~0ull, Bits)); break;
        }
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *, ICpuValue *, ICpuValue *, ICpuValue **ppValue) override { *ppValue = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("t%u=gF(%u)", D, (UINT32) Flag); return Make (D, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        Line ("sF(%u,t%u)", (UINT32) Flag, IdOf (pValue)); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override { Line ("sP(%llu)", (unsigned long long) Pc); return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override { *ppBlock = new PyBlock (); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    ICpuCode *Build () {
        char DirTmpl[] = "/tmp/libcpu_py_XXXXXX";
        if (mkdtemp (DirTmpl) == nullptr) return nullptr;
        std::string Dir = DirTmpl, Src = Dir + "/insn.py";

        std::string Full = kTemplate;
        Subst (Full, "%S%", std::to_string (sizeof (CPU_STATE)));
        Subst (Full, "%B%", m_Body);

        int Fd = open (Src.c_str (), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600);
        if (Fd < 0) { rmdir (Dir.c_str ()); return nullptr; }
        FILE *pF = fdopen (Fd, "w");
        if (!pF) { close (Fd); unlink (Src.c_str ()); rmdir (Dir.c_str ()); return nullptr; }
        std::fwrite (Full.data (), 1, Full.size (), pF);
        std::fclose (pF);
        return new PyCode (Dir);
    }

private:
    UINT32 Fresh () { return m_Next++; }
    void Line (CONST CHAR8 *pFmt, ...) {
        char Buf[256];
        va_list Args; va_start (Args, pFmt);
        std::vsnprintf (Buf, sizeof (Buf), pFmt, Args);
        va_end (Args);
        m_Body += Buf; m_Body += "\n";   // top-level statement, no indent
    }
    static VOID Subst (std::string &S, CHAR8 CONST *Key, std::string CONST &Val) {
        size_t Pos;
        while ((Pos = S.find (Key)) != std::string::npos) S.replace (Pos, std::strlen (Key), Val);
    }
    HRESULT Make (UINT32 Id, UINT32 Bits, ICpuValue **ppValue) { *ppValue = new PyValue (Id, Bits); return S_OK; }

    std::string m_Body;
    UINT32      m_Next = 0;
};

class PythonBackend final : public ComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuBackend, ppvObject); }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "python"; }
    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override { *ppEmitter = new PyEmitter (); return S_OK; }
    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<PyEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *
CreatePythonBackend (VOID)
{
    return new PythonBackend ();
}

} // namespace LibCPU
