/*
 * libcpu Backend Emulation Layer
 *
 * Provides transparent emulation of operations not supported by backend JITs.
 * Works with any backend through encapsulation - wraps IBackend/IModule/IBuilder
 * and automatically provides fallback implementations for missing functionality.
 *
 * Supported emulation categories:
 * - FPU operations (sin, cos, tan, sqrt, pow, etc.)
 * - SIMD operations (vector arithmetic, shuffles, etc.)
 * - Builtins (memcpy, memset, strlen, etc.)
 * - Integer operations (div, mod for simple backends)
 * - Atomic operations
 * - Type conversions
 */

#ifndef __LIBCPU_BACKEND_EMULATION_H__
#define __LIBCPU_BACKEND_EMULATION_H__

#include "backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************
 * Backend Capability Detection
 ***************************************************************************/

/* Capability flags - what operations a backend natively supports */
typedef enum {
	/* Integer arithmetic */
	BACKEND_CAP_INT_DIV       = (1 << 0),   /* Integer division */
	BACKEND_CAP_INT_REM       = (1 << 1),   /* Integer remainder/modulo */

	/* Floating point operations */
	BACKEND_CAP_FP_BASIC      = (1 << 2),   /* Basic FP (add, sub, mul, div) */
	BACKEND_CAP_FP_SQRT       = (1 << 3),   /* Square root */
	BACKEND_CAP_FP_FMA        = (1 << 4),   /* Fused multiply-add */
	BACKEND_CAP_FP_TRIG       = (1 << 5),   /* Trigonometric (sin, cos, tan) */
	BACKEND_CAP_FP_TRANSCEND  = (1 << 6),   /* Transcendental (exp, log, pow) */

	/* SIMD/Vector operations */
	BACKEND_CAP_SIMD_INT      = (1 << 7),   /* Integer SIMD */
	BACKEND_CAP_SIMD_FP       = (1 << 8),   /* Float SIMD */
	BACKEND_CAP_SIMD_SHUFFLE  = (1 << 9),   /* Vector shuffle/permute */

	/* Memory operations */
	BACKEND_CAP_MEMCPY        = (1 << 10),  /* Optimized memcpy */
	BACKEND_CAP_MEMSET        = (1 << 11),  /* Optimized memset */

	/* Atomics */
	BACKEND_CAP_ATOMIC_CAS    = (1 << 12),  /* Compare-and-swap */
	BACKEND_CAP_ATOMIC_FETCH  = (1 << 13),  /* Atomic fetch operations */

	/* Bit manipulation */
	BACKEND_CAP_CTZ           = (1 << 14),  /* Count trailing zeros */
	BACKEND_CAP_CLZ           = (1 << 15),  /* Count leading zeros */
	BACKEND_CAP_POPCNT        = (1 << 16),  /* Population count */

	/* Type support */
	BACKEND_CAP_FLOAT80       = (1 << 17),  /* 80-bit extended precision */
	BACKEND_CAP_FLOAT128      = (1 << 18),  /* 128-bit quad precision */
	BACKEND_CAP_INT128        = (1 << 19),  /* 128-bit integers */

	/* Advanced features */
	BACKEND_CAP_INLINE_ASM    = (1 << 20),  /* Inline assembly */
	BACKEND_CAP_EXCEPTION     = (1 << 21),  /* Exception handling */
} backend_capability_t;

/* Query backend capabilities */
uint64_t backend_get_capabilities(IBackend *backend);

/* Check if specific capability is supported */
int backend_has_capability(IBackend *backend, backend_capability_t cap);

/***************************************************************************
 * Emulation Layer - Wraps backend to provide missing operations
 ***************************************************************************/

/* Create an emulation-wrapped backend
 * Returns a new IBackend that wraps the original and provides emulation
 */
IBackend* backend_create_with_emulation(IBackend *backend);

/* Create emulation-wrapped backend directly from type */
IBackend* backend_create_emulated(backend_type_t type);

/***************************************************************************
 * Emulation Runtime Library
 *
 * These functions are automatically injected into modules when needed.
 * They provide software implementations of missing hardware operations.
 ***************************************************************************/

/* FPU emulation functions */
typedef struct {
	/* Trigonometric */
	double (*sin_f64)(double x);
	double (*cos_f64)(double x);
	double (*tan_f64)(double x);
	double (*asin_f64)(double x);
	double (*acos_f64)(double x);
	double (*atan_f64)(double x);
	double (*atan2_f64)(double y, double x);

	float (*sin_f32)(float x);
	float (*cos_f32)(float x);
	float (*tan_f32)(float x);

	/* Transcendental */
	double (*exp_f64)(double x);
	double (*log_f64)(double x);
	double (*log10_f64)(double x);
	double (*pow_f64)(double x, double y);
	double (*sqrt_f64)(double x);

	float (*exp_f32)(float x);
	float (*log_f32)(float x);
	float (*sqrt_f32)(float x);
	float (*pow_f32)(float x, float y);

	/* Rounding */
	double (*floor_f64)(double x);
	double (*ceil_f64)(double x);
	double (*trunc_f64)(double x);
	double (*round_f64)(double x);

	float (*floor_f32)(float x);
	float (*ceil_f32)(float x);
	float (*trunc_f32)(float x);
	float (*round_f32)(float x);

	/* Other */
	double (*fabs_f64)(double x);
	double (*fmod_f64)(double x, double y);
	float (*fabs_f32)(float x);
	float (*fmod_f32)(float x, float y);
} emulation_fpu_ops_t;

