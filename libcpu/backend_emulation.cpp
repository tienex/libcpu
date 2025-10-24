/*
 * libcpu Backend Emulation Layer Implementation
 *
 * Transparent emulation of operations not supported by backend JITs.
 * Uses wrapper pattern to intercept operations and provide fallbacks.
 */

#include "backend_emulation.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <map>
#include <string>
#include <vector>

/***************************************************************************
 * Emulation Runtime Library - Actual implementations
 ***************************************************************************/

/* FPU emulation implementations */
static double emu_sin_f64(double x) { return sin(x); }
static double emu_cos_f64(double x) { return cos(x); }
static double emu_tan_f64(double x) { return tan(x); }
static double emu_asin_f64(double x) { return asin(x); }
static double emu_acos_f64(double x) { return acos(x); }
static double emu_atan_f64(double x) { return atan(x); }
static double emu_atan2_f64(double y, double x) { return atan2(y, x); }

static float emu_sin_f32(float x) { return sinf(x); }
static float emu_cos_f32(float x) { return cosf(x); }
static float emu_tan_f32(float x) { return tanf(x); }

static double emu_exp_f64(double x) { return exp(x); }
static double emu_log_f64(double x) { return log(x); }
static double emu_log10_f64(double x) { return log10(x); }
static double emu_pow_f64(double x, double y) { return pow(x, y); }
static double emu_sqrt_f64(double x) { return sqrt(x); }

static float emu_exp_f32(float x) { return expf(x); }
static float emu_log_f32(float x) { return logf(x); }
static float emu_sqrt_f32(float x) { return sqrtf(x); }
static float emu_pow_f32(float x, float y) { return powf(x, y); }

static double emu_floor_f64(double x) { return floor(x); }
static double emu_ceil_f64(double x) { return ceil(x); }
static double emu_trunc_f64(double x) { return trunc(x); }
static double emu_round_f64(double x) { return round(x); }

static float emu_floor_f32(float x) { return floorf(x); }
static float emu_ceil_f32(float x) { return ceilf(x); }
static float emu_trunc_f32(float x) { return truncf(x); }
static float emu_round_f32(float x) { return roundf(x); }

static double emu_fabs_f64(double x) { return fabs(x); }
static double emu_fmod_f64(double x, double y) { return fmod(x, y); }
static float emu_fabs_f32(float x) { return fabsf(x); }
static float emu_fmod_f32(float x, float y) { return fmodf(x, y); }

static const emulation_fpu_ops_t fpu_ops = {
	emu_sin_f64, emu_cos_f64, emu_tan_f64, emu_asin_f64, emu_acos_f64, emu_atan_f64, emu_atan2_f64,
	emu_sin_f32, emu_cos_f32, emu_tan_f32,
	emu_exp_f64, emu_log_f64, emu_log10_f64, emu_pow_f64, emu_sqrt_f64,
	emu_exp_f32, emu_log_f32, emu_sqrt_f32, emu_pow_f32,
	emu_floor_f64, emu_ceil_f64, emu_trunc_f64, emu_round_f64,
	emu_floor_f32, emu_ceil_f32, emu_trunc_f32, emu_round_f32,
	emu_fabs_f64, emu_fmod_f64, emu_fabs_f32, emu_fmod_f32
};

/* Integer emulation implementations */
static int64_t emu_sdiv_i64(int64_t a, int64_t b) { return a / b; }
static uint64_t emu_udiv_i64(uint64_t a, uint64_t b) { return a / b; }
static int32_t emu_sdiv_i32(int32_t a, int32_t b) { return a / b; }
static uint32_t emu_udiv_i32(uint32_t a, uint32_t b) { return a / b; }

static int64_t emu_srem_i64(int64_t a, int64_t b) { return a % b; }
static uint64_t emu_urem_i64(uint64_t a, uint64_t b) { return a % b; }
static int32_t emu_srem_i32(int32_t a, int32_t b) { return a % b; }
static uint32_t emu_urem_i32(uint32_t a, uint32_t b) { return a % b; }

static uint32_t emu_clz_i32(uint32_t x) {
	if (x == 0) return 32;
	uint32_t n = 0;
	if ((x & 0xFFFF0000) == 0) { n += 16; x <<= 16; }
	if ((x & 0xFF000000) == 0) { n += 8; x <<= 8; }
	if ((x & 0xF0000000) == 0) { n += 4; x <<= 4; }
	if ((x & 0xC0000000) == 0) { n += 2; x <<= 2; }
	if ((x & 0x80000000) == 0) { n += 1; }
	return n;
}

static uint32_t emu_ctz_i32(uint32_t x) {
	if (x == 0) return 32;
	uint32_t n = 0;
	if ((x & 0x0000FFFF) == 0) { n += 16; x >>= 16; }
	if ((x & 0x000000FF) == 0) { n += 8; x >>= 8; }
	if ((x & 0x0000000F) == 0) { n += 4; x >>= 4; }
	if ((x & 0x00000003) == 0) { n += 2; x >>= 2; }
	if ((x & 0x00000001) == 0) { n += 1; }
	return n;
}

static uint32_t emu_popcnt_i32(uint32_t x) {
	x = x - ((x >> 1) & 0x55555555);
	x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
	x = (x + (x >> 4)) & 0x0F0F0F0F;
	x = x + (x >> 8);
	x = x + (x >> 16);
	return x & 0x3F;
}

static uint64_t emu_clz_i64(uint64_t x) {
	if (x == 0) return 64;
	uint64_t n = 0;
	if ((x & 0xFFFFFFFF00000000ULL) == 0) { n += 32; x <<= 32; }
	if ((x & 0xFFFF000000000000ULL) == 0) { n += 16; x <<= 16; }
	if ((x & 0xFF00000000000000ULL) == 0) { n += 8; x <<= 8; }
	if ((x & 0xF000000000000000ULL) == 0) { n += 4; x <<= 4; }
	if ((x & 0xC000000000000000ULL) == 0) { n += 2; x <<= 2; }
	if ((x & 0x8000000000000000ULL) == 0) { n += 1; }
	return n;
}

static uint64_t emu_ctz_i64(uint64_t x) {
	if (x == 0) return 64;
	uint64_t n = 0;
	if ((x & 0x00000000FFFFFFFFULL) == 0) { n += 32; x >>= 32; }
	if ((x & 0x000000000000FFFFULL) == 0) { n += 16; x >>= 16; }
	if ((x & 0x00000000000000FFULL) == 0) { n += 8; x >>= 8; }
	if ((x & 0x000000000000000FULL) == 0) { n += 4; x >>= 4; }
	if ((x & 0x0000000000000003ULL) == 0) { n += 2; x >>= 2; }
	if ((x & 0x0000000000000001ULL) == 0) { n += 1; }
	return n;
}

static uint64_t emu_popcnt_i64(uint64_t x) {
	x = x - ((x >> 1) & 0x5555555555555555ULL);
	x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
	x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
	x = x + (x >> 8);
	x = x + (x >> 16);
	x = x + (x >> 32);
	return x & 0x7F;
}

static const emulation_int_ops_t int_ops = {
	emu_sdiv_i64, emu_udiv_i64, emu_sdiv_i32, emu_udiv_i32,
	emu_srem_i64, emu_urem_i64, emu_srem_i32, emu_urem_i32,
	emu_clz_i32, emu_ctz_i32, emu_popcnt_i32,
	emu_clz_i64, emu_ctz_i64, emu_popcnt_i64
};

/* Memory operation emulation */
static void emu_memcpy(void *dst, const void *src, size_t n) { memcpy(dst, src, n); }
static void emu_memset(void *dst, int c, size_t n) { memset(dst, c, n); }
static void emu_memmove(void *dst, const void *src, size_t n) { memmove(dst, src, n); }
static int emu_memcmp(const void *s1, const void *s2, size_t n) { return memcmp(s1, s2, n); }
static size_t emu_strlen(const char *s) { return strlen(s); }
static char* emu_strcpy(char *dst, const char *src) { return strcpy(dst, src); }
static int emu_strcmp(const char *s1, const char *s2) { return strcmp(s1, s2); }

static const emulation_mem_ops_t mem_ops = {
	emu_memcpy, emu_memset, emu_memmove, emu_memcmp,
	emu_strlen, emu_strcpy, emu_strcmp
};

/* SIMD emulation - scalar fallbacks */
static void emu_v4i32_add(int32_t *dst, const int32_t *a, const int32_t *b) {
	for (int i = 0; i < 4; i++) dst[i] = a[i] + b[i];
}
static void emu_v4i32_sub(int32_t *dst, const int32_t *a, const int32_t *b) {
	for (int i = 0; i < 4; i++) dst[i] = a[i] - b[i];
}
static void emu_v4i32_mul(int32_t *dst, const int32_t *a, const int32_t *b) {
	for (int i = 0; i < 4; i++) dst[i] = a[i] * b[i];
}

