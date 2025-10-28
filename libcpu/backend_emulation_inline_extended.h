/*
 * libcpu Extended Inline Emulation Layer
 *
 * Provides additional inline operations beyond standard IBuilder interface:
 * - Advanced math functions (sin, cos, exp, log, pow, etc.)
 * - Bit manipulation (CLZ, CTZ, POPCNT)
 * - Atomic operations
 * - SIMD operations
 *
 * All operations generate inline JIT code for maximum performance.
 */

#ifndef BACKEND_EMULATION_INLINE_EXTENDED_H
#define BACKEND_EMULATION_INLINE_EXTENDED_H

#include "backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************
 * Extended Builder Interface
 ***************************************************************************/

typedef struct IBuilderExtended IBuilderExtended;

struct IBuilderExtended {
	IBuilder base;  /* Standard IBuilder interface */

	/* Extended math operations - generate inline approximations */
	IValue* (*CreateSqrt)(IBuilderExtended *self, IValue *x, const char *name);
	IValue* (*CreateSin)(IBuilderExtended *self, IValue *x, const char *name);
	IValue* (*CreateCos)(IBuilderExtended *self, IValue *x, const char *name);
	IValue* (*CreateTan)(IBuilderExtended *self, IValue *x, const char *name);
	IValue* (*CreateExp)(IBuilderExtended *self, IValue *x, const char *name);
	IValue* (*CreateLog)(IBuilderExtended *self, IValue *x, const char *name);
	IValue* (*CreateLog2)(IBuilderExtended *self, IValue *x, const char *name);
	IValue* (*CreateLog10)(IBuilderExtended *self, IValue *x, const char *name);
	IValue* (*CreatePow)(IBuilderExtended *self, IValue *x, IValue *y, const char *name);

	/* Bit manipulation operations - generate inline code */
	IValue* (*CreateCLZ)(IBuilderExtended *self, IValue *x, const char *name);  /* Count leading zeros */
	IValue* (*CreateCTZ)(IBuilderExtended *self, IValue *x, const char *name);  /* Count trailing zeros */
	IValue* (*CreatePOPCNT)(IBuilderExtended *self, IValue *x, const char *name);  /* Population count (count 1 bits) */
	IValue* (*CreateBSWAP)(IBuilderExtended *self, IValue *x, const char *name);  /* Byte swap */
	IValue* (*CreateBREV)(IBuilderExtended *self, IValue *x, const char *name);  /* Bit reverse */

	/* Min/Max operations - generate inline selects */
	IValue* (*CreateMin)(IBuilderExtended *self, IValue *a, IValue *b, int is_signed, const char *name);
	IValue* (*CreateMax)(IBuilderExtended *self, IValue *a, IValue *b, int is_signed, const char *name);
	IValue* (*CreateFMin)(IBuilderExtended *self, IValue *a, IValue *b, const char *name);
	IValue* (*CreateFMax)(IBuilderExtended *self, IValue *a, IValue *b, const char *name);

	/* Saturating arithmetic - generate inline clamping */
	IValue* (*CreateSAddSat)(IBuilderExtended *self, IValue *a, IValue *b, const char *name);  /* Saturating add */
	IValue* (*CreateUAddSat)(IBuilderExtended *self, IValue *a, IValue *b, const char *name);
	IValue* (*CreateSSubSat)(IBuilderExtended *self, IValue *a, IValue *b, const char *name);  /* Saturating sub */
	IValue* (*CreateUSubSat)(IBuilderExtended *self, IValue *a, IValue *b, const char *name);

	/* Overflow detection - generate inline checks */
	IValue* (*CreateAddOverflow)(IBuilderExtended *self, IValue *a, IValue *b, IValue **overflow, const char *name);
	IValue* (*CreateSubOverflow)(IBuilderExtended *self, IValue *a, IValue *b, IValue **overflow, const char *name);
	IValue* (*CreateMulOverflow)(IBuilderExtended *self, IValue *a, IValue *b, IValue **overflow, const char *name);

	/* Fused multiply-add - generate inline a*b+c */
	IValue* (*CreateFMA)(IBuilderExtended *self, IValue *a, IValue *b, IValue *c, const char *name);

	/* Absolute value */
	IValue* (*CreateAbs)(IBuilderExtended *self, IValue *x, const char *name);
	IValue* (*CreateFAbs)(IBuilderExtended *self, IValue *x, const char *name);

	/* Sign operations */
	IValue* (*CreateCopySign)(IBuilderExtended *self, IValue *mag, IValue *sign, const char *name);
};

/**
 * Create extended inline emulation builder with additional operations.
 *
 * This returns an IBuilderExtended* which can be used as a regular IBuilder*,
 * plus provides additional extended operations.
 *
 * @param wrapped_builder The backend builder to wrap
 * @param wrapped_module The backend module
 * @return Extended builder with inline code generation for all operations
 */
IBuilderExtended* emulation_create_inline_builder_extended(IBuilder *wrapped_builder, IModule *wrapped_module);

