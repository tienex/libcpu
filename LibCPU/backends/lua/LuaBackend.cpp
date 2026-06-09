/** @file  Lua textual-emit backend. See LuaBackend.h. Values are Lua integers
  (tN) masked to width; d is the 1-indexed state byte table (RAM then state). **/
#include "LuaBackend.h"
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
    "local p=arg[1]\n"
    "local f=io.open(p,'rb');local s=f:read('a');f:close()\n"
    "local d={};for i=1,#s do d[i]=s:byte(i) end\n"
    "local function gR(i,b) local v=0;for k=0,b//8-1 do v=v|(d[65536+i*8+k+1]<<(8*k)) end return v end\n"
    "local function pR(i,v,b) for k=0,7 do d[65536+i*8+k+1]=(k<b//8) and ((v>>(8*k))&0xff) or 0 end end\n"
    "local function rM(a,b) local v=0;for k=0,b//8-1 do v=v|(d[a+k+1]<<(8*k)) end return v end\n"
    "local function wM(a,v,b) for k=0,b//8-1 do d[a+k+1]=(v>>(8*k))&0xff end end\n"
    "local function gF(x) return d[65536+256+x+1]&1 end\n"
    "local function sF(x,v) d[65536+256+x+1]=v&1 end\n"
    "local function sP(pc) for k=0,7 do d[65536+264+k+1]=(pc>>(8*k))&0xff end end\n"
    "local function sx(v,s) if (v&(1<<(s-1)))~=0 then return v-(1<<s) else return v end end\n"
    "%B%"
    "local o={};for i=1,#d do o[i]=string.char(d[i]) end\n"
    "local w=io.open(p,'wb');w:write(table.concat(o));w:close()\n";

static UINT64 Mask (UINT64 V, UINT32 Bits) { return (Bits >= 64) ? V : (V & (((UINT64) 1 << Bits) - 1)); }

class LuValue final : public LcComObject<ICpuValue> {
public:
    LuValue (UINT32 Id, UINT32 Bits) : m_Id (Id), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuValue, ppvObject); }
    UINT32 m_Id, m_Bits;
};
class LuBlock final : public LcComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuBlock, ppvObject); }
};
static UINT32 IdOf   (ICpuValue *pV) { return static_cast<LuValue *> (pV)->m_Id; }
static UINT32 BitsOf (ICpuValue *pV) { return static_cast<LuValue *> (pV)->m_Bits; }

class LuCode final : public LcComObject<ICpuCode> {
public:
    LuCode (std::string Dir) : m_Dir (std::move (Dir)) {}
    ~LuCode () override {
        if (!m_Dir.empty ()) { unlink ((m_Dir + "/insn.lua").c_str ()); unlink ((m_Dir + "/state.bin").c_str ()); rmdir (m_Dir.c_str ()); }
    }
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuCode, ppvObject); }
    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID * /*pFRF*/) override {
        std::string St = m_Dir + "/state.bin";
        FILE *pF = std::fopen (St.c_str (), "wb"); if (!pF) return ExecTrap;
        std::fwrite (pRAM, 1, RAM_SIZE, pF); std::fwrite (pGRF, 1, sizeof (CPU_STATE), pF); std::fclose (pF);
        std::string Cmd = "lua '" + m_Dir + "/insn.lua' '" + St + "' 2>/dev/null";
        if (std::system (Cmd.c_str ()) != 0) return ExecTrap;
        pF = std::fopen (St.c_str (), "rb"); if (!pF) return ExecOk;
        if (std::fread (pRAM, 1, RAM_SIZE, pF) != RAM_SIZE) { std::fclose (pF); return ExecOk; }
        (void) !std::fread (pGRF, 1, sizeof (CPU_STATE), pF); std::fclose (pF);
        return ExecOk;
    }
private:
    std::string m_Dir;
};

