/** @file
  LLVM JIT backend implementation. See LlvmBackend.h.

  LLVM headers are included first, before the LibCPU headers, so that LibCPU's
  UEFI IN/OUT/CONST/VOID macros are not in effect while LLVM is parsed.
**/
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Error.h"

#include "LlvmBackend.h"
#include "LibCPU/CpuState.h"

#include <memory>
#include <atomic>

using namespace llvm;

#define LIBCPU_STR2(x) #x
#define LIBCPU_STR(x)  LIBCPU_STR2 (x)

namespace LibCPU {
namespace {

//
// One-time native target initialization.
//
static VOID
EnsureNativeTargetInit ()
{
    static std::atomic<bool> Done{false};
    bool Expected = false;
    if (Done.compare_exchange_strong (Expected, true)) {
        InitializeNativeTarget ();
        InitializeNativeTargetAsmPrinter ();
    }
}

//
// Opaque-handle wrappers over LLVM Value / BasicBlock.
//
class LlvmValue final : public LcComObject<ICpuValue> {
public:
    explicit LlvmValue (llvm::Value *pV) : m_pV (pV) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuValue, ppvObject);
    }
    llvm::Value *m_pV;
};

class LlvmBlock final : public LcComObject<ICpuBlock> {
public:
    explicit LlvmBlock (llvm::BasicBlock *pB) : m_pB (pB) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBlock, ppvObject);
    }
    llvm::BasicBlock *m_pB;
};

static llvm::Value      *ValOf (ICpuValue *pValue) { return static_cast<LlvmValue *> (pValue)->m_pV; }
static llvm::BasicBlock *BlkOf (ICpuBlock *pBlock) { return static_cast<LlvmBlock *> (pBlock)->m_pB; }

//
// The JIT'd code object: owns its LLJIT (keeps the code alive) and the entry pointer.
//
typedef int (*JittedFn) (void *pRAM, void *pGRF, void *pFRF);

class LlvmCode final : public LcComObject<ICpuCode> {
public:
    LlvmCode (std::unique_ptr<orc::LLJIT> Jit, JittedFn Fn)
        : m_Jit (std::move (Jit)), m_Fn (Fn) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuCode, ppvObject);
    }
    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID *pFRF) override {
        return (CPU_EXEC_STATUS) m_Fn (pRAM, pGRF, pFRF);
    }
private:
    std::unique_ptr<orc::LLJIT> m_Jit;
    JittedFn                    m_Fn;
};

