/*
 * libcpu: llvm_compat.cpp
 *
 * LLVM C API Compatibility Layer Implementation
 *
 * This implements the compatibility layer using LLVM C++ API.
 * In the future, this can be replaced with a pure C API implementation.
 */

#include "llvm_compat.h"

#if LIBCPU_USE_LLVM_C_API
/* Future: Pure C API implementation using LLVM-C headers */
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#else
/* Current: C++ API implementation */
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Verifier.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Utils.h>
#include <llvm/IR/LegacyPassManager.h>

#include <string>
#include <cstring>
#endif

/*
 * Internal Structure Wrappers (C++ API)
 */

#if !LIBCPU_USE_LLVM_C_API

struct LLVMCompatContext_s {
    llvm::LLVMContext *ctx;

    LLVMCompatContext_s() : ctx(new llvm::LLVMContext()) {}
    ~LLVMCompatContext_s() { delete ctx; }
};

struct LLVMCompatModule_s {
    llvm::Module *mod;
    LLVMCompatContext_s *ctx_wrapper;

    LLVMCompatModule_s(llvm::Module *m, LLVMCompatContext_s *c)
        : mod(m), ctx_wrapper(c) {}
    ~LLVMCompatModule_s() { delete mod; }
};

struct LLVMCompatFunction_s {
    llvm::Function *func;

    explicit LLVMCompatFunction_s(llvm::Function *f) : func(f) {}
};

struct LLVMCompatBasicBlock_s {
    llvm::BasicBlock *bb;

    explicit LLVMCompatBasicBlock_s(llvm::BasicBlock *b) : bb(b) {}
};

struct LLVMCompatValue_s {
    llvm::Value *value;

    explicit LLVMCompatValue_s(llvm::Value *v) : value(v) {}
};

struct LLVMCompatType_s {
    llvm::Type *type;

    explicit LLVMCompatType_s(llvm::Type *t) : type(t) {}
};

struct LLVMCompatBuilder_s {
    llvm::IRBuilder<> *builder;
    LLVMCompatContext_s *ctx_wrapper;

    LLVMCompatBuilder_s(LLVMCompatContext_s *c)
        : builder(new llvm::IRBuilder<>(*c->ctx)), ctx_wrapper(c) {}
    ~LLVMCompatBuilder_s() { delete builder; }
};

struct LLVMCompatEngine_s {
    llvm::ExecutionEngine *engine;

    explicit LLVMCompatEngine_s(llvm::ExecutionEngine *e) : engine(e) {}
    ~LLVMCompatEngine_s() { delete engine; }
};

#endif /* !LIBCPU_USE_LLVM_C_API */

/*
 * Initialization and Cleanup
 */

extern "C" int LLVMCompatInitialize(void) {
#if LIBCPU_USE_LLVM_C_API
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeAsmParser();
    return 0;
#else
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    return 0;
#endif
}

extern "C" void LLVMCompatShutdown(void) {
#if LIBCPU_USE_LLVM_C_API
    LLVMShutdown();
#else
    llvm::llvm_shutdown();
#endif
}

/*
 * Context Management
 */

extern "C" LLVMCompatContextRef LLVMCompatContextCreate(void) {
#if LIBCPU_USE_LLVM_C_API
    LLVMCompatContext_s *ctx = new LLVMCompatContext_s();
    ctx->ctx = (llvm::LLVMContext*)LLVMContextCreate();
    return ctx;
#else
    return new LLVMCompatContext_s();
#endif
}

extern "C" void LLVMCompatContextDispose(LLVMCompatContextRef ctx) {
#if LIBCPU_USE_LLVM_C_API
    if (ctx) {
        LLVMContextDispose((LLVMContextRef)ctx->ctx);
        delete ctx;
    }
#else
    delete ctx;
#endif
}

/*
 * Module Management
 */

extern "C" LLVMCompatModuleRef LLVMCompatModuleCreate(
    const char *name,
    LLVMCompatContextRef ctx)
{
#if LIBCPU_USE_LLVM_C_API
    LLVMModuleRef mod = LLVMModuleCreateWithNameInContext(name, (LLVMContextRef)ctx->ctx);
    LLVMCompatModule_s *wrapper = new LLVMCompatModule_s();
    wrapper->mod = (llvm::Module*)mod;
    wrapper->ctx_wrapper = ctx;
    return wrapper;
#else
    if (!ctx || !name) return nullptr;
    llvm::Module *mod = new llvm::Module(name, *ctx->ctx);
    return new LLVMCompatModule_s(mod, ctx);
#endif
}