static void emu_v4f32_add(float *dst, const float *a, const float *b) {
	for (int i = 0; i < 4; i++) dst[i] = a[i] + b[i];
}
static void emu_v4f32_sub(float *dst, const float *a, const float *b) {
	for (int i = 0; i < 4; i++) dst[i] = a[i] - b[i];
}
static void emu_v4f32_mul(float *dst, const float *a, const float *b) {
	for (int i = 0; i < 4; i++) dst[i] = a[i] * b[i];
}
static void emu_v4f32_div(float *dst, const float *a, const float *b) {
	for (int i = 0; i < 4; i++) dst[i] = a[i] / b[i];
}

static void emu_v2f64_add(double *dst, const double *a, const double *b) {
	for (int i = 0; i < 2; i++) dst[i] = a[i] + b[i];
}
static void emu_v2f64_sub(double *dst, const double *a, const double *b) {
	for (int i = 0; i < 2; i++) dst[i] = a[i] - b[i];
}
static void emu_v2f64_mul(double *dst, const double *a, const double *b) {
	for (int i = 0; i < 2; i++) dst[i] = a[i] * b[i];
}
static void emu_v2f64_div(double *dst, const double *a, const double *b) {
	for (int i = 0; i < 2; i++) dst[i] = a[i] / b[i];
}

static const emulation_simd_ops_t simd_ops = {
	emu_v4i32_add, emu_v4i32_sub, emu_v4i32_mul,
	emu_v4f32_add, emu_v4f32_sub, emu_v4f32_mul, emu_v4f32_div,
	emu_v2f64_add, emu_v2f64_sub, emu_v2f64_mul, emu_v2f64_div
};

/* Atomic operation emulation (with simple spinlock) */
static volatile int atomic_lock = 0;

static void acquire_lock(void) {
	while (__sync_lock_test_and_set(&atomic_lock, 1)) {
		while (atomic_lock) { /* spin */ }
	}
}

static void release_lock(void) {
	__sync_lock_release(&atomic_lock);
}

static int emu_cas_i32(int32_t *ptr, int32_t expected, int32_t desired) {
	acquire_lock();
	int success = (*ptr == expected);
	if (success) *ptr = desired;
	release_lock();
	return success;
}

static int emu_cas_i64(int64_t *ptr, int64_t expected, int64_t desired) {
	acquire_lock();
	int success = (*ptr == expected);
	if (success) *ptr = desired;
	release_lock();
	return success;
}

static int32_t emu_fetch_add_i32(int32_t *ptr, int32_t val) {
	acquire_lock();
	int32_t old = *ptr;
	*ptr = old + val;
	release_lock();
	return old;
}

static int64_t emu_fetch_add_i64(int64_t *ptr, int64_t val) {
	acquire_lock();
	int64_t old = *ptr;
	*ptr = old + val;
	release_lock();
	return old;
}

static int32_t emu_fetch_sub_i32(int32_t *ptr, int32_t val) {
	acquire_lock();
	int32_t old = *ptr;
	*ptr = old - val;
	release_lock();
	return old;
}

static int64_t emu_fetch_sub_i64(int64_t *ptr, int64_t val) {
	acquire_lock();
	int64_t old = *ptr;
	*ptr = old - val;
	release_lock();
	return old;
}

static const emulation_atomic_ops_t atomic_ops = {
	emu_cas_i32, emu_cas_i64,
	emu_fetch_add_i32, emu_fetch_add_i64,
	emu_fetch_sub_i32, emu_fetch_sub_i64
};

/* Public API to get emulation operations */
const emulation_fpu_ops_t* emulation_get_fpu_ops(void) { return &fpu_ops; }
const emulation_int_ops_t* emulation_get_int_ops(void) { return &int_ops; }
const emulation_mem_ops_t* emulation_get_mem_ops(void) { return &mem_ops; }
const emulation_simd_ops_t* emulation_get_simd_ops(void) { return &simd_ops; }
const emulation_atomic_ops_t* emulation_get_atomic_ops(void) { return &atomic_ops; }

/***************************************************************************
 * Backend Capability Detection
 ***************************************************************************/

/* Capability map for each backend type */
static uint64_t get_backend_capabilities_by_type(backend_type_t type)
{
	uint64_t caps = 0;

	switch (type) {
	case BACKEND_LLVM:
		/* LLVM supports everything */
		caps = BACKEND_CAP_INT_DIV | BACKEND_CAP_INT_REM |
		       BACKEND_CAP_FP_BASIC | BACKEND_CAP_FP_SQRT | BACKEND_CAP_FP_FMA |
		       BACKEND_CAP_FP_TRIG | BACKEND_CAP_FP_TRANSCEND |
		       BACKEND_CAP_SIMD_INT | BACKEND_CAP_SIMD_FP | BACKEND_CAP_SIMD_SHUFFLE |
		       BACKEND_CAP_MEMCPY | BACKEND_CAP_MEMSET |
		       BACKEND_CAP_ATOMIC_CAS | BACKEND_CAP_ATOMIC_FETCH |
		       BACKEND_CAP_CTZ | BACKEND_CAP_CLZ | BACKEND_CAP_POPCNT |
		       BACKEND_CAP_FLOAT80 | BACKEND_CAP_FLOAT128 | BACKEND_CAP_INT128 |
		       BACKEND_CAP_INLINE_ASM | BACKEND_CAP_EXCEPTION;
		break;

	case BACKEND_QBE:
		/* QBE supports basic operations */
		caps = BACKEND_CAP_INT_DIV | BACKEND_CAP_INT_REM |
		       BACKEND_CAP_FP_BASIC |
		       BACKEND_CAP_MEMCPY | BACKEND_CAP_MEMSET;
		break;

	case BACKEND_GCCJIT:
		/* GCCJIT has good support */
		caps = BACKEND_CAP_INT_DIV | BACKEND_CAP_INT_REM |
		       BACKEND_CAP_FP_BASIC | BACKEND_CAP_FP_SQRT |
		       BACKEND_CAP_FP_TRIG | BACKEND_CAP_FP_TRANSCEND |
		       BACKEND_CAP_MEMCPY | BACKEND_CAP_MEMSET |
		       BACKEND_CAP_ATOMIC_CAS | BACKEND_CAP_ATOMIC_FETCH |
		       BACKEND_CAP_CTZ | BACKEND_CAP_CLZ | BACKEND_CAP_POPCNT |
		       BACKEND_CAP_INLINE_ASM;
		break;

	case BACKEND_TCG:
		/* TCG supports basic integer ops */
		caps = BACKEND_CAP_INT_DIV | BACKEND_CAP_INT_REM |
		       BACKEND_CAP_FP_BASIC;
		break;

	case BACKEND_ASMJIT:
	case BACKEND_DYNASM:
		/* Direct x86-64 code generation - supports hardware ops */
		caps = BACKEND_CAP_INT_DIV | BACKEND_CAP_INT_REM |
		       BACKEND_CAP_FP_BASIC | BACKEND_CAP_FP_SQRT |
		       BACKEND_CAP_SIMD_INT | BACKEND_CAP_SIMD_FP |
		       BACKEND_CAP_CTZ | BACKEND_CAP_CLZ | BACKEND_CAP_POPCNT;
		break;

	case BACKEND_SLJIT:
	case BACKEND_NANOJIT:
		/* Portable JITs - basic ops only */
		caps = BACKEND_CAP_INT_DIV | BACKEND_CAP_INT_REM |
		       BACKEND_CAP_FP_BASIC;
		break;

	case BACKEND_MIR:
		/* MIR has good support */
		caps = BACKEND_CAP_INT_DIV | BACKEND_CAP_INT_REM |
		       BACKEND_CAP_FP_BASIC | BACKEND_CAP_FP_SQRT |
		       BACKEND_CAP_MEMCPY | BACKEND_CAP_MEMSET |
		       BACKEND_CAP_CTZ | BACKEND_CAP_CLZ | BACKEND_CAP_POPCNT;
		break;

	case BACKEND_CRANELIFT:
		/* Cranelift has good support */
		caps = BACKEND_CAP_INT_DIV | BACKEND_CAP_INT_REM |
		       BACKEND_CAP_FP_BASIC | BACKEND_CAP_FP_SQRT |
		       BACKEND_CAP_SIMD_INT | BACKEND_CAP_SIMD_FP |
		       BACKEND_CAP_CTZ | BACKEND_CAP_CLZ | BACKEND_CAP_POPCNT;
		break;

	case BACKEND_LIBJIT:
		/* LibJIT basic support */
		caps = BACKEND_CAP_INT_DIV | BACKEND_CAP_INT_REM |
		       BACKEND_CAP_FP_BASIC | BACKEND_CAP_FP_SQRT;
		break;

	case BACKEND_NJ:
		/* nj basic support */
		caps = BACKEND_CAP_INT_DIV | BACKEND_CAP_INT_REM |
		       BACKEND_CAP_FP_BASIC;
		break;

	default:
		caps = 0;
		break;
	}

	return caps;
}

uint64_t backend_get_capabilities(IBackend *backend)
{
	if (!backend) return 0;
	backend_type_t type = backend->GetType(backend);
	return get_backend_capabilities_by_type(type);
}

int backend_has_capability(IBackend *backend, backend_capability_t cap)
{
	uint64_t caps = backend_get_capabilities(backend);
	return (caps & cap) != 0;
}

