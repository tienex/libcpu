/** @file
  In-process JVM textual-emit backend. See JvmBackend.h.

  Each ICpuValue is a JVM long held in a local variable slot. The emitted method,
  "static void insn(byte[] ram, byte[] grf)", is hand-assembled JVM bytecode: a
  constant pool plus a straight-line Code attribute. Comparisons are computed
  arithmetically -- (a-b)>>>63 is 1 exactly when a<b for the (non-negative, masked)
  values in play -- so the bytecode needs no branches and therefore no jump-offset
  patching. The class is installed with JNI DefineClass (no javac) and invoked on a
  process-wide embedded JVM; the guest state is marshalled through byte[] arrays.
**/
#include "JvmBackend.h"
#include "LibCPU/CpuState.h"

#include <jni.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace LibCPU {
namespace {

static CONST UINT32 RAM_SIZE = 0x10000;   // assumed guest RAM (6502 / CHIP-8)

//
// Process-wide embedded JVM, created on first use.
//
static JavaVM  *g_Jvm    = nullptr;        // process-global; created once
static jobject  g_Loader = nullptr;        // system class loader (global ref, cross-thread)

//
// Detach guard. JNI requires a thread that attached itself to the VM to detach
// before it exits, or the VM may fault on thread teardown. One of these lives in
// each background thread's storage (thread_local); its destructor runs at thread
// exit and detaches that thread. The VM-creating thread is attached implicitly and
// must NOT be detached this way, so a guard is armed only for threads we attach.
//
struct JvmThreadDetach {
    BOOLEAN Armed = FALSE;
    ~JvmThreadDetach () {
        if (Armed && g_Jvm != nullptr) {
            g_Jvm->DetachCurrentThread ();
        }
    }
};

//
// Return the JNIEnv for the CALLING thread. A JNIEnv is per-thread and is only
// valid on the thread that obtained it -- it must never be cached and shared
// across threads (the JIT recompiles on a background thread). The JavaVM, by
// contrast, is process-global: create it once, then ask it for this thread's env,
// attaching the thread on first use.
//
static JNIEnv *
GetEnv (VOID)
{
    if (g_Jvm == nullptr) {
        JNIEnv         *Env = nullptr;
        JavaVMInitArgs  Args;
        Args.version            = JNI_VERSION_1_8;
        Args.nOptions           = 0;
        Args.options            = nullptr;
        Args.ignoreUnrecognized = JNI_TRUE;
        if (JNI_CreateJavaVM (&g_Jvm, (void **) &Env, &Args) != JNI_OK) {
            g_Jvm = nullptr;
            return nullptr;
        }
        jclass    ClCls = Env->FindClass ("java/lang/ClassLoader");
        jmethodID Get   = Env->GetStaticMethodID (ClCls, "getSystemClassLoader", "()Ljava/lang/ClassLoader;");
        jobject   Ldr   = Env->CallStaticObjectMethod (ClCls, Get);
        g_Loader = Env->NewGlobalRef (Ldr);
        return Env;
    }

    JNIEnv *Env = nullptr;
    if (g_Jvm->GetEnv ((void **) &Env, JNI_VERSION_1_8) == JNI_OK) {
        return Env;
    }
    // A thread the VM does not yet know about (e.g. the JIT's background recompile
    // thread): attach it and arm its detach guard so it unwinds cleanly at exit.
    if (g_Jvm->AttachCurrentThread ((void **) &Env, nullptr) != JNI_OK) {
        return nullptr;
    }
    static thread_local JvmThreadDetach s_Detach;
    s_Detach.Armed = TRUE;
    return Env;
}

static UINT64
Mask (UINT64 Value, UINT32 Bits)
{
    return (Bits >= 64) ? Value : (Value & (((UINT64) 1 << Bits) - 1));
}

//
// Growable constant pool. Seven fixed entries are reserved up front (see Assemble);
// Integer constants referenced by the bytecode are appended and de-duplicated.
//
class ConstantPool {
public:
    UINT16 AddInteger (INT32 Value) {
        auto It = m_IntIndex.find (Value);
        if (It != m_IntIndex.end ()) {
            return It->second;
        }
        UINT16 Index = (UINT16) (kFixedCount + m_Ints.size () + 1);
        m_Ints.push_back (Value);
        m_IntIndex[Value] = Index;
        return Index;
    }

