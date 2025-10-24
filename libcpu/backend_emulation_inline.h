/*
 * libcpu Inline Emulation Layer Header
 *
 * Provides direct JIT code generation for operations instead of calling helpers.
 */

#ifndef BACKEND_EMULATION_INLINE_H
#define BACKEND_EMULATION_INLINE_H

#include "backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create an inline emulation builder that wraps another backend.
 *
 * This builder intercepts IBuilder operations and generates inline JIT code
 * instead of calling runtime helper functions.
 *
 * @param wrapped_builder The backend builder to wrap (e.g., LLVM, SLJIT)
 * @param wrapped_module The backend module (for type queries)
 * @return A new IBuilder that generates inline code, or NULL on error
 *
 * Example usage:
 *   IBackend *llvm_backend = backend_create_llvm();
 *   IModule *module = llvm_backend->CreateModule(llvm_backend, "test");
 *   IBuilder *llvm_builder = module->CreateBuilder(module);
 *
 *   // Wrap with inline emulation
 *   IBuilder *inline_builder = emulation_create_inline_builder(llvm_builder, module);
 *
 *   // Now use inline_builder - it will generate inline code for operations
 *   IValue *result = inline_builder->CreateUDiv(inline_builder, a, b, "div");
 *   // This generates inline DIV instruction, not a call to emu_udiv()
 */
IBuilder* emulation_create_inline_builder(IBuilder *wrapped_builder, IModule *wrapped_module);

#ifdef __cplusplus
}
#endif

#endif /* BACKEND_EMULATION_INLINE_H */