/***************************************************************************
 * Emulation Wrapper Layer
 ***************************************************************************/

/* Wrapper structures */
typedef struct EmulatedModule EmulatedModule;
typedef struct EmulatedBuilder EmulatedBuilder;

/* Emulated Backend */
typedef struct {
	IBackend base;
	uint32_t refcount;
	IBackend *wrapped_backend;
	uint64_t capabilities;
} EmulatedBackend;

/* Emulated Module */
struct EmulatedModule {
	IModule base;
	uint32_t refcount;
	IModule *wrapped_module;
	EmulatedBackend *backend;

	/* Injected helper functions for emulation */
	std::map<std::string, IFunction*> helper_functions;
};

/* Emulated Builder */
struct EmulatedBuilder {
	IBuilder base;
	uint32_t refcount;
	IBuilder *wrapped_builder;
	EmulatedModule *module;
};

/* Forward declarations */
static IBuilder* emulated_module_create_builder(IModule *self);

/***************************************************************************
 * Emulated Builder Implementation
 ***************************************************************************/

static uint32_t emulated_builder_addref(void *self) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return ++builder->refcount;
}

static uint32_t emulated_builder_release(void *self) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	uint32_t count = --builder->refcount;
	if (count == 0) {
		if (builder->wrapped_builder) {
			builder->wrapped_builder->base.Release(builder->wrapped_builder);
		}
		free(builder);
	}
	return count;
}

static int emulated_builder_query_interface(void *self, const char *iid, void **out) {
	if (!out) return -1;
	*out = self;
	((IUnknown*)self)->AddRef(self);
	return 0;
}

/* Helper: Check if we need to emulate an FPU operation */
static int need_fpu_emulation(EmulatedBuilder *builder) {
	uint64_t caps = builder->module->backend->capabilities;
	return (caps & BACKEND_CAP_FP_TRIG) == 0 || (caps & BACKEND_CAP_FP_TRANSCEND) == 0;
}

/* Helper: Inject emulation helper function */
static IFunction* inject_helper_function(EmulatedModule *module, const char *name,
                                         void *func_ptr, IType *ret_type,
                                         IType **param_types, uint32_t num_params)
{
	/* Check if already injected */
	auto it = module->helper_functions.find(name);
	if (it != module->helper_functions.end()) {
		return it->second;
	}

	/* Create function type */
	IType *func_type = module->wrapped_module->GetFunctionType(
		module->wrapped_module, ret_type, param_types, num_params, 0);

	/* Create function */
	IFunction *func = module->wrapped_module->CreateFunction(
		module->wrapped_module, name, func_type);

	/* Store helper */
	module->helper_functions[name] = func;

	return func;
}

/* Pass-through operations to wrapped builder */
static void emulated_builder_set_insert_point(IBuilder *self, IBasicBlock *bb) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	builder->wrapped_builder->SetInsertPoint(builder->wrapped_builder, bb);
}

static IBasicBlock* emulated_builder_get_insert_block(IBuilder *self) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->GetInsertBlock(builder->wrapped_builder);
}

/* Constant creation - pass through */
static IValue* emulated_builder_create_const_int(IBuilder *self, IType *type, uint64_t val, int is_signed) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateConstInt(builder->wrapped_builder, type, val, is_signed);
}

static IValue* emulated_builder_create_const_int1(IBuilder *self, int val) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateConstInt1(builder->wrapped_builder, val);
}

static IValue* emulated_builder_create_const_int8(IBuilder *self, uint8_t val) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateConstInt8(builder->wrapped_builder, val);
}

static IValue* emulated_builder_create_const_int16(IBuilder *self, uint16_t val) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateConstInt16(builder->wrapped_builder, val);
}

static IValue* emulated_builder_create_const_int32(IBuilder *self, uint32_t val) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateConstInt32(builder->wrapped_builder, val);
}

static IValue* emulated_builder_create_const_int64(IBuilder *self, uint64_t val) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateConstInt64(builder->wrapped_builder, val);
}

static IValue* emulated_builder_create_const_float(IBuilder *self, float val) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateConstFloat(builder->wrapped_builder, val);
}

static IValue* emulated_builder_create_const_double(IBuilder *self, double val) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateConstDouble(builder->wrapped_builder, val);
}

/* Arithmetic - check for div/rem emulation needs */
static IValue* emulated_builder_create_add(IBuilder *self, IValue *lhs, IValue *rhs, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateAdd(builder->wrapped_builder, lhs, rhs, name);
}

static IValue* emulated_builder_create_sub(IBuilder *self, IValue *lhs, IValue *rhs, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateSub(builder->wrapped_builder, lhs, rhs, name);
}

static IValue* emulated_builder_create_mul(IBuilder *self, IValue *lhs, IValue *rhs, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateMul(builder->wrapped_builder, lhs, rhs, name);
}

static IValue* emulated_builder_create_udiv(IBuilder *self, IValue *lhs, IValue *rhs, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	uint64_t caps = builder->module->backend->capabilities;

	/* Check if backend supports division */
	if (caps & BACKEND_CAP_INT_DIV) {
		return builder->wrapped_builder->CreateUDiv(builder->wrapped_builder, lhs, rhs, name);
	}

	/* Emulate via function call */
	IType *type = lhs->GetType(lhs);
	uint32_t bits = type->GetBitWidth(type);

	if (bits == 32) {
		IType *i32 = builder->module->wrapped_module->GetInt32Type(builder->module->wrapped_module);
		IType *params[2] = {i32, i32};
		IFunction *helper = inject_helper_function(builder->module, "__emu_udiv_i32",
			(void*)emu_udiv_i32, i32, params, 2);
		IValue *args[2] = {lhs, rhs};
		return builder->wrapped_builder->CreateCall(builder->wrapped_builder, helper, args, 2, name);
	} else {
		IType *i64 = builder->module->wrapped_module->GetInt64Type(builder->module->wrapped_module);
		IType *params[2] = {i64, i64};
		IFunction *helper = inject_helper_function(builder->module, "__emu_udiv_i64",
			(void*)emu_udiv_i64, i64, params, 2);
		IValue *args[2] = {lhs, rhs};
		return builder->wrapped_builder->CreateCall(builder->wrapped_builder, helper, args, 2, name);
	}
}

static IValue* emulated_builder_create_sdiv(IBuilder *self, IValue *lhs, IValue *rhs, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	uint64_t caps = builder->module->backend->capabilities;

	if (caps & BACKEND_CAP_INT_DIV) {
		return builder->wrapped_builder->CreateSDiv(builder->wrapped_builder, lhs, rhs, name);
	}

	IType *type = lhs->GetType(lhs);
	uint32_t bits = type->GetBitWidth(type);

	if (bits == 32) {
		IType *i32 = builder->module->wrapped_module->GetInt32Type(builder->module->wrapped_module);
		IType *params[2] = {i32, i32};
		IFunction *helper = inject_helper_function(builder->module, "__emu_sdiv_i32",
			(void*)emu_sdiv_i32, i32, params, 2);
		IValue *args[2] = {lhs, rhs};
		return builder->wrapped_builder->CreateCall(builder->wrapped_builder, helper, args, 2, name);
	} else {
		IType *i64 = builder->module->wrapped_module->GetInt64Type(builder->module->wrapped_module);
		IType *params[2] = {i64, i64};
		IFunction *helper = inject_helper_function(builder->module, "__emu_sdiv_i64",
			(void*)emu_sdiv_i64, i64, params, 2);
		IValue *args[2] = {lhs, rhs};
		return builder->wrapped_builder->CreateCall(builder->wrapped_builder, helper, args, 2, name);
	}
}

static IValue* emulated_builder_create_urem(IBuilder *self, IValue *lhs, IValue *rhs, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	uint64_t caps = builder->module->backend->capabilities;

	if (caps & BACKEND_CAP_INT_REM) {
		return builder->wrapped_builder->CreateURem(builder->wrapped_builder, lhs, rhs, name);
	}

	IType *type = lhs->GetType(lhs);
	uint32_t bits = type->GetBitWidth(type);

	if (bits == 32) {
		IType *i32 = builder->module->wrapped_module->GetInt32Type(builder->module->wrapped_module);
		IType *params[2] = {i32, i32};
		IFunction *helper = inject_helper_function(builder->module, "__emu_urem_i32",
			(void*)emu_urem_i32, i32, params, 2);
		IValue *args[2] = {lhs, rhs};
		return builder->wrapped_builder->CreateCall(builder->wrapped_builder, helper, args, 2, name);
	} else {
		IType *i64 = builder->module->wrapped_module->GetInt64Type(builder->module->wrapped_module);
		IType *params[2] = {i64, i64};
		IFunction *helper = inject_helper_function(builder->module, "__emu_urem_i64",
			(void*)emu_urem_i64, i64, params, 2);
		IValue *args[2] = {lhs, rhs};
		return builder->wrapped_builder->CreateCall(builder->wrapped_builder, helper, args, 2, name);
	}
}

