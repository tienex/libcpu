/*
 * libcpu Backend Emulation - Comprehensive Builtin Enumeration
 *
 * Covers all builtins from LLVM, GCC, and MSVC.
 * Organized by SEMANTIC operation, not by ISA.
 * Maps intrinsics from x86 (SSE-AVX512), ARM (NEON, SVE, SME),
 * RISC-V (RVV), PowerPC (VMX, VSX), MIPS/Loongson, etc.
 */

#ifndef __LIBCPU_BACKEND_EMULATION_BUILTINS_H__
#define __LIBCPU_BACKEND_EMULATION_BUILTINS_H__

#include "backend_emulation_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************
 * Builtin Operation Categories
 ***************************************************************************/

typedef enum {
	/* ===== Arithmetic Operations ===== */
	BUILTIN_VEC_ADD = 1000,              /* Vector addition */
	BUILTIN_VEC_SUB,                     /* Vector subtraction */
	BUILTIN_VEC_MUL,                     /* Vector multiplication */
	BUILTIN_VEC_DIV,                     /* Vector division */
	BUILTIN_VEC_ABS,                     /* Vector absolute value */
	BUILTIN_VEC_NEG,                     /* Vector negation */
	BUILTIN_VEC_MIN,                     /* Element-wise minimum */
	BUILTIN_VEC_MAX,                     /* Element-wise maximum */
	BUILTIN_VEC_ADDS_SAT,                /* Saturating add (signed) */
	BUILTIN_VEC_ADDU_SAT,                /* Saturating add (unsigned) */
	BUILTIN_VEC_SUBS_SAT,                /* Saturating sub (signed) */
	BUILTIN_VEC_SUBU_SAT,                /* Saturating sub (unsigned) */
	BUILTIN_VEC_MULHI,                   /* Multiply high */
	BUILTIN_VEC_MULLO,                   /* Multiply low */
	BUILTIN_VEC_MADD,                    /* Multiply-add */
	BUILTIN_VEC_MSUB,                    /* Multiply-subtract */
	BUILTIN_VEC_AVG,                     /* Average (rounded) */

	/* ===== Fused Multiply-Add ===== */
	BUILTIN_VEC_FMA = 1100,              /* a * b + c */
	BUILTIN_VEC_FMS,                     /* a * b - c */
	BUILTIN_VEC_FNMA,                    /* -(a * b) + c */
	BUILTIN_VEC_FNMS,                    /* -(a * b) - c */
	BUILTIN_VEC_FMADD,                   /* Fused multiply-add */
	BUILTIN_VEC_FMSUB,                   /* Fused multiply-sub */

	/* ===== Horizontal Operations ===== */
	BUILTIN_VEC_HADD = 1200,             /* Horizontal add */
	BUILTIN_VEC_HSUB,                    /* Horizontal subtract */
	BUILTIN_VEC_HADDS,                   /* Horizontal add saturated */
	BUILTIN_VEC_HSUBS,                   /* Horizontal subtract saturated */

	/* ===== Reduction Operations ===== */
	BUILTIN_VEC_REDUCE_ADD = 1300,       /* Sum all elements */
	BUILTIN_VEC_REDUCE_MUL,              /* Multiply all elements */
	BUILTIN_VEC_REDUCE_MIN,              /* Minimum of all elements */
	BUILTIN_VEC_REDUCE_MAX,              /* Maximum of all elements */
	BUILTIN_VEC_REDUCE_AND,              /* Bitwise AND all elements */
	BUILTIN_VEC_REDUCE_OR,               /* Bitwise OR all elements */
	BUILTIN_VEC_REDUCE_XOR,              /* Bitwise XOR all elements */

	/* ===== Dot Products ===== */
	BUILTIN_VEC_DOT = 1400,              /* Dot product (2 vectors) */
	BUILTIN_VEC_DOT4,                    /* 4-element dot product */
	BUILTIN_VEC_DP,                      /* Configurable dot product */
	BUILTIN_VEC_DPPS,                    /* Dot product single precision */
	BUILTIN_VEC_DPPD,                    /* Dot product double precision */

	/* ===== Bitwise Operations ===== */
	BUILTIN_VEC_AND = 1500,              /* Bitwise AND */
	BUILTIN_VEC_OR,                      /* Bitwise OR */
	BUILTIN_VEC_XOR,                     /* Bitwise XOR */
	BUILTIN_VEC_ANDN,                    /* AND-NOT */
	BUILTIN_VEC_NOT,                     /* Bitwise NOT */
	BUILTIN_VEC_SLL,                     /* Shift left logical */
	BUILTIN_VEC_SRL,                     /* Shift right logical */
	BUILTIN_VEC_SRA,                     /* Shift right arithmetic */
	BUILTIN_VEC_ROL,                     /* Rotate left */
	BUILTIN_VEC_ROR,                     /* Rotate right */

	/* ===== Comparison Operations ===== */
	BUILTIN_VEC_CMPEQ = 1600,            /* Compare equal */
	BUILTIN_VEC_CMPNE,                   /* Compare not equal */
	BUILTIN_VEC_CMPLT,                   /* Compare less than */
	BUILTIN_VEC_CMPLE,                   /* Compare less or equal */
	BUILTIN_VEC_CMPGT,                   /* Compare greater than */
	BUILTIN_VEC_CMPGE,                   /* Compare greater or equal */
	BUILTIN_VEC_CMPORD,                  /* Compare ordered (FP) */
	BUILTIN_VEC_CMPUNORD,                /* Compare unordered (FP) */

	/* ===== Conversion Operations ===== */
	BUILTIN_VEC_CVTI2F = 1700,           /* Convert int to float */
	BUILTIN_VEC_CVTF2I,                  /* Convert float to int */
	BUILTIN_VEC_CVTF2I_TRUNC,            /* Convert float to int (truncate) */
	BUILTIN_VEC_CVTF2I_ROUND,            /* Convert float to int (round) */
	BUILTIN_VEC_CVTF2I_FLOOR,            /* Convert float to int (floor) */
	BUILTIN_VEC_CVTF2I_CEIL,             /* Convert float to int (ceil) */
	BUILTIN_VEC_CVTPS2PD,                /* Convert f32 to f64 */
	BUILTIN_VEC_CVTPD2PS,                /* Convert f64 to f32 */
	BUILTIN_VEC_CVTPH2PS,                /* Convert fp16 to f32 */
	BUILTIN_VEC_CVTPS2PH,                /* Convert f32 to fp16 */
	BUILTIN_VEC_CVTBF16_F32,             /* Convert bf16 to f32 */
	BUILTIN_VEC_CVTF32_BF16,             /* Convert f32 to bf16 */
	BUILTIN_VEC_CVTFP8_F32,              /* Convert fp8 to f32 */
	BUILTIN_VEC_CVTF32_FP8,              /* Convert f32 to fp8 */

	/* ===== Pack/Unpack Operations ===== */
	BUILTIN_VEC_PACK = 1800,             /* Pack two vectors (narrow) */
	BUILTIN_VEC_PACKUS,                  /* Pack unsigned saturate */
	BUILTIN_VEC_PACKSS,                  /* Pack signed saturate */
	BUILTIN_VEC_UNPACKLO,                /* Unpack low half */
	BUILTIN_VEC_UNPACKHI,                /* Unpack high half */
	BUILTIN_VEC_EXTEND_LOW,              /* Sign/zero extend low */
	BUILTIN_VEC_EXTEND_HIGH,             /* Sign/zero extend high */

	/* ===== Shuffle/Permute Operations ===== */
	BUILTIN_VEC_SHUFFLE = 1900,          /* General shuffle */
	BUILTIN_VEC_PERMUTE,                 /* General permute */
	BUILTIN_VEC_BLEND,                   /* Blend two vectors */
	BUILTIN_VEC_SELECT,                  /* Select with mask */
	BUILTIN_VEC_SWIZZLE,                 /* Element swizzle */
	BUILTIN_VEC_EXTRACT,                 /* Extract element */
	BUILTIN_VEC_INSERT,                  /* Insert element */
	BUILTIN_VEC_BROADCAST,               /* Broadcast element */
	BUILTIN_VEC_SPLAT,                   /* Splat scalar to all lanes */
	BUILTIN_VEC_PSHUFB,                  /* Byte shuffle */
	BUILTIN_VEC_SHUFPS,                  /* Shuffle packed singles */
	BUILTIN_VEC_SHUFPD,                  /* Shuffle packed doubles */
	BUILTIN_VEC_ALIGNR,                  /* Concatenate and shift */
	BUILTIN_VEC_PALIGNR,                 /* Packed align right */

	/* ===== Load/Store Operations ===== */
	BUILTIN_VEC_LOAD = 2000,             /* Aligned load */
	BUILTIN_VEC_LOADU,                   /* Unaligned load */
	BUILTIN_VEC_STORE,                   /* Aligned store */
	BUILTIN_VEC_STOREU,                  /* Unaligned store */
	BUILTIN_VEC_LOAD_SPLAT,              /* Load and splat */
	BUILTIN_VEC_LOAD_ZERO,               /* Load with zero extend */
	BUILTIN_VEC_STREAM_LOAD,             /* Non-temporal load */
	BUILTIN_VEC_STREAM_STORE,            /* Non-temporal store */
	BUILTIN_VEC_GATHER,                  /* Gather with indices */
	BUILTIN_VEC_SCATTER,                 /* Scatter with indices */
	BUILTIN_VEC_MASKED_LOAD,             /* Predicated load */
	BUILTIN_VEC_MASKED_STORE,            /* Predicated store */

	/* ===== FP Math Operations ===== */
	BUILTIN_VEC_SQRT = 2100,             /* Square root */
	BUILTIN_VEC_RSQRT,                   /* Reciprocal square root */
	BUILTIN_VEC_RCP,                     /* Reciprocal */
	BUILTIN_VEC_CEIL,                    /* Ceiling */
	BUILTIN_VEC_FLOOR,                   /* Floor */
	BUILTIN_VEC_TRUNC,                   /* Truncate */
	BUILTIN_VEC_ROUND,                   /* Round to nearest */
	BUILTIN_VEC_RINT,                    /* Round to integer */
	BUILTIN_VEC_SIN,                     /* Sine */
	BUILTIN_VEC_COS,                     /* Cosine */
	BUILTIN_VEC_TAN,                     /* Tangent */
	BUILTIN_VEC_EXP,                     /* Exponential */
	BUILTIN_VEC_EXP2,                    /* Base-2 exponential */
	BUILTIN_VEC_LOG,                     /* Natural logarithm */
	BUILTIN_VEC_LOG2,                    /* Base-2 logarithm */
	BUILTIN_VEC_LOG10,                   /* Base-10 logarithm */
	BUILTIN_VEC_POW,                     /* Power */
	BUILTIN_VEC_CBRT,                    /* Cube root */

	/* ===== Integer Bit Operations ===== */
	BUILTIN_VEC_POPCNT = 2200,           /* Population count */
	BUILTIN_VEC_CLZ,                     /* Count leading zeros */
	BUILTIN_VEC_CTZ,                     /* Count trailing zeros */
	BUILTIN_VEC_BSWAP,                   /* Byte swap */
	BUILTIN_VEC_BITREV,                  /* Bit reverse */
	BUILTIN_VEC_PARITY,                  /* Parity */

	/* ===== Crypto Operations ===== */
	BUILTIN_VEC_AES_ENC = 2300,          /* AES encrypt */
	BUILTIN_VEC_AES_DEC,                 /* AES decrypt */
	BUILTIN_VEC_AES_ENC_LAST,            /* AES encrypt last round */
	BUILTIN_VEC_AES_DEC_LAST,            /* AES decrypt last round */
	BUILTIN_VEC_AES_IMC,                 /* AES inverse mix columns */
	BUILTIN_VEC_AES_KEYGEN,              /* AES key generation */
	BUILTIN_VEC_SHA1,                    /* SHA-1 operations */
	BUILTIN_VEC_SHA256,                  /* SHA-256 operations */
	BUILTIN_VEC_SM3,                     /* SM3 (Chinese standard) */
	BUILTIN_VEC_SM4,                     /* SM4 (Chinese standard) */
	BUILTIN_VEC_CRC32,                   /* CRC-32 */
	BUILTIN_VEC_PCLMULQDQ,               /* Carry-less multiply */

	/* ===== Matrix Operations (AMX, SME) ===== */
	BUILTIN_MAT_LOAD = 3000,             /* Load matrix tile */
	BUILTIN_MAT_STORE,                   /* Store matrix tile */
	BUILTIN_MAT_ZERO,                    /* Zero matrix tile */
	BUILTIN_MAT_MUL,                     /* Matrix multiply */
	BUILTIN_MAT_DPBSSD,                  /* Dot product i8 -> i32 */
	BUILTIN_MAT_DPBSUD,                  /* Dot product i8/u8 -> i32 */
	BUILTIN_MAT_DPBUSD,                  /* Dot product u8/i8 -> i32 */
	BUILTIN_MAT_DPBUUD,                  /* Dot product u8 -> i32 */
	BUILTIN_MAT_DPBF16PS,                /* Dot product bf16 -> f32 */
	BUILTIN_MAT_DPFP16PS,                /* Dot product fp16 -> f32 */
	BUILTIN_MAT_CONFIG,                  /* Configure tile */

	/* ===== SVE/RVV Predicated Operations ===== */
	BUILTIN_PRED_CREATE = 3100,          /* Create predicate */
	BUILTIN_PRED_WHILE_LT,               /* While less than */
	BUILTIN_PRED_PTRUE,                  /* All true */
	BUILTIN_PRED_PFALSE,                 /* All false */
	BUILTIN_PRED_FIRST,                  /* First active */
	BUILTIN_PRED_LAST,                   /* Last active */
	BUILTIN_PRED_CNTP,                   /* Count predicate bits */

	/* ===== Scalable Vector Operations (SVE, RVV) ===== */
	BUILTIN_SVE_LEN = 3200,              /* Get vector length */
	BUILTIN_SVE_INC,                     /* Increment by vector length */
	BUILTIN_SVE_DEC,                     /* Decrement by vector length */
	BUILTIN_SVE_COMPACT,                 /* Compact active elements */
	BUILTIN_SVE_SPLICE,                  /* Splice vectors */
	BUILTIN_SVE_REV,                     /* Reverse elements */
	BUILTIN_SVE_TBL,                     /* Table lookup */

	/* ===== Mask Operations (AVX-512, ARM SVE) ===== */
	BUILTIN_MASK_LOAD = 3300,            /* Load mask register */
	BUILTIN_MASK_STORE,                  /* Store mask register */
	BUILTIN_MASK_AND,                    /* Mask AND */
	BUILTIN_MASK_OR,                     /* Mask OR */
	BUILTIN_MASK_XOR,                    /* Mask XOR */
	BUILTIN_MASK_NOT,                    /* Mask NOT */
	BUILTIN_MASK_TEST,                   /* Test mask bits */
	BUILTIN_MASK_TESTZ,                  /* Test all zeros */
	BUILTIN_MASK_TESTC,                  /* Test all ones */
	BUILTIN_MASK_CMP,                    /* Compare with mask */

	/* ===== Special Operations ===== */
	BUILTIN_PREFETCH = 3400,             /* Prefetch */
	BUILTIN_FENCE,                       /* Memory fence */
	BUILTIN_LFENCE,                      /* Load fence */
	BUILTIN_SFENCE,                      /* Store fence */
	BUILTIN_MFENCE,                      /* Memory fence (full) */
	BUILTIN_CLFLUSH,                     /* Cache line flush */
	BUILTIN_CLFLUSHOPT,                  /* Optimized cache line flush */
	BUILTIN_CLWB,                        /* Cache line write-back */

	/* ===== Miscellaneous ===== */
	BUILTIN_VEC_SET1 = 3500,             /* Set all lanes to scalar */
	BUILTIN_VEC_SETZERO,                 /* Set to zero */
	BUILTIN_VEC_SETR,                    /* Set in reverse order */
	BUILTIN_VEC_UNDEFINED,               /* Undefined value */
	BUILTIN_VEC_MOVEMASK,                /* Move mask to integer */
	BUILTIN_VEC_TESTZ,                   /* Test all zeros */
	BUILTIN_VEC_TESTC,                   /* Test all ones */
	BUILTIN_VEC_TESTNZC,                 /* Test not zero and not all ones */

	BUILTIN_MAX                          /* Sentinel */
} builtin_operation_t;