extern "C" void LLVMCompatModuleDispose(LLVMCompatModuleRef mod) {
    delete mod;
}

extern "C" void LLVMCompatModuleDump(LLVMCompatModuleRef mod) {
#if LIBCPU_USE_LLVM_C_API
    LLVMDumpModule((LLVMModuleRef)mod->mod);
#else
    if (mod && mod->mod) {
        mod->mod->print(llvm::errs(), nullptr);
    }
#endif
}

extern "C" int LLVMCompatModuleVerify(LLVMCompatModuleRef mod) {
#if LIBCPU_USE_LLVM_C_API
    char *error = nullptr;
    LLVMBool failed = LLVMVerifyModule((LLVMModuleRef)mod->mod,
                                        LLVMReturnStatusAction, &error);
    if (error) LLVMDisposeMessage(error);
    return failed ? -1 : 0;
#else
    if (!mod || !mod->mod) return -1;

    std::string errorStr;
    llvm::raw_string_ostream errorStream(errorStr);
    bool failed = llvm::verifyModule(*mod->mod, &errorStream);
    return failed ? -1 : 0;
#endif
}

/*
 * Type Creation
 */

extern "C" LLVMCompatTypeRef LLVMCompatTypeVoid(LLVMCompatContextRef ctx) {
#if LIBCPU_USE_LLVM_C_API
    LLVMCompatType_s *type = new LLVMCompatType_s();
    type->type = (llvm::Type*)LLVMVoidTypeInContext((LLVMContextRef)ctx->ctx);
    return type;
#else
    if (!ctx) return nullptr;
    return new LLVMCompatType_s(llvm::Type::getVoidTy(*ctx->ctx));
#endif
}

extern "C" LLVMCompatTypeRef LLVMCompatTypeInt(
    LLVMCompatContextRef ctx,
    unsigned bits)
{
#if LIBCPU_USE_LLVM_C_API
    LLVMCompatType_s *type = new LLVMCompatType_s();
    type->type = (llvm::Type*)LLVMIntTypeInContext((LLVMContextRef)ctx->ctx, bits);
    return type;
#else
    if (!ctx) return nullptr;
    return new LLVMCompatType_s(llvm::Type::getIntNTy(*ctx->ctx, bits));
#endif
}

extern "C" LLVMCompatTypeRef LLVMCompatTypeFloat(LLVMCompatContextRef ctx) {
#if LIBCPU_USE_LLVM_C_API
    LLVMCompatType_s *type = new LLVMCompatType_s();
    type->type = (llvm::Type*)LLVMFloatTypeInContext((LLVMContextRef)ctx->ctx);
    return type;
#else
    if (!ctx) return nullptr;
    return new LLVMCompatType_s(llvm::Type::getFloatTy(*ctx->ctx));
#endif
}

extern "C" LLVMCompatTypeRef LLVMCompatTypeDouble(LLVMCompatContextRef ctx) {
#if LIBCPU_USE_LLVM_C_API
    LLVMCompatType_s *type = new LLVMCompatType_s();
    type->type = (llvm::Type*)LLVMDoubleTypeInContext((LLVMContextRef)ctx->ctx);
    return type;
#else
    if (!ctx) return nullptr;
    return new LLVMCompatType_s(llvm::Type::getDoubleTy(*ctx->ctx));
#endif
}

extern "C" LLVMCompatTypeRef LLVMCompatTypePointer(LLVMCompatTypeRef elementType) {
#if LIBCPU_USE_LLVM_C_API
    LLVMCompatType_s *type = new LLVMCompatType_s();
    type->type = (llvm::Type*)LLVMPointerType((LLVMTypeRef)elementType->type, 0);
    return type;
#else
    if (!elementType) return nullptr;
    return new LLVMCompatType_s(llvm::PointerType::getUnqual(elementType->type));
#endif
}

