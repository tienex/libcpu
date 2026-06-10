/** @file
  In-process CLR textual-emit backend. See ClrBackend.h.

  Each ICpuValue is a CIL int64 local. The emitter hand-assembles the raw CIL for a
  "void insn(byte[] ram, byte[] grf)" method plus its local-variable signature; the
  managed helper installs the IL with DynamicILInfo.SetCode and hands back a
  delegate, which the .NET JIT compiles. The IL uses only arithmetic, array access
  and the native comparison opcodes (clt/cgt/ceq) -- no metadata tokens -- so the
  raw byte stream needs no token fixups, and the guest state is marshalled through
  byte[] arrays. CoreCLR is embedded once via nethost/hostfxr.
**/
#include "ClrBackend.h"
#include "LibCPU/CpuState.h"

#include <nethost.h>
#include <hostfxr.h>
#include <coreclr_delegates.h>

#include <dlfcn.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifndef LIBCPU_CLR_DIR
#define LIBCPU_CLR_DIR "."
#endif

namespace LibCPU {
namespace {

static CONST UINT32 RAM_SIZE = 0x10000;   // assumed guest RAM (6502 / CHIP-8)

typedef intptr_t (*COMPILE_FN) (UINT8 *Il, int IlLen, UINT8 *Sig, int SigLen, int MaxStack);
typedef void     (*EXECUTE_FN) (intptr_t Handle, UINT8 *Ram, int RamLen, UINT8 *Grf, int GrfLen);

static COMPILE_FN g_Compile = nullptr;
static EXECUTE_FN g_Execute = nullptr;
static BOOLEAN    g_Tried   = FALSE;

//
// Embed CoreCLR once and resolve the two managed entry points from Emitter.dll.
//
static BOOLEAN
InitClr (VOID)
{
    if (g_Tried) {
        return (BOOLEAN) (g_Compile != nullptr);
    }
    g_Tried = TRUE;

    char    Path[1024];
    size_t  Size = sizeof (Path);
    if (get_hostfxr_path (Path, &Size, nullptr) != 0) {
        return FALSE;
    }
    void *Lib = dlopen (Path, RTLD_LAZY | RTLD_LOCAL);
    if (Lib == nullptr) {
        return FALSE;
    }
    auto Init   = (hostfxr_initialize_for_runtime_config_fn) dlsym (Lib, "hostfxr_initialize_for_runtime_config");
    auto GetDel = (hostfxr_get_runtime_delegate_fn) dlsym (Lib, "hostfxr_get_runtime_delegate");
    if (Init == nullptr || GetDel == nullptr) {
        return FALSE;
    }

    std::string Config = std::string (LIBCPU_CLR_DIR) + "/Emitter.runtimeconfig.json";
    std::string Dll    = std::string (LIBCPU_CLR_DIR) + "/Emitter.dll";

    hostfxr_handle Ctx = nullptr;
    if (Init (Config.c_str (), nullptr, &Ctx) != 0 || Ctx == nullptr) {
        return FALSE;
    }
    load_assembly_and_get_function_pointer_fn LoadFp = nullptr;
    GetDel (Ctx, hdt_load_assembly_and_get_function_pointer, (void **) &LoadFp);
    if (LoadFp == nullptr) {
        return FALSE;
    }

    LoadFp (Dll.c_str (), "Emitter, Emitter", "Compile", UNMANAGEDCALLERSONLY_METHOD, nullptr, (void **) &g_Compile);
    LoadFp (Dll.c_str (), "Emitter, Emitter", "Execute", UNMANAGEDCALLERSONLY_METHOD, nullptr, (void **) &g_Execute);
    return (BOOLEAN) (g_Compile != nullptr && g_Execute != nullptr);
}

static UINT64
Mask (UINT64 Value, UINT32 Bits)
{
    return (Bits >= 64) ? Value : (Value & (((UINT64) 1 << Bits) - 1));
}

class ClrValue final : public LcComObject<ICpuValue> {
public:
    ClrValue (UINT32 Id, UINT32 Bits) : m_Id (Id), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    UINT32 m_Id;
    UINT32 m_Bits;
};

class ClrBlock final : public LcComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
};

static UINT32 IdOf   (ICpuValue *pValue) { return static_cast<ClrValue *> (pValue)->m_Id; }
static UINT32 BitsOf (ICpuValue *pValue) { return static_cast<ClrValue *> (pValue)->m_Bits; }

class ClrCode final : public LcComObject<ICpuCode> {
public:
    ClrCode (intptr_t Handle) : m_Handle (Handle) {}

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuCode, ppvObject);
    }

    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID * /*pFRF*/) override {
        if (g_Execute == nullptr) {
            return ExecTrap;
        }
        g_Execute (m_Handle, (UINT8 *) pRAM, (int) RAM_SIZE, (UINT8 *) pGRF, (int) sizeof (CPU_STATE));
        return ExecOk;
    }