    UINT16 Count () CONST { return (UINT16) (kFixedCount + m_Ints.size () + 1); }   // pool_count = entries + 1

    // Serialise: the seven fixed entries (using ClassName) then the Integer entries.
    void Serialise (std::vector<UINT8> &Out, std::string CONST &ClassName) CONST {
        AddU2 (Out, Count ());
        AddUtf8 (Out, ClassName);            // 1: Utf8 class name
        AddClass (Out, 1);                   // 2: Class -> 1
        AddUtf8 (Out, "java/lang/Object");   // 3: Utf8 Object
        AddClass (Out, 3);                   // 4: Class -> 3
        AddUtf8 (Out, "insn");               // 5: Utf8 method name
        AddUtf8 (Out, "([B[B)V");            // 6: Utf8 descriptor
        AddUtf8 (Out, "Code");               // 7: Utf8 Code
        for (INT32 Value : m_Ints) {         // 8..: Integer constants
            Out.push_back (3);
            Out.push_back ((UINT8) (Value >> 24));
            Out.push_back ((UINT8) (Value >> 16));
            Out.push_back ((UINT8) (Value >> 8));
            Out.push_back ((UINT8) Value);
        }
    }

    static CONST UINT16 kThisClass  = 2;
    static CONST UINT16 kSuperClass = 4;
    static CONST UINT16 kMethodName = 5;
    static CONST UINT16 kMethodDesc = 6;
    static CONST UINT16 kCodeName   = 7;

private:
    static CONST UINT16 kFixedCount = 7;

    static void AddU2 (std::vector<UINT8> &Out, UINT16 V) {
        Out.push_back ((UINT8) (V >> 8));
        Out.push_back ((UINT8) V);
    }
    static void AddUtf8 (std::vector<UINT8> &Out, std::string CONST &S) {
        Out.push_back (1);
        AddU2 (Out, (UINT16) S.size ());
        Out.insert (Out.end (), S.begin (), S.end ());
    }
    static void AddClass (std::vector<UINT8> &Out, UINT16 NameIndex) {
        Out.push_back (7);
        AddU2 (Out, NameIndex);
    }

    std::vector<INT32>                m_Ints;
    std::unordered_map<INT32, UINT16> m_IntIndex;
};

class JvmValue final : public LcComObject<ICpuValue> {
public:
    JvmValue (UINT32 Id, UINT32 Bits) : m_Id (Id), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    UINT32 m_Id;
    UINT32 m_Bits;
};

class JvmBlock final : public LcComObject<ICpuBlock> {
public:
    explicit JvmBlock (UINT32 Id) : m_Id (Id) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
    UINT32 m_Id;
};

static UINT32 IdOf   (ICpuValue *pValue) { return static_cast<JvmValue *> (pValue)->m_Id; }
static UINT32 BitsOf (ICpuValue *pValue) { return static_cast<JvmValue *> (pValue)->m_Bits; }
static UINT32 BlkId  (ICpuBlock *pBlock) { return static_cast<JvmBlock *> (pBlock)->m_Id; }

class JvmCode final : public LcComObject<ICpuCode> {
public:
    JvmCode (jclass Class, jmethodID Method) : m_Class (Class), m_Method (Method) {}

