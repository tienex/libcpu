/*
 * libcpu: llvm_compat.h
 *
 * LLVM C API Compatibility Layer
 *
 * This header provides a stable API layer between libcpu and LLVM.
 * It allows gradual migration from LLVM C++ API to LLVM C API while
 * maintaining a consistent interface.
 *
 * Design Goals:
 * - Provide stable API that doesn't break with LLVM version changes
 * - Enable gradual migration from C++ to C API
 * - Support both Win32 and POSIX platforms
 * - Minimize performance overhead
 */

#ifndef __libcpu_llvm_compat_h
#define __libcpu_llvm_compat_h

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Configuration: Choose API backend */
#ifndef LIBCPU_USE_LLVM_C_API
#define LIBCPU_USE_LLVM_C_API 0  /* Default: Use C++ API for now */
#endif

/*
 * Opaque Handle Types
 * These represent LLVM objects without exposing C++ implementation details
 */
typedef struct LLVMCompatContext_s*     LLVMCompatContextRef;
typedef struct LLVMCompatModule_s*      LLVMCompatModuleRef;
typedef struct LLVMCompatFunction_s*    LLVMCompatFunctionRef;
typedef struct LLVMCompatBasicBlock_s*  LLVMCompatBasicBlockRef;
typedef struct LLVMCompatValue_s*       LLVMCompatValueRef;
typedef struct LLVMCompatType_s*        LLVMCompatTypeRef;
typedef struct LLVMCompatBuilder_s*     LLVMCompatBuilderRef;
typedef struct LLVMCompatEngine_s*      LLVMCompatEngineRef;

/*
 * Initialization and Cleanup
 */

/**
 * Initialize LLVM system
 * Must be called before any other LLVM operations
 * Returns 0 on success, -1 on failure
 */
int LLVMCompatInitialize(void);

/**
 * Shut down LLVM system
 * Should be called when LLVM is no longer needed
 */
void LLVMCompatShutdown(void);

/*
 * Context Management
 */

/**
 * Create a new LLVM context
 * Context provides isolation for concurrent LLVM use
 */
LLVMCompatContextRef LLVMCompatContextCreate(void);

/**
 * Destroy an LLVM context
 */
void LLVMCompatContextDispose(LLVMCompatContextRef ctx);

/*
 * Module Management
 */

/**
 * Create a new module in the given context
 */
LLVMCompatModuleRef LLVMCompatModuleCreate(const char *name,
                                             LLVMCompatContextRef ctx);

/**
 * Dispose of a module
 */
void LLVMCompatModuleDispose(LLVMCompatModuleRef mod);

/**
 * Print module IR to stderr (for debugging)
 */
void LLVMCompatModuleDump(LLVMCompatModuleRef mod);

/**
 * Verify module correctness
 * Returns 0 if valid, -1 if invalid
 */
int LLVMCompatModuleVerify(LLVMCompatModuleRef mod);

/*
 * Type Creation
 */

/**
 * Get void type
 */
LLVMCompatTypeRef LLVMCompatTypeVoid(LLVMCompatContextRef ctx);

/**
 * Get integer type of specified bit width
 */
LLVMCompatTypeRef LLVMCompatTypeInt(LLVMCompatContextRef ctx, unsigned bits);

/**
 * Get float type (32-bit)
 */
LLVMCompatTypeRef LLVMCompatTypeFloat(LLVMCompatContextRef ctx);

/**
 * Get double type (64-bit)
 */
LLVMCompatTypeRef LLVMCompatTypeDouble(LLVMCompatContextRef ctx);

/**
 * Get pointer type to the given element type
 */
LLVMCompatTypeRef LLVMCompatTypePointer(LLVMCompatTypeRef elementType);

/**
 * Get function type
 * @param returnType Return type of function
 * @param paramTypes Array of parameter types
 * @param paramCount Number of parameters
 * @param isVarArg Whether function is variadic
 */
LLVMCompatTypeRef LLVMCompatTypeFunctionCreate(
    LLVMCompatTypeRef returnType,
    LLVMCompatTypeRef *paramTypes,
    unsigned paramCount,
    int isVarArg);

/*
 * Constant Creation
 */

/**
 * Create integer constant
 */
LLVMCompatValueRef LLVMCompatConstInt(LLVMCompatTypeRef type,
                                       uint64_t value,
                                       int signExtend);

/**
 * Create floating-point constant
 */
LLVMCompatValueRef LLVMCompatConstFloat(LLVMCompatTypeRef type, double value);

/**
 * Create null pointer constant
 */
LLVMCompatValueRef LLVMCompatConstNull(LLVMCompatTypeRef type);

/*
 * Function Management
 */

/**
 * Add a function to a module
 */
LLVMCompatFunctionRef LLVMCompatFunctionCreate(
    LLVMCompatModuleRef mod,
    const char *name,
    LLVMCompatTypeRef functionType);

/**
 * Get a function parameter by index
 */
LLVMCompatValueRef LLVMCompatFunctionGetParam(
    LLVMCompatFunctionRef func,
    unsigned index);

/**
 * Verify function correctness
 * Returns 0 if valid, -1 if invalid
 */
int LLVMCompatFunctionVerify(LLVMCompatFunctionRef func);

/*
 * Basic Block Management
 */

/**
 * Create a new basic block in a function
 */
LLVMCompatBasicBlockRef LLVMCompatBasicBlockCreate(
    LLVMCompatContextRef ctx,
    LLVMCompatFunctionRef func,
    const char *name);

