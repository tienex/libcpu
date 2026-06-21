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

class ClrValue final : public ComObject<ICpuValue> {
public:
    ClrValue (UINT32 Id, UINT32 Bits) : m_Id (Id), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    UINT32 m_Id;
    UINT32 m_Bits;
};

class ClrBlock final : public ComObject<ICpuBlock> {
public:
    explicit ClrBlock (UINT32 Id) : m_Id (Id) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
    UINT32 m_Id;
};

static UINT32 IdOf   (ICpuValue *pValue) { return static_cast<ClrValue *> (pValue)->m_Id; }
static UINT32 BitsOf (ICpuValue *pValue) { return static_cast<ClrValue *> (pValue)->m_Bits; }
static UINT32 BlkId  (ICpuBlock *pBlock) { return static_cast<ClrBlock *> (pBlock)->m_Id; }

class ClrCode final : public ComObject<ICpuCode> {
public:
    ClrCode (intptr_t Handle) : m_Handle (Handle) {}

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuCode, ppvObject);
    }

    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID * /*pFRF*/) override {
        if (g_Execute == nullptr) {
            return ExecTrap;
        }
        UINT64 RamSize = ((CPU_STATE *) pGRF)->RamSize;   // host-stated; 0 -> 64 KiB
        if (RamSize == 0) { RamSize = CPU_RAM_DEFAULT; }
        g_Execute (m_Handle, (UINT8 *) pRAM, (int) RamSize, (UINT8 *) pGRF, (int) sizeof (CPU_STATE));
        return ExecOk;
    }

private:
    intptr_t m_Handle;
};