    ~JvmCode () override {
        JNIEnv *Env = GetEnv ();
        if (m_Class != nullptr && Env != nullptr) {
            Env->DeleteGlobalRef (m_Class);
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuCode, ppvObject);
    }

    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID * /*pFRF*/) override {
        JNIEnv *Env = GetEnv ();
        if (Env == nullptr) {
            return ExecTrap;
        }
        // The host states how much RAM to marshal (0 -> legacy 64 KiB).
        UINT64 RamSize = ((CPU_STATE *) pGRF)->RamSize;
        if (RamSize == 0) { RamSize = CPU_RAM_DEFAULT; }

        jbyteArray Ram = Env->NewByteArray ((jsize) RamSize);
        jbyteArray Grf = Env->NewByteArray ((jsize) sizeof (CPU_STATE));
        Env->SetByteArrayRegion (Ram, 0, (jsize) RamSize, (CONST jbyte *) pRAM);
        Env->SetByteArrayRegion (Grf, 0, (jsize) sizeof (CPU_STATE), (CONST jbyte *) pGRF);

        Env->CallStaticVoidMethod (m_Class, m_Method, Ram, Grf);
        if (Env->ExceptionCheck ()) {
            Env->ExceptionClear ();
            Env->DeleteLocalRef (Ram);
            Env->DeleteLocalRef (Grf);
            return ExecTrap;
        }

        Env->GetByteArrayRegion (Ram, 0, (jsize) RamSize, (jbyte *) pRAM);
        Env->GetByteArrayRegion (Grf, 0, (jsize) sizeof (CPU_STATE), (jbyte *) pGRF);
        Env->DeleteLocalRef (Ram);
        Env->DeleteLocalRef (Grf);
        return ExecOk;
    }

private:
    jclass    m_Class;
    jmethodID m_Method;
};