/*
 * IR Builder Operations
 */

/**
 * Create a new IR builder
 */
LLVMCompatBuilderRef LLVMCompatBuilderCreate(LLVMCompatContextRef ctx);

/**
 * Dispose of an IR builder
 */
void LLVMCompatBuilderDispose(LLVMCompatBuilderRef builder);

/**
 * Position builder at end of basic block
 */
void LLVMCompatBuilderSetInsertPoint(LLVMCompatBuilderRef builder,
                                      LLVMCompatBasicBlockRef block);

/**
 * Build return instruction
 */
LLVMCompatValueRef LLVMCompatBuilderBuildRet(LLVMCompatBuilderRef builder,
                                               LLVMCompatValueRef value);

/**
 * Build return void instruction
 */
LLVMCompatValueRef LLVMCompatBuilderBuildRetVoid(LLVMCompatBuilderRef builder);

/**
 * Build unconditional branch
 */
LLVMCompatValueRef LLVMCompatBuilderBuildBr(LLVMCompatBuilderRef builder,
                                              LLVMCompatBasicBlockRef dest);

/**
 * Build conditional branch
 */
LLVMCompatValueRef LLVMCompatBuilderBuildCondBr(
    LLVMCompatBuilderRef builder,
    LLVMCompatValueRef condition,
    LLVMCompatBasicBlockRef trueBB,
    LLVMCompatBasicBlockRef falseBB);

/**
 * Build binary operation (add, sub, mul, etc.)
 */
typedef enum {
    LLVMCompatAdd,
    LLVMCompatSub,
    LLVMCompatMul,
    LLVMCompatUDiv,
    LLVMCompatSDiv,
    LLVMCompatURem,
    LLVMCompatSRem,
    LLVMCompatShl,
    LLVMCompatLShr,
    LLVMCompatAShr,
    LLVMCompatAnd,
    LLVMCompatOr,
    LLVMCompatXor
} LLVMCompatBinOp;

LLVMCompatValueRef LLVMCompatBuilderBuildBinOp(
    LLVMCompatBuilderRef builder,
    LLVMCompatBinOp op,
    LLVMCompatValueRef lhs,
    LLVMCompatValueRef rhs,
    const char *name);

/**
 * Build integer comparison
 */
typedef enum {
    LLVMCompatIntEQ,
    LLVMCompatIntNE,
    LLVMCompatIntUGT,
    LLVMCompatIntUGE,
    LLVMCompatIntULT,
    LLVMCompatIntULE,
    LLVMCompatIntSGT,
    LLVMCompatIntSGE,
    LLVMCompatIntSLT,
    LLVMCompatIntSLE
} LLVMCompatIntPredicate;

LLVMCompatValueRef LLVMCompatBuilderBuildICmp(
    LLVMCompatBuilderRef builder,
    LLVMCompatIntPredicate pred,
    LLVMCompatValueRef lhs,
    LLVMCompatValueRef rhs,
    const char *name);

/**
 * Build alloca (stack allocation)
 */
LLVMCompatValueRef LLVMCompatBuilderBuildAlloca(
    LLVMCompatBuilderRef builder,
    LLVMCompatTypeRef type,
    const char *name);

/**
 * Build load from memory
 */
LLVMCompatValueRef LLVMCompatBuilderBuildLoad(
    LLVMCompatBuilderRef builder,
    LLVMCompatValueRef ptr,
    const char *name);

/**
 * Build store to memory
 */
LLVMCompatValueRef LLVMCompatBuilderBuildStore(
    LLVMCompatBuilderRef builder,
    LLVMCompatValueRef value,
    LLVMCompatValueRef ptr);

/**
 * Build function call
 */
LLVMCompatValueRef LLVMCompatBuilderBuildCall(
    LLVMCompatBuilderRef builder,
    LLVMCompatFunctionRef func,
    LLVMCompatValueRef *args,
    unsigned numArgs,
    const char *name);

/*
 * JIT Execution Engine
 */

/**
 * Create a JIT execution engine for a module
 * Returns 0 on success, -1 on failure
 */
int LLVMCompatEngineCreate(LLVMCompatEngineRef *outEngine,
                             LLVMCompatModuleRef mod,
                             char **outError);

/**
 * Dispose of execution engine
 */
void LLVMCompatEngineDispose(LLVMCompatEngineRef engine);

/**
 * Get compiled function address
 */
uint64_t LLVMCompatEngineGetFunctionAddress(LLVMCompatEngineRef engine,
                                              const char *name);

/**
 * Get pointer to function (for direct call)
 */
void *LLVMCompatEngineGetFunctionPointer(LLVMCompatEngineRef engine,
                                          LLVMCompatFunctionRef func);

/*
 * Optimization
 */

/**
 * Run basic optimizations on module
 * Level: 0 = none, 1 = light, 2 = moderate, 3 = aggressive
 */
void LLVMCompatOptimizeModule(LLVMCompatModuleRef mod, unsigned level);

/**
 * Run basic optimizations on function
 */
void LLVMCompatOptimizeFunction(LLVMCompatFunctionRef func, unsigned level);

/*
 * Platform Utilities
 */

/**
 * Check if running on Win32
 */
int LLVMCompatIsWin32(void);

/**
 * Get LLVM version string
 */
const char *LLVMCompatGetVersion(void);

#ifdef __cplusplus
}
#endif

#endif /* __libcpu_llvm_compat_h */