/***************************************************************************
 * Builtin Descriptor
 ***************************************************************************/

typedef struct {
	builtin_operation_t operation;
	const char *name;                    /* Canonical name */
	const char *llvm_intrinsic;          /* LLVM intrinsic name */
	const char *gcc_builtin;             /* GCC builtin name */
	const char *msvc_intrinsic;          /* MSVC intrinsic name */
	const char **isa_variants;           /* Array of ISA-specific names */
	int num_operands;
	int has_predicate;                   /* Supports predication */
	int is_fp;                           /* Floating point operation */
	int is_saturating;                   /* Saturating arithmetic */
} builtin_descriptor_t;

/***************************************************************************
 * ISA Mapping - Maps ISA intrinsics to semantic operations
 ***************************************************************************/

/* x86 ISA families */
typedef enum {
	ISA_X86_MMX,
	ISA_X86_SSE,
	ISA_X86_SSE2,
	ISA_X86_SSE3,
	ISA_X86_SSSE3,
	ISA_X86_SSE41,
	ISA_X86_SSE42,
	ISA_X86_AVX,
	ISA_X86_AVX2,
	ISA_X86_FMA,
	ISA_X86_AVX512F,
	ISA_X86_AVX512BW,
	ISA_X86_AVX512DQ,
	ISA_X86_AVX512VL,
	ISA_X86_AVX512VNNI,
	ISA_X86_AVX512BF16,
	ISA_X86_AVX512FP16,
	ISA_X86_AMX_TILE,
	ISA_X86_AMX_INT8,
	ISA_X86_AMX_BF16,
	ISA_X86_AMX_FP16,
	ISA_X86_AES,
	ISA_X86_SHA,

	/* ARM ISA families */
	ISA_ARM_NEON,
	ISA_ARM_NEON_FP16,
	ISA_ARM_NEON_BF16,
	ISA_ARM_SVE,
	ISA_ARM_SVE2,
	ISA_ARM_SVE2P1,
	ISA_ARM_SME,
	ISA_ARM_SME2,
	ISA_ARM_CRYPTO,

	/* RISC-V */
	ISA_RV_V,                            /* RVV 1.0 */
	ISA_RV_ZVBB,                         /* Bit manipulation */
	ISA_RV_ZVBC,                         /* Carry-less multiply */
	ISA_RV_ZVKG,                         /* GCM crypto */

	/* PowerPC */
	ISA_PPC_ALTIVEC,                     /* VMX */
	ISA_PPC_VSX,
	ISA_PPC_VSX2,
	ISA_PPC_VSX3,

	/* MIPS/Loongson */
	ISA_MIPS_MSA,                        /* MIPS SIMD */
	ISA_LOONGSON_LSX,                    /* Loongson 128-bit */
	ISA_LOONGSON_LASX,                   /* Loongson 256-bit */

	ISA_MAX
} isa_family_t;