static IValue* emulated_builder_create_srem(IBuilder *self, IValue *lhs, IValue *rhs, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	uint64_t caps = builder->module->backend->capabilities;

	if (caps & BACKEND_CAP_INT_REM) {
		return builder->wrapped_builder->CreateSRem(builder->wrapped_builder, lhs, rhs, name);
	}

	IType *type = lhs->GetType(lhs);
	uint32_t bits = type->GetBitWidth(type);

	if (bits == 32) {
		IType *i32 = builder->module->wrapped_module->GetInt32Type(builder->module->wrapped_module);
		IType *params[2] = {i32, i32};
		IFunction *helper = inject_helper_function(builder->module, "__emu_srem_i32",
			(void*)emu_srem_i32, i32, params, 2);
		IValue *args[2] = {lhs, rhs};
		return builder->wrapped_builder->CreateCall(builder->wrapped_builder, helper, args, 2, name);
	} else {
		IType *i64 = builder->module->wrapped_module->GetInt64Type(builder->module->wrapped_module);
		IType *params[2] = {i64, i64};
		IFunction *helper = inject_helper_function(builder->module, "__emu_srem_i64",
			(void*)emu_srem_i64, i64, params, 2);
		IValue *args[2] = {lhs, rhs};
		return builder->wrapped_builder->CreateCall(builder->wrapped_builder, helper, args, 2, name);
	}
}

static IValue* emulated_builder_create_neg(IBuilder *self, IValue *val, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateNeg(builder->wrapped_builder, val, name);
}

/* Bitwise operations - pass through */
static IValue* emulated_builder_create_and(IBuilder *self, IValue *lhs, IValue *rhs, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateAnd(builder->wrapped_builder, lhs, rhs, name);
}

static IValue* emulated_builder_create_or(IBuilder *self, IValue *lhs, IValue *rhs, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateOr(builder->wrapped_builder, lhs, rhs, name);
}

static IValue* emulated_builder_create_xor(IBuilder *self, IValue *lhs, IValue *rhs, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateXor(builder->wrapped_builder, lhs, rhs, name);
}

static IValue* emulated_builder_create_not(IBuilder *self, IValue *val, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateNot(builder->wrapped_builder, val, name);
}

static IValue* emulated_builder_create_shl(IBuilder *self, IValue *lhs, IValue *rhs, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateShl(builder->wrapped_builder, lhs, rhs, name);
}

static IValue* emulated_builder_create_lshr(IBuilder *self, IValue *lhs, IValue *rhs, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateLShr(builder->wrapped_builder, lhs, rhs, name);
}

static IValue* emulated_builder_create_ashr(IBuilder *self, IValue *lhs, IValue *rhs, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateAShr(builder->wrapped_builder, lhs, rhs, name);
}

/* Float operations - pass through for basic ops */
static IValue* emulated_builder_create_fadd(IBuilder *self, IValue *lhs, IValue *rhs, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateFAdd(builder->wrapped_builder, lhs, rhs, name);
}

static IValue* emulated_builder_create_fsub(IBuilder *self, IValue *lhs, IValue *rhs, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateFSub(builder->wrapped_builder, lhs, rhs, name);
}

static IValue* emulated_builder_create_fmul(IBuilder *self, IValue *lhs, IValue *rhs, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateFMul(builder->wrapped_builder, lhs, rhs, name);
}

static IValue* emulated_builder_create_fdiv(IBuilder *self, IValue *lhs, IValue *rhs, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateFDiv(builder->wrapped_builder, lhs, rhs, name);
}

static IValue* emulated_builder_create_fneg(IBuilder *self, IValue *val, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateFNeg(builder->wrapped_builder, val, name);
}

/* Comparison operations - pass through */
static IValue* emulated_builder_create_icmp(IBuilder *self, icmp_predicate_t pred, IValue *lhs, IValue *rhs, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateICmp(builder->wrapped_builder, pred, lhs, rhs, name);
}

static IValue* emulated_builder_create_fcmp(IBuilder *self, fcmp_predicate_t pred, IValue *lhs, IValue *rhs, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateFCmp(builder->wrapped_builder, pred, lhs, rhs, name);
}

/* Memory operations - pass through */
static IValue* emulated_builder_create_load(IBuilder *self, IType *type, IValue *ptr, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateLoad(builder->wrapped_builder, type, ptr, name);
}

static IValue* emulated_builder_create_store(IBuilder *self, IValue *val, IValue *ptr) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateStore(builder->wrapped_builder, val, ptr);
}

static IValue* emulated_builder_create_gep(IBuilder *self, IType *type, IValue *ptr, IValue **indices, size_t num_indices, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateGEP(builder->wrapped_builder, type, ptr, indices, num_indices, name);
}

static IValue* emulated_builder_create_inbounds_gep(IBuilder *self, IType *type, IValue *ptr, IValue **indices, size_t num_indices, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateInBoundsGEP(builder->wrapped_builder, type, ptr, indices, num_indices, name);
}

/* Cast operations - pass through */
static IValue* emulated_builder_create_trunc(IBuilder *self, IValue *val, IType *dest_type, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateTrunc(builder->wrapped_builder, val, dest_type, name);
}

static IValue* emulated_builder_create_zext(IBuilder *self, IValue *val, IType *dest_type, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateZExt(builder->wrapped_builder, val, dest_type, name);
}

static IValue* emulated_builder_create_sext(IBuilder *self, IValue *val, IType *dest_type, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateSExt(builder->wrapped_builder, val, dest_type, name);
}

static IValue* emulated_builder_create_fptrunc(IBuilder *self, IValue *val, IType *dest_type, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateFPTrunc(builder->wrapped_builder, val, dest_type, name);
}

static IValue* emulated_builder_create_fpext(IBuilder *self, IValue *val, IType *dest_type, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateFPExt(builder->wrapped_builder, val, dest_type, name);
}

static IValue* emulated_builder_create_fptoui(IBuilder *self, IValue *val, IType *dest_type, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateFPToUI(builder->wrapped_builder, val, dest_type, name);
}

static IValue* emulated_builder_create_fptosi(IBuilder *self, IValue *val, IType *dest_type, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateFPToSI(builder->wrapped_builder, val, dest_type, name);
}

static IValue* emulated_builder_create_uitofp(IBuilder *self, IValue *val, IType *dest_type, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateUIToFP(builder->wrapped_builder, val, dest_type, name);
}

static IValue* emulated_builder_create_sitofp(IBuilder *self, IValue *val, IType *dest_type, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateSIToFP(builder->wrapped_builder, val, dest_type, name);
}

static IValue* emulated_builder_create_ptrtoint(IBuilder *self, IValue *val, IType *dest_type, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreatePtrToInt(builder->wrapped_builder, val, dest_type, name);
}

static IValue* emulated_builder_create_inttoptr(IBuilder *self, IValue *val, IType *dest_type, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateIntToPtr(builder->wrapped_builder, val, dest_type, name);
}

static IValue* emulated_builder_create_bitcast(IBuilder *self, IValue *val, IType *dest_type, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateBitCast(builder->wrapped_builder, val, dest_type, name);
}

/* Control flow - pass through */
static IValue* emulated_builder_create_br(IBuilder *self, IBasicBlock *dest) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateBr(builder->wrapped_builder, dest);
}

static IValue* emulated_builder_create_cond_br(IBuilder *self, IValue *cond, IBasicBlock *true_bb, IBasicBlock *false_bb) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateCondBr(builder->wrapped_builder, cond, true_bb, false_bb);
}

static IValue* emulated_builder_create_switch(IBuilder *self, IValue *val, IBasicBlock *default_bb, uint32_t num_cases) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateSwitch(builder->wrapped_builder, val, default_bb, num_cases);
}

static void emulated_builder_add_switch_case(IBuilder *self, IValue *switch_inst, uint64_t case_val, IBasicBlock *dest) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	builder->wrapped_builder->AddSwitchCase(builder->wrapped_builder, switch_inst, case_val, dest);
}

static IValue* emulated_builder_create_ret(IBuilder *self, IValue *val) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateRet(builder->wrapped_builder, val);
}

static IValue* emulated_builder_create_ret_void(IBuilder *self) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateRetVoid(builder->wrapped_builder);
}

/* Function calls - pass through */
static IValue* emulated_builder_create_call(IBuilder *self, IFunction *func, IValue **args, size_t num_args, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateCall(builder->wrapped_builder, func, args, num_args, name);
}

/* PHI nodes - pass through */
static IValue* emulated_builder_create_phi(IBuilder *self, IType *type, uint32_t num_reserved, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreatePHI(builder->wrapped_builder, type, num_reserved, name);
}

static void emulated_builder_add_phi_incoming(IBuilder *self, IValue *phi, IValue *val, IBasicBlock *bb) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	builder->wrapped_builder->AddPHIIncoming(builder->wrapped_builder, phi, val, bb);
}

/* Select - pass through */
static IValue* emulated_builder_create_select(IBuilder *self, IValue *cond, IValue *true_val, IValue *false_val, const char *name) {
	EmulatedBuilder *builder = (EmulatedBuilder*)self;
	return builder->wrapped_builder->CreateSelect(builder->wrapped_builder, cond, true_val, false_val, name);
}

