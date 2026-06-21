/** @file
  .NET textual-emit backend. See DotnetBackend.h.

  Each ICpuValue is a C# `long` local masked to its width. Build() emits a C#
  class, compiles it with Roslyn (csc) to a MSIL assembly, and ICpuCode::Execute
  marshals the guest state through a temp file and runs it on the CLR (mono).
**/
#include "DotnetBackend.h"
#include "LibCPU/CpuState.h"

#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <string>
#include <atomic>

namespace LibCPU {
namespace {

static CONST UINT32 RAM_SIZE = 0x10000;   // assumed guest RAM (6502)

// %S% is sizeof(CPU_STATE), %B% the emitted body. Logical shifts/unsigned ops use
// ulong casts so it builds on any Roslyn (no dependency on C# 11 '>>>').
static CHAR8 CONST *kTemplate =
    "using System;using System.IO;\n"
    "class Insn{\n"
    " static long gR(byte[] S,int i,int b){long v=0;for(int k=0;k<b/8;k++)v|=((long)(S[i*8+k]&0xff))<<(8*k);return v;}\n"
    " static void pR(byte[] S,int i,long v,int b){for(int k=0;k<8;k++){if(k<b/8)S[i*8+k]=(byte)(((ulong)v>>(8*k))&0xff);else S[i*8+k]=0;}}\n"
    " static long rM(byte[] R,int a,int b){long v=0;for(int k=0;k<b/8;k++)v|=((long)(R[a+k]&0xff))<<(8*k);return v;}\n"
    " static void wM(byte[] R,int a,long v,int b){for(int k=0;k<b/8;k++)R[a+k]=(byte)(((ulong)v>>(8*k))&0xff);}\n"
    " static long gF(byte[] S,int f){return S[256+f]&1;}\n"
    " static void sF(byte[] S,int f,long v){S[256+f]=(byte)(v&1);}\n"
    " static void sP(byte[] S,long p){for(int k=0;k<8;k++)S[264+k]=(byte)(((ulong)p>>(8*k))&0xff);}\n"
    " static void Main(string[] A){\n"
    "  byte[] X=File.ReadAllBytes(A[0]);\n"
    "  byte[] R=new byte[65536];Array.Copy(X,0,R,0,65536);\n"
    "  byte[] S=new byte[%S%];Array.Copy(X,65536,S,0,%S%);\n"
    "%B%"
    "  Array.Copy(R,0,X,0,65536);Array.Copy(S,0,X,65536,%S%);\n"
    "  File.WriteAllBytes(A[0],X);\n"
    " }\n"
    "}\n";

static UINT64 Mask (UINT64 V, UINT32 Bits) { return (Bits >= 64) ? V : (V & (((UINT64) 1 << Bits) - 1)); }

class DnValue final : public ComObject<ICpuValue> {
public:
    DnValue (UINT32 Id, UINT32 Bits) : m_Id (Id), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    UINT32 m_Id, m_Bits;
};

class DnBlock final : public ComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
};

static UINT32 IdOf   (ICpuValue *pV) { return static_cast<DnValue *> (pV)->m_Id; }
static UINT32 BitsOf (ICpuValue *pV) { return static_cast<DnValue *> (pV)->m_Bits; }

class DnCode final : public ComObject<ICpuCode> {
public:
    DnCode (std::string Dir, std::string Runner) : m_Dir (std::move (Dir)), m_Runner (std::move (Runner)) {}
    ~DnCode () override {
        if (!m_Dir.empty ()) {
            unlink ((m_Dir + "/Insn.cs").c_str ());
            unlink ((m_Dir + "/insn.exe").c_str ());
            unlink ((m_Dir + "/state.bin").c_str ());
            rmdir (m_Dir.c_str ());
        }
    }
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuCode, ppvObject);
    }
    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID * /*pFRF*/) override {
        std::string State = m_Dir + "/state.bin";
        FILE *pF = std::fopen (State.c_str (), "wb");
        if (!pF) return ExecTrap;
        std::fwrite (pRAM, 1, RAM_SIZE, pF);
        std::fwrite (pGRF, 1, sizeof (CPU_STATE), pF);
        std::fclose (pF);

        std::string Cmd = m_Runner + " '" + m_Dir + "/insn.exe' '" + State + "' 2>/dev/null";
        if (std::system (Cmd.c_str ()) != 0) return ExecTrap;

        pF = std::fopen (State.c_str (), "rb");
        if (!pF) return ExecOk;
        if (std::fread (pRAM, 1, RAM_SIZE, pF) != RAM_SIZE) { std::fclose (pF); return ExecOk; }
        (void) !std::fread (pGRF, 1, sizeof (CPU_STATE), pF);
        std::fclose (pF);
        return ExecOk;
    }