/* Get builtin descriptor */
const builtin_descriptor_t* builtin_get_descriptor(builtin_operation_t op);

/* Map ISA intrinsic name to semantic operation */
builtin_operation_t builtin_map_intrinsic(const char *intrinsic_name, isa_family_t isa);

/* Get all variants of an operation for different ISAs */
const char** builtin_get_isa_variants(builtin_operation_t op);

/***************************************************************************
 * Common Intrinsic Name Mappings (examples)
 ***************************************************************************/

/* Example mappings (subset):
 *
 * BUILTIN_VEC_ADD maps to:
 *   - LLVM: llvm.vector.add
 *   - x86 SSE: _mm_add_ps, _mm_add_pd, _mm_add_epi32, etc.
 *   - x86 AVX: _mm256_add_ps, _mm256_add_pd, _mm256_add_epi32, etc.
 *   - x86 AVX-512: _mm512_add_ps, _mm512_add_pd, _mm512_add_epi32, etc.
 *   - ARM NEON: vaddq_f32, vaddq_s32, vaddq_u32, etc.
 *   - ARM SVE: svadd_f32_z, svadd_s32_z, etc.
 *   - PowerPC: vec_add
 *   - RISC-V: vadd_vv_*
 *
 * BUILTIN_VEC_FMA maps to:
 *   - LLVM: llvm.fma.*
 *   - x86 FMA: _mm_fmadd_ps, _mm_fmadd_pd, _mm256_fmadd_ps, etc.
 *   - x86 AVX-512: _mm512_fmadd_ps, etc.
 *   - ARM NEON: vfma_f32, vfmaq_f32
 *   - ARM SVE: svmla_f32_z
 *   - PowerPC: vec_madd
 *
 * BUILTIN_MAT_MUL maps to:
 *   - x86 AMX: _tile_dpbssd, _tile_dpbf16ps
 *   - ARM SME: svmopa_za32_*
 */

#ifdef __cplusplus
}
#endif

#endif /* __LIBCPU_BACKEND_EMULATION_BUILTINS_H__ */