class JvmEmitter final : public LcComObject<ICpuEmitter>, public ICpuSmcEmitter {
public:
    // Two interfaces (ICpuEmitter + ICpuSmcEmitter): resolve QI here, forward
    // refcounting to the LcComObject base.
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        if (ppvObject == nullptr) {
            return E_POINTER;
        }
        if (LcIsEqualGUID (&riid, &IID_ICpuSmcEmitter)) {
            *ppvObject = static_cast<ICpuSmcEmitter *> (this);
            AddRef ();
            return S_OK;
        }
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }
    UINT32 STDMETHODCALLTYPE AddRef () override { return LcComObject<ICpuEmitter>::AddRef (); }
    UINT32 STDMETHODCALLTYPE Release () override { return LcComObject<ICpuEmitter>::Release (); }

    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        PushLong ((INT64) Mask (Value, Bits));
        LStore (Dest);
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        for (UINT32 k = 0; k < Bits / 8; k++) {
            ALoad (1);
            PushInt ((INT32) (CPU_STATE_REG_OFFSET + Index * 8 + k));
            B (0x33);                    // baload
            PushInt (255);
            B (0x7e);                    // iand
            B (0x85);                    // i2l
            if (k > 0) {
                PushInt ((INT32) (8 * k));
                B (0x79);                // lshl
                B (0x81);                // lor
            }
        }
        LStore (Dest);
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 Bits, BOOLEAN /*Sext*/) override {
        UINT32 Width = Bits ? Bits : BitsOf (pValue);
        for (UINT32 k = 0; k < Width / 8; k++) {
            ALoad (1);
            PushInt ((INT32) (CPU_STATE_REG_OFFSET + Index * 8 + k));
            StoreByteValue (IdOf (pValue), 8 * k);
            B (0x54);                    // bastore
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        for (UINT32 k = 0; k < Bits / 8; k++) {
            ALoad (0);
            LLoad (IdOf (pAddr));
            B (0x88);                    // l2i
            PushInt ((INT32) k);
            B (0x60);                    // iadd  -> index = addr + k
            B (0x33);                    // baload
            PushInt (255);
            B (0x7e);                    // iand
            B (0x85);                    // i2l
            if (k > 0) {
                PushInt ((INT32) (8 * k));
                B (0x79);                // lshl
                B (0x81);                // lor
            }
        }
        LStore (Dest);
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        for (UINT32 k = 0; k < Bits / 8; k++) {
            ALoad (0);
            LLoad (IdOf (pAddr));
            B (0x88);                    // l2i
            PushInt ((INT32) k);
            B (0x60);                    // iadd
            StoreByteValue (IdOf (pValue), 8 * k);
            B (0x54);                    // bastore
        }
        EmitStoreBarrier (IdOf (pAddr));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        UINT32 Dest = Fresh ();
        LLoad (IdOf (pA));
        LLoad (IdOf (pB));
        switch (Op) {
        case BinAdd:  B (0x61); break;                       // ladd
        case BinSub:  B (0x65); break;                       // lsub
        case BinMul:  B (0x69); break;                       // lmul
        case BinAnd:  B (0x7f); break;                       // land
        case BinOr:   B (0x81); break;                       // lor
        case BinXor:  B (0x83); break;                       // lxor
        case BinUDiv: B (0x6d); break;                       // ldiv
        case BinURem: B (0x71); break;                       // lrem
        case BinShl:  B (0x88); B (0x79); break;             // l2i; lshl
        case BinLShr: B (0x88); B (0x7d); break;             // l2i; lushr
        case BinAShr: B (0x88); B (0x7b); break;             // l2i; lshr
        default:      B (0x61); break;
        }
        PushLong ((INT64) Mask (~0ull, Bits));
        B (0x7f);                        // land  -> mask to width
        LStore (Dest);
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        UINT32 Dest = Fresh ();
        UINT64 FullMask = Mask (~0ull, Bits);
        switch (Op) {
        case UnNeg:
            LLoad (IdOf (pA));
            B (0x75);                    // lneg
            PushLong ((INT64) FullMask);
            B (0x7f);                    // land
            LStore (Dest);
            return Make (Dest, Bits, ppValue);
        case UnCom:
            LLoad (IdOf (pA));
            PushLong ((INT64) FullMask);
            B (0x83);                    // lxor  -> ~a within the mask
            LStore (Dest);
            return Make (Dest, Bits, ppValue);
        case UnNot:
            // (a == 0) ? 1 : 0  ==  ((a | -a) >>> 63) ^ 1
            LLoad (IdOf (pA));
            LLoad (IdOf (pA));
            B (0x75);                    // lneg
            B (0x81);                    // lor
            PushInt (63);
            B (0x7d);                    // lushr
            PushLong (1);
            B (0x83);                    // lxor
            LStore (Dest);
            return Make (Dest, 1, ppValue);
        }
        return E_INVALIDARG;
    }

    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        UINT32 A = IdOf (pA);
        UINT32 Bv = IdOf (pB);
        // All forms reduce to a sign-bit test of a difference; for the non-negative,
        // masked values in play, signed and unsigned comparisons coincide.
        switch (Pred) {
        case CmpEq:
            DiffNonZero (A, Bv);
            PushLong (1);
            B (0x83);                    // lxor  -> (a == b)
            break;
        case CmpNe:
            DiffNonZero (A, Bv);
            break;
        case CmpULt: case CmpSLt:
            LessThan (A, Bv);
            break;
        case CmpUGe: case CmpSGe:
            LessThan (A, Bv);
            PushLong (1);
            B (0x83);                    // lxor  -> !(a < b)
            break;
        case CmpUGt: case CmpSGt:
            LessThan (Bv, A);            // a > b  ==  b < a
            break;
        case CmpULe: case CmpSLe:
            LessThan (Bv, A);
            PushLong (1);
            B (0x83);                    // lxor  -> !(b < a)
            break;
        default:
            DiffNonZero (A, Bv);
            break;
        }
        LStore (Dest);
        return Make (Dest, 1, ppValue);
    }

    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA);
        UINT32 Dest = Fresh ();
        switch (Op) {
        case CastTrunc:
            LLoad (IdOf (pA));
            PushLong ((INT64) Mask (~0ull, Bits));
            B (0x7f);                    // land
            break;
        case CastZExt:
            LLoad (IdOf (pA));
            break;
        case CastSExt:
            // ((a << (64-src)) >> (64-src)) & dstmask  -- arithmetic right shift sign-extends.
            LLoad (IdOf (pA));
            PushInt ((INT32) (64 - SrcBits));
            B (0x79);                    // lshl
            PushInt ((INT32) (64 - SrcBits));
            B (0x7b);                    // lshr (arithmetic)
            PushLong ((INT64) Mask (~0ull, Bits));
            B (0x7f);                    // land
            break;
        }
        LStore (Dest);
        return Make (Dest, Bits, ppValue);
    }

    HRESULT STDMETHODCALLTYPE Select (ICpuValue *, ICpuValue *, ICpuValue *, ICpuValue **ppValue) override {
        *ppValue = nullptr;
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        ALoad (1);
        PushInt ((INT32) (CPU_STATE_FLAG_OFFSET + (UINT32) Flag));
        B (0x33);                        // baload
        PushInt (1);
        B (0x7e);                        // iand
        B (0x85);                        // i2l
        LStore (Dest);
        return Make (Dest, 1, ppValue);
    }

    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        ALoad (1);
        PushInt ((INT32) (CPU_STATE_FLAG_OFFSET + (UINT32) Flag));
        LLoad (IdOf (pValue));
        PushLong (1);
        B (0x7f);                        // land
        B (0x88);                        // l2i
        B (0x54);                        // bastore
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override {
        for (UINT32 k = 0; k < 8; k++) {
            ALoad (1);
            PushInt ((INT32) (CPU_STATE_PC_OFFSET + k));
            PushInt ((INT32) ((Pc >> (8 * k)) & 0xff));
            B (0x54);                    // bastore
        }
        return S_OK;
    }

    // ---- control flow: blocks are byte offsets, edges are goto/ifne -------
    // Code is emitted into one stream; each block's start offset is recorded when
    // SetInsertBlock is called; branch operands are 2-byte placeholders patched in
    // Build once every block's offset is known.
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override {
        *ppBlock = new JvmBlock ((UINT32) m_BlockStart.size ());
        m_BlockStart.push_back (0xFFFFFFFFu);   // unset until SetInsertBlock (the exit block stays unset)
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *pBlock) override {
        m_BlockStart[BlkId (pBlock)] = (UINT32) m_Code.size ();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override { *ppBlock = nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *pTarget) override {
        EmitGoto (BlkId (pTarget));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *pCond, ICpuBlock *pTrue, ICpuBlock *pFalse) override {
        LLoad (IdOf (pCond));
        B (0x88);                    // l2i -> int 0/1
        UINT32 P = (UINT32) m_Code.size ();
        B (0x9a);                    // ifne <true>
        m_Fixups.push_back ({ P, BlkId (pTrue) });
        B2 (0);
        EmitGoto (BlkId (pFalse));   // else fall to false
        return S_OK;
    }

    // ---- self-modifying-code guard (ICpuSmcEmitter) -----------------------
    HRESULT STDMETHODCALLTYPE EmitCodeGuard (CPU_ADDR Pc) override {
        UINT32 Page = (UINT32) ((Pc >> 8) & 255);
        ALoad (1);
        PushInt ((INT32) (CPU_STATE_CODEDIRTY_OFFSET + (Page >> 3)));
        B (0x33);                    // baload (this block's bitmap byte)
        PushInt ((INT32) (1u << (Page & 7)));
        B (0x7e);                    // iand (test this page's bit)
        UINT32 P = (UINT32) m_Code.size ();
        B (0x99);                    // ifeq <skip>  (clean -> run the block)
        B2 (0);
        for (UINT32 k = 0; k < 8; k++) {           // dirty: record TrapPc = Pc ...
            ALoad (1);
            PushInt ((INT32) (CPU_STATE_TRAPPC_OFFSET + k));
            PushInt ((INT32) ((Pc >> (8 * k)) & 0xff));
            B (0x54);                // bastore
        }
        B (0xb1);                    // ... and return (void)
        UINT32 Off = (UINT32) m_Code.size () - P;  // intra-block forward branch: patch now
        m_Code[P + 1] = (UINT8) (Off >> 8);
        m_Code[P + 2] = (UINT8) Off;
        return S_OK;
    }

    // Indirect branch: store the runtime target's 8 bytes into TrapPc and return;
    // the host resume loop re-enters at that address.
    HRESULT STDMETHODCALLTYPE IndirectBranch (ICpuValue *pTargetPc) override {
        for (UINT32 k = 0; k < 8; k++) {
            ALoad (1);
            PushInt ((INT32) (CPU_STATE_TRAPPC_OFFSET + k));
            LLoad (IdOf (pTargetPc));
            PushInt ((INT32) (8 * k));
            B (0x7d);                    // lushr
            PushLong (255);
            B (0x7f);                    // land
            B (0x88);                    // l2i
            B (0x54);                    // bastore
        }
        B (0xb1);                        // return (void)
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetDispatchTarget (ICpuValue *pTargetPc) override {
        for (UINT32 k = 0; k < 8; k++) {     // write the target's 8 bytes into DispPc
            ALoad (1);
            PushInt ((INT32) (CPU_STATE_DISPPC_OFFSET + k));
            LLoad (IdOf (pTargetPc));
            PushInt ((INT32) (8 * k));
            B (0x7d);                    // lushr
            PushLong (255);
            B (0x7f);                    // land
            B (0x88);                    // l2i
            B (0x54);                    // bastore
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDispatchTarget (ICpuValue **ppValue) override {
        UINT32 Dest = Fresh ();
        for (UINT32 k = 0; k < 8; k++) {     // assemble DispPc's 8 bytes into a long
            ALoad (1);
            PushInt ((INT32) (CPU_STATE_DISPPC_OFFSET + k));
            B (0x33);                    // baload
            PushInt (255);
            B (0x7e);                    // iand
            B (0x85);                    // i2l
            if (k > 0) {
                PushInt ((INT32) (8 * k));
                B (0x79);                // lshl
                B (0x81);                // lor
            }
        }
        LStore (Dest);
        return Make (Dest, 64, ppValue);
    }

    ICpuCode *Build () {
        JNIEnv *Env = GetEnv ();
        if (Env == nullptr) {
            return nullptr;
        }
        // Final "return": terminates the entry/last block on fall-through and is the
        // landing pad for any block with no recorded start (the AOT exit block).
        UINT32 RetPos = (UINT32) m_Code.size ();
        B (0xb1);                        // return
        for (auto CONST &Fx : m_Fixups) {
            INT32 Target = (m_BlockStart[Fx.second] != 0xFFFFFFFFu) ? (INT32) m_BlockStart[Fx.second] : (INT32) RetPos;
            INT32 Off    = Target - (INT32) Fx.first;   // relative to the branch opcode
            m_Code[Fx.first + 1] = (UINT8) ((Off >> 8) & 0xff);
            m_Code[Fx.first + 2] = (UINT8) (Off & 0xff);
        }

        static UINT32 s_Counter = 0;
        std::string ClassName = "Insn" + std::to_string (s_Counter++);

        std::vector<UINT8> Class;
        Assemble (Class, ClassName);

        jclass Local = Env->DefineClass (ClassName.c_str (), g_Loader,
                                         (CONST jbyte *) Class.data (), (jsize) Class.size ());
        if (Local == nullptr) {
            if (Env->ExceptionCheck ()) {
                Env->ExceptionClear ();
            }
            return nullptr;
        }
        jmethodID Method = Env->GetStaticMethodID (Local, "insn", "([B[B)V");
        jclass Global = (jclass) Env->NewGlobalRef (Local);
        Env->DeleteLocalRef (Local);
        if (Method == nullptr) {
            Env->DeleteGlobalRef (Global);
            return nullptr;
        }
        return new JvmCode (Global, Method);
    }

private:
    UINT32 Fresh () { return m_Next++; }
    UINT8  Slot (UINT32 Id) { return (UINT8) (2 + Id * 2); }   // 2 param slots, longs take 2 each

    void B  (UINT8 Op) { m_Code.push_back (Op); }
    void B2 (UINT16 V) { m_Code.push_back ((UINT8) (V >> 8)); m_Code.push_back ((UINT8) V); }

    void ALoad  (UINT8 N)   { B ((UINT8) (0x2a + N)); }       // aload_0 / aload_1
    void LLoad  (UINT32 Id) { B (0x16); B (Slot (Id)); }      // lload <slot>
    void LStore (UINT32 Id) { B (0x37); B (Slot (Id)); }      // lstore <slot>

    void EmitGoto (UINT32 BlockId) {
        UINT32 P = (UINT32) m_Code.size ();
        B (0xa7);                                            // goto
        m_Fixups.push_back ({ P, BlockId });
        B2 (0);
    }
    // The guest address (a long local) as a 32-bit int.
    void PushAddrInt (UINT32 AddrId) { LLoad (AddrId); B (0x88); }   // lload; l2i
    // The low 16 bits of a CPU_STATE bound (CodeStart/CodeEnd) as an int.
    void PushBound16 (UINT32 Off) {
        ALoad (1); PushInt ((INT32) Off);       B (0x33); PushInt (255); B (0x7e);   // grf[Off] & 255
        ALoad (1); PushInt ((INT32) (Off + 1)); B (0x33); PushInt (255); B (0x7e);   // grf[Off+1] & 255
        PushInt (8); B (0x78); B (0x80);                                             // <<8 ; or
    }
    // SMC write-barrier: grf[CodeDirty + ((a>>8)&255)] |= (CodeStart<=a<CodeEnd).
    // Branchless; inert when CodeStart==CodeEnd. Addresses/bounds are <= 16 bits.
    void EmitStoreBarrier (UINT32 AddrId) {
        ALoad (1);                                                                   // grf
        PushInt ((INT32) CPU_STATE_CODEDIRTY_OFFSET);
        PushAddrInt (AddrId); PushInt (11); B (0x7a); PushInt (31); B (0x7e);         // (a>>11)&31 = bitmap byte
        B (0x60);                                                                    // iadd -> index
        B (0x5c);                                                                    // dup2 (grf,index)
        B (0x33);                                                                    // baload -> current
        PushAddrInt (AddrId); PushBound16 (CPU_STATE_CODESTART_OFFSET); B (0x64); PushInt (31); B (0x7c); PushInt (1); B (0x82);  // (a>=cs)
        PushAddrInt (AddrId); PushBound16 (CPU_STATE_CODEEND_OFFSET);   B (0x64); PushInt (31); B (0x7c);                          // (a<ce)
        B (0x7e);                                                                    // iand -> indirty (0/1)
        PushAddrInt (AddrId); PushInt (8); B (0x7a); PushInt (7); B (0x7e);           // bit = (a>>8)&7
        B (0x78);                                                                    // ishl -> indirty<<bit
        B (0x80);                                                                    // ior  -> current | (indirty<<bit)
        B (0x54);                                                                    // bastore
    }

    void PushInt (INT32 V) {
        if (V >= -1 && V <= 5) {
            B ((UINT8) (0x03 + V));                           // iconst_<V> (iconst_m1 = 0x02)
        } else if (V >= -128 && V <= 127) {
            B (0x10); B ((UINT8) V);                          // bipush
        } else if (V >= -32768 && V <= 32767) {
            B (0x11); B2 ((UINT16) V);                        // sipush
        } else {
            B (0x13); B2 (m_Pool.AddInteger (V));             // ldc_w
        }
    }

    void PushLong (INT64 V) {
        PushInt ((INT32) V);             // slice values fit in a signed int
        B (0x85);                        // i2l
    }

    // Push (value >>> shift) & 0xff as an int, ready for bastore.
    void StoreByteValue (UINT32 ValueId, UINT32 Shift) {
        LLoad (ValueId);
        if (Shift > 0) {
            PushInt ((INT32) Shift);
            B (0x7d);                    // lushr
        }
        PushLong (255);
        B (0x7f);                        // land
        B (0x88);                        // l2i
    }

    // Leaves ((a - b) | -(a - b)) >>> 63  on the stack: 1 if a != b, else 0.
    void DiffNonZero (UINT32 A, UINT32 B_) {
        LLoad (A);
        LLoad (B_);
        B (0x65);                        // lsub  -> d
        B (0x5c);                        // dup2
        B (0x75);                        // lneg
        B (0x81);                        // lor
        PushInt (63);
        B (0x7d);                        // lushr
    }

    // Leaves (a - b) >>> 63 on the stack: 1 if a < b, else 0.
    void LessThan (UINT32 A, UINT32 B_) {
        LLoad (A);
        LLoad (B_);
        B (0x65);                        // lsub
        PushInt (63);
        B (0x7d);                        // lushr
    }

    void Assemble (std::vector<UINT8> &Out, std::string CONST &ClassName) {
        auto U2 = [&] (UINT16 V) { Out.push_back ((UINT8) (V >> 8)); Out.push_back ((UINT8) V); };
        auto U4 = [&] (UINT32 V) {
            Out.push_back ((UINT8) (V >> 24)); Out.push_back ((UINT8) (V >> 16));
            Out.push_back ((UINT8) (V >> 8));  Out.push_back ((UINT8) V);
        };

        U4 (0xCAFEBABE);
        U2 (0);                          // minor
        U2 (49);                         // major (Java 5): old inference verifier, so
                                         // branches need no StackMapTable attribute
        m_Pool.Serialise (Out, ClassName);

        U2 (0x0021);                     // ACC_PUBLIC | ACC_SUPER
        U2 (ConstantPool::kThisClass);
        U2 (ConstantPool::kSuperClass);
        U2 (0);                          // interfaces
        U2 (0);                          // fields
        U2 (1);                          // methods

        U2 (0x0009);                     // ACC_PUBLIC | ACC_STATIC
        U2 (ConstantPool::kMethodName);
        U2 (ConstantPool::kMethodDesc);
        U2 (1);                          // one attribute (Code)
        U2 (ConstantPool::kCodeName);
        U4 ((UINT32) (12 + m_Code.size ()));   // attribute_length
        U2 (64);                         // max_stack (generous; verifier needs only an upper bound)
        U2 ((UINT16) (2 + m_Next * 2 + 2));     // max_locals
        U4 ((UINT32) m_Code.size ());
        Out.insert (Out.end (), m_Code.begin (), m_Code.end ());
        U2 (0);                          // exception_table_length
        U2 (0);                          // attributes_count

        U2 (0);                          // class attributes_count
    }

    HRESULT Make (UINT32 Id, UINT32 Bits, ICpuValue **ppValue) {
        *ppValue = new JvmValue (Id, Bits);
        return S_OK;
    }

    std::vector<UINT8>                       m_Code;
    ConstantPool                             m_Pool;
    UINT32                                   m_Next = 0;
    std::vector<UINT32>                      m_BlockStart;   // block id -> code offset (0xFFFFFFFF = unset)
    std::vector<std::pair<UINT32, UINT32>>   m_Fixups;       // (offset-field opcode pos, target block id)
};

class JvmBackend final : public LcComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBackend, ppvObject);
    }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "jvm"; }

    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override {
        *ppEmitter = new JvmEmitter ();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<JvmEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *
CreateJvmBackend (VOID)
{
    return new JvmBackend ();
}

} // namespace LibCPU
