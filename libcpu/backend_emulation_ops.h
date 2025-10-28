/*
 * libcpu Backend Emulation - Unified Generic Operations
 *
 * Complete set of architecture-agnostic operations for emulating ANY architecture.
 * Operations are not bound to specific CPUs - they're generic enough to implement
 * PDP-1/6/10, UNIVAC 1100/2200, CDC 6600, VAX, IBM mainframes, and modern CPUs.
 *
 * Design Principles:
 * 1. Operations defined by BEHAVIOR, not architecture
 * 2. Configurable word sizes, arithmetic modes, number formats
 * 3. Single operation works on scalars, vectors, matrices
 * 4. No architecture-specific names
 * 5. Comprehensive enough to emulate any historical or modern system
 */

#ifndef __LIBCPU_BACKEND_EMULATION_OPS_H__
#define __LIBCPU_BACKEND_EMULATION_OPS_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************
 * CONFIGURATION TYPES
 ***************************************************************************/

/* Arithmetic mode for integer operations */
typedef enum {
	ARITH_TWOS_COMPLEMENT,      /* Standard: -128 to +127 (8-bit) */
	ARITH_ONES_COMPLEMENT,      /* PDP-1, CDC 6600, UNIVAC: two zeros */
	ARITH_SIGN_MAGNITUDE,       /* Early computers: sign bit + magnitude */
	ARITH_EXCESS_N,             /* Biased representation */
	ARITH_BCD,                  /* Binary coded decimal */
	ARITH_GRAY_CODE             /* Gray code representation */
} arithmetic_mode_t;

/* Floating point format */
typedef enum {
	FP_FORMAT_IEEE_BINARY16,    /* IEEE 754 binary16 (half) */
	FP_FORMAT_IEEE_BINARY32,    /* IEEE 754 binary32 (float) */
	FP_FORMAT_IEEE_BINARY64,    /* IEEE 754 binary64 (double) */
	FP_FORMAT_IEEE_BINARY128,   /* IEEE 754 binary128 (quad) */
	FP_FORMAT_IEEE_DECIMAL32,   /* IEEE 754-2008 decimal32 */
	FP_FORMAT_IEEE_DECIMAL64,   /* IEEE 754-2008 decimal64 */
	FP_FORMAT_IEEE_DECIMAL128,  /* IEEE 754-2008 decimal128 */
	FP_FORMAT_VAX_F,            /* VAX F_floating (32-bit) */
	FP_FORMAT_VAX_D,            /* VAX D_floating (64-bit) */
	FP_FORMAT_VAX_G,            /* VAX G_floating (64-bit extended) */
	FP_FORMAT_VAX_H,            /* VAX H_floating (128-bit) */
	FP_FORMAT_IBM_SHORT,        /* IBM hexadecimal short (32-bit) */
	FP_FORMAT_IBM_LONG,         /* IBM hexadecimal long (64-bit) */
	FP_FORMAT_IBM_EXTENDED,     /* IBM hexadecimal extended (128-bit) */
	FP_FORMAT_CRAY,             /* Cray floating point (64-bit) */
	FP_FORMAT_ARM_FPA,          /* ARM FPA extended (80-bit) */
	FP_FORMAT_X87_EXTENDED,     /* x87 extended (80-bit) */
	FP_FORMAT_PPC_DOUBLE_DOUBLE,/* PowerPC double-double (128-bit) */
	FP_FORMAT_BFLOAT16,         /* Google bfloat16 */
	FP_FORMAT_FP8_E4M3,         /* 8-bit float (4 exp, 3 mantissa) */
	FP_FORMAT_FP8_E5M2          /* 8-bit float (5 exp, 2 mantissa) */
} fp_format_t;

/* Rounding mode */
typedef enum {
	ROUND_NEAREST_EVEN,         /* IEEE default */
	ROUND_NEAREST_AWAY,
	ROUND_TOWARD_ZERO,
	ROUND_TOWARD_POSITIVE,
	ROUND_TOWARD_NEGATIVE,
	ROUND_NEAREST_UP,           /* Round half up */
	ROUND_NEAREST_DOWN          /* Round half down */
} rounding_mode_t;

