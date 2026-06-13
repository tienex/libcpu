/** @file
  In-process JavaScriptCore backend. See JscoreBackend.h.

  Each ICpuValue is a JS `var` masked to its width. Build() assembles a portable
  `insn(RAM, ST)` function. Execute() creates a JSGlobalContext, exposes the guest
  RAM and register file as no-copy Uint8Arrays over the native pointers, and
  evaluates the function -- so the JS mutates the real guest state directly.

  Note: ICpuCode::Execute carries no RAM size, so a 64 KiB guest RAM is assumed
  (true for the 6502).
**/
#include "JscoreBackend.h"
#include "LibCPU/CpuState.h"

#include <string>
#include <cstdarg>
#include <cstdio>

#include <JavaScriptCore/JavaScriptCore.h>

namespace LibCPU {
namespace {

static CONST UINT32 RAM_SIZE = 0x10000;   // assumed guest RAM (6502)

static CHAR8 CONST *kHelpers =
    "function getReg(i,b){var v=0;for(var k=0;k<b/8;k++)v=(v|(ST[i*8+k]<<(8*k)))>>>0;return v>>>0;}"
    "function putReg(i,v,b){for(var k=0;k<8;k++)ST[i*8+k]=(k<b/8)?((v>>>(8*k))&0xff):0;}"
    "function rdMem(a,b){var v=0;for(var k=0;k<b/8;k++)v=(v|(RAM[a+k]<<(8*k)))>>>0;return v>>>0;}"
    "function wrMem(a,v,b){for(var k=0;k<b/8;k++)RAM[a+k]=(v>>>(8*k))&0xff;}"
    "function getFlag(f){return ST[256+f]&1;}"
    "function setFlag(f,v){ST[256+f]=v&1;}\n";

static UINT64 Mask (UINT64 V, UINT32 Bits) { return (Bits >= 64) ? V : (V & (((UINT64) 1 << Bits) - 1)); }

class JcValue final : public ComObject<ICpuValue> {
public:
    JcValue (UINT32 Id, UINT32 Bits) : m_Id (Id), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuValue, ppvObject); }
    UINT32 m_Id, m_Bits;
};
class JcBlock final : public ComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuBlock, ppvObject); }
};
static UINT32 IdOf   (ICpuValue *pV) { return static_cast<JcValue *> (pV)->m_Id; }
static UINT32 BitsOf (ICpuValue *pV) { return static_cast<JcValue *> (pV)->m_Bits; }

class JcCode final : public ComObject<ICpuCode> {
public:
    explicit JcCode (std::string Script) : m_Script (std::move (Script)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuCode, ppvObject); }
    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID * /*pFRF*/) override {
        JSGlobalContextRef Ctx = JSGlobalContextCreate (nullptr);
        JSObjectRef Global = JSContextGetGlobalObject (Ctx);
        JSValueRef Exc = nullptr;

        JSObjectRef Ram = JSObjectMakeTypedArrayWithBytesNoCopy (
            Ctx, kJSTypedArrayTypeUint8Array, pRAM, RAM_SIZE, nullptr, nullptr, &Exc);
        JSObjectRef St = JSObjectMakeTypedArrayWithBytesNoCopy (
            Ctx, kJSTypedArrayTypeUint8Array, pGRF, sizeof (CPU_STATE), nullptr, nullptr, &Exc);
        Bind (Ctx, Global, "RAM", Ram);
        Bind (Ctx, Global, "ST", St);

        JSStringRef Src = JSStringCreateWithUTF8CString (m_Script.c_str ());
        JSEvaluateScript (Ctx, Src, nullptr, nullptr, 1, &Exc);
        JSStringRelease (Src);
        JSGlobalContextRelease (Ctx);
        return Exc ? ExecTrap : ExecOk;
    }
private:
    static VOID Bind (JSGlobalContextRef Ctx, JSObjectRef Global, CHAR8 CONST *Name, JSValueRef Val) {
        JSStringRef N = JSStringCreateWithUTF8CString (Name);
        JSValueRef Exc = nullptr;
        JSObjectSetProperty (Ctx, Global, N, Val, kJSPropertyAttributeNone, &Exc);
        JSStringRelease (N);
    }
    std::string m_Script;
};