/* Create emulated builder */
static IBuilder* create_emulated_builder(EmulatedModule *module, IBuilder *wrapped_builder)
{
	EmulatedBuilder *builder = (EmulatedBuilder*)calloc(1, sizeof(EmulatedBuilder));
	builder->refcount = 1;
	builder->wrapped_builder = wrapped_builder;
	builder->module = module;

	/* Set up IUnknown */
	builder->base.base.AddRef = emulated_builder_addref;
	builder->base.base.Release = emulated_builder_release;
	builder->base.base.QueryInterface = emulated_builder_query_interface;

	/* Set up IBuilder vtable */
	builder->base.SetInsertPoint = emulated_builder_set_insert_point;
	builder->base.GetInsertBlock = emulated_builder_get_insert_block;

	builder->base.CreateConstInt = emulated_builder_create_const_int;
	builder->base.CreateConstInt1 = emulated_builder_create_const_int1;
	builder->base.CreateConstInt8 = emulated_builder_create_const_int8;
	builder->base.CreateConstInt16 = emulated_builder_create_const_int16;
	builder->base.CreateConstInt32 = emulated_builder_create_const_int32;
	builder->base.CreateConstInt64 = emulated_builder_create_const_int64;
	builder->base.CreateConstFloat = emulated_builder_create_const_float;
	builder->base.CreateConstDouble = emulated_builder_create_const_double;

	builder->base.CreateAdd = emulated_builder_create_add;
	builder->base.CreateSub = emulated_builder_create_sub;
	builder->base.CreateMul = emulated_builder_create_mul;
	builder->base.CreateUDiv = emulated_builder_create_udiv;
	builder->base.CreateSDiv = emulated_builder_create_sdiv;
	builder->base.CreateURem = emulated_builder_create_urem;
	builder->base.CreateSRem = emulated_builder_create_srem;
	builder->base.CreateNeg = emulated_builder_create_neg;

	builder->base.CreateAnd = emulated_builder_create_and;
	builder->base.CreateOr = emulated_builder_create_or;
	builder->base.CreateXor = emulated_builder_create_xor;
	builder->base.CreateNot = emulated_builder_create_not;
	builder->base.CreateShl = emulated_builder_create_shl;
	builder->base.CreateLShr = emulated_builder_create_lshr;
	builder->base.CreateAShr = emulated_builder_create_ashr;

	builder->base.CreateFAdd = emulated_builder_create_fadd;
	builder->base.CreateFSub = emulated_builder_create_fsub;
	builder->base.CreateFMul = emulated_builder_create_fmul;
	builder->base.CreateFDiv = emulated_builder_create_fdiv;
	builder->base.CreateFNeg = emulated_builder_create_fneg;

	builder->base.CreateICmp = emulated_builder_create_icmp;
	builder->base.CreateFCmp = emulated_builder_create_fcmp;

	builder->base.CreateLoad = emulated_builder_create_load;
	builder->base.CreateStore = emulated_builder_create_store;
	builder->base.CreateGEP = emulated_builder_create_gep;
	builder->base.CreateInBoundsGEP = emulated_builder_create_inbounds_gep;

	builder->base.CreateTrunc = emulated_builder_create_trunc;
	builder->base.CreateZExt = emulated_builder_create_zext;
	builder->base.CreateSExt = emulated_builder_create_sext;
	builder->base.CreateFPTrunc = emulated_builder_create_fptrunc;
	builder->base.CreateFPExt = emulated_builder_create_fpext;
	builder->base.CreateFPToUI = emulated_builder_create_fptoui;
	builder->base.CreateFPToSI = emulated_builder_create_fptosi;
	builder->base.CreateUIToFP = emulated_builder_create_uitofp;
	builder->base.CreateSIToFP = emulated_builder_create_sitofp;
	builder->base.CreatePtrToInt = emulated_builder_create_ptrtoint;
	builder->base.CreateIntToPtr = emulated_builder_create_inttoptr;
	builder->base.CreateBitCast = emulated_builder_create_bitcast;

	builder->base.CreateBr = emulated_builder_create_br;
	builder->base.CreateCondBr = emulated_builder_create_cond_br;
	builder->base.CreateSwitch = emulated_builder_create_switch;
	builder->base.AddSwitchCase = emulated_builder_add_switch_case;
	builder->base.CreateRet = emulated_builder_create_ret;
	builder->base.CreateRetVoid = emulated_builder_create_ret_void;

	builder->base.CreateCall = emulated_builder_create_call;

	builder->base.CreatePHI = emulated_builder_create_phi;
	builder->base.AddPHIIncoming = emulated_builder_add_phi_incoming;

	builder->base.CreateSelect = emulated_builder_create_select;

	if (wrapped_builder) {
		wrapped_builder->base.AddRef(wrapped_builder);
	}

	return (IBuilder*)builder;
}

/***************************************************************************
 * Emulated Module Implementation
 ***************************************************************************/

static uint32_t emulated_module_addref(void *self) {
	EmulatedModule *module = (EmulatedModule*)self;
	return ++module->refcount;
}

static uint32_t emulated_module_release(void *self) {
	EmulatedModule *module = (EmulatedModule*)self;
	uint32_t count = --module->refcount;
	if (count == 0) {
		if (module->wrapped_module) {
			module->wrapped_module->base.Release(module->wrapped_module);
		}
		free(module);
	}
	return count;
}

static int emulated_module_query_interface(void *self, const char *iid, void **out) {
	if (!out) return -1;
	*out = self;
	((IUnknown*)self)->AddRef(self);
	return 0;
}

/* Pass-through module operations */
static const char* emulated_module_get_name(IModule *self) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->GetName(module->wrapped_module);
}

static void emulated_module_set_data_layout(IModule *self, const char *layout) {
	EmulatedModule *module = (EmulatedModule*)self;
	module->wrapped_module->SetDataLayout(module->wrapped_module, layout);
}

static const char* emulated_module_get_data_layout(IModule *self) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->GetDataLayout(module->wrapped_module);
}

/* Type creation - pass through */
static IType* emulated_module_get_void_type(IModule *self) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->GetVoidType(module->wrapped_module);
}

static IType* emulated_module_get_int1_type(IModule *self) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->GetInt1Type(module->wrapped_module);
}

static IType* emulated_module_get_int8_type(IModule *self) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->GetInt8Type(module->wrapped_module);
}

static IType* emulated_module_get_int16_type(IModule *self) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->GetInt16Type(module->wrapped_module);
}

static IType* emulated_module_get_int32_type(IModule *self) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->GetInt32Type(module->wrapped_module);
}

static IType* emulated_module_get_int64_type(IModule *self) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->GetInt64Type(module->wrapped_module);
}

static IType* emulated_module_get_int128_type(IModule *self) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->GetInt128Type(module->wrapped_module);
}

static IType* emulated_module_get_int_type(IModule *self, uint32_t num_bits) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->GetIntType(module->wrapped_module, num_bits);
}

static IType* emulated_module_get_float_type(IModule *self) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->GetFloatType(module->wrapped_module);
}

static IType* emulated_module_get_double_type(IModule *self) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->GetDoubleType(module->wrapped_module);
}

static IType* emulated_module_get_fp80_type(IModule *self) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->GetFP80Type(module->wrapped_module);
}

static IType* emulated_module_get_fp128_type(IModule *self) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->GetFP128Type(module->wrapped_module);
}

static IType* emulated_module_get_pointer_type(IModule *self, IType *element_type) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->GetPointerType(module->wrapped_module, element_type);
}

static IType* emulated_module_get_struct_type(IModule *self, IType **element_types, uint32_t num_elements, const char *name) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->GetStructType(module->wrapped_module, element_types, num_elements, name);
}

static IType* emulated_module_get_array_type(IModule *self, IType *element_type, uint32_t num_elements) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->GetArrayType(module->wrapped_module, element_type, num_elements);
}

static IType* emulated_module_get_function_type(IModule *self, IType *return_type, IType **param_types, uint32_t num_params, int is_vararg) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->GetFunctionType(module->wrapped_module, return_type, param_types, num_params, is_vararg);
}

/* Function creation - pass through */
static IFunction* emulated_module_create_function(IModule *self, const char *name, IType *function_type) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->CreateFunction(module->wrapped_module, name, function_type);
}

static IFunction* emulated_module_get_function(IModule *self, const char *name) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->GetFunction(module->wrapped_module, name);
}

/* Global variables - pass through */
static IValue* emulated_module_create_global_variable(IModule *self, IType *type, const char *name, int is_constant) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->CreateGlobalVariable(module->wrapped_module, type, name, is_constant);
}

static IValue* emulated_module_get_global_variable(IModule *self, const char *name) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->GetGlobalVariable(module->wrapped_module, name);
}

/* Builder creation - wrap it */
static IBuilder* emulated_module_create_builder(IModule *self) {
	EmulatedModule *module = (EmulatedModule*)self;
	IBuilder *wrapped_builder = module->wrapped_module->CreateBuilder(module->wrapped_module);
	return create_emulated_builder(module, wrapped_builder);
}

/* Compilation - pass through */
static int emulated_module_compile(IModule *self) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->Compile(module->wrapped_module);
}