private:
    intptr_t m_Handle;
};

class ClrEmitter final : public LcComObject<ICpuEmitter> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }

    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        PushI8 ((INT64) Mask (Value, Bits));
        StLoc (Dest);
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        for (UINT32 k = 0; k < Bits / 8; k++) {
            LdArg (1);
            PushI4 ((INT32) (CPU_STATE_REG_OFFSET + Index * 8 + k));
            B (0x91);                    // ldelem.u1
            B (0x6a);                    // conv.i8
            if (k > 0) {
                PushI4 ((INT32) (8 * k));
                B (0x62);                // shl
                B (0x60);                // or
            }
        }
        StLoc (Dest);
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 Bits, BOOLEAN /*Sext*/) override {
        UINT32 Width = Bits ? Bits : BitsOf (pValue);
        for (UINT32 k = 0; k < Width / 8; k++) {
            LdArg (1);
            PushI4 ((INT32) (CPU_STATE_REG_OFFSET + Index * 8 + k));
            ByteValue (IdOf (pValue), 8 * k);
            B (0x9c);                    // stelem.i1
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        for (UINT32 k = 0; k < Bits / 8; k++) {
            LdArg (0);
            LdLoc (IdOf (pAddr));
            B (0x69);                    // conv.i4
            PushI4 ((INT32) k);
            B (0x58);                    // add
            B (0x91);                    // ldelem.u1
            B (0x6a);                    // conv.i8
            if (k > 0) {
                PushI4 ((INT32) (8 * k));
                B (0x62);                // shl
                B (0x60);                // or
            }
        }
        StLoc (Dest);
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        for (UINT32 k = 0; k < Bits / 8; k++) {
            LdArg (0);
            LdLoc (IdOf (pAddr));
            B (0x69);                    // conv.i4
            PushI4 ((INT32) k);
            B (0x58);                    // add
            ByteValue (IdOf (pValue), 8 * k);
            B (0x9c);                    // stelem.i1
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        UINT32 Dest = Fresh ();
        LdLoc (IdOf (pA));
        LdLoc (IdOf (pB));
        switch (Op) {
        case BinAdd:  B (0x58); break;                       // add
        case BinSub:  B (0x59); break;                       // sub
        case BinMul:  B (0x5a); break;                       // mul
        case BinAnd:  B (0x5f); break;                       // and
        case BinOr:   B (0x60); break;                       // or
        case BinXor:  B (0x61); break;                       // xor
        case BinUDiv: B (0x5c); break;                       // div.un
        case BinURem: B (0x5e); break;                       // rem.un
        case BinShl:  B (0x69); B (0x62); break;             // conv.i4; shl
        case BinLShr: B (0x69); B (0x64); break;             // conv.i4; shr.un
        case BinAShr: B (0x69); B (0x63); break;             // conv.i4; shr
        default:      B (0x58); break;
        }
        PushI8 ((INT64) Mask (~0ull, Bits));
        B (0x5f);                        // and  -> mask to width
        StLoc (Dest);
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        UINT32 Dest = Fresh ();
        UINT64 FullMask = Mask (~0ull, Bits);
        switch (Op) {
        case UnNeg:
            LdLoc (IdOf (pA));
            B (0x65);                    // neg
            PushI8 ((INT64) FullMask);
            B (0x5f);                    // and
            StLoc (Dest);
            return Make (Dest, Bits, ppValue);
        case UnCom:
            LdLoc (IdOf (pA));
            B (0x66);                    // not
            PushI8 ((INT64) FullMask);
            B (0x5f);                    // and
            StLoc (Dest);
            return Make (Dest, Bits, ppValue);
        case UnNot:
            LdLoc (IdOf (pA));
            PushI8 (0);
            B (0xfe); B (0x01);          // ceq
            B (0x6a);                    // conv.i8
            StLoc (Dest);
            return Make (Dest, 1, ppValue);
        }
        return E_INVALIDARG;
    }

    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        LdLoc (IdOf (pA));
        LdLoc (IdOf (pB));
        switch (Pred) {
        case CmpEq:  B (0xfe); B (0x01); break;                               // ceq
        case CmpNe:  B (0xfe); B (0x01); B (0x16); B (0xfe); B (0x01); break; // ceq; ldc.i4.0; ceq
        case CmpULt: B (0xfe); B (0x05); break;                               // clt.un
        case CmpSLt: B (0xfe); B (0x04); break;                               // clt
        case CmpUGt: B (0xfe); B (0x03); break;                               // cgt.un
        case CmpSGt: B (0xfe); B (0x02); break;                               // cgt
        case CmpULe: B (0xfe); B (0x03); B (0x16); B (0xfe); B (0x01); break; // !(a>b)
        case CmpSLe: B (0xfe); B (0x02); B (0x16); B (0xfe); B (0x01); break;
        case CmpUGe: B (0xfe); B (0x05); B (0x16); B (0xfe); B (0x01); break; // !(a<b)
        case CmpSGe: B (0xfe); B (0x04); B (0x16); B (0xfe); B (0x01); break;
        default:     B (0xfe); B (0x01); break;
        }
        B (0x6a);                        // conv.i8
        StLoc (Dest);
        return Make (Dest, 1, ppValue);
    }

    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA);
        UINT32 Dest = Fresh ();
        switch (Op) {
        case CastTrunc:
            LdLoc (IdOf (pA));
            PushI8 ((INT64) Mask (~0ull, Bits));
            B (0x5f);                    // and
            break;
        case CastZExt:
            LdLoc (IdOf (pA));
            break;
        case CastSExt:
            LdLoc (IdOf (pA));
            PushI4 ((INT32) (64 - SrcBits));
            B (0x62);                    // shl
            PushI4 ((INT32) (64 - SrcBits));
            B (0x63);                    // shr (arithmetic)
            PushI8 ((INT64) Mask (~0ull, Bits));
            B (0x5f);                    // and
            break;
        }
        StLoc (Dest);
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE Select (ICpuValue *, ICpuValue *, ICpuValue *, ICpuValue **ppValue) override {
        *ppValue = nullptr;
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        LdArg (1);
        PushI4 ((INT32) (CPU_STATE_FLAG_OFFSET + (UINT32) Flag));
        B (0x91);                        // ldelem.u1
        B (0x6a);                        // conv.i8
        PushI8 (1);
        B (0x5f);                        // and
        StLoc (Dest);
        return Make (Dest, 1, ppValue);
    }

    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        LdArg (1);
        PushI4 ((INT32) (CPU_STATE_FLAG_OFFSET + (UINT32) Flag));
        LdLoc (IdOf (pValue));
        PushI8 (1);
        B (0x5f);                        // and
        B (0x69);                        // conv.i4
        B (0x9c);                        // stelem.i1
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override {
        for (UINT32 k = 0; k < 8; k++) {
            LdArg (1);
            PushI4 ((INT32) (CPU_STATE_PC_OFFSET + k));
            PushI4 ((INT32) ((Pc >> (8 * k)) & 0xff));
            B (0x9c);                    // stelem.i1
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override {
        *ppBlock = new ClrBlock ();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *, ICpuBlock *, ICpuBlock *) override { return E_NOTIMPL; }

    ICpuCode *Build () {
        if (!InitClr ()) {
            return nullptr;
        }
        B (0x2a);                        // ret

        // Local-variable signature: LOCAL_SIG, count, then count * ELEMENT_TYPE_I8.
        std::vector<UINT8> Sig;
        Sig.push_back (0x07);
        AddCompressed (Sig, m_Next);
        for (UINT32 i = 0; i < m_Next; i++) {
            Sig.push_back (0x0a);        // ELEMENT_TYPE_I8
        }

        intptr_t Handle = g_Compile (m_Code.data (), (int) m_Code.size (),
                                     Sig.data (), (int) Sig.size (), 16);
        if (Handle == 0) {
            return nullptr;
        }
        return new ClrCode (Handle);
    }

private:
    UINT32 Fresh () { return m_Next++; }

    void B (UINT8 Op) { m_Code.push_back (Op); }

    void LdArg (UINT8 N) { B ((UINT8) (0x02 + N)); }         // ldarg.0 / ldarg.1

    void LdLoc (UINT32 Id) { B (0x11); B ((UINT8) Id); }     // ldloc.s
    void StLoc (UINT32 Id) { B (0x13); B ((UINT8) Id); }     // stloc.s

    void PushI4 (INT32 V) {
        if (V >= 0 && V <= 8) {
            B ((UINT8) (0x16 + V));                          // ldc.i4.0 .. ldc.i4.8
        } else if (V == -1) {
            B (0x15);                                        // ldc.i4.m1
        } else if (V >= -128 && V <= 127) {
            B (0x1f); B ((UINT8) V);                         // ldc.i4.s
        } else {
            B (0x20);                                        // ldc.i4
            B ((UINT8) V); B ((UINT8) (V >> 8)); B ((UINT8) (V >> 16)); B ((UINT8) (V >> 24));
        }
    }

    void PushI8 (INT64 V) {
        B (0x21);                                            // ldc.i8
        for (int i = 0; i < 8; i++) {
            B ((UINT8) (V >> (8 * i)));
        }
    }

    // Push ((value >> shift) & 0xff) as an int32, ready for stelem.i1.
    void ByteValue (UINT32 ValueId, UINT32 Shift) {
        LdLoc (ValueId);
        if (Shift > 0) {
            PushI4 ((INT32) Shift);
            B (0x64);                    // shr.un
        }
        PushI8 (255);
        B (0x5f);                        // and
        B (0x69);                        // conv.i4
    }

    static void AddCompressed (std::vector<UINT8> &Out, UINT32 Value) {
        if (Value < 0x80) {
            Out.push_back ((UINT8) Value);
        } else if (Value < 0x4000) {
            Out.push_back ((UINT8) (0x80 | (Value >> 8)));
            Out.push_back ((UINT8) Value);
        } else {
            Out.push_back ((UINT8) (0xc0 | (Value >> 24)));
            Out.push_back ((UINT8) (Value >> 16));
            Out.push_back ((UINT8) (Value >> 8));
            Out.push_back ((UINT8) Value);
        }
    }

    HRESULT Make (UINT32 Id, UINT32 Bits, ICpuValue **ppValue) {
        *ppValue = new ClrValue (Id, Bits);
        return S_OK;
    }

    std::vector<UINT8> m_Code;
    UINT32             m_Next = 0;
};

class ClrBackend final : public LcComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBackend, ppvObject);
    }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "clr"; }

    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override {
        *ppEmitter = new ClrEmitter ();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<ClrEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *
CreateClrBackend (VOID)
{
    return new ClrBackend ();
}

} // namespace LibCPU