/* Endianness */
typedef enum {
	ENDIAN_LITTLE,
	ENDIAN_BIG,
	ENDIAN_PDP,                 /* PDP-11 mixed endian */
	ENDIAN_HONEYWELL            /* Honeywell 36-bit word order */
} endianness_t;

/* Word configuration */
typedef struct {
	uint32_t bits;              /* Word size in bits (8, 16, 32, 36, 60, 64, etc.) */
	arithmetic_mode_t arith_mode;
	endianness_t endian;
	int is_signed;
} word_config_t;

/* Floating point configuration */
typedef struct {
	fp_format_t format;
	rounding_mode_t rounding;
	int flush_denormals;        /* Treat denormals as zero */
	int trap_on_inexact;
	int trap_on_underflow;
	int trap_on_overflow;
	int trap_on_invalid;
	int trap_on_divzero;
} fp_config_t;

/* Vector configuration */
typedef struct {
	uint32_t num_elements;
	uint32_t element_bits;      /* Bits per element */
	int is_scalable;            /* SVE/RVV style scalable vectors */
	int is_predicated;          /* Has predicate/mask */
} vector_config_t;

/***************************************************************************
 * UNIFIED VALUE TYPE
 * Represents scalar, vector, matrix, or predicate with any configuration
 ***************************************************************************/

typedef enum {
	VALUE_KIND_SCALAR,
	VALUE_KIND_VECTOR,
	VALUE_KIND_MATRIX,
	VALUE_KIND_PREDICATE,
	VALUE_KIND_POINTER,         /* For byte pointers (PDP-10 style) */
	VALUE_KIND_STRING           /* For string operations */
} value_kind_t;

typedef enum {
	VALUE_TYPE_INTEGER,
	VALUE_TYPE_FLOAT,
	VALUE_TYPE_DECIMAL,
	VALUE_TYPE_BCD,
	VALUE_TYPE_PACKED_DECIMAL
} value_type_t;

/* Generic value that can be any size, format, or kind */
typedef struct unified_value {
	value_kind_t kind;
	value_type_t type;

	/* Configuration */
	word_config_t word_config;
	fp_config_t fp_config;
	vector_config_t vec_config;

	/* Data storage (dynamically sized) */
	void *data;
	size_t data_size;

	/* Metadata */
	int is_constant;
	const char *name;           /* Optional debug name */
} unified_value_t;

/* Value creation/destruction */
unified_value_t* uval_create_scalar(value_type_t type, const word_config_t *config);
unified_value_t* uval_create_vector(value_type_t type, const vector_config_t *vec_config, const word_config_t *word_config);
unified_value_t* uval_create_matrix(uint32_t rows, uint32_t cols, value_type_t type, const word_config_t *config);
unified_value_t* uval_create_predicate(uint32_t num_bits, int is_scalable);
unified_value_t* uval_create_pointer(uint32_t address_bits, uint32_t position_bits, uint32_t size_bits);  /* PDP-10 style */
void uval_free(unified_value_t *val);

/* Value access */
void uval_set_i64(unified_value_t *val, int64_t value);
void uval_set_u64(unified_value_t *val, uint64_t value);
void uval_set_f64(unified_value_t *val, double value);
void uval_set_bytes(unified_value_t *val, const void *bytes, size_t len);
int64_t uval_get_i64(const unified_value_t *val);
uint64_t uval_get_u64(const unified_value_t *val);
double uval_get_f64(const unified_value_t *val);
void uval_get_bytes(const unified_value_t *val, void *bytes, size_t len);

/***************************************************************************
 * ARITHMETIC OPERATIONS
 * Work on any word size, arithmetic mode, or number format
 ***************************************************************************/