extern "C" LLVMCompatTypeRef LLVMCompatTypeFunctionCreate(
    LLVMCompatTypeRef returnType,
    LLVMCompatTypeRef *paramTypes,
    unsigned paramCount,
    int isVarArg)
{
#if LIBCPU_USE_LLVM_C_API
    LLVMTypeRef *params = new LLVMTypeRef[paramCount];
    for (unsigned i = 0; i < paramCount; i++) {
        params[i] = (LLVMTypeRef)paramTypes[i]->type;
    }

    LLVMCompatType_s *type = new LLVMCompatType_s();
    type->type = (llvm::Type*)LLVMFunctionType(
        (LLVMTypeRef)returnType->type, params, paramCount, isVarArg);

    delete[] params;
    return type;
#else
    if (!returnType) return nullptr;

    std::vector<llvm::Type*> params;
    for (unsigned i = 0; i < paramCount; i++) {
        if (paramTypes[i]) {
            params.push_back(paramTypes[i]->type);
        }
    }

    llvm::FunctionType *ft = llvm::FunctionType::get(
        returnType->type, params, isVarArg != 0);
    return new LLVMCompatType_s(ft);
#endif
}

/*
 * Constant Creation
 */

extern "C" LLVMCompatValueRef LLVMCompatConstInt(
    LLVMCompatTypeRef type,
    uint64_t value,
    int signExtend)
{
#if LIBCPU_USE_LLVM_C_API
    LLVMCompatValue_s *val = new LLVMCompatValue_s();
    val->value = (llvm::Value*)LLVMConstInt(
        (LLVMTypeRef)type->type, value, signExtend);
    return val;
#else
    if (!type) return nullptr;
    llvm::Value *v = llvm::ConstantInt::get(type->type, value, signExtend != 0);
    return new LLVMCompatValue_s(v);
#endif
}

extern "C" LLVMCompatValueRef LLVMCompatConstFloat(
    LLVMCompatTypeRef type,
    double value)
{
#if LIBCPU_USE_LLVM_C_API
    LLVMCompatValue_s *val = new LLVMCompatValue_s();
    val->value = (llvm::Value*)LLVMConstReal((LLVMTypeRef)type->type, value);
    return val;
#else
    if (!type) return nullptr;
    llvm::Value *v = llvm::ConstantFP::get(type->type, value);
    return new LLVMCompatValue_s(v);
#endif
}

extern "C" LLVMCompatValueRef LLVMCompatConstNull(LLVMCompatTypeRef type) {
#if LIBCPU_USE_LLVM_C_API
    LLVMCompatValue_s *val = new LLVMCompatValue_s();
    val->value = (llvm::Value*)LLVMConstNull((LLVMTypeRef)type->type);
    return val;
#else
    if (!type) return nullptr;
    llvm::Value *v = llvm::Constant::getNullValue(type->type);
    return new LLVMCompatValue_s(v);
#endif
}

/*
 * Function Management
 */

extern "C" LLVMCompatFunctionRef LLVMCompatFunctionCreate(
    LLVMCompatModuleRef mod,
    const char *name,
    LLVMCompatTypeRef functionType)
{
#if LIBCPU_USE_LLVM_C_API
    LLVMCompatFunction_s *func = new LLVMCompatFunction_s();
    func->func = (llvm::Function*)LLVMAddFunction(
        (LLVMModuleRef)mod->mod, name, (LLVMTypeRef)functionType->type);
    return func;
#else
    if (!mod || !mod->mod || !name || !functionType) return nullptr;

    llvm::FunctionType *ft = llvm::cast<llvm::FunctionType>(functionType->type);
    llvm::Function *f = llvm::Function::Create(
        ft, llvm::Function::ExternalLinkage, name, mod->mod);

    return new LLVMCompatFunction_s(f);
#endif
}

extern "C" LLVMCompatValueRef LLVMCompatFunctionGetParam(
    LLVMCompatFunctionRef func,
    unsigned index)
{
#if LIBCPU_USE_LLVM_C_API
    LLVMCompatValue_s *val = new LLVMCompatValue_s();
    val->value = (llvm::Value*)LLVMGetParam((LLVMValueRef)func->func, index);
    return val;
#else
    if (!func || !func->func) return nullptr;

    llvm::Function::arg_iterator it = func->func->arg_begin();
    for (unsigned i = 0; i < index && it != func->func->arg_end(); i++, ++it);

    if (it == func->func->arg_end()) return nullptr;
    return new LLVMCompatValue_s(&*it);
#endif
}

