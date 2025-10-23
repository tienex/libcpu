/*
 * libcpu LLVM Version Compatibility Layer
 *
 * Supports multiple LLVM JIT engines:
 * - Legacy JIT (LLVM 3.0 - 3.5)
 * - MCJIT (LLVM 3.5 - 8.0)
 * - ORC JIT v1 (LLVM 5.0 - 8.0)
 * - ORC JIT v2 (LLVM 9.0+)
 */

#ifndef __LLVM_VERSIONS_H__
#define __LLVM_VERSIONS_H__

#include "llvm/Config/llvm-config.h"
#include <stdint.h>

/* LLVM version macros */
#define LLVM_VERSION_MAJOR LLVM_VERSION_MAJOR
#define LLVM_VERSION_MINOR LLVM_VERSION_MINOR

/* Determine LLVM version category */
#if LLVM_VERSION_MAJOR < 3
  #error "LLVM version too old (< 3.0)"
#elif LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 5
  #define LLVM_HAS_LEGACY_JIT 1
  #define LLVM_HAS_MCJIT 0
  #define LLVM_HAS_ORC 0
  #define LLVM_HAS_ORCv2 0
#elif LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR >= 5
  #define LLVM_HAS_LEGACY_JIT 1
  #define LLVM_HAS_MCJIT 1
  #define LLVM_HAS_ORC 0
  #define LLVM_HAS_ORCv2 0
#elif LLVM_VERSION_MAJOR >= 4 && LLVM_VERSION_MAJOR < 5
  #define LLVM_HAS_LEGACY_JIT 0
  #define LLVM_HAS_MCJIT 1
  #define LLVM_HAS_ORC 0
  #define LLVM_HAS_ORCv2 0
#elif LLVM_VERSION_MAJOR >= 5 && LLVM_VERSION_MAJOR < 9
  #define LLVM_HAS_LEGACY_JIT 0
  #define LLVM_HAS_MCJIT 1
  #define LLVM_HAS_ORC 1
  #define LLVM_HAS_ORCv2 0
#elif LLVM_VERSION_MAJOR >= 9
  #define LLVM_HAS_LEGACY_JIT 0
  #define LLVM_HAS_MCJIT 1
  #define LLVM_HAS_ORC 1
  #define LLVM_HAS_ORCv2 1
#endif

/* JIT engine types */
typedef enum {
	LLVM_JIT_LEGACY = 0,  /* Legacy JIT (LLVM 3.0-3.5) */
	LLVM_JIT_MCJIT,       /* MCJIT (LLVM 3.5+) */
	LLVM_JIT_ORC_V1,      /* ORC JIT v1 (LLVM 5.0-8.0) */
	LLVM_JIT_ORC_V2,      /* ORC JIT v2 (LLVM 9.0+) */
	LLVM_JIT_AUTO         /* Auto-detect best available */
} llvm_jit_type_t;

/* Forward declarations */
struct LLVMJITContext;

#ifdef __cplusplus
extern "C" {
#endif

/* Get available JIT engines */
uint32_t llvm_get_available_jits(llvm_jit_type_t *jits, uint32_t max_count);

/* Get JIT engine name */
const char* llvm_jit_get_name(llvm_jit_type_t jit);

/* Check if JIT engine is available */
int llvm_jit_is_available(llvm_jit_type_t jit);

/* Get recommended JIT for current LLVM version */
llvm_jit_type_t llvm_jit_get_recommended(void);

/* Create JIT context */
struct LLVMJITContext* llvm_jit_create_context(llvm_jit_type_t jit);

/* Destroy JIT context */
void llvm_jit_destroy_context(struct LLVMJITContext *ctx);

/* Compile module */
int llvm_jit_compile_module(struct LLVMJITContext *ctx, void *llvm_module);

/* Get function address */
void* llvm_jit_get_function_address(struct LLVMJITContext *ctx, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* __LLVM_VERSIONS_H__ */