static void* emulated_module_get_function_address(IModule *self, const char *name) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->GetFunctionAddress(module->wrapped_module, name);
}

/* Debug - pass through */
static void emulated_module_dump(IModule *self) {
	EmulatedModule *module = (EmulatedModule*)self;
	module->wrapped_module->Dump(module->wrapped_module);
}

static char* emulated_module_to_string(IModule *self) {
	EmulatedModule *module = (EmulatedModule*)self;
	return module->wrapped_module->ToString(module->wrapped_module);
}

/***************************************************************************
 * Emulated Backend Implementation
 ***************************************************************************/

static uint32_t emulated_backend_addref(void *self) {
	EmulatedBackend *backend = (EmulatedBackend*)self;
	return ++backend->refcount;
}

static uint32_t emulated_backend_release(void *self) {
	EmulatedBackend *backend = (EmulatedBackend*)self;
	uint32_t count = --backend->refcount;
	if (count == 0) {
		if (backend->wrapped_backend) {
			backend->wrapped_backend->base.Release(backend->wrapped_backend);
		}
		free(backend);
	}
	return count;
}

static int emulated_backend_query_interface(void *self, const char *iid, void **out) {
	if (!out) return -1;
	*out = self;
	((IUnknown*)self)->AddRef(self);
	return 0;
}

/* Backend information - pass through */
static const char* emulated_backend_get_name(IBackend *self) {
	EmulatedBackend *backend = (EmulatedBackend*)self;
	return backend->wrapped_backend->GetName(backend->wrapped_backend);
}

static const char* emulated_backend_get_version(IBackend *self) {
	EmulatedBackend *backend = (EmulatedBackend*)self;
	return backend->wrapped_backend->GetVersion(backend->wrapped_backend);
}

static backend_type_t emulated_backend_get_type(IBackend *self) {
	EmulatedBackend *backend = (EmulatedBackend*)self;
	return backend->wrapped_backend->GetType(backend->wrapped_backend);
}

static int emulated_backend_initialize(IBackend *self) {
	EmulatedBackend *backend = (EmulatedBackend*)self;
	return backend->wrapped_backend->Initialize(backend->wrapped_backend);
}

static void emulated_backend_shutdown(IBackend *self) {
	EmulatedBackend *backend = (EmulatedBackend*)self;
	backend->wrapped_backend->Shutdown(backend->wrapped_backend);
}

/* Module creation - wrap it */
static IModule* emulated_backend_create_module(IBackend *self, const char *name) {
	EmulatedBackend *backend = (EmulatedBackend*)self;
	IModule *wrapped_module = backend->wrapped_backend->CreateModule(backend->wrapped_backend, name);

	if (!wrapped_module) return NULL;

	/* Create emulated module wrapper */
	EmulatedModule *module = (EmulatedModule*)calloc(1, sizeof(EmulatedModule));
	module->refcount = 1;
	module->wrapped_module = wrapped_module;
	module->backend = backend;

	/* Set up IUnknown */
	module->base.base.AddRef = emulated_module_addref;
	module->base.base.Release = emulated_module_release;
	module->base.base.QueryInterface = emulated_module_query_interface;

	/* Set up IModule vtable */
	module->base.GetName = emulated_module_get_name;
	module->base.SetDataLayout = emulated_module_set_data_layout;
	module->base.GetDataLayout = emulated_module_get_data_layout;

	module->base.GetVoidType = emulated_module_get_void_type;
	module->base.GetInt1Type = emulated_module_get_int1_type;
	module->base.GetInt8Type = emulated_module_get_int8_type;
	module->base.GetInt16Type = emulated_module_get_int16_type;
	module->base.GetInt32Type = emulated_module_get_int32_type;
	module->base.GetInt64Type = emulated_module_get_int64_type;
	module->base.GetInt128Type = emulated_module_get_int128_type;
	module->base.GetIntType = emulated_module_get_int_type;
	module->base.GetFloatType = emulated_module_get_float_type;
	module->base.GetDoubleType = emulated_module_get_double_type;
	module->base.GetFP80Type = emulated_module_get_fp80_type;
	module->base.GetFP128Type = emulated_module_get_fp128_type;
	module->base.GetPointerType = emulated_module_get_pointer_type;
	module->base.GetStructType = emulated_module_get_struct_type;
	module->base.GetArrayType = emulated_module_get_array_type;
	module->base.GetFunctionType = emulated_module_get_function_type;

	module->base.CreateFunction = emulated_module_create_function;
	module->base.GetFunction = emulated_module_get_function;

	module->base.CreateGlobalVariable = emulated_module_create_global_variable;
	module->base.GetGlobalVariable = emulated_module_get_global_variable;

	module->base.CreateBuilder = emulated_module_create_builder;

	module->base.Compile = emulated_module_compile;
	module->base.GetFunctionAddress = emulated_module_get_function_address;

	module->base.Dump = emulated_module_dump;
	module->base.ToString = emulated_module_to_string;

	return (IModule*)module;
}

/* Optimization - pass through */
static void emulated_backend_set_optimization_level(IBackend *self, uint32_t level) {
	EmulatedBackend *backend = (EmulatedBackend*)self;
	backend->wrapped_backend->SetOptimizationLevel(backend->wrapped_backend, level);
}

static uint32_t emulated_backend_get_optimization_level(IBackend *self) {
	EmulatedBackend *backend = (EmulatedBackend*)self;
	return backend->wrapped_backend->GetOptimizationLevel(backend->wrapped_backend);
}

static int emulated_backend_supports_feature(IBackend *self, const char *feature) {
	EmulatedBackend *backend = (EmulatedBackend*)self;
	return backend->wrapped_backend->SupportsFeature(backend->wrapped_backend, feature);
}

/* Target information - pass through */
static const char* emulated_backend_get_target_triple(IBackend *self) {
	EmulatedBackend *backend = (EmulatedBackend*)self;
	return backend->wrapped_backend->GetTargetTriple(backend->wrapped_backend);
}

static const char* emulated_backend_get_data_layout(IBackend *self) {
	EmulatedBackend *backend = (EmulatedBackend*)self;
	return backend->wrapped_backend->GetDataLayout(backend->wrapped_backend);
}

static int emulated_backend_supports_float80(IBackend *self) {
	EmulatedBackend *backend = (EmulatedBackend*)self;
	return backend->wrapped_backend->SupportsFloat80(backend->wrapped_backend);
}

static int emulated_backend_supports_float128(IBackend *self) {
	EmulatedBackend *backend = (EmulatedBackend*)self;
	return backend->wrapped_backend->SupportsFloat128(backend->wrapped_backend);
}

/***************************************************************************
 * Public API - Create emulation-wrapped backend
 ***************************************************************************/

IBackend* backend_create_with_emulation(IBackend *wrapped_backend)
{
	if (!wrapped_backend) return NULL;

	EmulatedBackend *backend = (EmulatedBackend*)calloc(1, sizeof(EmulatedBackend));
	backend->refcount = 1;
	backend->wrapped_backend = wrapped_backend;
	backend->capabilities = backend_get_capabilities(wrapped_backend);

	/* Set up IUnknown */
	backend->base.base.AddRef = emulated_backend_addref;
	backend->base.base.Release = emulated_backend_release;
	backend->base.base.QueryInterface = emulated_backend_query_interface;

	/* Set up IBackend vtable */
	backend->base.GetName = emulated_backend_get_name;
	backend->base.GetVersion = emulated_backend_get_version;
	backend->base.GetType = emulated_backend_get_type;

	backend->base.Initialize = emulated_backend_initialize;
	backend->base.Shutdown = emulated_backend_shutdown;

	backend->base.CreateModule = emulated_backend_create_module;

	backend->base.SetOptimizationLevel = emulated_backend_set_optimization_level;
	backend->base.GetOptimizationLevel = emulated_backend_get_optimization_level;

	backend->base.SupportsFeature = emulated_backend_supports_feature;

	backend->base.GetTargetTriple = emulated_backend_get_target_triple;
	backend->base.GetDataLayout = emulated_backend_get_data_layout;
	backend->base.SupportsFloat80 = emulated_backend_supports_float80;
	backend->base.SupportsFloat128 = emulated_backend_supports_float128;

	wrapped_backend->base.AddRef(wrapped_backend);

	return (IBackend*)backend;
}

IBackend* backend_create_emulated(backend_type_t type)
{
	IBackend *base_backend = backend_create(type);
	if (!base_backend) return NULL;

	IBackend *emulated = backend_create_with_emulation(base_backend);
	base_backend->base.Release(base_backend);

	return emulated;
}

/***************************************************************************
 * Extended Builder Operations - High-level API
 ***************************************************************************/

/* These functions check if we're using an emulated builder and call helpers */