class LuEmitter final : public LcComObject<ICpuEmitter> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuEmitter, ppvObject); }

    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("local t%u=%llu", D, (unsigned long long) Mask (Value, Bits)); return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("local t%u=gR(%u,%u)", D, Index, Bits); return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 Bits, BOOLEAN) override {
        Line ("pR(%u,t%u,%u)", Index, IdOf (pValue), Bits ? Bits : BitsOf (pValue)); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("local t%u=rM(t%u,%u)", D, IdOf (pAddr), Bits); return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        Line ("wM(t%u,t%u,%u)", IdOf (pAddr), IdOf (pValue), Bits); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        CONST CHAR8 *o;
        switch (Op) {
        case BinAdd:o="+";break; case BinSub:o="-";break; case BinMul:o="*";break;
        case BinAnd:o="&";break; case BinOr:o="|";break; case BinXor:o="~";break;   // Lua binary ~ is bxor
        case BinShl:o="<<";break; case BinLShr:o=">>";break; case BinAShr:o=">>";break;
        case BinUDiv:o="//";break; case BinURem:o="%";break; default:o="+";break;
        }
        UINT32 D = Fresh ();
        Line ("local t%u=(t%u%st%u)&%llu", D, IdOf (pA), o, IdOf (pB), (unsigned long long) Mask (~0ull, Bits));
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA), D = Fresh ();
        switch (Op) {
        case UnNeg: Line ("local t%u=(-t%u)&%llu", D, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); return Make (D, Bits, ppValue);
        case UnCom: Line ("local t%u=(~t%u)&%llu", D, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); return Make (D, Bits, ppValue);
        case UnNot: Line ("local t%u=(t%u==0) and 1 or 0", D, IdOf (pA)); return Make (D, 1, ppValue);
        }
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        CONST CHAR8 *o;
        switch (Pred) {
        case CmpEq:o="==";break; case CmpNe:o="~=";break;   // Lua not-equal is ~=
        case CmpULt:case CmpSLt:o="<";break; case CmpULe:case CmpSLe:o="<=";break;
        case CmpUGt:case CmpSGt:o=">";break; case CmpUGe:case CmpSGe:o=">=";break; default:o="==";break;
        }
        UINT32 D = Fresh (); Line ("local t%u=(t%u%st%u) and 1 or 0", D, IdOf (pA), o, IdOf (pB)); return Make (D, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 Src = BitsOf (pA), D = Fresh ();
        switch (Op) {
        case CastTrunc: Line ("local t%u=t%u&%llu", D, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); break;
        case CastZExt:  Line ("local t%u=t%u", D, IdOf (pA)); break;
        case CastSExt:  Line ("local t%u=sx(t%u,%u)&%llu", D, IdOf (pA), Src, (unsigned long long) Mask (~0ull, Bits)); break;
        }
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *, ICpuValue *, ICpuValue *, ICpuValue **ppValue) override { *ppValue = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("local t%u=gF(%u)", D, (UINT32) Flag); return Make (D, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override { Line ("sF(%u,t%u)", (UINT32) Flag, IdOf (pValue)); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override { Line ("sP(%llu)", (unsigned long long) Pc); return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override { *ppBlock = new LuBlock (); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    ICpuCode *Build () {
        char DirTmpl[] = "/tmp/libcpu_lua_XXXXXX";
        if (mkdtemp (DirTmpl) == nullptr) return nullptr;
        std::string Dir = DirTmpl, Src = Dir + "/insn.lua", Full = kTemplate;
        Subst (Full, "%B%", m_Body);
        int Fd = open (Src.c_str (), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600);
        if (Fd < 0) { rmdir (Dir.c_str ()); return nullptr; }
        FILE *pF = fdopen (Fd, "w"); if (!pF) { close (Fd); unlink (Src.c_str ()); rmdir (Dir.c_str ()); return nullptr; }
        std::fwrite (Full.data (), 1, Full.size (), pF); std::fclose (pF);
        return new LuCode (Dir);
    }
private:
    UINT32 Fresh () { return m_Next++; }
    void Line (CONST CHAR8 *pFmt, ...) {
        char Buf[256]; va_list A; va_start (A, pFmt); std::vsnprintf (Buf, sizeof (Buf), pFmt, A); va_end (A);
        m_Body += Buf; m_Body += "\n";
    }
    static VOID Subst (std::string &S, CHAR8 CONST *K, std::string CONST &V) { size_t P; while ((P = S.find (K)) != std::string::npos) S.replace (P, std::strlen (K), V); }
    HRESULT Make (UINT32 Id, UINT32 Bits, ICpuValue **ppValue) { *ppValue = new LuValue (Id, Bits); return S_OK; }
    std::string m_Body; UINT32 m_Next = 0;
};

class LuaBackend final : public LcComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuBackend, ppvObject); }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "lua"; }
    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override { *ppEmitter = new LuEmitter (); return S_OK; }
    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<LuEmitter *> (pEmitter)->Build (); *ppCode = pCode; return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *CreateLuaBackend (VOID) { return new LuaBackend (); }

} // namespace LibCPU