/* Basic arithmetic */
void op_add(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_sub(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_mul(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_div(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_rem(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_neg(unified_value_t *dst, const unified_value_t *src);
void op_abs(unified_value_t *dst, const unified_value_t *src);

/* Arithmetic with carry/borrow (for multi-precision) */
void op_adc(unified_value_t *dst, unified_value_t *carry, const unified_value_t *a, const unified_value_t *b, const unified_value_t *carry_in);
void op_sbb(unified_value_t *dst, unified_value_t *borrow, const unified_value_t *a, const unified_value_t *b, const unified_value_t *borrow_in);

/* Saturating arithmetic */
void op_add_sat(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_sub_sat(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_mul_sat(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);

/* Widening/narrowing multiply */
void op_mul_wide(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);  /* Double width result */
void op_mulhi(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);     /* High half */
void op_mullo(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);     /* Low half */

/* Average (rounded) */
void op_avg(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_avg_round_up(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_avg_round_down(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);

/* Min/max */
void op_min(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_max(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_minmax(unified_value_t *min_dst, unified_value_t *max_dst, const unified_value_t *a, const unified_value_t *b);

/* Fused multiply-add family */
void op_fma(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, const unified_value_t *c);   /* a*b+c */
void op_fms(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, const unified_value_t *c);   /* a*b-c */
void op_fnma(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, const unified_value_t *c);  /* -(a*b)+c */
void op_fnms(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, const unified_value_t *c);  /* -(a*b)-c */

/* Multiply-add with accumulator (DSP style) */
void op_mac(unified_value_t *acc, const unified_value_t *a, const unified_value_t *b);       /* acc += a*b */
void op_msu(unified_value_t *acc, const unified_value_t *a, const unified_value_t *b);       /* acc -= a*b */

/***************************************************************************
 * BITWISE/LOGICAL OPERATIONS
 ***************************************************************************/

void op_and(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_or(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_xor(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_not(unified_value_t *dst, const unified_value_t *src);
void op_nand(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_nor(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_xnor(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_andn(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);  /* a & ~b */
void op_orn(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);   /* a | ~b */

/* Shifts (work with any word size) */
void op_sll(unified_value_t *dst, const unified_value_t *a, const unified_value_t *shift);  /* Shift left logical */
void op_srl(unified_value_t *dst, const unified_value_t *a, const unified_value_t *shift);  /* Shift right logical */
void op_sra(unified_value_t *dst, const unified_value_t *a, const unified_value_t *shift);  /* Shift right arithmetic */
void op_rol(unified_value_t *dst, const unified_value_t *a, const unified_value_t *shift);  /* Rotate left */
void op_ror(unified_value_t *dst, const unified_value_t *a, const unified_value_t *shift);  /* Rotate right */

/* Funnel shifts (concatenate and shift) */
void op_funnel_shl(unified_value_t *dst, const unified_value_t *hi, const unified_value_t *lo, const unified_value_t *shift);
void op_funnel_shr(unified_value_t *dst, const unified_value_t *hi, const unified_value_t *lo, const unified_value_t *shift);

/***************************************************************************
 * BIT MANIPULATION
 ***************************************************************************/

/* Bit counting */
void op_popcnt(unified_value_t *dst, const unified_value_t *src);  /* Count 1 bits */
void op_clz(unified_value_t *dst, const unified_value_t *src);     /* Count leading zeros */
void op_ctz(unified_value_t *dst, const unified_value_t *src);     /* Count trailing zeros */
void op_clo(unified_value_t *dst, const unified_value_t *src);     /* Count leading ones */
void op_cto(unified_value_t *dst, const unified_value_t *src);     /* Count trailing ones */
void op_parity(unified_value_t *dst, const unified_value_t *src);  /* Parity bit (even/odd) */

/* Bit scanning */
void op_ffs(unified_value_t *dst, const unified_value_t *src);     /* Find first set (1 bit) */
void op_fls(unified_value_t *dst, const unified_value_t *src);     /* Find last set */
void op_ffc(unified_value_t *dst, const unified_value_t *src);     /* Find first clear (0 bit) */

/* Bit deposit/extract (BMI2 PDEP/PEXT style) */
void op_bit_deposit(unified_value_t *dst, const unified_value_t *src, const unified_value_t *mask);
void op_bit_extract(unified_value_t *dst, const unified_value_t *src, const unified_value_t *mask);

/* Bit field operations (generalized from 68K, but works on any word size) */
void op_bitfield_extract(unified_value_t *dst, const unified_value_t *src, uint32_t offset, uint32_t width);
void op_bitfield_extract_signed(unified_value_t *dst, const unified_value_t *src, uint32_t offset, uint32_t width);
void op_bitfield_insert(unified_value_t *dst, const unified_value_t *base, const unified_value_t *src, uint32_t offset, uint32_t width);
void op_bitfield_set(unified_value_t *dst, const unified_value_t *src, uint32_t offset, uint32_t width);
void op_bitfield_clear(unified_value_t *dst, const unified_value_t *src, uint32_t offset, uint32_t width);
void op_bitfield_toggle(unified_value_t *dst, const unified_value_t *src, uint32_t offset, uint32_t width);
void op_bitfield_test(unified_value_t *dst, const unified_value_t *src, uint32_t offset, uint32_t width);

/* Bit reversal and byte swapping */
void op_bitrev(unified_value_t *dst, const unified_value_t *src);
void op_bswap(unified_value_t *dst, const unified_value_t *src);

/***************************************************************************
 * BYTE MANIPULATION
 ***************************************************************************/

/* Variable-size byte operations (for PDP-10, CDC, etc.) */
void op_byte_load(unified_value_t *dst, const unified_value_t *src, uint32_t position, uint32_t size);
void op_byte_store(unified_value_t *dst, const unified_value_t *src, const unified_value_t *byte, uint32_t position, uint32_t size);

/* Byte masking (from ALPHA ZAP/ZAPNOT, generalized) */
void op_byte_zap(unified_value_t *dst, const unified_value_t *src, uint64_t mask);     /* Zero bytes where mask=1 */
void op_byte_zapnot(unified_value_t *dst, const unified_value_t *src, uint64_t mask);  /* Zero bytes where mask=0 */

/* Byte extraction/insertion */
void op_byte_extract(unified_value_t *dst, const unified_value_t *src, uint32_t index);
void op_byte_insert(unified_value_t *dst, const unified_value_t *base, const unified_value_t *byte, uint32_t index);

/* Byte shuffle/permute */
void op_byte_shuffle(unified_value_t *dst, const unified_value_t *src, const uint8_t *pattern, uint32_t pattern_len);

/***************************************************************************
 * COMPARISON OPERATIONS
 ***************************************************************************/

/* Comparisons (return boolean mask) */
void op_cmpeq(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_cmpne(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_cmplt(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_cmple(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_cmpgt(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_cmpge(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);

/* Floating point ordered/unordered comparisons */
void op_cmpord(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_cmpunord(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);

/* Compare and set predicate (for predicated ISAs) */
void op_cmpeq_pred(unified_value_t *pred, const unified_value_t *a, const unified_value_t *b);
void op_cmplt_pred(unified_value_t *pred, const unified_value_t *a, const unified_value_t *b);
void op_cmple_pred(unified_value_t *pred, const unified_value_t *a, const unified_value_t *b);

/* Select/conditional move */
void op_select(unified_value_t *dst, const unified_value_t *cond, const unified_value_t *true_val, const unified_value_t *false_val);
void op_blend(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, const unified_value_t *mask);

/***************************************************************************
 * CONVERSION OPERATIONS
 ***************************************************************************/

/* Type conversions (automatic based on src/dst configs) */
void op_convert(unified_value_t *dst, const unified_value_t *src);

/* Integer <-> Float */
void op_cvt_i2f(unified_value_t *dst, const unified_value_t *src);
void op_cvt_f2i(unified_value_t *dst, const unified_value_t *src);  /* Round to nearest */
void op_cvt_f2i_trunc(unified_value_t *dst, const unified_value_t *src);
void op_cvt_f2i_floor(unified_value_t *dst, const unified_value_t *src);
void op_cvt_f2i_ceil(unified_value_t *dst, const unified_value_t *src);

/* Width conversions */
void op_extend_signed(unified_value_t *dst, const unified_value_t *src);
void op_extend_unsigned(unified_value_t *dst, const unified_value_t *src);
void op_truncate(unified_value_t *dst, const unified_value_t *src);

/* Saturation */
void op_saturate(unified_value_t *dst, const unified_value_t *src, int64_t min, int64_t max);

/* Number format conversions (between IEEE, VAX, IBM, Cray, etc.) */
void op_fp_convert_format(unified_value_t *dst, const unified_value_t *src, fp_format_t dst_fmt, fp_format_t src_fmt);

/* Decimal conversions */
void op_cvt_binary_to_bcd(unified_value_t *dst, const unified_value_t *src);
void op_cvt_bcd_to_binary(unified_value_t *dst, const unified_value_t *src);
void op_cvt_binary_to_decimal(unified_value_t *dst, const unified_value_t *src);
void op_cvt_decimal_to_binary(unified_value_t *dst, const unified_value_t *src);

/***************************************************************************
 * PACK/UNPACK OPERATIONS
 ***************************************************************************/

/* Pack (narrow) - reduce element width */
void op_pack(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_pack_sat_signed(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_pack_sat_unsigned(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);

/* Unpack (widen) - increase element width */
void op_unpack_low(unified_value_t *dst, const unified_value_t *src);
void op_unpack_high(unified_value_t *dst, const unified_value_t *src);
void op_unpack_low_signed(unified_value_t *dst, const unified_value_t *src);
void op_unpack_high_signed(unified_value_t *dst, const unified_value_t *src);

/* Interleave/deinterleave */
void op_interleave_low(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_interleave_high(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_deinterleave_even(unified_value_t *dst, const unified_value_t *src);
void op_deinterleave_odd(unified_value_t *dst, const unified_value_t *src);

/***************************************************************************
 * SHUFFLE/PERMUTE/SELECT OPERATIONS
 ***************************************************************************/

/* Shuffle with index vector */
void op_shuffle(unified_value_t *dst, const unified_value_t *src, const unified_value_t *indices);
void op_shuffle2(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, const unified_value_t *indices);

/* Permute (reorder elements) */
void op_permute(unified_value_t *dst, const unified_value_t *src, const unified_value_t *control);

/* Extract/insert element */
void op_extract_element(unified_value_t *dst, const unified_value_t *src, uint32_t index);
void op_insert_element(unified_value_t *dst, const unified_value_t *vec, const unified_value_t *elem, uint32_t index);

/* Broadcast/splat */
void op_broadcast(unified_value_t *dst, const unified_value_t *scalar);
void op_splat(unified_value_t *dst, const unified_value_t *vec, uint32_t index);

/* Reverse */
void op_reverse(unified_value_t *dst, const unified_value_t *src);
void op_reverse_bytes(unified_value_t *dst, const unified_value_t *src);

/* Rotate elements */
void op_rotate_elements(unified_value_t *dst, const unified_value_t *src, int32_t count);

/* Concatenate and extract */
void op_concat_extract(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, uint32_t offset);

/***************************************************************************
 * REDUCTION OPERATIONS (Vector -> Scalar)
 ***************************************************************************/

void op_reduce_add(unified_value_t *dst, const unified_value_t *src);
void op_reduce_mul(unified_value_t *dst, const unified_value_t *src);
void op_reduce_min(unified_value_t *dst, const unified_value_t *src);
void op_reduce_max(unified_value_t *dst, const unified_value_t *src);
void op_reduce_and(unified_value_t *dst, const unified_value_t *src);
void op_reduce_or(unified_value_t *dst, const unified_value_t *src);
void op_reduce_xor(unified_value_t *dst, const unified_value_t *src);

/* Index reductions */
void op_reduce_minidx(unified_value_t *dst, const unified_value_t *src);
void op_reduce_maxidx(unified_value_t *dst, const unified_value_t *src);

/***************************************************************************
 * HORIZONTAL OPERATIONS
 ***************************************************************************/

void op_hadd(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_hsub(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_hadds(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);  /* Saturating */
void op_hsubs(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);

/* Sum across (add adjacent pairs) */
void op_pairwise_add(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);

/***************************************************************************
 * DOT PRODUCT / MATRIX OPERATIONS
 ***************************************************************************/

void op_dot(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_dot4(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);

/* Matrix multiply-accumulate (AMX, SME style) */
void op_matrix_mul(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_matrix_mul_acc(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);

/* Tile operations */
void op_tile_load(unified_value_t *dst, const void *ptr, size_t stride);
void op_tile_store(void *ptr, const unified_value_t *src, size_t stride);

/***************************************************************************
 * FLOATING-POINT MATH OPERATIONS
 ***************************************************************************/

/* Basic FP operations */
void op_sqrt(unified_value_t *dst, const unified_value_t *src);
void op_rsqrt(unified_value_t *dst, const unified_value_t *src);  /* Reciprocal sqrt (approx) */
void op_rcp(unified_value_t *dst, const unified_value_t *src);    /* Reciprocal (approx) */

/* Rounding */
void op_round(unified_value_t *dst, const unified_value_t *src);
void op_floor(unified_value_t *dst, const unified_value_t *src);
void op_ceil(unified_value_t *dst, const unified_value_t *src);
void op_trunc(unified_value_t *dst, const unified_value_t *src);

/* Fraction/exponent manipulation */
void op_fract(unified_value_t *dst, const unified_value_t *src);  /* Fractional part */
void op_modf(unified_value_t *int_part, unified_value_t *frac_part, const unified_value_t *src);
void op_ldexp(unified_value_t *dst, const unified_value_t *mantissa, const unified_value_t *exp);
void op_frexp(unified_value_t *mantissa, unified_value_t *exp, const unified_value_t *src);
void op_logb(unified_value_t *dst, const unified_value_t *src);
void op_scalbn(unified_value_t *dst, const unified_value_t *src, const unified_value_t *n);

/* FP classification */
void op_fpclassify(unified_value_t *dst, const unified_value_t *src);
void op_isnan(unified_value_t *dst, const unified_value_t *src);
void op_isinf(unified_value_t *dst, const unified_value_t *src);
void op_isfinite(unified_value_t *dst, const unified_value_t *src);
void op_isnormal(unified_value_t *dst, const unified_value_t *src);
void op_signbit(unified_value_t *dst, const unified_value_t *src);

/* FP manipulation */
void op_copysign(unified_value_t *dst, const unified_value_t *mag, const unified_value_t *sign);
void op_nextafter(unified_value_t *dst, const unified_value_t *from, const unified_value_t *to);

/***************************************************************************
 * TRANSCENDENTAL FUNCTIONS (Hardware or emulated)
 ***************************************************************************/

/* Trigonometric */
void op_sin(unified_value_t *dst, const unified_value_t *src);
void op_cos(unified_value_t *dst, const unified_value_t *src);
void op_tan(unified_value_t *dst, const unified_value_t *src);
void op_sincos(unified_value_t *sin_dst, unified_value_t *cos_dst, const unified_value_t *src);

/* Inverse trigonometric */
void op_asin(unified_value_t *dst, const unified_value_t *src);
void op_acos(unified_value_t *dst, const unified_value_t *src);
void op_atan(unified_value_t *dst, const unified_value_t *src);
void op_atan2(unified_value_t *dst, const unified_value_t *y, const unified_value_t *x);

/* Hyperbolic */
void op_sinh(unified_value_t *dst, const unified_value_t *src);
void op_cosh(unified_value_t *dst, const unified_value_t *src);
void op_tanh(unified_value_t *dst, const unified_value_t *src);

/* Exponential/logarithmic */
void op_exp(unified_value_t *dst, const unified_value_t *src);
void op_exp2(unified_value_t *dst, const unified_value_t *src);
void op_exp10(unified_value_t *dst, const unified_value_t *src);
void op_expm1(unified_value_t *dst, const unified_value_t *src);
void op_log(unified_value_t *dst, const unified_value_t *src);
void op_log2(unified_value_t *dst, const unified_value_t *src);
void op_log10(unified_value_t *dst, const unified_value_t *src);
void op_log1p(unified_value_t *dst, const unified_value_t *src);

/* Power */
void op_pow(unified_value_t *dst, const unified_value_t *base, const unified_value_t *exp);
void op_hypot(unified_value_t *dst, const unified_value_t *x, const unified_value_t *y);

/* Polynomial evaluation (from VAX POLY) */
void op_poly(unified_value_t *dst, const unified_value_t *x, const unified_value_t *coeffs, uint32_t degree);

/***************************************************************************
 * STRING/MEMORY OPERATIONS (for CISC ISAs)
 ***************************************************************************/

/* String compare/search */
void op_strcmp(unified_value_t *dst, const unified_value_t *s1, const unified_value_t *s2, uint32_t max_len);
void op_strchr(unified_value_t *dst, const unified_value_t *str, const unified_value_t *chr, uint32_t max_len);
void op_strlen(unified_value_t *dst, const unified_value_t *str, uint32_t max_len);

/* Block operations (from Z80, 6809, VAX) */
void op_block_move(void *dst, const void *src, uint32_t count, uint32_t elem_size);
void op_block_fill(void *dst, const unified_value_t *value, uint32_t count);
void op_block_compare(unified_value_t *result, const void *a, const void *b, uint32_t count);
void op_block_scan(unified_value_t *index, const void *block, const unified_value_t *value, uint32_t count);

/***************************************************************************
 * PREDICATE/MASK OPERATIONS (for predicated ISAs)
 ***************************************************************************/

/* Predicate logical operations */
void op_pred_and(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_pred_or(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_pred_xor(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_pred_not(unified_value_t *dst, const unified_value_t *src);

/* Predicate manipulation */
void op_pred_first_true(unified_value_t *dst, const unified_value_t *pred);
void op_pred_last_true(unified_value_t *dst, const unified_value_t *pred);
void op_pred_count_true(unified_value_t *dst, const unified_value_t *pred);
void op_pred_all_true(unified_value_t *dst, const unified_value_t *pred);
void op_pred_any_true(unified_value_t *dst, const unified_value_t *pred);
void op_pred_none_true(unified_value_t *dst, const unified_value_t *pred);

/* Masked operations */
void op_masked_add(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, const unified_value_t *mask);
void op_masked_load(unified_value_t *dst, const void *ptr, const unified_value_t *mask);
void op_masked_store(void *ptr, const unified_value_t *src, const unified_value_t *mask);

/***************************************************************************
 * BCD / DECIMAL OPERATIONS (for commercial ISAs)
 ***************************************************************************/

/* BCD arithmetic */
void op_bcd_add(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_bcd_sub(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_bcd_mul(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_bcd_div(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);

/* Decimal adjust */
void op_daa(unified_value_t *dst, const unified_value_t *src);  /* Decimal adjust after add */
void op_das(unified_value_t *dst, const unified_value_t *src);  /* Decimal adjust after subtract */

/* Packed decimal (IBM style) */
void op_packed_decimal_add(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_packed_decimal_sub(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_packed_decimal_mul(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_packed_decimal_div(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);

/***************************************************************************
 * QUEUE/LINKED LIST OPERATIONS (from VAX, 68K)
 ***************************************************************************/

void op_queue_insert(void *entry, void *predecessor);
void op_queue_remove(void **entry, void *header);

/***************************************************************************
 * SPECIALIZED OPERATIONS
 ***************************************************************************/

/* CRC (from SSE4.2, ARM) */
void op_crc32(unified_value_t *dst, const unified_value_t *crc, const unified_value_t *data);

/* AES (from AES-NI, ARM Crypto) */
void op_aes_enc(unified_value_t *dst, const unified_value_t *state, const unified_value_t *key);
void op_aes_enc_last(unified_value_t *dst, const unified_value_t *state, const unified_value_t *key);
void op_aes_dec(unified_value_t *dst, const unified_value_t *state, const unified_value_t *key);
void op_aes_dec_last(unified_value_t *dst, const unified_value_t *state, const unified_value_t *key);
void op_aes_keygen(unified_value_t *dst, const unified_value_t *key, uint8_t rcon);

/* SHA (from SHA extensions) */
void op_sha1_c(unified_value_t *dst, const unified_value_t *abcd, const unified_value_t *e, const unified_value_t *msg);
void op_sha1_p(unified_value_t *dst, const unified_value_t *abcd, const unified_value_t *e, const unified_value_t *msg);
void op_sha1_m(unified_value_t *dst, const unified_value_t *msg0, const unified_value_t *msg1, const unified_value_t *msg2);
void op_sha256_rnds2(unified_value_t *dst, const unified_value_t *src, const unified_value_t *wk);
void op_sha256_msg1(unified_value_t *dst, const unified_value_t *src);
void op_sha256_msg2(unified_value_t *dst, const unified_value_t *src);

/* Random number generation */
void op_rand(unified_value_t *dst);
void op_rand_seed(const unified_value_t *seed);

/***************************************************************************
 * CONDITIONAL/PREDICATED EXECUTION (for IA-64, ARM)
 ***************************************************************************/

/* Predicated compare (IA-64 style) */
void op_pred_cmp_eq(unified_value_t *p_true, unified_value_t *p_false, const unified_value_t *a, const unified_value_t *b);
void op_pred_cmp_lt(unified_value_t *p_true, unified_value_t *p_false, const unified_value_t *a, const unified_value_t *b);

/* Speculative load (IA-64 style) */
void op_speculative_load(unified_value_t *dst, const void *ptr, unified_value_t *nat_bit);

/***************************************************************************
 * ATOMIC OPERATIONS (for multi-processor systems)
 ***************************************************************************/

void op_atomic_add(unified_value_t *dst, void *ptr, const unified_value_t *value);
void op_atomic_sub(unified_value_t *dst, void *ptr, const unified_value_t *value);
void op_atomic_and(unified_value_t *dst, void *ptr, const unified_value_t *value);
void op_atomic_or(unified_value_t *dst, void *ptr, const unified_value_t *value);
void op_atomic_xor(unified_value_t *dst, void *ptr, const unified_value_t *value);
void op_atomic_swap(unified_value_t *dst, void *ptr, const unified_value_t *value);
void op_atomic_cas(unified_value_t *dst, void *ptr, const unified_value_t *expected, const unified_value_t *desired);
void op_atomic_min(unified_value_t *dst, void *ptr, const unified_value_t *value);
void op_atomic_max(unified_value_t *dst, void *ptr, const unified_value_t *value);

/***************************************************************************
 * MEMORY BARRIER / FENCE OPERATIONS
 ***************************************************************************/

void op_fence(void);
void op_fence_acquire(void);
void op_fence_release(void);
void op_fence_seq_cst(void);

/***************************************************************************
 * UTILITY FUNCTIONS
 ***************************************************************************/

/* Get default configurations for common systems */
word_config_t get_config_pdp1(void);       /* 18-bit, 1's complement */
word_config_t get_config_pdp6(void);       /* 36-bit, 1's complement */
word_config_t get_config_pdp10(void);      /* 36-bit, 2's complement */
word_config_t get_config_pdp11(void);      /* 16-bit, 2's complement */
word_config_t get_config_univac1100(void); /* 36-bit, 1's complement */
word_config_t get_config_univac2200(void); /* 36-bit, 1's complement */
word_config_t get_config_cdc6600(void);    /* 60-bit, 1's complement */
word_config_t get_config_vax(void);        /* 32-bit, 2's complement */
word_config_t get_config_ibm360(void);     /* 32-bit, 2's complement */
word_config_t get_config_modern_32(void);  /* 32-bit, 2's complement */
word_config_t get_config_modern_64(void);  /* 64-bit, 2's complement */

fp_config_t get_fp_config_ieee(void);
fp_config_t get_fp_config_vax(void);
fp_config_t get_fp_config_ibm(void);
fp_config_t get_fp_config_cray(void);

/* Word size mask generation */
uint64_t get_word_mask(uint32_t bits);

/* Arithmetic mode helpers */
int is_zero_in_mode(const unified_value_t *val);  /* Handles +0/-0 in 1's complement */
void normalize_value(unified_value_t *val);       /* Normalize representation */

#ifdef __cplusplus
}
#endif

#endif /* __LIBCPU_BACKEND_EMULATION_OPS_H__ */