//
// The builder: emits LLVM IR for one translation unit (one function "insn").
//
class LlvmEmitter final : public LcComObject<ICpuEmitter> {
public:
    LlvmEmitter () {
        m_Ctx     = std::make_unique<LLVMContext> ();
        m_Mod     = std::make_unique<Module> ("libcpu.jit", *m_Ctx);
        m_Builder = std::make_unique<IRBuilder<>> (*m_Ctx);

        Type     *pPtrTy = PointerType::getUnqual (*m_Ctx);
        Type     *pI32Ty = Type::getInt32Ty (*m_Ctx);
        Type     *Args[] = { pPtrTy, pPtrTy, pPtrTy };
        FunctionType *pFnTy = FunctionType::get (pI32Ty, Args, false);
        m_pFn = Function::Create (pFnTy, Function::ExternalLinkage, "insn", m_Mod.get ());

        BasicBlock *pEntry = BasicBlock::Create (*m_Ctx, "entry", m_pFn);
        m_Builder->SetInsertPoint (pEntry);

        auto It  = m_pFn->arg_begin ();
        m_pRAM   = &*It++; m_pRAM->setName ("RAM");
        m_pGRF   = &*It++; m_pGRF->setName ("GRF");
        m_pFRF   = &*It++; m_pFRF->setName ("FRF");
    }

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }

    // ---- values -----------------------------------------------------------
    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        return Wrap (ConstantInt::get (IntTy (Bits), Value), ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        llvm::Value *pPtr = RegPtr (Index);
        llvm::Value *pV64 = m_Builder->CreateLoad (IntTy (64), pPtr);
        return Wrap (m_Builder->CreateZExtOrTrunc (pV64, IntTy (Bits)), ppValue);
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 /*Bits*/, BOOLEAN Sext) override {
        llvm::Value *pV = ValOf (pValue);
        llvm::Value *pV64 = Sext ? m_Builder->CreateSExtOrTrunc (pV, IntTy (64))
                                 : m_Builder->CreateZExtOrTrunc (pV, IntTy (64));
        m_Builder->CreateStore (pV64, RegPtr (Index));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        return Wrap (m_Builder->CreateLoad (IntTy (Bits), MemPtr (ValOf (pAddr))), ppValue);
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        llvm::Value *pV = m_Builder->CreateZExtOrTrunc (ValOf (pValue), IntTy (Bits));
        m_Builder->CreateStore (pV, MemPtr (ValOf (pAddr)));
        return S_OK;
    }

    // ---- arithmetic / compare / cast / select -----------------------------
    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        llvm::Value *A = ValOf (pA), *B = ValOf (pB), *R = nullptr;
        switch (Op) {
        case BinAdd:  R = m_Builder->CreateAdd  (A, B); break;
        case BinSub:  R = m_Builder->CreateSub  (A, B); break;
        case BinMul:  R = m_Builder->CreateMul  (A, B); break;
        case BinUDiv: R = m_Builder->CreateUDiv (A, B); break;
        case BinSDiv: R = m_Builder->CreateSDiv (A, B); break;
        case BinURem: R = m_Builder->CreateURem (A, B); break;
        case BinSRem: R = m_Builder->CreateSRem (A, B); break;
        case BinAnd:  R = m_Builder->CreateAnd  (A, B); break;
        case BinOr:   R = m_Builder->CreateOr   (A, B); break;
        case BinXor:  R = m_Builder->CreateXor  (A, B); break;
        case BinShl:  R = m_Builder->CreateShl  (A, B); break;
        case BinLShr: R = m_Builder->CreateLShr (A, B); break;
        case BinAShr: R = m_Builder->CreateAShr (A, B); break;
        case BinRol:  R = m_Builder->CreateOr (m_Builder->CreateShl (A, B), m_Builder->CreateLShr (A, B)); break;
        case BinRor:  R = m_Builder->CreateOr (m_Builder->CreateLShr (A, B), m_Builder->CreateShl (A, B)); break;
        }
        return Wrap (R, ppValue);
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        llvm::Value *A = ValOf (pA), *R = nullptr;
        switch (Op) {
        case UnNeg: R = m_Builder->CreateNeg (A); break;
        case UnCom: R = m_Builder->CreateNot (A); break;
        case UnNot: R = m_Builder->CreateICmpEQ (A, ConstantInt::get (A->getType (), 0)); break;
        }
        return Wrap (R, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        llvm::Value *A = ValOf (pA), *B = ValOf (pB), *R = nullptr;
        switch (Pred) {
        case CmpEq:  R = m_Builder->CreateICmpEQ  (A, B); break;
        case CmpNe:  R = m_Builder->CreateICmpNE  (A, B); break;
        case CmpULt: R = m_Builder->CreateICmpULT (A, B); break;
        case CmpULe: R = m_Builder->CreateICmpULE (A, B); break;
        case CmpUGt: R = m_Builder->CreateICmpUGT (A, B); break;
        case CmpUGe: R = m_Builder->CreateICmpUGE (A, B); break;
        case CmpSLt: R = m_Builder->CreateICmpSLT (A, B); break;
        case CmpSLe: R = m_Builder->CreateICmpSLE (A, B); break;
        case CmpSGt: R = m_Builder->CreateICmpSGT (A, B); break;
        case CmpSGe: R = m_Builder->CreateICmpSGE (A, B); break;
        }
        return Wrap (R, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        llvm::Value *A = ValOf (pA), *R = nullptr;
        switch (Op) {
        case CastTrunc: R = m_Builder->CreateTrunc (A, IntTy (Bits)); break;
        case CastZExt:  R = m_Builder->CreateZExt  (A, IntTy (Bits)); break;
        case CastSExt:  R = m_Builder->CreateSExt  (A, IntTy (Bits)); break;
        }
        return Wrap (R, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *pCond, ICpuValue *pTrue, ICpuValue *pFalse, ICpuValue **ppValue) override {
        return Wrap (m_Builder->CreateSelect (ValOf (pCond), ValOf (pTrue), ValOf (pFalse)), ppValue);
    }

    // ---- flags ------------------------------------------------------------
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        llvm::Value *pByte = m_Builder->CreateLoad (IntTy (8), FlagPtr (Flag));
        return Wrap (m_Builder->CreateTrunc (pByte, IntTy (1)), ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        llvm::Value *pByte = m_Builder->CreateZExtOrTrunc (ValOf (pValue), IntTy (8));
        m_Builder->CreateStore (pByte, FlagPtr (Flag));
        return S_OK;
    }

    // ---- control flow -----------------------------------------------------
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override {
        m_Builder->CreateStore (ConstantInt::get (IntTy (64), Pc), StatePtr (CPU_STATE_PC_OFFSET));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *pName, ICpuBlock **ppBlock) override {
        *ppBlock = new LlvmBlock (BasicBlock::Create (*m_Ctx, pName ? pName : "", m_pFn));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *pBlock) override {
        m_Builder->SetInsertPoint (BlkOf (pBlock));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override {
        *ppBlock = new LlvmBlock (m_Builder->GetInsertBlock ());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *pTarget) override {
        m_Builder->CreateBr (BlkOf (pTarget));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *pCond, ICpuBlock *pTrue, ICpuBlock *pFalse) override {
        m_Builder->CreateCondBr (ValOf (pCond), BlkOf (pTrue), BlkOf (pFalse));
        return S_OK;
    }

    //
    // Finalize: terminate the entry block and JIT-compile. Returns the code object.
    //
    ICpuCode *Build () {
        m_Builder->CreateRet (ConstantInt::get (IntTy (32), 0));
        if (verifyFunction (*m_pFn, &errs ())) {
            return nullptr;
        }

        EnsureNativeTargetInit ();
        auto JitOrErr = orc::LLJITBuilder ().create ();
        if (!JitOrErr) {
            consumeError (JitOrErr.takeError ());
            return nullptr;
        }
        std::unique_ptr<orc::LLJIT> Jit = std::move (*JitOrErr);

        orc::ThreadSafeContext Tsc (std::move (m_Ctx));
        if (auto Err = Jit->addIRModule (orc::ThreadSafeModule (std::move (m_Mod), Tsc))) {
            consumeError (std::move (Err));
            return nullptr;
        }

        auto SymOrErr = Jit->lookup ("insn");
        if (!SymOrErr) {
            consumeError (SymOrErr.takeError ());
            return nullptr;
        }
        JittedFn Fn = SymOrErr->toPtr<JittedFn> ();
        return new LlvmCode (std::move (Jit), Fn);
    }

private:
    IntegerType *IntTy (UINT32 Bits) { return IntegerType::get (*m_Ctx, Bits); }

    llvm::Value *StatePtr (UINT32 Offset) {
        return m_Builder->CreateGEP (IntTy (8), m_pGRF,
                                     ConstantInt::get (IntTy (64), Offset));
    }
    llvm::Value *RegPtr (UINT32 Index) { return StatePtr (CPU_STATE_REG_OFFSET + Index * 8); }
    llvm::Value *FlagPtr (CPU_FLAG Flag) { return StatePtr (CPU_STATE_FLAG_OFFSET + (UINT32) Flag); }
    llvm::Value *MemPtr (llvm::Value *pAddr) {
        llvm::Value *pIdx = m_Builder->CreateZExtOrTrunc (pAddr, IntTy (64));
        return m_Builder->CreateGEP (IntTy (8), m_pRAM, pIdx);
    }
    HRESULT Wrap (llvm::Value *pV, ICpuValue **ppValue) {
        *ppValue = new LlvmValue (pV);
        return S_OK;
    }

    std::unique_ptr<LLVMContext> m_Ctx;
    std::unique_ptr<Module>      m_Mod;
    std::unique_ptr<IRBuilder<>> m_Builder;
    Function    *m_pFn  = nullptr;
    llvm::Value *m_pRAM = nullptr;
    llvm::Value *m_pGRF = nullptr;
    llvm::Value *m_pFRF = nullptr;
};

class LlvmBackend final : public LcComObject<ICpuBackend> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuBackend, ppvObject);
    }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "llvm-" LIBCPU_STR (LLVM_VERSION_MAJOR); }
    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture * /*pArch*/, ICpuEmitter **ppEmitter) override {
        *ppEmitter = new LlvmEmitter ();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<LlvmEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
};

} // anonymous namespace

ICpuBackend *
CreateLlvmBackend (VOID)
{
    return new LlvmBackend ();
}

} // namespace LibCPU
