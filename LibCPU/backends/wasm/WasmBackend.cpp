/** @file
  WebAssembly backend. See WasmBackend.h.

  Build() encodes a WASM module: insn() (no args) whose body is a stack-machine
  program over i64 locals (one per ICpuValue) that calls seven host-imported
  accessors -- rd/wr (RAM), gr/pr (registers), gf/sf (flags), sp (PC). The module
  is run in-process on wasm3; the imports are bound to C callbacks that touch the
  native guest state, so no memory is copied.
**/
#include "WasmBackend.h"
#include "LibCPU/CpuState.h"

#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
#include "wasm3.h"
#include "m3_env.h"
}

namespace LibCPU {
namespace {

static UINT64 MaskBits (UINT64 V, UINT32 Bits) { return (Bits >= 64) ? V : (V & (((UINT64) 1 << Bits) - 1)); }

// ---- host state + import callbacks ----
struct WCtx { UINT8 *RAM; UINT8 *GRF; };

static UINT64 RdN (UINT8 *p, UINT64 Bits) { UINT64 v = 0; for (UINT64 k = 0; k < Bits / 8; k++) v |= (UINT64) p[k] << (8 * k); return v; }
static VOID   WrN (UINT8 *p, UINT64 V, UINT64 Bits) { for (UINT64 k = 0; k < Bits / 8; k++) p[k] = (UINT8) ((V >> (8 * k)) & 0xff); }
static UINT64 Msk (UINT64 Bits) { return (Bits >= 64) ? ~UINT64_C (0) : ((UINT64_C (1) << Bits) - 1); }

static m3ApiRawFunction (cb_rd) {
    m3ApiReturnType (uint64_t); m3ApiGetArg (uint64_t, addr); m3ApiGetArg (uint64_t, bits);
    WCtx *c = (WCtx *) m3_GetUserData (runtime);
    m3ApiReturn (RdN (c->RAM + addr, bits));
}
static m3ApiRawFunction (cb_wr) {
    m3ApiGetArg (uint64_t, addr); m3ApiGetArg (uint64_t, val); m3ApiGetArg (uint64_t, bits);
    WCtx *c = (WCtx *) m3_GetUserData (runtime);
    WrN (c->RAM + addr, val, bits);
    return m3Err_none;
}
static m3ApiRawFunction (cb_gr) {
    m3ApiReturnType (uint64_t); m3ApiGetArg (uint64_t, idx); m3ApiGetArg (uint64_t, bits);
    WCtx *c = (WCtx *) m3_GetUserData (runtime);
    m3ApiReturn (RdN (c->GRF + CPU_STATE_REG_OFFSET + idx * 8, 64) & Msk (bits));
}
static m3ApiRawFunction (cb_pr) {
    m3ApiGetArg (uint64_t, idx); m3ApiGetArg (uint64_t, val); m3ApiGetArg (uint64_t, bits);
    WCtx *c = (WCtx *) m3_GetUserData (runtime);
    WrN (c->GRF + CPU_STATE_REG_OFFSET + idx * 8, val & Msk (bits), 64);
    return m3Err_none;
}
static m3ApiRawFunction (cb_gf) {
    m3ApiReturnType (uint64_t); m3ApiGetArg (uint64_t, f);
    WCtx *c = (WCtx *) m3_GetUserData (runtime);
    m3ApiReturn ((uint64_t) (c->GRF[CPU_STATE_FLAG_OFFSET + f] & 1));
}
static m3ApiRawFunction (cb_sf) {
    m3ApiGetArg (uint64_t, f); m3ApiGetArg (uint64_t, val);
    WCtx *c = (WCtx *) m3_GetUserData (runtime);
    c->GRF[CPU_STATE_FLAG_OFFSET + f] = (UINT8) (val & 1);
    return m3Err_none;
}
static m3ApiRawFunction (cb_sp) {
    m3ApiGetArg (uint64_t, pc);
    WCtx *c = (WCtx *) m3_GetUserData (runtime);
    WrN (c->GRF + CPU_STATE_PC_OFFSET, pc, 64);
    return m3Err_none;
}

// ---- WASM binary helpers ----
typedef std::vector<UINT8> Bytes;
static VOID U (Bytes &B, UINT64 X) { do { UINT8 b = X & 0x7f; X >>= 7; if (X) b |= 0x80; B.push_back (b); } while (X); }
static VOID S (Bytes &B, INT64 X) {
    bool More = true;
    while (More) {
        UINT8 b = X & 0x7f; X >>= 7;
        if ((X == 0 && !(b & 0x40)) || (X == -1 && (b & 0x40))) More = false; else b |= 0x80;
        B.push_back (b);
    }
}
static VOID Name (Bytes &B, CHAR8 CONST *s) { UINTN n = std::strlen (s); U (B, n); for (UINTN i = 0; i < n; i++) B.push_back ((UINT8) s[i]); }
static VOID Section (Bytes &Mod, UINT8 Id, Bytes CONST &C) { Mod.push_back (Id); U (Mod, C.size ()); Mod.insert (Mod.end (), C.begin (), C.end ()); }

class WaValue final : public ComObject<ICpuValue> {
public:
    WaValue (UINT32 L, UINT32 Bits) : m_Local (L), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuValue, ppvObject); }
    UINT32 m_Local, m_Bits;
};
class WaBlock final : public ComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuBlock, ppvObject); }
};
static UINT32 LocOf  (ICpuValue *pV) { return static_cast<WaValue *> (pV)->m_Local; }
static UINT32 BitsOf (ICpuValue *pV) { return static_cast<WaValue *> (pV)->m_Bits; }