/* Integer emulation functions */
typedef struct {
	/* Division (for backends without div) */
	int64_t (*sdiv_i64)(int64_t a, int64_t b);
	uint64_t (*udiv_i64)(uint64_t a, uint64_t b);
	int32_t (*sdiv_i32)(int32_t a, int32_t b);
	uint32_t (*udiv_i32)(uint32_t a, uint32_t b);

	/* Remainder/modulo */
	int64_t (*srem_i64)(int64_t a, int64_t b);
	uint64_t (*urem_i64)(uint64_t a, uint64_t b);
	int32_t (*srem_i32)(int32_t a, int32_t b);
	uint32_t (*urem_i32)(uint32_t a, uint32_t b);

	/* Bit manipulation */
	uint32_t (*clz_i32)(uint32_t x);
	uint32_t (*ctz_i32)(uint32_t x);
	uint32_t (*popcnt_i32)(uint32_t x);
	uint64_t (*clz_i64)(uint64_t x);
	uint64_t (*ctz_i64)(uint64_t x);
	uint64_t (*popcnt_i64)(uint64_t x);
} emulation_int_ops_t;

/* Memory operation emulation */
typedef struct {
	void (*memcpy)(void *dst, const void *src, size_t n);
	void (*memset)(void *dst, int c, size_t n);
	void (*memmove)(void *dst, const void *src, size_t n);
	int (*memcmp)(const void *s1, const void *s2, size_t n);
	size_t (*strlen)(const char *s);
	char* (*strcpy)(char *dst, const char *src);
	int (*strcmp)(const char *s1, const char *s2);
} emulation_mem_ops_t;

/* SIMD emulation - scalar fallbacks for vector operations */
typedef struct {
	/* Vector integer add (4x i32) */
	void (*v4i32_add)(int32_t *dst, const int32_t *a, const int32_t *b);
	void (*v4i32_sub)(int32_t *dst, const int32_t *a, const int32_t *b);
	void (*v4i32_mul)(int32_t *dst, const int32_t *a, const int32_t *b);

	/* Vector float add (4x f32) */
	void (*v4f32_add)(float *dst, const float *a, const float *b);
	void (*v4f32_sub)(float *dst, const float *a, const float *b);
	void (*v4f32_mul)(float *dst, const float *a, const float *b);
	void (*v4f32_div)(float *dst, const float *a, const float *b);

	/* Vector double (2x f64) */
	void (*v2f64_add)(double *dst, const double *a, const double *b);
	void (*v2f64_sub)(double *dst, const double *a, const double *b);
	void (*v2f64_mul)(double *dst, const double *a, const double *b);
	void (*v2f64_div)(double *dst, const double *a, const double *b);
} emulation_simd_ops_t;

/* Atomic operation emulation (with locks) */
typedef struct {
	/* Compare and swap */
	int (*cas_i32)(int32_t *ptr, int32_t expected, int32_t desired);
	int (*cas_i64)(int64_t *ptr, int64_t expected, int64_t desired);

	/* Fetch and add */
	int32_t (*fetch_add_i32)(int32_t *ptr, int32_t val);
	int64_t (*fetch_add_i64)(int64_t *ptr, int64_t val);

	/* Fetch and sub */
	int32_t (*fetch_sub_i32)(int32_t *ptr, int32_t val);
	int64_t (*fetch_sub_i64)(int64_t *ptr, int64_t val);
} emulation_atomic_ops_t;

/* Get emulation runtime operations */
const emulation_fpu_ops_t* emulation_get_fpu_ops(void);
const emulation_int_ops_t* emulation_get_int_ops(void);
const emulation_mem_ops_t* emulation_get_mem_ops(void);
const emulation_simd_ops_t* emulation_get_simd_ops(void);
const emulation_atomic_ops_t* emulation_get_atomic_ops(void);

/***************************************************************************
 * Extended Builder Operations
 *
 * High-level operations that are automatically emulated if not supported
 ***************************************************************************/

/* Extended FPU operations */
IValue* builder_create_sin(IBuilder *builder, IValue *val, const char *name);
IValue* builder_create_cos(IBuilder *builder, IValue *val, const char *name);
IValue* builder_create_tan(IBuilder *builder, IValue *val, const char *name);
IValue* builder_create_sqrt(IBuilder *builder, IValue *val, const char *name);
IValue* builder_create_exp(IBuilder *builder, IValue *val, const char *name);
IValue* builder_create_log(IBuilder *builder, IValue *val, const char *name);
IValue* builder_create_pow(IBuilder *builder, IValue *base, IValue *exp, const char *name);
IValue* builder_create_floor(IBuilder *builder, IValue *val, const char *name);
IValue* builder_create_ceil(IBuilder *builder, IValue *val, const char *name);

/* Extended integer operations */
IValue* builder_create_clz(IBuilder *builder, IValue *val, const char *name);
IValue* builder_create_ctz(IBuilder *builder, IValue *val, const char *name);
IValue* builder_create_popcnt(IBuilder *builder, IValue *val, const char *name);

/* Memory intrinsics */
void builder_create_memcpy(IBuilder *builder, IValue *dst, IValue *src, IValue *size);
void builder_create_memset(IBuilder *builder, IValue *dst, IValue *val, IValue *size);

/* Atomic operations */
IValue* builder_create_atomic_cas(IBuilder *builder, IValue *ptr, IValue *expected, IValue *desired, const char *name);
IValue* builder_create_atomic_fetch_add(IBuilder *builder, IValue *ptr, IValue *val, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* __LIBCPU_BACKEND_EMULATION_H__ */