extern "C" int LLVMCompatFunctionVerify(LLVMCompatFunctionRef func) {
#if LIBCPU_USE_LLVM_C_API
    // C API doesn't have function-level verify, use module verify instead
    return 0;
#else
    if (!func || !func->func) return -1;

    std::string errorStr;
    llvm::raw_string_ostream errorStream(errorStr);
    bool failed = llvm::verifyFunction(*func->func, &errorStream);
    return failed ? -1 : 0;
#endif
}

/*
 * Basic Block Management
 */

extern "C" LLVMCompatBasicBlockRef LLVMCompatBasicBlockCreate(
    LLVMCompatContextRef ctx,
    LLVMCompatFunctionRef func,
    const char *name)
{
#if LIBCPU_USE_LLVM_C_API
    LLVMCompatBasicBlock_s *bb = new LLVMCompatBasicBlock_s();
    bb->bb = (llvm::BasicBlock*)LLVMAppendBasicBlockInContext(
        (LLVMContextRef)ctx->ctx, (LLVMValueRef)func->func, name);
    return bb;
#else
    if (!ctx || !func || !func->func) return nullptr;

    llvm::BasicBlock *bb = llvm::BasicBlock::Create(
        *ctx->ctx, name ? name : "", func->func);
    return new LLVMCompatBasicBlock_s(bb);
#endif
}

/*
 * IR Builder Operations
 */

extern "C" LLVMCompatBuilderRef LLVMCompatBuilderCreate(LLVMCompatContextRef ctx) {
#if LIBCPU_USE_LLVM_C_API
    LLVMCompatBuilder_s *builder = new LLVMCompatBuilder_s();
    builder->builder = (llvm::IRBuilder<>*)LLVMCreateBuilderInContext(
        (LLVMContextRef)ctx->ctx);
    builder->ctx_wrapper = ctx;
    return builder;
#else
    if (!ctx) return nullptr;
    return new LLVMCompatBuilder_s(ctx);
#endif
}

extern "C" void LLVMCompatBuilderDispose(LLVMCompatBuilderRef builder) {
    delete builder;
}

extern "C" void LLVMCompatBuilderSetInsertPoint(
    LLVMCompatBuilderRef builder,
    LLVMCompatBasicBlockRef block)
{
#if LIBCPU_USE_LLVM_C_API
    LLVMPositionBuilderAtEnd((LLVMBuilderRef)builder->builder,
                              (LLVMBasicBlockRef)block->bb);
#else
    if (builder && builder->builder && block && block->bb) {
        builder->builder->SetInsertPoint(block->bb);
    }
#endif
}

extern "C" LLVMCompatValueRef LLVMCompatBuilderBuildRet(
    LLVMCompatBuilderRef builder,
    LLVMCompatValueRef value)
{
#if LIBCPU_USE_LLVM_C_API
    LLVMCompatValue_s *val = new LLVMCompatValue_s();
    val->value = (llvm::Value*)LLVMBuildRet(
        (LLVMBuilderRef)builder->builder, (LLVMValueRef)value->value);
    return val;
#else
    if (!builder || !builder->builder || !value) return nullptr;
    llvm::Value *v = builder->builder->CreateRet(value->value);
    return new LLVMCompatValue_s(v);
#endif
}

extern "C" LLVMCompatValueRef LLVMCompatBuilderBuildRetVoid(
    LLVMCompatBuilderRef builder)
{
#if LIBCPU_USE_LLVM_C_API
    LLVMCompatValue_s *val = new LLVMCompatValue_s();
    val->value = (llvm::Value*)LLVMBuildRetVoid((LLVMBuilderRef)builder->builder);
    return val;
#else
    if (!builder || !builder->builder) return nullptr;
    llvm::Value *v = builder->builder->CreateRetVoid();
    return new LLVMCompatValue_s(v);
#endif
}

extern "C" LLVMCompatValueRef LLVMCompatBuilderBuildBr(
    LLVMCompatBuilderRef builder,
    LLVMCompatBasicBlockRef dest)
{
#if LIBCPU_USE_LLVM_C_API
    LLVMCompatValue_s *val = new LLVMCompatValue_s();
    val->value = (llvm::Value*)LLVMBuildBr(
        (LLVMBuilderRef)builder->builder, (LLVMBasicBlockRef)dest->bb);
    return val;
#else
    if (!builder || !builder->builder || !dest) return nullptr;
    llvm::Value *v = builder->builder->CreateBr(dest->bb);
    return new LLVMCompatValue_s(v);
#endif
}