class ClrEmitter final : public ComObject<ICpuEmitter>,
                         public ICpuSmcEmitter,
                         public ICpuProfileEmitter,
                         public ICpuClockEmitter {
public:
    // Four interfaces (ICpuEmitter + ICpuSmcEmitter + ICpuProfileEmitter + ICpuClockEmitter):
    // resolve QI here, forward refcounting to the ComObject base. The clock emitter matters on the
    // machine path: a clock-less backend leaves CPU_STATE.Cycles untouched, the host falls back to a
    // coarse per-burst estimate, and the BIOS's PIT-calibration (DMA-Timer) test fails -- so the
    // shadow path's per-instruction tick must reach a real Cycles bump here.
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        if (ppvObject != nullptr && CompareGuid (&riid, &IID_ICpuSmcEmitter)) {
            *ppvObject = static_cast<ICpuSmcEmitter *> (this);
            AddRef ();
            return S_OK;
        }
        if (ppvObject != nullptr && CompareGuid (&riid, &IID_ICpuProfileEmitter)) {
            *ppvObject = static_cast<ICpuProfileEmitter *> (this);
            AddRef ();
            return S_OK;
        }
        if (ppvObject != nullptr && CompareGuid (&riid, &IID_ICpuClockEmitter)) {
            *ppvObject = static_cast<ICpuClockEmitter *> (this);
            AddRef ();
            return S_OK;
        }
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }
    UINT32 STDMETHODCALLTYPE AddRef () override { return ComObject<ICpuEmitter>::AddRef (); }
    UINT32 STDMETHODCALLTYPE Release () override { return ComObject<ICpuEmitter>::Release (); }

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

    // Guest RAM is arg0, passed as a NATIVE POINTER (not a managed byte[]): the host's RAM is read
    // and written in place via ldind/stind, so no per-execution copy of the (up to 640 KiB) address
    // space is needed -- see Emitter.cs Execute. The byte address is base + (addr + k).
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        for (UINT32 k = 0; k < Bits / 8; k++) {
            LdArg (0);                   // ram base (native int)
            LdLoc (IdOf (pAddr));
            B (0x69);                    // conv.i4
            PushI4 ((INT32) k);
            B (0x58);                    // add        (addr + k)
            B (0x58);                    // add        (base + (addr + k) -> native int)
            B (0x47);                    // ldind.u1
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
            LdArg (0);                   // ram base (native int)
            LdLoc (IdOf (pAddr));
            B (0x69);                    // conv.i4
            PushI4 ((INT32) k);
            B (0x58);                    // add        (addr + k)
            B (0x58);                    // add        (base + (addr + k) -> native int)
            ByteValue (IdOf (pValue), 8 * k);
            B (0x52);                    // stind.i1
        }
        EmitStoreBarrier (IdOf (pAddr));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        UINT32 Dest = Fresh ();
        if (Op == BinUDiv || Op == BinURem) {
            // Division is a TOTAL function here (like the interpreter: divisor 0 -> result 0); a raw
            // div.un would throw DivideByZeroException and abort the host. Branchless guard, with
            // Z = (B == 0): compute A / (B | Z) so the divisor is never 0, then mask the result with
            // (Z - 1) -- all-ones when B != 0, zero when B == 0.
            UINT32 Z = Fresh ();
            LdLoc (IdOf (pB));
            PushI8 (0);
            B (0xfe); B (0x01);          // ceq  -> i4 (1 if B == 0)
            B (0x6a);                    // conv.i8
            StLoc (Z);
            LdLoc (IdOf (pA));
            LdLoc (IdOf (pB));
            LdLoc (Z);
            B (0x60);                    // or   -> B | Z (never 0)
            B (Op == BinUDiv ? 0x5c : 0x5e);   // div.un / rem.un
            LdLoc (Z);
            PushI8 (1);
            B (0x59);                    // sub  -> Z - 1
            B (0x5f);                    // and  -> 0 when B was 0
        } else {
            LdLoc (IdOf (pA));
            LdLoc (IdOf (pB));
            switch (Op) {
            case BinAdd:  B (0x58); break;                   // add
            case BinSub:  B (0x59); break;                   // sub
            case BinMul:  B (0x5a); break;                   // mul
            case BinAnd:  B (0x5f); break;                   // and
            case BinOr:   B (0x60); break;                   // or
            case BinXor:  B (0x61); break;                   // xor
            case BinShl:  B (0x69); B (0x62); break;         // conv.i4; shl
            case BinLShr: B (0x69); B (0x64); break;         // conv.i4; shr.un
            case BinAShr: B (0x69); B (0x63); break;         // conv.i4; shr
            default:      B (0x58); break;
            }
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
        // floating-point ops: unsupported by this backend
        default: return E_INVALIDARG;
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
        // floating-point ops: unsupported by this backend
        default: return E_INVALIDARG;
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

    // ---- control flow: blocks are byte offsets, edges are br/brtrue --------
    // CIL branch operands are 4-byte and relative to the NEXT instruction; emitted
    // as placeholders and patched in Build once block offsets are known.
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override {
        *ppBlock = new ClrBlock ((UINT32) m_BlockStart.size ());
        m_BlockStart.push_back (0xFFFFFFFFu);   // unset until SetInsertBlock (exit block stays unset)
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *pBlock) override {
        m_BlockStart[BlkId (pBlock)] = (UINT32) m_Code.size ();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *pTarget) override {
        EmitBr (BlkId (pTarget));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *pCond, ICpuBlock *pTrue, ICpuBlock *pFalse) override {
        LdLoc (IdOf (pCond));        // i8 cond (brtrue treats any nonzero as true)
        UINT32 P = (UINT32) m_Code.size ();
        B (0x3a);                    // brtrue <true>
        m_Fixups.push_back ({ P, BlkId (pTrue) });
        B4 (0);
        EmitBr (BlkId (pFalse));     // else br false
        return S_OK;
    }

    // ---- self-modifying-code guard (ICpuSmcEmitter) -----------------------
    HRESULT STDMETHODCALLTYPE EmitCodeGuard (CPU_ADDR Pc) override {
        UINT32 Page = (UINT32) ((Pc >> 8) & 255);
        LdArg (1);
        PushI4 ((INT32) (CPU_STATE_CODEDIRTY_OFFSET + (Page >> 3)));
        B (0x91);                    // ldelem.u1 (this block's bitmap byte)
        PushI4 ((INT32) (1u << (Page & 7)));
        B (0x5f);                    // and (test this page's bit)
        UINT32 P = (UINT32) m_Code.size ();
        B (0x39);                    // brfalse <skip>  (clean -> run the block)
        B4 (0);
        for (UINT32 k = 0; k < 8; k++) {           // dirty: record TrapPc = Pc ...
            LdArg (1);
            PushI4 ((INT32) (CPU_STATE_TRAPPC_OFFSET + k));
            PushI4 ((INT32) ((Pc >> (8 * k)) & 0xff));
            B (0x9c);                // stelem.i1
        }
        B (0x2a);                    // ... and ret
        UINT32 Off = (UINT32) m_Code.size () - (P + 5);   // relative to instr after the brfalse
        m_Code[P + 1] = (UINT8) Off;        m_Code[P + 2] = (UINT8) (Off >> 8);
        m_Code[P + 3] = (UINT8) (Off >> 16); m_Code[P + 4] = (UINT8) (Off >> 24);
        return S_OK;
    }

    // Indirect branch: store the runtime target's 8 bytes into TrapPc and ret; the
    // host resume loop re-enters at that address.
    HRESULT STDMETHODCALLTYPE IndirectBranch (ICpuValue *pTargetPc) override {
        for (UINT32 k = 0; k < 8; k++) {
            LdArg (1);
            PushI4 ((INT32) (CPU_STATE_TRAPPC_OFFSET + k));
            LdLoc (IdOf (pTargetPc));
            if (k != 0) { PushI4 ((INT32) (8 * k)); B (0x64); }   // shr.un
            PushI8 (255); B (0x5f); B (0x69);                     // and ; conv.i4
            B (0x9c);                                             // stelem.i1
        }
        B (0x2a);                                                 // ret
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetDispatchTarget (ICpuValue *pTargetPc) override {
        for (UINT32 k = 0; k < 8; k++) {                          // target's 8 bytes -> DispPc
            LdArg (1);
            PushI4 ((INT32) (CPU_STATE_DISPPC_OFFSET + k));
            LdLoc (IdOf (pTargetPc));
            if (k != 0) { PushI4 ((INT32) (8 * k)); B (0x64); }   // shr.un
            PushI8 (255); B (0x5f); B (0x69);                     // and ; conv.i4
            B (0x9c);                                             // stelem.i1
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDispatchTarget (ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        for (UINT32 k = 0; k < 8; k++) {                          // DispPc's 8 bytes -> int64
            LdArg (1);
            PushI4 ((INT32) (CPU_STATE_DISPPC_OFFSET + k));
            B (0x91);                    // ldelem.u1
            B (0x6a);                    // conv.i8
            if (k > 0) {
                PushI4 ((INT32) (8 * k));
                B (0x62);                // shl
                B (0x60);                // or
            }
        }
        StLoc (Dest);
        return Make (Dest, 64, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetCodeBase (ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        for (UINT32 k = 0; k < 8; k++) {                          // CodeBase's 8 bytes -> int64
            LdArg (1);
            PushI4 ((INT32) (CPU_STATE_CODEBASE_OFFSET + k));
            B (0x91);                    // ldelem.u1
            B (0x6a);                    // conv.i8
            if (k > 0) {
                PushI4 ((INT32) (8 * k));
                B (0x62);                // shl
                B (0x60);                // or
            }
        }
        StLoc (Dest);
        return Make (Dest, 64, ppValue);
    }

    // ---- cycle clock (ICpuClockEmitter) -----------------------------------
    // CPU_STATE.Cycles += Count: assemble the field's 8 little-endian bytes into an int64, add
    // Count, write the 8 bytes back. The shadow path forwards its per-instruction tick here, giving
    // the machine a real instruction-retired time base (the PIT reads it), so the BIOS calibration
    // loops behave the same as on a natively clock-emitting backend.
    HRESULT STDMETHODCALLTYPE EmitTick (UINT32 Count) override {
        for (UINT32 k = 0; k < 8; k++) {                          // Cycles' 8 bytes -> int64
            LdArg (1);
            PushI4 ((INT32) (CPU_STATE_CYCLES_OFFSET + k));
            B (0x91);                    // ldelem.u1
            B (0x6a);                    // conv.i8
            if (k > 0) {
                PushI4 ((INT32) (8 * k));
                B (0x62);                // shl
                B (0x60);                // or
            }
        }
        PushI8 ((INT64) Count);
        B (0x58);                        // add
        UINT32 Tmp = ClockTmp ();
        StLoc (Tmp);
        for (UINT32 k = 0; k < 8; k++) {                          // int64 -> Cycles' 8 bytes
            LdArg (1);
            PushI4 ((INT32) (CPU_STATE_CYCLES_OFFSET + k));
            LdLoc (Tmp);
            if (k != 0) { PushI4 ((INT32) (8 * k)); B (0x64); }   // shr.un
            PushI8 (255); B (0x5f); B (0x69);                     // and ; conv.i4
            B (0x9c);                                             // stelem.i1
        }
        return S_OK;
    }

    // ---- runtime edge profiling (ICpuProfileEmitter) ----------------------
    // ++EdgeCount[Index]: assemble the slot's 8 little-endian bytes into an int64,
    // add 1, write the 8 bytes back. EdgeCount[] starts at CPU_STATE_EDGECOUNT_OFFSET.
    HRESULT STDMETHODCALLTYPE EmitEdgeCounter (UINT32 Index) override {
        if (Index >= CPU_PROFILE_SLOTS) {
            return S_OK;
        }
        UINT32 Base = CPU_STATE_EDGECOUNT_OFFSET + Index * 8;
        UINT32 Tmp  = Fresh ();
        for (UINT32 k = 0; k < 8; k++) {                          // grf[Base..) -> int64
            LdArg (1);
            PushI4 ((INT32) (Base + k));
            B (0x91);                    // ldelem.u1
            B (0x6a);                    // conv.i8
            if (k > 0) {
                PushI4 ((INT32) (8 * k));
                B (0x62);                // shl
                B (0x60);                // or
            }
        }
        PushI8 (1);
        B (0x58);                        // add
        StLoc (Tmp);
        for (UINT32 k = 0; k < 8; k++) {                          // int64 -> grf[Base..)
            LdArg (1);
            PushI4 ((INT32) (Base + k));
            ByteValue (Tmp, 8 * k);
            B (0x9c);                    // stelem.i1
        }
        return S_OK;
    }

    ICpuCode *Build () {
        if (!InitClr ()) {
            return nullptr;
        }
        // Final ret: terminates the entry/last block on fall-through and is the
        // landing pad for any block with no recorded start (the AOT exit block).
        UINT32 RetPos = (UINT32) m_Code.size ();
        B (0x2a);                        // ret
        for (auto CONST &Fx : m_Fixups) {
            INT32 Target = (m_BlockStart[Fx.second] != 0xFFFFFFFFu) ? (INT32) m_BlockStart[Fx.second] : (INT32) RetPos;
            INT32 Off    = Target - (INT32) (Fx.first + 5);   // CIL: relative to instr after the branch
            m_Code[Fx.first + 1] = (UINT8) Off;        m_Code[Fx.first + 2] = (UINT8) (Off >> 8);
            m_Code[Fx.first + 3] = (UINT8) (Off >> 16); m_Code[Fx.first + 4] = (UINT8) (Off >> 24);
        }

        // Local-variable signature: LOCAL_SIG, count, then count * ELEMENT_TYPE_I8.
        std::vector<UINT8> Sig;
        Sig.push_back (0x07);
        AddCompressed (Sig, m_Next);
        for (UINT32 i = 0; i < m_Next; i++) {
            Sig.push_back (0x0a);        // ELEMENT_TYPE_I8
        }

        intptr_t Handle = g_Compile (m_Code.data (), (int) m_Code.size (),
                                     Sig.data (), (int) Sig.size (), 64);
        if (Handle == 0) {
            return nullptr;
        }
        return new ClrCode (Handle);
    }

private:
    UINT32 Fresh () { return m_Next++; }

    // One reusable scratch local for the cycle-clock read-modify-write (allocated on first tick), so
    // the per-instruction EmitTick does not allocate a fresh local each time.
    UINT32 ClockTmp () {
        if (m_ClockTmp == 0xFFFFFFFFu) { m_ClockTmp = Fresh (); }
        return m_ClockTmp;
    }

    void B (UINT8 Op) { m_Code.push_back (Op); }

    void LdArg (UINT8 N) { B ((UINT8) (0x02 + N)); }         // ldarg.0 / ldarg.1

    void LdLoc (UINT32 Id) { B (0x11); B ((UINT8) Id); }     // ldloc.s
    void StLoc (UINT32 Id) { B (0x13); B ((UINT8) Id); }     // stloc.s

    void B4 (UINT32 V) { B ((UINT8) V); B ((UINT8) (V >> 8)); B ((UINT8) (V >> 16)); B ((UINT8) (V >> 24)); }
    void EmitBr (UINT32 BlockId) {
        UINT32 P = (UINT32) m_Code.size ();
        B (0x38);                                            // br
        m_Fixups.push_back ({ P, BlockId });
        B4 (0);
    }
    void PushAddrI4 (UINT32 AddrId) { LdLoc (AddrId); B (0x69); }   // ldloc; conv.i4
    void PushBound16 (UINT32 Off) {                                // low 16 bits of CodeStart/CodeEnd
        LdArg (1); PushI4 ((INT32) Off);       B (0x91);            // grf[Off]   (ldelem.u1, unsigned)
        LdArg (1); PushI4 ((INT32) (Off + 1)); B (0x91);            // grf[Off+1]
        PushI4 (8); B (0x62); B (0x60);                             // shl ; or
    }
    // SMC write-barrier: grf[CodeDirty + ((a>>8)&255)] |= (CodeStart<=a<CodeEnd).
    // Branchless; inert when CodeStart==CodeEnd. Addresses/bounds are <= 16 bits.
    void EmitStoreBarrier (UINT32 AddrId) {
        LdArg (1);                                                  // array
        PushI4 ((INT32) CPU_STATE_CODEDIRTY_OFFSET); PushAddrI4 (AddrId); PushI4 (11); B (0x64); PushI4 (31); B (0x5f); B (0x58);  // index = CD + (a>>11)&31
        LdArg (1);
        PushI4 ((INT32) CPU_STATE_CODEDIRTY_OFFSET); PushAddrI4 (AddrId); PushI4 (11); B (0x64); PushI4 (31); B (0x5f); B (0x58); B (0x91);  // current = grf[index]
        PushAddrI4 (AddrId); PushBound16 (CPU_STATE_CODESTART_OFFSET); B (0x59); PushI4 (31); B (0x64); PushI4 (1); B (0x61);  // (a>=cs)
        PushAddrI4 (AddrId); PushBound16 (CPU_STATE_CODEEND_OFFSET);   B (0x59); PushI4 (31); B (0x64);                        // (a<ce)
        B (0x5f);                                                   // and -> indirty (0/1)
        PushAddrI4 (AddrId); PushI4 (8); B (0x64); PushI4 (7); B (0x5f);   // bit = (a>>8)&7
        B (0x62);                                                   // shl -> indirty<<bit
        B (0x60);                                                   // or  -> current | (indirty<<bit)
        B (0x9c);                                                   // stelem.i1
    }

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

    std::vector<UINT8>                     m_Code;
    UINT32                                 m_Next = 0;
    UINT32                                 m_ClockTmp = 0xFFFFFFFFu;   // reusable local for EmitTick (lazy)
    std::vector<UINT32>                    m_BlockStart;   // block id -> code offset (0xFFFFFFFF = unset)
    std::vector<std::pair<UINT32, UINT32>> m_Fixups;       // (branch opcode pos, target block id)
};

class ClrBackend final : public ComObject<ICpuBackend> {
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