private:
    std::string m_Dir, m_Runner;
};

class DnEmitter final : public ComObject<ICpuEmitter> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }

    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("long t%u=%lld;", D, (long long) Mask (Value, Bits)); return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("long t%u=gR(S,%u,%u);", D, Index, Bits); return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 Bits, BOOLEAN /*Sext*/) override {
        Line ("pR(S,%u,t%u,%u);", Index, IdOf (pValue), Bits ? Bits : BitsOf (pValue)); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("long t%u=rM(R,(int)t%u,%u);", D, IdOf (pAddr), Bits); return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        Line ("wM(R,(int)t%u,t%u,%u);", IdOf (pAddr), IdOf (pValue), Bits); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA), D = Fresh (), A = IdOf (pA), B = IdOf (pB);
        unsigned long long M = (unsigned long long) Mask (~0ull, Bits);
        switch (Op) {
        case BinAdd: Line ("long t%u=(t%u+t%u)&%llu;", D, A, B, M); break;
        case BinSub: Line ("long t%u=(t%u-t%u)&%llu;", D, A, B, M); break;
        case BinMul: Line ("long t%u=(t%u*t%u)&%llu;", D, A, B, M); break;
        case BinAnd: Line ("long t%u=(t%u&t%u)&%llu;", D, A, B, M); break;
        case BinOr:  Line ("long t%u=(t%u|t%u)&%llu;", D, A, B, M); break;
        case BinXor: Line ("long t%u=(t%u^t%u)&%llu;", D, A, B, M); break;
        case BinShl: Line ("long t%u=(t%u<<(int)t%u)&%llu;", D, A, B, M); break;
        case BinLShr:Line ("long t%u=(long)((ulong)t%u>>(int)t%u)&%llu;", D, A, B, M); break;
        case BinAShr:Line ("long t%u=(t%u>>(int)t%u)&%llu;", D, A, B, M); break;
        case BinUDiv:Line ("long t%u=(long)((ulong)t%u/(ulong)t%u)&%llu;", D, A, B, M); break;
        case BinURem:Line ("long t%u=(long)((ulong)t%u%%(ulong)t%u)&%llu;", D, A, B, M); break;
        default:     Line ("long t%u=(t%u+t%u)&%llu;", D, A, B, M); break;
        }
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA), D = Fresh ();
        unsigned long long M = (unsigned long long) Mask (~0ull, Bits);
        switch (Op) {
        case UnNeg: Line ("long t%u=(-t%u)&%llu;", D, IdOf (pA), M); return Make (D, Bits, ppValue);
        case UnCom: Line ("long t%u=(~t%u)&%llu;", D, IdOf (pA), M); return Make (D, Bits, ppValue);
        case UnNot: Line ("long t%u=(t%u==0)?1:0;", D, IdOf (pA)); return Make (D, 1, ppValue);
        // floating-point ops: unsupported by this backend
        default: return E_INVALIDARG;
        }
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 D = Fresh (), A = IdOf (pA), B = IdOf (pB);
        switch (Pred) {
        case CmpEq:  Line ("long t%u=(t%u==t%u)?1:0;", D, A, B); break;
        case CmpNe:  Line ("long t%u=(t%u!=t%u)?1:0;", D, A, B); break;
        case CmpULt: Line ("long t%u=((ulong)t%u<(ulong)t%u)?1:0;", D, A, B); break;
        case CmpULe: Line ("long t%u=((ulong)t%u<=(ulong)t%u)?1:0;", D, A, B); break;
        case CmpUGt: Line ("long t%u=((ulong)t%u>(ulong)t%u)?1:0;", D, A, B); break;
        case CmpUGe: Line ("long t%u=((ulong)t%u>=(ulong)t%u)?1:0;", D, A, B); break;
        case CmpSLt: Line ("long t%u=(t%u<t%u)?1:0;", D, A, B); break;
        case CmpSLe: Line ("long t%u=(t%u<=t%u)?1:0;", D, A, B); break;
        case CmpSGt: Line ("long t%u=(t%u>t%u)?1:0;", D, A, B); break;
        case CmpSGe: Line ("long t%u=(t%u>=t%u)?1:0;", D, A, B); break;
        // floating-point ops: unsupported by this backend
        default: return E_INVALIDARG;
        }
        return Make (D, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA), D = Fresh ();
        unsigned long long M = (unsigned long long) Mask (~0ull, Bits);
        switch (Op) {
        case CastTrunc: Line ("long t%u=t%u&%llu;", D, IdOf (pA), M); break;
        case CastZExt:  Line ("long t%u=t%u;", D, IdOf (pA)); break;
        case CastSExt:  Line ("long t%u=((t%u<<%u)>>%u)&%llu;", D, IdOf (pA), 64 - SrcBits, 64 - SrcBits, M); break;
        // floating-point ops: unsupported by this backend
        default: return E_INVALIDARG;
        }
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *, ICpuValue *, ICpuValue *, ICpuValue **ppValue) override { *ppValue = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("long t%u=gF(S,%u);", D, (UINT32) Flag); return Make (D, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        Line ("sF(S,%u,t%u);", (UINT32) Flag, IdOf (pValue)); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override { Line ("sP(S,%lld);", (long long) Pc); return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override { *ppBlock = new DnBlock (); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    ICpuCode *Build () {
        // Locate a Roslyn C# compiler and a CLR runner.
        std::string Csc = Which ("csc"), Runner = Which ("mono");
        if (Csc.empty () || Runner.empty ()) return nullptr;

        char DirTmpl[] = "/tmp/libcpu_dotnet_XXXXXX";
        if (mkdtemp (DirTmpl) == nullptr) return nullptr;
        std::string Dir = DirTmpl, Src = Dir + "/Insn.cs", Exe = Dir + "/insn.exe";

        std::string Full = kTemplate;
        Subst (Full, "%S%", std::to_string (sizeof (CPU_STATE)));
        Subst (Full, "%B%", m_Body);

        int Fd = open (Src.c_str (), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600);
        if (Fd < 0) { rmdir (Dir.c_str ()); return nullptr; }
        FILE *pF = fdopen (Fd, "w");
        if (!pF) { close (Fd); unlink (Src.c_str ()); rmdir (Dir.c_str ()); return nullptr; }
        std::fwrite (Full.data (), 1, Full.size (), pF);
        std::fclose (pF);

        std::string Cmd = "'" + Csc + "' -nologo -out:'" + Exe + "' '" + Src + "' 2>/dev/null";
        if (std::system (Cmd.c_str ()) != 0) { unlink (Src.c_str ()); rmdir (Dir.c_str ()); return nullptr; }
        return new DnCode (Dir, "'" + Runner + "'");
    }

private:
    static std::string Which (CHAR8 CONST *Name) {
        std::string Cmd = std::string ("command -v ") + Name + " 2>/dev/null";
        FILE *pP = popen (Cmd.c_str (), "r");
        if (!pP) return "";
        char Buf[512]; std::string Out;
        if (std::fgets (Buf, sizeof (Buf), pP)) Out = Buf;
        pclose (pP);
        while (!Out.empty () && (Out.back () == '\n' || Out.back () == '\r')) Out.pop_back ();
        return Out;
    }
    UINT32 Fresh () { return m_Next++; }
    void Line (CONST CHAR8 *pFmt, ...) {
        char Buf[256];
        va_list Args; va_start (Args, pFmt);
        std::vsnprintf (Buf, sizeof (Buf), pFmt, Args);
        va_end (Args);
        m_Body += "  "; m_Body += Buf; m_Body += "\n";
    }
    static VOID Subst (std::string &S, CHAR8 CONST *Key, std::string CONST &Val) {
        size_t Pos;
        while ((Pos = S.find (Key)) != std::string::npos) S.replace (Pos, std::strlen (Key), Val);
    }
    HRESULT Make (UINT32 Id, UINT32 Bits, ICpuValue **ppValue) { *ppValue = new DnValue (Id, Bits); return S_OK; }

    std::string m_Body;
    UINT32      m_Next = 0;
};

class DotnetBackend final : public ComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBackend, ppvObject);
    }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "dotnet"; }
    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override {
        *ppEmitter = new DnEmitter (); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<DnEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *
CreateDotnetBackend (VOID)
{
    return new DotnetBackend ();
}

} // namespace LibCPU