extern "C" LLVMCompatValueRef LLVMCompatBuilderBuildCondBr(
    LLVMCompatBuilderRef builder,
    LLVMCompatValueRef condition,
    LLVMCompatBasicBlockRef trueBB,
    LLVMCompatBasicBlockRef falseBB)
{
#if LIBCPU_USE_LLVM_C_API
    LLVMCompatValue_s *val = new LLVMCompatValue_s();
    val->value = (llvm::Value*)LLVMBuildCondBr(
        (LLVMBuilderRef)builder->builder,
        (LLVMValueRef)condition->value,
        (LLVMBasicBlockRef)trueBB->bb,
        (LLVMBasicBlockRef)falseBB->bb);
    return val;
#else
    if (!builder || !builder->builder || !condition || !trueBB || !falseBB)
        return nullptr;
    llvm::Value *v = builder->builder->CreateCondBr(
        condition->value, trueBB->bb, falseBB->bb);
    return new LLVMCompatValue_s(v);
#endif
}

extern "C" LLVMCompatValueRef LLVMCompatBuilderBuildAlloca(
    LLVMCompatBuilderRef builder,
    LLVMCompatTypeRef type,
    const char *name)
{
#if LIBCPU_USE_LLVM_C_API
    LLVMCompatValue_s *val = new LLVMCompatValue_s();
    val->value = (llvm::Value*)LLVMBuildAlloca(
        (LLVMBuilderRef)builder->builder, (LLVMTypeRef)type->type, name);
    return val;
#else
    if (!builder || !builder->builder || !type) return nullptr;
    llvm::Value *v = builder->builder->CreateAlloca(type->type, nullptr,
                                                      name ? name : "");
    return new LLVMCompatValue_s(v);
#endif
}

extern "C" LLVMCompatValueRef LLVMCompatBuilderBuildLoad(
    LLVMCompatBuilderRef builder,
    LLVMCompatValueRef ptr,
    const char *name)
{
#if LIBCPU_USE_LLVM_C_API
    LLVMCompatValue_s *val = new LLVMCompatValue_s();
    val->value = (llvm::Value*)LLVMBuildLoad(
        (LLVMBuilderRef)builder->builder, (LLVMValueRef)ptr->value, name);
    return val;
#else
    if (!builder || !builder->builder || !ptr) return nullptr;
    llvm::Value *v = builder->builder->CreateLoad(ptr->value, name ? name : "");
    return new LLVMCompatValue_s(v);
#endif
}

extern "C" LLVMCompatValueRef LLVMCompatBuilderBuildStore(
    LLVMCompatBuilderRef builder,
    LLVMCompatValueRef value,
    LLVMCompatValueRef ptr)
{
#if LIBCPU_USE_LLVM_C_API
    LLVMCompatValue_s *val = new LLVMCompatValue_s();
    val->value = (llvm::Value*)LLVMBuildStore(
        (LLVMBuilderRef)builder->builder,
        (LLVMValueRef)value->value,
        (LLVMValueRef)ptr->value);
    return val;
#else
    if (!builder || !builder->builder || !value || !ptr) return nullptr;
    llvm::Value *v = builder->builder->CreateStore(value->value, ptr->value);
    return new LLVMCompatValue_s(v);
#endif
}

/*
 * Platform Utilities
 */

extern "C" int LLVMCompatIsWin32(void) {
#ifdef _WIN32
    return 1;
#else
    return 0;
#endif
}

extern "C" const char *LLVMCompatGetVersion(void) {
#define LLVM_VERSION_STRING_HELPER(x) #x
#define LLVM_VERSION_STRING(x) LLVM_VERSION_STRING_HELPER(x)
    return LLVM_VERSION_STRING(LLVM_VERSION_MAJOR) "."
           LLVM_VERSION_STRING(LLVM_VERSION_MINOR);
#undef LLVM_VERSION_STRING
#undef LLVM_VERSION_STRING_HELPER
}

/* Note: Additional functions like BinOp, ICmp, Call, Engine functions
 * would be implemented similarly. For brevity, showing the pattern above. */
