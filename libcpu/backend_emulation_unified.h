/*
 * libcpu Backend Emulation - Unified Operations
 *
 * Architecture-agnostic semantic operations that work on BOTH scalars and vectors.
 * Operations are defined by WHAT they do, not which CPU they came from.
 *
 * Design Principles:
 * 1. Every operation has scalar and vector variants
 * 2. No architecture-specific names (semantic names only)
 * 3. Operations defined by behavior, not encoding
 * 4. Extensible to any element type
 * 5. Predicates/masks supported throughout
 */

#ifndef __LIBCPU_BACKEND_EMULATION_UNIFIED_H__
#define __LIBCPU_BACKEND_EMULATION_UNIFIED_H__

#include "backend_emulation_types.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************
 * Unified Value Type - Works for both scalars and vectors
 ***************************************************************************/

typedef enum {
	VALUE_KIND_SCALAR,
	VALUE_KIND_VECTOR,
	VALUE_KIND_MATRIX,
	VALUE_KIND_PREDICATE
} value_kind_t;

typedef struct unified_value {
	value_kind_t kind;
	union {
		struct {
			vector_element_type_t type;
			union {
				uint64_t u64;
				int64_t i64;
				double f64;
				uint8_t bytes[16];  /* For larger scalars */
			} data;
		} scalar;
		vector_t *vector;
		matrix_tile_t *matrix;
		predicate_t *predicate;
	};
} unified_value_t;

/* Create values */
unified_value_t* uval_scalar_i64(int64_t value);
unified_value_t* uval_scalar_u64(uint64_t value);
unified_value_t* uval_scalar_f64(double value);
unified_value_t* uval_vector(const vector_type_desc_t *desc);
unified_value_t* uval_matrix(const matrix_tile_desc_t *desc);
unified_value_t* uval_predicate(uint32_t num_bits, int is_scalable);
void uval_free(unified_value_t *val);

/***************************************************************************
 * ARITHMETIC OPERATIONS
 * Work on both scalars and vectors transparently
 ***************************************************************************/