class WasmCode final : public ComObject<ICpuCode> {
public:
    WasmCode (IM3Environment Env, IM3Runtime Rt, IM3Function Fn, WCtx *Ctx, Bytes Mod)
        : m_Env (Env), m_Rt (Rt), m_Fn (Fn), m_Ctx (Ctx), m_Mod (std::move (Mod)) {}
    ~WasmCode () override {
        if (m_Rt) m3_FreeRuntime (m_Rt);
        if (m_Env) m3_FreeEnvironment (m_Env);
        delete m_Ctx;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuCode, ppvObject); }
    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID * /*pFRF*/) override {
        m_Ctx->RAM = (UINT8 *) pRAM;
        m_Ctx->GRF = (UINT8 *) pGRF;
        return m3_CallV (m_Fn) ? ExecTrap : ExecOk;
    }
private:
    IM3Environment m_Env; IM3Runtime m_Rt; IM3Function m_Fn; WCtx *m_Ctx; Bytes m_Mod;
};

class WasmEmitter final : public ComObject<ICpuEmitter> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuEmitter, ppvObject); }

    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        Const ((INT64) MaskBits (Value, Bits)); UINT32 L = Set (); return Make (L, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        Const (Index); Const (Bits); Call (2 /*gr*/); UINT32 L = Set (); return Make (L, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 Bits, BOOLEAN /*Sext*/) override {
        Const (Index); Get (LocOf (pValue)); Const (Bits ? Bits : BitsOf (pValue)); Call (3 /*pr*/); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        Get (LocOf (pAddr)); Const (Bits); Call (0 /*rd*/); UINT32 L = Set (); return Make (L, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        Get (LocOf (pAddr)); Get (LocOf (pValue)); Const (Bits); Call (1 /*wr*/); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        Get (LocOf (pA)); Get (LocOf (pB));
        UINT8 op;
        switch (Op) {
        case BinAdd: op = 0x7C; break; case BinSub: op = 0x7D; break; case BinMul: op = 0x7E; break;
        case BinSDiv:op = 0x7F; break; case BinUDiv:op = 0x80; break; case BinSRem:op = 0x81; break; case BinURem:op = 0x82; break;
        case BinAnd: op = 0x83; break; case BinOr: op = 0x84; break; case BinXor: op = 0x85; break;
        case BinShl: op = 0x86; break; case BinAShr:op = 0x87; break; case BinLShr:op = 0x88; break;
        default: op = 0x7C; break;
        }
        B (op); MaskTop (Bits); UINT32 L = Set (); return Make (L, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        switch (Op) {
        case UnNeg: Const (0); Get (LocOf (pA)); B (0x7D); MaskTop (Bits); { UINT32 L = Set (); return Make (L, Bits, ppValue); }
        case UnCom: Get (LocOf (pA)); Const (-1); B (0x85); MaskTop (Bits); { UINT32 L = Set (); return Make (L, Bits, ppValue); }
        case UnNot: Get (LocOf (pA)); B (0x50); B (0xAD); { UINT32 L = Set (); return Make (L, 1, ppValue); }  // i64.eqz; extend_i32_u
        }
        return E_INVALIDARG;
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        Get (LocOf (pA)); Get (LocOf (pB));
        UINT8 op;
        switch (Pred) {
        case CmpEq: op = 0x51; break; case CmpNe: op = 0x52; break;
        case CmpSLt:op = 0x53; break; case CmpULt:op = 0x54; break; case CmpSGt:op = 0x55; break; case CmpUGt:op = 0x56; break;
        case CmpSLe:op = 0x57; break; case CmpULe:op = 0x58; break; case CmpSGe:op = 0x59; break; case CmpUGe:op = 0x5A; break;
        default: op = 0x51; break;
        }
        B (op); B (0xAD);   // i64.extend_i32_u
        UINT32 L = Set (); return Make (L, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA);
        Get (LocOf (pA));
        switch (Op) {
        case CastTrunc: MaskTop (Bits); break;
        case CastZExt:  break;
        case CastSExt:
            if (SrcBits == 8)       B (0xC2);  // i64.extend8_s
            else if (SrcBits == 16) B (0xC3);  // i64.extend16_s
            else if (SrcBits == 32) B (0xC4);  // i64.extend32_s
            MaskTop (Bits);
            break;
        }
        UINT32 L = Set (); return Make (L, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *, ICpuValue *, ICpuValue *, ICpuValue **ppValue) override { *ppValue = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        Const ((UINT32) Flag); Call (4 /*gf*/); UINT32 L = Set (); return Make (L, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        Const ((UINT32) Flag); Get (LocOf (pValue)); Call (5 /*sf*/); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override { Const ((INT64) Pc); Call (6 /*sp*/); return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override { *ppBlock = new WaBlock (); return S_OK; }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    ICpuCode *Build () {
        Bytes Mod = { 0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00 };

        Bytes T;  U (T, 6);
        UINT8 i64 = 0x7E;
        T.push_back (0x60); U (T, 2); T.push_back (i64); T.push_back (i64); U (T, 1); T.push_back (i64);   // T0 (II)->I
        T.push_back (0x60); U (T, 3); T.push_back (i64); T.push_back (i64); T.push_back (i64); U (T, 0);    // T1 (III)->()
        T.push_back (0x60); U (T, 1); T.push_back (i64); U (T, 1); T.push_back (i64);                       // T2 (I)->I
        T.push_back (0x60); U (T, 2); T.push_back (i64); T.push_back (i64); U (T, 0);                       // T3 (II)->()
        T.push_back (0x60); U (T, 1); T.push_back (i64); U (T, 0);                                          // T4 (I)->()
        T.push_back (0x60); U (T, 0); U (T, 0);                                                             // T5 ()->()
        Section (Mod, 1, T);

        Bytes I;  U (I, 7);
        CHAR8 CONST *Nm[] = { "rd", "wr", "gr", "pr", "gf", "sf", "sp" };
        UINT8 Ty[] = { 0, 1, 0, 1, 2, 3, 4 };
        for (int k = 0; k < 7; k++) { Name (I, "e"); Name (I, Nm[k]); I.push_back (0x00); U (I, Ty[k]); }
        Section (Mod, 2, I);

        Bytes F;  U (F, 1); U (F, 5);  // one function, type T5
        Section (Mod, 3, F);

        Bytes E;  U (E, 1); Name (E, "insn"); E.push_back (0x00); U (E, 7);  // export func idx 7
        Section (Mod, 7, E);

        Bytes Body;
        if (m_NumLocals > 0) { U (Body, 1); U (Body, m_NumLocals); Body.push_back (i64); } else { U (Body, 0); }
        Body.insert (Body.end (), m_Body.begin (), m_Body.end ());
        Body.push_back (0x0B);  // end
        Bytes Code;  U (Code, 1); U (Code, Body.size ()); Code.insert (Code.end (), Body.begin (), Body.end ());
        Section (Mod, 10, Code);

        IM3Environment Env = m3_NewEnvironment ();
        if (!Env) return nullptr;
        WCtx *Ctx = new WCtx{ nullptr, nullptr };
        IM3Runtime Rt = m3_NewRuntime (Env, 64 * 1024, Ctx);
        IM3Module Module = nullptr;
        if (m3_ParseModule (Env, &Module, Mod.data (), (uint32_t) Mod.size ())) { Fail (Env, Rt, Ctx); return nullptr; }
        if (m3_LoadModule (Rt, Module)) { m3_FreeModule (Module); Fail (Env, Rt, Ctx); return nullptr; }
        M3RawCall Cb[] = { cb_rd, cb_wr, cb_gr, cb_pr, cb_gf, cb_sf, cb_sp };
        CHAR8 CONST *Sig[] = { "I(II)", "v(III)", "I(II)", "v(III)", "I(I)", "v(II)", "v(I)" };
        for (int k = 0; k < 7; k++) m3_LinkRawFunction (Module, "e", Nm2 (k), Sig[k], Cb[k]);
        IM3Function Fn = nullptr;
        if (m3_FindFunction (&Fn, Rt, "insn") || !Fn) { Fail (Env, Rt, Ctx); return nullptr; }
        return new WasmCode (Env, Rt, Fn, Ctx, std::move (Mod));
    }

private:
    static CHAR8 CONST *Nm2 (int k) { static CHAR8 CONST *N[] = { "rd", "wr", "gr", "pr", "gf", "sf", "sp" }; return N[k]; }
    static VOID Fail (IM3Environment Env, IM3Runtime Rt, WCtx *Ctx) { if (Rt) m3_FreeRuntime (Rt); if (Env) m3_FreeEnvironment (Env); delete Ctx; }

    void B (UINT8 b) { m_Body.push_back (b); }
    void Const (INT64 v) { B (0x42); S (m_Body, v); }                 // i64.const
    void Get (UINT32 L) { B (0x20); U (m_Body, L); }                  // local.get
    UINT32 Set () { UINT32 L = m_NumLocals++; B (0x21); U (m_Body, L); return L; }  // local.set new local
    void Call (UINT32 F) { B (0x10); U (m_Body, F); }                 // call
    void MaskTop (UINT32 Bits) { if (Bits < 64) { Const ((INT64) MaskBits (~UINT64_C (0), Bits)); B (0x83); } }  // & mask
    HRESULT Make (UINT32 L, UINT32 Bits, ICpuValue **ppValue) { *ppValue = new WaValue (L, Bits); return S_OK; }

    Bytes  m_Body;
    UINT32 m_NumLocals = 0;
};

class WasmBackend final : public ComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuBackend, ppvObject); }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "wasm"; }
    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override { *ppEmitter = new WasmEmitter (); return S_OK; }
    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<WasmEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *
CreateWasmBackend (VOID)
{
    return new WasmBackend ();
}

} // namespace LibCPU