class JcEmitter final : public ComObject<ICpuEmitter> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuEmitter, ppvObject); }

    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("var t%u=%llu;", D, (unsigned long long) Mask (Value, Bits)); return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("var t%u=getReg(%u,%u);", D, Index, Bits); return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 Bits, BOOLEAN /*Sext*/) override {
        Line ("putReg(%u,t%u,%u);", Index, IdOf (pValue), Bits ? Bits : BitsOf (pValue)); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("var t%u=rdMem(t%u,%u);", D, IdOf (pAddr), Bits); return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        Line ("wrMem(t%u,t%u,%u);", IdOf (pAddr), IdOf (pValue), Bits); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        CONST CHAR8 *pOp;
        switch (Op) {
        case BinAdd: pOp = "+"; break;  case BinSub: pOp = "-"; break;  case BinMul: pOp = "*"; break;
        case BinAnd: pOp = "&"; break;  case BinOr: pOp = "|"; break;   case BinXor: pOp = "^"; break;
        case BinShl: pOp = "<<"; break; case BinLShr: pOp = ">>>"; break; case BinAShr: pOp = ">>"; break;
        case BinUDiv: pOp = "/"; break; case BinURem: pOp = "%"; break;
        default: pOp = "+"; break;
        }
        UINT32 D = Fresh ();
        Line ("var t%u=((t%u%st%u)&%llu)>>>0;", D, IdOf (pA), pOp, IdOf (pB), (unsigned long long) Mask (~0ull, Bits));
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA), D = Fresh ();
        switch (Op) {
        case UnNeg: Line ("var t%u=((-t%u)&%llu)>>>0;", D, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); return Make (D, Bits, ppValue);
        case UnCom: Line ("var t%u=((~t%u)&%llu)>>>0;", D, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); return Make (D, Bits, ppValue);
        case UnNot: Line ("var t%u=(t%u==0)?1:0;", D, IdOf (pA)); return Make (D, 1, ppValue);
        }
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        CONST CHAR8 *pOp; bool Signed = false;
        switch (Pred) {
        case CmpEq: pOp = "=="; break; case CmpNe: pOp = "!="; break;
        case CmpULt: pOp = "<"; break; case CmpULe: pOp = "<="; break;
        case CmpUGt: pOp = ">"; break; case CmpUGe: pOp = ">="; break;
        case CmpSLt: pOp = "<"; Signed = true; break; case CmpSLe: pOp = "<="; Signed = true; break;
        case CmpSGt: pOp = ">"; Signed = true; break; case CmpSGe: pOp = ">="; Signed = true; break;
        default: pOp = "=="; break;
        }
        UINT32 D = Fresh ();
        if (Signed) Line ("var t%u=((t%u|0)%s(t%u|0))?1:0;", D, IdOf (pA), pOp, IdOf (pB));
        else        Line ("var t%u=(t%u%st%u)?1:0;", D, IdOf (pA), pOp, IdOf (pB));
        return Make (D, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA), D = Fresh ();
        switch (Op) {
        case CastTrunc: Line ("var t%u=t%u&%llu;", D, IdOf (pA), (unsigned long long) Mask (~0ull, Bits)); break;
        case CastZExt:  Line ("var t%u=t%u;", D, IdOf (pA)); break;
        case CastSExt:  Line ("var t%u=(((t%u<<%u)>>%u)&%llu)>>>0;", D, IdOf (pA), 32 - SrcBits, 32 - SrcBits, (unsigned long long) Mask (~0ull, Bits)); break;
        }
        return Make (D, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *, ICpuValue *, ICpuValue *, ICpuValue **ppValue) override { *ppValue = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        UINT32 D = Fresh (); Line ("var t%u=getFlag(%u);", D, (UINT32) Flag); return Make (D, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        Line ("setFlag(%u,t%u);", (UINT32) Flag, IdOf (pValue)); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override {
        Line ("for(var __k=0;__k<8;__k++)ST[264+__k]=(Math.floor(%llu/Math.pow(2,8*__k)))&0xff;", (unsigned long long) Pc); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override { *ppBlock = new JcBlock (); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    ICpuCode *Build () {
        std::string Script = "function insn(RAM,ST){\n" + std::string (kHelpers) + m_Body + "}\ninsn(RAM,ST);\n";
        return new JcCode (Script);
    }

private:
    UINT32 Fresh () { return m_Next++; }
    void Line (CONST CHAR8 *pFmt, ...) {
        char Buf[256];
        va_list Args; va_start (Args, pFmt);
        std::vsnprintf (Buf, sizeof (Buf), pFmt, Args);
        va_end (Args);
        m_Body += Buf; m_Body += "\n";
    }
    HRESULT Make (UINT32 Id, UINT32 Bits, ICpuValue **ppValue) { *ppValue = new JcValue (Id, Bits); return S_OK; }

    std::string m_Body;
    UINT32      m_Next = 0;
};

class JscoreBackend final : public ComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuBackend, ppvObject); }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "jscore"; }
    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override { *ppEmitter = new JcEmitter (); return S_OK; }
    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<JcEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *
CreateJscoreBackend (VOID)
{
    return new JscoreBackend ();
}

} // namespace LibCPU