/* Basic arithmetic - element-wise for vectors */
void op_add(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_sub(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_mul(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_div(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_rem(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_abs(unified_value_t *dst, const unified_value_t *src);
void op_neg(unified_value_t *dst, const unified_value_t *src);

/* Min/max */
void op_min(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_max(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);

/* Saturating arithmetic */
void op_add_sat(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_sub_sat(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);

/* Multiply variants */
void op_mulhi(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);  /* High half */
void op_mullo(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);  /* Low half */

/* Average (rounded) */
void op_avg(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);

/* Fused multiply-add family */
void op_fma(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, const unified_value_t *c);   /* a*b+c */
void op_fms(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, const unified_value_t *c);   /* a*b-c */
void op_fnma(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, const unified_value_t *c);  /* -(a*b)+c */
void op_fnms(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, const unified_value_t *c);  /* -(a*b)-c */

/***************************************************************************
 * BITWISE OPERATIONS
 ***************************************************************************/

void op_and(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_or(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_xor(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_andn(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);  /* a & ~b */
void op_not(unified_value_t *dst, const unified_value_t *src);

/* Shifts */
void op_sll(unified_value_t *dst, const unified_value_t *a, const unified_value_t *shift);  /* Shift left logical */
void op_srl(unified_value_t *dst, const unified_value_t *a, const unified_value_t *shift);  /* Shift right logical */
void op_sra(unified_value_t *dst, const unified_value_t *a, const unified_value_t *shift);  /* Shift right arithmetic */
void op_rol(unified_value_t *dst, const unified_value_t *a, const unified_value_t *shift);  /* Rotate left */
void op_ror(unified_value_t *dst, const unified_value_t *a, const unified_value_t *shift);  /* Rotate right */

/* Shifts by immediate */
void op_slli(unified_value_t *dst, const unified_value_t *src, uint32_t shift);
void op_srli(unified_value_t *dst, const unified_value_t *src, uint32_t shift);
void op_srai(unified_value_t *dst, const unified_value_t *src, uint32_t shift);
void op_roli(unified_value_t *dst, const unified_value_t *src, uint32_t shift);
void op_rori(unified_value_t *dst, const unified_value_t *src, uint32_t shift);

/***************************************************************************
 * BYTE/BIT MANIPULATION OPERATIONS
 * Including ALPHA ZAP, ZAPNOT and similar useful operations
 ***************************************************************************/

/* Byte masking (from ALPHA ZAP, ZAPNOT) */
void op_byte_zap(unified_value_t *dst, const unified_value_t *src, uint8_t mask);
void op_byte_zapnot(unified_value_t *dst, const unified_value_t *src, uint8_t mask);

/* Extract bytes by mask */
void op_byte_extract(unified_value_t *dst, const unified_value_t *src, uint8_t mask);

/* Byte shuffle/permute */
void op_byte_shuffle(unified_value_t *dst, const unified_value_t *src, const uint8_t *pattern);

/* Byte swap */
void op_bswap(unified_value_t *dst, const unified_value_t *src);

/* Bit reversal */
void op_bitrev(unified_value_t *dst, const unified_value_t *src);

/* Bit field operations (from 68K, but generalized) */
void op_bitfield_extract(unified_value_t *dst, const unified_value_t *src, uint32_t offset, uint32_t width);
void op_bitfield_extract_signed(unified_value_t *dst, const unified_value_t *src, uint32_t offset, uint32_t width);
void op_bitfield_insert(unified_value_t *dst, const unified_value_t *base, const unified_value_t *src, uint32_t offset, uint32_t width);
void op_bitfield_set(unified_value_t *dst, const unified_value_t *src, uint32_t offset, uint32_t width);
void op_bitfield_clear(unified_value_t *dst, const unified_value_t *src, uint32_t offset, uint32_t width);
void op_bitfield_toggle(unified_value_t *dst, const unified_value_t *src, uint32_t offset, uint32_t width);
int op_bitfield_test(const unified_value_t *src, uint32_t offset, uint32_t width);

/* Bit scan (from x86 BSF, BSR, but generalized) */
void op_find_first_set(unified_value_t *dst, const unified_value_t *src);     /* Find first 1 bit */
void op_find_last_set(unified_value_t *dst, const unified_value_t *src);      /* Find last 1 bit */
void op_find_first_clear(unified_value_t *dst, const unified_value_t *src);   /* Find first 0 bit */

/* Bit counting (from x86, ARM, RISCV, but generalized) */
void op_popcnt(unified_value_t *dst, const unified_value_t *src);  /* Count 1 bits */
void op_clz(unified_value_t *dst, const unified_value_t *src);     /* Count leading zeros */
void op_ctz(unified_value_t *dst, const unified_value_t *src);     /* Count trailing zeros */
void op_parity(unified_value_t *dst, const unified_value_t *src);  /* Parity bit */

/* Bit deposit/extract (from x86 BMI2 PDEP/PEXT) */
void op_bit_deposit(unified_value_t *dst, const unified_value_t *src, const unified_value_t *mask);
void op_bit_extract(unified_value_t *dst, const unified_value_t *src, const unified_value_t *mask);

/***************************************************************************
 * COMPARISON OPERATIONS
 ***************************************************************************/

/* Comparisons return mask (all 1s or all 0s per element for vectors) */
void op_cmpeq(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_cmpne(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_cmplt(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_cmple(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_cmpgt(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_cmpge(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);

/* FP comparisons with ordering */
void op_cmpord(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_cmpunord(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);

/* Compare and set predicate (for SVE/RVV/AVX-512 style) */
void op_cmpeq_pred(unified_value_t *pred, const unified_value_t *a, const unified_value_t *b);
void op_cmplt_pred(unified_value_t *pred, const unified_value_t *a, const unified_value_t *b);

/* Min/max index (return index of min/max element) */
void op_minidx(unified_value_t *dst, const unified_value_t *src);
void op_maxidx(unified_value_t *dst, const unified_value_t *src);

/***************************************************************************
 * CONVERSION OPERATIONS
 ***************************************************************************/

/* Type conversions */
void op_convert(unified_value_t *dst, const unified_value_t *src);  /* Automatic based on types */

/* Integer <-> Float */
void op_cvt_i2f(unified_value_t *dst, const unified_value_t *src);
void op_cvt_f2i(unified_value_t *dst, const unified_value_t *src);
void op_cvt_f2i_trunc(unified_value_t *dst, const unified_value_t *src);
void op_cvt_f2i_floor(unified_value_t *dst, const unified_value_t *src);
void op_cvt_f2i_ceil(unified_value_t *dst, const unified_value_t *src);
void op_cvt_f2i_round(unified_value_t *dst, const unified_value_t *src);

/* Sign extension / zero extension */
void op_extend_signed(unified_value_t *dst, const unified_value_t *src);
void op_extend_unsigned(unified_value_t *dst, const unified_value_t *src);
void op_truncate(unified_value_t *dst, const unified_value_t *src);

/* Saturation */
void op_saturate(unified_value_t *dst, const unified_value_t *src, int64_t min, int64_t max);

/***************************************************************************
 * PACK/UNPACK OPERATIONS
 ***************************************************************************/

/* Pack (narrow) - two inputs to one output */
void op_pack(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_pack_sat_signed(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_pack_sat_unsigned(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);

/* Unpack (widen) - one input to one output */
void op_unpack_low(unified_value_t *dst, const unified_value_t *src);
void op_unpack_high(unified_value_t *dst, const unified_value_t *src);

/* Interleave */
void op_interleave_low(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_interleave_high(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);

/***************************************************************************
 * SHUFFLE/PERMUTE/SELECT OPERATIONS
 ***************************************************************************/

/* General shuffle with index vector/scalar */
void op_shuffle(unified_value_t *dst, const unified_value_t *src, const unified_value_t *indices);
void op_shuffle2(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, const unified_value_t *indices);

/* Permute (single source) */
void op_permute(unified_value_t *dst, const unified_value_t *src, const unified_value_t *control);

/* Blend/select with mask */
void op_blend(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, const unified_value_t *mask);
void op_select(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, const unified_value_t *cond);

/* Extract/insert element */
void op_extract_element(unified_value_t *dst, const unified_value_t *src, uint32_t index);
void op_insert_element(unified_value_t *dst, const unified_value_t *vec, const unified_value_t *elem, uint32_t index);

/* Broadcast/splat scalar to all elements */
void op_broadcast(unified_value_t *dst, const unified_value_t *scalar);
void op_splat(unified_value_t *dst, const unified_value_t *vec, uint32_t index);

/* Reverse elements */
void op_reverse(unified_value_t *dst, const unified_value_t *src);

/* Rotate elements */
void op_rotate_elements(unified_value_t *dst, const unified_value_t *src, int32_t count);

/* Concatenate and extract (alignr-style) */
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

/***************************************************************************
 * HORIZONTAL OPERATIONS
 ***************************************************************************/

void op_hadd(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_hsub(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_hadds(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);  /* Saturating */
void op_hsubs(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);

/***************************************************************************
 * DOT PRODUCT OPERATIONS
 ***************************************************************************/

void op_dot(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_dot4(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);  /* 4-element dot */

/***************************************************************************
 * FLOATING-POINT MATH OPERATIONS
 ***************************************************************************/

/* Rounding */
void op_sqrt(unified_value_t *dst, const unified_value_t *src);
void op_rsqrt(unified_value_t *dst, const unified_value_t *src);  /* Reciprocal sqrt */
void op_rcp(unified_value_t *dst, const unified_value_t *src);    /* Reciprocal */
void op_ceil(unified_value_t *dst, const unified_value_t *src);
void op_floor(unified_value_t *dst, const unified_value_t *src);
void op_trunc(unified_value_t *dst, const unified_value_t *src);
void op_round(unified_value_t *dst, const unified_value_t *src);
void op_rint(unified_value_t *dst, const unified_value_t *src);

/* Transcendental */
void op_sin(unified_value_t *dst, const unified_value_t *src);
void op_cos(unified_value_t *dst, const unified_value_t *src);
void op_tan(unified_value_t *dst, const unified_value_t *src);
void op_asin(unified_value_t *dst, const unified_value_t *src);
void op_acos(unified_value_t *dst, const unified_value_t *src);
void op_atan(unified_value_t *dst, const unified_value_t *src);
void op_atan2(unified_value_t *dst, const unified_value_t *y, const unified_value_t *x);

void op_exp(unified_value_t *dst, const unified_value_t *src);
void op_exp2(unified_value_t *dst, const unified_value_t *src);
void op_exp10(unified_value_t *dst, const unified_value_t *src);
void op_expm1(unified_value_t *dst, const unified_value_t *src);  /* exp(x) - 1 */

void op_log(unified_value_t *dst, const unified_value_t *src);
void op_log2(unified_value_t *dst, const unified_value_t *src);
void op_log10(unified_value_t *dst, const unified_value_t *src);
void op_log1p(unified_value_t *dst, const unified_value_t *src);  /* log(1 + x) */

void op_pow(unified_value_t *dst, const unified_value_t *base, const unified_value_t *exp);
void op_hypot(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);  /* sqrt(a²+b²) */
void op_cbrt(unified_value_t *dst, const unified_value_t *src);  /* Cube root */

/* Sign manipulation */
void op_copysign(unified_value_t *dst, const unified_value_t *mag, const unified_value_t *sgn);
void op_signbit(unified_value_t *dst, const unified_value_t *src);

/***************************************************************************
 * PREDICATED OPERATIONS (SVE/RVV/AVX-512 style)
 ***************************************************************************/

/* Execute operation only where predicate is true */
void op_add_pred(unified_value_t *dst, const unified_value_t *pred, const unified_value_t *a, const unified_value_t *b);
void op_mul_pred(unified_value_t *dst, const unified_value_t *pred, const unified_value_t *a, const unified_value_t *b);
void op_load_pred(unified_value_t *dst, const unified_value_t *pred, const void *ptr);
void op_store_pred(const unified_value_t *src, const unified_value_t *pred, void *ptr);

/* Predicate operations */
void op_pred_and(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_pred_or(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_pred_xor(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_pred_not(unified_value_t *dst, const unified_value_t *src);
void op_pred_count(unified_value_t *dst, const unified_value_t *pred);  /* Count true bits */

/* Predicate creation */
void op_pred_while_lt(unified_value_t *pred, uint64_t start, uint64_t end);
void op_pred_all_true(unified_value_t *pred);
void op_pred_all_false(unified_value_t *pred);

/***************************************************************************
 * LOAD/STORE OPERATIONS
 ***************************************************************************/

void op_load(unified_value_t *dst, const void *ptr);
void op_store(const unified_value_t *src, void *ptr);
void op_load_splat(unified_value_t *dst, const void *ptr);  /* Load scalar and broadcast */

/* Gather/scatter */
void op_gather(unified_value_t *dst, const void *base, const unified_value_t *indices, uint32_t scale);
void op_scatter(const unified_value_t *src, void *base, const unified_value_t *indices, uint32_t scale);

/***************************************************************************
 * STRING/BLOCK OPERATIONS (semantic versions)
 ***************************************************************************/

/* Block move/fill */
void op_block_move(void *dst, const void *src, size_t count);
void op_block_fill(void *dst, const unified_value_t *value, size_t count);
void op_block_compare(unified_value_t *result, const void *a, const void *b, size_t count);

/* String search */
void op_string_search(unified_value_t *result, const void *str, const unified_value_t *chr, size_t count);
void op_string_search_not(unified_value_t *result, const void *str, const unified_value_t *chr, size_t count);

/***************************************************************************
 * BCD OPERATIONS (semantic versions)
 ***************************************************************************/

void op_bcd_add(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_bcd_sub(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b);
void op_bcd_adjust_add(unified_value_t *dst, const unified_value_t *src);  /* Decimal adjust */

/***************************************************************************
 * FIXED-POINT OPERATIONS (semantic versions)
 ***************************************************************************/

void op_fixed_add(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, uint32_t fraction_bits);
void op_fixed_mul(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, uint32_t fraction_bits);
void op_fixed_div(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, uint32_t fraction_bits);

/***************************************************************************
 * CONDITIONAL MOVE (semantic versions)
 ***************************************************************************/

void op_select_eq(unified_value_t *dst, const unified_value_t *true_val, const unified_value_t *false_val,
                  const unified_value_t *a, const unified_value_t *b);
void op_select_ne(unified_value_t *dst, const unified_value_t *true_val, const unified_value_t *false_val,
                  const unified_value_t *a, const unified_value_t *b);
void op_select_lt(unified_value_t *dst, const unified_value_t *true_val, const unified_value_t *false_val,
                  const unified_value_t *a, const unified_value_t *b);
void op_select_le(unified_value_t *dst, const unified_value_t *true_val, const unified_value_t *false_val,
                  const unified_value_t *a, const unified_value_t *b);
void op_select_gt(unified_value_t *dst, const unified_value_t *true_val, const unified_value_t *false_val,
                  const unified_value_t *a, const unified_value_t *b);
void op_select_ge(unified_value_t *dst, const unified_value_t *true_val, const unified_value_t *false_val,
                  const unified_value_t *a, const unified_value_t *b);

/***************************************************************************
 * CRYPTO OPERATIONS (semantic versions)
 ***************************************************************************/

void op_aes_encrypt(unified_value_t *dst, const unified_value_t *state, const unified_value_t *key);
void op_aes_decrypt(unified_value_t *dst, const unified_value_t *state, const unified_value_t *key);
void op_aes_keygen(unified_value_t *dst, const unified_value_t *key, uint8_t round);

void op_sha256_update(unified_value_t *dst, const unified_value_t *state, const unified_value_t *data);
void op_crc32_update(unified_value_t *dst, const unified_value_t *crc, const unified_value_t *data);

/***************************************************************************
 * MATRIX OPERATIONS (semantic versions)
 ***************************************************************************/

void op_matrix_mul(unified_value_t *c, const unified_value_t *a, const unified_value_t *b);
void op_matrix_mul_acc(unified_value_t *c, const unified_value_t *a, const unified_value_t *b);  /* C += A*B */
void op_matrix_transpose(unified_value_t *dst, const unified_value_t *src);

/***************************************************************************
 * UTILITY FUNCTIONS
 ***************************************************************************/

/* Set all elements to value */
void op_set_all(unified_value_t *dst, const unified_value_t *scalar);
void op_set_zero(unified_value_t *dst);

/* Move mask to integer (extract comparison results) */
uint64_t op_movemask(const unified_value_t *src);

/* Test operations */
int op_test_all_zeros(const unified_value_t *a, const unified_value_t *b);
int op_test_all_ones(const unified_value_t *a, const unified_value_t *b);

/***************************************************************************
 * ARCHITECTURE MAPPING UTILITIES
 *
 * These help map architecture-specific operations to unified ops
 ***************************************************************************/

/* Map ALPHA operations */
static inline void alpha_zap(unified_value_t *dst, const unified_value_t *src, uint8_t mask) {
	op_byte_zap(dst, src, mask);
}

static inline void alpha_zapnot(unified_value_t *dst, const unified_value_t *src, uint8_t mask) {
	op_byte_zapnot(dst, src, mask);
}

/* Map x86 operations */
static inline void x86_paddb(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	op_add(dst, a, b);
}

static inline void x86_psubb(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	op_sub(dst, a, b);
}

static inline void x86_pshufb(unified_value_t *dst, const unified_value_t *src, const unified_value_t *control) {
	op_permute(dst, src, control);
}

/* Map ARM NEON operations */
static inline void neon_vaddq(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	op_add(dst, a, b);
}

static inline void neon_vsubq(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	op_sub(dst, a, b);
}

/* Map SPARC VIS operations */
static inline void vis_fpack16(unified_value_t *dst, const unified_value_t *src) {
	op_pack_sat_unsigned(dst, src, src);
}

/* Map VAX operations */
static inline void vax_movc3(void *dst, const void *src, size_t count) {
	op_block_move(dst, src, count);
}

/* Map 68K operations */
static inline void m68k_bfextu(unified_value_t *dst, const unified_value_t *src, uint32_t offset, uint32_t width) {
	op_bitfield_extract(dst, src, offset, width);
}

/* Map Z80 operations */
static inline void z80_ldir(void *dst, const void *src, uint16_t count) {
	op_block_move(dst, src, count);
}

#ifdef __cplusplus
}
#endif

#endif /* __LIBCPU_BACKEND_EMULATION_UNIFIED_H__ */