/***************************************************************************
 * Capability Flags
 ***************************************************************************/

/* Backend capability detection */
typedef enum {
	EMU_CAP_NATIVE_DIV       = (1 << 0),   /* Backend has native div/rem */
	EMU_CAP_NATIVE_SQRT      = (1 << 1),   /* Backend has native sqrt */
	EMU_CAP_NATIVE_FMA       = (1 << 2),   /* Backend has native FMA */
	EMU_CAP_NATIVE_MINMAX    = (1 << 3),   /* Backend has native min/max */
	EMU_CAP_NATIVE_POPCNT    = (1 << 4),   /* Backend has native popcnt */
	EMU_CAP_NATIVE_CLZ       = (1 << 5),   /* Backend has native clz */
	EMU_CAP_NATIVE_CTZ       = (1 << 6),   /* Backend has native ctz */
	EMU_CAP_NATIVE_BSWAP     = (1 << 7),   /* Backend has native bswap */
	EMU_CAP_NATIVE_ABS       = (1 << 8),   /* Backend has native abs */
	EMU_CAP_NATIVE_SIN       = (1 << 9),   /* Backend has native sin */
	EMU_CAP_NATIVE_COS       = (1 << 10),  /* Backend has native cos */
	EMU_CAP_NATIVE_EXP       = (1 << 11),  /* Backend has native exp */
	EMU_CAP_NATIVE_LOG       = (1 << 12),  /* Backend has native log */
	EMU_CAP_NATIVE_POW       = (1 << 13),  /* Backend has native pow */
} emulation_capability_flags_t;

/**
 * Detect backend capabilities to choose between native and inline implementations.
 *
 * @param backend The backend to query
 * @return Bitmask of EMU_CAP_* flags
 */
uint32_t emulation_detect_backend_capabilities(IBackend *backend);

/***************************************************************************
 * Inline Code Generation Strategies
 ***************************************************************************/

/* Strategy selection for different operations */
typedef enum {
	INLINE_STRATEGY_AUTO,       /* Choose best based on backend capabilities */
	INLINE_STRATEGY_NATIVE,     /* Use backend native instruction if available */
	INLINE_STRATEGY_APPROX,     /* Use fast approximation (lower accuracy) */
	INLINE_STRATEGY_PRECISE,    /* Use precise inline code (higher accuracy) */
	INLINE_STRATEGY_CALL,       /* Fall back to helper function call */
} inline_strategy_t;

/**
 * Configure inline generation strategy for extended builder.
 *
 * @param builder The extended builder
 * @param operation The operation name (e.g., "sqrt", "sin", "clz")
 * @param strategy The strategy to use
 */
void emulation_set_inline_strategy(IBuilderExtended *builder, const char *operation, inline_strategy_t strategy);

/***************************************************************************
 * Performance Tuning
 ***************************************************************************/

/* Accuracy vs performance trade-off for math operations */
typedef enum {
	INLINE_ACCURACY_FAST,      /* Fast approximation (1-2 ULP error) */
	INLINE_ACCURACY_STANDARD,  /* Standard accuracy (0.5 ULP error) */
	INLINE_ACCURACY_PRECISE,   /* High precision (0.1 ULP error) */
} inline_accuracy_t;

/**
 * Set accuracy level for inline math operations.
 *
 * Higher accuracy generates more instructions but produces better results.
 *
 * @param builder The extended builder
 * @param accuracy The accuracy level
 */
void emulation_set_accuracy(IBuilderExtended *builder, inline_accuracy_t accuracy);

/**
 * Enable/disable range reduction for transcendental functions.
 *
 * Range reduction improves accuracy for large inputs but adds overhead.
 *
 * @param builder The extended builder
 * @param enable 1 to enable, 0 to disable
 */
void emulation_enable_range_reduction(IBuilderExtended *builder, int enable);

/***************************************************************************
 * Statistics and Profiling
 ***************************************************************************/

typedef struct {
	uint64_t total_operations;
	uint64_t native_operations;      /* Used backend native instruction */
	uint64_t inline_operations;      /* Generated inline code */
	uint64_t fallback_operations;    /* Fell back to helper call */
	uint64_t instructions_generated; /* Total instructions emitted */
	uint64_t instructions_saved;     /* Instructions saved vs helpers */
} emulation_stats_t;

/**
 * Get statistics about inline code generation.
 *
 * @param builder The extended builder
 * @return Statistics structure
 */
emulation_stats_t emulation_get_stats(IBuilderExtended *builder);

/**
 * Reset statistics counters.
 *
 * @param builder The extended builder
 */
void emulation_reset_stats(IBuilderExtended *builder);

/**
 * Print statistics report.
 *
 * @param builder The extended builder
 */
void emulation_print_stats(IBuilderExtended *builder);

#ifdef __cplusplus
}
#endif

#endif /* BACKEND_EMULATION_INLINE_EXTENDED_H */