IValue* builder_create_sin(IBuilder *builder, IValue *val, const char *name)
{
	EmulatedBuilder *eb = (EmulatedBuilder*)builder;
	if (!eb->module) {
		/* Not an emulated builder - can't help */
		return NULL;
	}

	IType *type = val->GetType(val);
	value_type_t kind = type->GetKind(type);

	if (kind == VALUE_TYPE_DOUBLE) {
		IType *f64 = eb->module->wrapped_module->GetDoubleType(eb->module->wrapped_module);
		IType *params[1] = {f64};
		IFunction *helper = inject_helper_function(eb->module, "__emu_sin_f64",
			(void*)emu_sin_f64, f64, params, 1);
		IValue *args[1] = {val};
		return builder->CreateCall(builder, helper, args, 1, name);
	} else {
		IType *f32 = eb->module->wrapped_module->GetFloatType(eb->module->wrapped_module);
		IType *params[1] = {f32};
		IFunction *helper = inject_helper_function(eb->module, "__emu_sin_f32",
			(void*)emu_sin_f32, f32, params, 1);
		IValue *args[1] = {val};
		return builder->CreateCall(builder, helper, args, 1, name);
	}
}

IValue* builder_create_cos(IBuilder *builder, IValue *val, const char *name)
{
	EmulatedBuilder *eb = (EmulatedBuilder*)builder;
	if (!eb->module) return NULL;

	IType *type = val->GetType(val);
	value_type_t kind = type->GetKind(type);

	if (kind == VALUE_TYPE_DOUBLE) {
		IType *f64 = eb->module->wrapped_module->GetDoubleType(eb->module->wrapped_module);
		IType *params[1] = {f64};
		IFunction *helper = inject_helper_function(eb->module, "__emu_cos_f64",
			(void*)emu_cos_f64, f64, params, 1);
		IValue *args[1] = {val};
		return builder->CreateCall(builder, helper, args, 1, name);
	} else {
		IType *f32 = eb->module->wrapped_module->GetFloatType(eb->module->wrapped_module);
		IType *params[1] = {f32};
		IFunction *helper = inject_helper_function(eb->module, "__emu_cos_f32",
			(void*)emu_cos_f32, f32, params, 1);
		IValue *args[1] = {val};
		return builder->CreateCall(builder, helper, args, 1, name);
	}
}

IValue* builder_create_sqrt(IBuilder *builder, IValue *val, const char *name)
{
	EmulatedBuilder *eb = (EmulatedBuilder*)builder;
	if (!eb->module) return NULL;

	IType *type = val->GetType(val);
	value_type_t kind = type->GetKind(type);

	if (kind == VALUE_TYPE_DOUBLE) {
		IType *f64 = eb->module->wrapped_module->GetDoubleType(eb->module->wrapped_module);
		IType *params[1] = {f64};
		IFunction *helper = inject_helper_function(eb->module, "__emu_sqrt_f64",
			(void*)emu_sqrt_f64, f64, params, 1);
		IValue *args[1] = {val};
		return builder->CreateCall(builder, helper, args, 1, name);
	} else {
		IType *f32 = eb->module->wrapped_module->GetFloatType(eb->module->wrapped_module);
		IType *params[1] = {f32};
		IFunction *helper = inject_helper_function(eb->module, "__emu_sqrt_f32",
			(void*)emu_sqrt_f32, f32, params, 1);
		IValue *args[1] = {val};
		return builder->CreateCall(builder, helper, args, 1, name);
	}
}

/* Extended FP operations */
IValue* builder_create_tan(IBuilder *builder, IValue *val, const char *name)
{
	EmulatedBuilder *eb = (EmulatedBuilder*)builder;
	if (!eb->module) return NULL;

	IType *type = val->GetType(val);
	value_type_t kind = type->GetKind(type);

	if (kind == VALUE_TYPE_DOUBLE) {
		IType *f64 = eb->module->wrapped_module->GetDoubleType(eb->module->wrapped_module);
		IType *params[1] = {f64};
		IFunction *helper = inject_helper_function(eb->module, "__emu_tan_f64",
			(void*)emu_tan_f64, f64, params, 1);
		IValue *args[1] = {val};
		return builder->CreateCall(builder, helper, args, 1, name);
	} else {
		IType *f32 = eb->module->wrapped_module->GetFloatType(eb->module->wrapped_module);
		IType *params[1] = {f32};
		IFunction *helper = inject_helper_function(eb->module, "__emu_tan_f32",
			(void*)emu_tan_f32, f32, params, 1);
		IValue *args[1] = {val};
		return builder->CreateCall(builder, helper, args, 1, name);
	}
}

IValue* builder_create_exp(IBuilder *builder, IValue *val, const char *name)
{
	EmulatedBuilder *eb = (EmulatedBuilder*)builder;
	if (!eb->module) return NULL;

	IType *type = val->GetType(val);
	value_type_t kind = type->GetKind(type);

	if (kind == VALUE_TYPE_DOUBLE) {
		IType *f64 = eb->module->wrapped_module->GetDoubleType(eb->module->wrapped_module);
		IType *params[1] = {f64};
		IFunction *helper = inject_helper_function(eb->module, "__emu_exp_f64",
			(void*)emu_exp_f64, f64, params, 1);
		IValue *args[1] = {val};
		return builder->CreateCall(builder, helper, args, 1, name);
	} else {
		IType *f32 = eb->module->wrapped_module->GetFloatType(eb->module->wrapped_module);
		IType *params[1] = {f32};
		IFunction *helper = inject_helper_function(eb->module, "__emu_exp_f32",
			(void*)emu_exp_f32, f32, params, 1);
		IValue *args[1] = {val};
		return builder->CreateCall(builder, helper, args, 1, name);
	}
}

IValue* builder_create_log(IBuilder *builder, IValue *val, const char *name)
{
	EmulatedBuilder *eb = (EmulatedBuilder*)builder;
	if (!eb->module) return NULL;

	IType *type = val->GetType(val);
	value_type_t kind = type->GetKind(type);

	if (kind == VALUE_TYPE_DOUBLE) {
		IType *f64 = eb->module->wrapped_module->GetDoubleType(eb->module->wrapped_module);
		IType *params[1] = {f64};
		IFunction *helper = inject_helper_function(eb->module, "__emu_log_f64",
			(void*)emu_log_f64, f64, params, 1);
		IValue *args[1] = {val};
		return builder->CreateCall(builder, helper, args, 1, name);
	} else {
		IType *f32 = eb->module->wrapped_module->GetFloatType(eb->module->wrapped_module);
		IType *params[1] = {f32};
		IFunction *helper = inject_helper_function(eb->module, "__emu_log_f32",
			(void*)emu_log_f32, f32, params, 1);
		IValue *args[1] = {val};
		return builder->CreateCall(builder, helper, args, 1, name);
	}
}

IValue* builder_create_pow(IBuilder *builder, IValue *base, IValue *exp, const char *name)
{
	EmulatedBuilder *eb = (EmulatedBuilder*)builder;
	if (!eb->module) return NULL;

	IType *type = base->GetType(base);
	value_type_t kind = type->GetKind(type);

	if (kind == VALUE_TYPE_DOUBLE) {
		IType *f64 = eb->module->wrapped_module->GetDoubleType(eb->module->wrapped_module);
		IType *params[2] = {f64, f64};
		IFunction *helper = inject_helper_function(eb->module, "__emu_pow_f64",
			(void*)emu_pow_f64, f64, params, 2);
		IValue *args[2] = {base, exp};
		return builder->CreateCall(builder, helper, args, 2, name);
	} else {
		IType *f32 = eb->module->wrapped_module->GetFloatType(eb->module->wrapped_module);
		IType *params[2] = {f32, f32};
		IFunction *helper = inject_helper_function(eb->module, "__emu_pow_f32",
			(void*)emu_pow_f32, f32, params, 2);
		IValue *args[2] = {base, exp};
		return builder->CreateCall(builder, helper, args, 2, name);
	}
}

IValue* builder_create_floor(IBuilder *builder, IValue *val, const char *name)
{
	EmulatedBuilder *eb = (EmulatedBuilder*)builder;
	if (!eb->module) return NULL;

	IType *type = val->GetType(val);
	value_type_t kind = type->GetKind(type);

	if (kind == VALUE_TYPE_DOUBLE) {
		IType *f64 = eb->module->wrapped_module->GetDoubleType(eb->module->wrapped_module);
		IType *params[1] = {f64};
		IFunction *helper = inject_helper_function(eb->module, "__emu_floor_f64",
			(void*)emu_floor_f64, f64, params, 1);
		IValue *args[1] = {val};
		return builder->CreateCall(builder, helper, args, 1, name);
	} else {
		IType *f32 = eb->module->wrapped_module->GetFloatType(eb->module->wrapped_module);
		IType *params[1] = {f32};
		IFunction *helper = inject_helper_function(eb->module, "__emu_floor_f32",
			(void*)emu_floor_f32, f32, params, 1);
		IValue *args[1] = {val};
		return builder->CreateCall(builder, helper, args, 1, name);
	}
}

IValue* builder_create_ceil(IBuilder *builder, IValue *val, const char *name)
{
	EmulatedBuilder *eb = (EmulatedBuilder*)builder;
	if (!eb->module) return NULL;

	IType *type = val->GetType(val);
	value_type_t kind = type->GetKind(type);

	if (kind == VALUE_TYPE_DOUBLE) {
		IType *f64 = eb->module->wrapped_module->GetDoubleType(eb->module->wrapped_module);
		IType *params[1] = {f64};
		IFunction *helper = inject_helper_function(eb->module, "__emu_ceil_f64",
			(void*)emu_ceil_f64, f64, params, 1);
		IValue *args[1] = {val};
		return builder->CreateCall(builder, helper, args, 1, name);
	} else {
		IType *f32 = eb->module->wrapped_module->GetFloatType(eb->module->wrapped_module);
		IType *params[1] = {f32};
		IFunction *helper = inject_helper_function(eb->module, "__emu_ceil_f32",
			(void*)emu_ceil_f32, f32, params, 1);
		IValue *args[1] = {val};
		return builder->CreateCall(builder, helper, args, 1, name);
	}
}

/* Bit manipulation operations */
IValue* builder_create_clz(IBuilder *builder, IValue *val, const char *name)
{
	EmulatedBuilder *eb = (EmulatedBuilder*)builder;
	if (!eb->module) return NULL;

	IType *type = val->GetType(val);
	uint32_t bits = type->GetBitWidth(type);

	if (bits == 32) {
		IType *i32 = eb->module->wrapped_module->GetInt32Type(eb->module->wrapped_module);
		IType *params[1] = {i32};
		IFunction *helper = inject_helper_function(eb->module, "__emu_clz_i32",
			(void*)emu_clz_i32, i32, params, 1);
		IValue *args[1] = {val};
		return builder->CreateCall(builder, helper, args, 1, name);
	} else {
		IType *i64 = eb->module->wrapped_module->GetInt64Type(eb->module->wrapped_module);
		IType *params[1] = {i64};
		IFunction *helper = inject_helper_function(eb->module, "__emu_clz_i64",
			(void*)emu_clz_i64, i64, params, 1);
		IValue *args[1] = {val};
		return builder->CreateCall(builder, helper, args, 1, name);
	}
}

IValue* builder_create_ctz(IBuilder *builder, IValue *val, const char *name)
{
	EmulatedBuilder *eb = (EmulatedBuilder*)builder;
	if (!eb->module) return NULL;

	IType *type = val->GetType(val);
	uint32_t bits = type->GetBitWidth(type);

	if (bits == 32) {
		IType *i32 = eb->module->wrapped_module->GetInt32Type(eb->module->wrapped_module);
		IType *params[1] = {i32};
		IFunction *helper = inject_helper_function(eb->module, "__emu_ctz_i32",
			(void*)emu_ctz_i32, i32, params, 1);
		IValue *args[1] = {val};
		return builder->CreateCall(builder, helper, args, 1, name);
	} else {
		IType *i64 = eb->module->wrapped_module->GetInt64Type(eb->module->wrapped_module);
		IType *params[1] = {i64};
		IFunction *helper = inject_helper_function(eb->module, "__emu_ctz_i64",
			(void*)emu_ctz_i64, i64, params, 1);
		IValue *args[1] = {val};
		return builder->CreateCall(builder, helper, args, 1, name);
	}
}

IValue* builder_create_popcnt(IBuilder *builder, IValue *val, const char *name)
{
	EmulatedBuilder *eb = (EmulatedBuilder*)builder;
	if (!eb->module) return NULL;

	IType *type = val->GetType(val);
	uint32_t bits = type->GetBitWidth(type);

	if (bits == 32) {
		IType *i32 = eb->module->wrapped_module->GetInt32Type(eb->module->wrapped_module);
		IType *params[1] = {i32};
		IFunction *helper = inject_helper_function(eb->module, "__emu_popcnt_i32",
			(void*)emu_popcnt_i32, i32, params, 1);
		IValue *args[1] = {val};
		return builder->CreateCall(builder, helper, args, 1, name);
	} else {
		IType *i64 = eb->module->wrapped_module->GetInt64Type(eb->module->wrapped_module);
		IType *params[1] = {i64};
		IFunction *helper = inject_helper_function(eb->module, "__emu_popcnt_i64",
			(void*)emu_popcnt_i64, i64, params, 1);
		IValue *args[1] = {val};
		return builder->CreateCall(builder, helper, args, 1, name);
	}
}

/* Memory operations */
void builder_create_memcpy(IBuilder *builder, IValue *dst, IValue *src, IValue *size)
{
	EmulatedBuilder *eb = (EmulatedBuilder*)builder;
	if (!eb->module) return;

	IType *void_type = eb->module->wrapped_module->GetVoidType(eb->module->wrapped_module);
	IType *ptr_type = eb->module->wrapped_module->GetPointerType(eb->module->wrapped_module,
		eb->module->wrapped_module->GetInt8Type(eb->module->wrapped_module));
	IType *size_type = size->GetType(size);  /* Use size's type (i32 or i64) */

	IType *params[3] = {ptr_type, ptr_type, size_type};
	IFunction *helper = inject_helper_function(eb->module, "__emu_memcpy",
		(void*)emu_memcpy, void_type, params, 3);

	IValue *args[3] = {dst, src, size};
	builder->CreateCall(builder, helper, args, 3, "");
}

void builder_create_memset(IBuilder *builder, IValue *dst, IValue *val, IValue *size)
{
	EmulatedBuilder *eb = (EmulatedBuilder*)builder;
	if (!eb->module) return;

	IType *void_type = eb->module->wrapped_module->GetVoidType(eb->module->wrapped_module);
	IType *ptr_type = eb->module->wrapped_module->GetPointerType(eb->module->wrapped_module,
		eb->module->wrapped_module->GetInt8Type(eb->module->wrapped_module));
	IType *i32 = eb->module->wrapped_module->GetInt32Type(eb->module->wrapped_module);
	IType *size_type = size->GetType(size);  /* Use size's type (i32 or i64) */

	IType *params[3] = {ptr_type, i32, size_type};
	IFunction *helper = inject_helper_function(eb->module, "__emu_memset",
		(void*)emu_memset, void_type, params, 3);

	IValue *args[3] = {dst, val, size};
	builder->CreateCall(builder, helper, args, 3, "");
}

/* Atomic operations */
IValue* builder_create_atomic_cas(IBuilder *builder, IValue *ptr, IValue *expected, IValue *desired, const char *name)
{
	EmulatedBuilder *eb = (EmulatedBuilder*)builder;
	if (!eb->module) return NULL;

	/* Determine the pointee type size */
	IType *ptr_type = ptr->GetType(ptr);
	IType *elem_type = ptr_type->GetElementType(ptr_type);
	uint32_t bits = elem_type->GetBitWidth(elem_type);

	if (bits == 32) {
		IType *i32 = eb->module->wrapped_module->GetInt32Type(eb->module->wrapped_module);
		IType *ptr_i32 = eb->module->wrapped_module->GetPointerType(eb->module->wrapped_module, i32);
		IType *params[3] = {ptr_i32, i32, i32};
		IFunction *helper = inject_helper_function(eb->module, "__emu_cas_i32",
			(void*)emu_cas_i32, i32, params, 3);
		IValue *args[3] = {ptr, expected, desired};
		return builder->CreateCall(builder, helper, args, 3, name);
	} else {
		IType *i64 = eb->module->wrapped_module->GetInt64Type(eb->module->wrapped_module);
		IType *ptr_i64 = eb->module->wrapped_module->GetPointerType(eb->module->wrapped_module, i64);
		IType *params[3] = {ptr_i64, i64, i64};
		IFunction *helper = inject_helper_function(eb->module, "__emu_cas_i64",
			(void*)emu_cas_i64, i64, params, 3);
		IValue *args[3] = {ptr, expected, desired};
		return builder->CreateCall(builder, helper, args, 3, name);
	}
}

IValue* builder_create_atomic_fetch_add(IBuilder *builder, IValue *ptr, IValue *val, const char *name)
{
	EmulatedBuilder *eb = (EmulatedBuilder*)builder;
	if (!eb->module) return NULL;

	/* Determine the pointee type size */
	IType *ptr_type = ptr->GetType(ptr);
	IType *elem_type = ptr_type->GetElementType(ptr_type);
	uint32_t bits = elem_type->GetBitWidth(elem_type);

	if (bits == 32) {
		IType *i32 = eb->module->wrapped_module->GetInt32Type(eb->module->wrapped_module);
		IType *ptr_i32 = eb->module->wrapped_module->GetPointerType(eb->module->wrapped_module, i32);
		IType *params[2] = {ptr_i32, i32};
		IFunction *helper = inject_helper_function(eb->module, "__emu_fetch_add_i32",
			(void*)emu_fetch_add_i32, i32, params, 2);
		IValue *args[2] = {ptr, val};
		return builder->CreateCall(builder, helper, args, 2, name);
	} else {
		IType *i64 = eb->module->wrapped_module->GetInt64Type(eb->module->wrapped_module);
		IType *ptr_i64 = eb->module->wrapped_module->GetPointerType(eb->module->wrapped_module, i64);
		IType *params[2] = {ptr_i64, i64};
		IFunction *helper = inject_helper_function(eb->module, "__emu_fetch_add_i64",
			(void*)emu_fetch_add_i64, i64, params, 2);
		IValue *args[2] = {ptr, val};
		return builder->CreateCall(builder, helper, args, 2, name);
	}
}
