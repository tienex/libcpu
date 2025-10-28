/*
 * libcpu Backend Emulation - Vector Operations API
 *
 * Semantic vector operations covering all modern SIMD ISAs:
 * x86: MMX, SSE-SSE4.2, AVX, AVX2, AVX-512
 * ARM: NEON, SVE, SVE2
 * RISC-V: RVV 1.0
 * PowerPC: VMX (AltiVec), VSX
 * MIPS/Loongson: MSA, LSX, LASX
 *
 * All operations work on generic vector_t types, not ISA-specific types.
 * This provides SEMANTIC emulation without reimplementing ISAs.
 */

#ifndef __LIBCPU_BACKEND_EMULATION_VECTOR_H__
#define __LIBCPU_BACKEND_EMULATION_VECTOR_H__

#include "backend_emulation_types.h"
#include "backend_emulation_builtins.h"

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************
 * Arithmetic Operations
 ***************************************************************************/

/* Basic arithmetic - element-wise */
void vec_add(vector_t *dst, const vector_t *a, const vector_t *b);
void vec_sub(vector_t *dst, const vector_t *a, const vector_t *b);
void vec_mul(vector_t *dst, const vector_t *a, const vector_t *b);
void vec_div(vector_t *dst, const vector_t *a, const vector_t *b);
void vec_abs(vector_t *dst, const vector_t *src);
void vec_neg(vector_t *dst, const vector_t *src);

/* Min/max */
void vec_min(vector_t *dst, const vector_t *a, const vector_t *b);
void vec_max(vector_t *dst, const vector_t *a, const vector_t *b);

/* Saturating arithmetic */
void vec_adds_sat(vector_t *dst, const vector_t *a, const vector_t *b);  /* Signed */
void vec_addu_sat(vector_t *dst, const vector_t *a, const vector_t *b);  /* Unsigned */
void vec_subs_sat(vector_t *dst, const vector_t *a, const vector_t *b);
void vec_subu_sat(vector_t *dst, const vector_t *a, const vector_t *b);

/* Multiply variants */
void vec_mulhi(vector_t *dst, const vector_t *a, const vector_t *b);  /* High half */
void vec_mullo(vector_t *dst, const vector_t *a, const vector_t *b);  /* Low half */

/* Average (rounded) */
void vec_avg(vector_t *dst, const vector_t *a, const vector_t *b);

/***************************************************************************
 * Fused Multiply-Add
 ***************************************************************************/

void vec_fma(vector_t *dst, const vector_t *a, const vector_t *b, const vector_t *c);   /* a*b+c */
void vec_fms(vector_t *dst, const vector_t *a, const vector_t *b, const vector_t *c);   /* a*b-c */
void vec_fnma(vector_t *dst, const vector_t *a, const vector_t *b, const vector_t *c);  /* -(a*b)+c */
void vec_fnms(vector_t *dst, const vector_t *a, const vector_t *b, const vector_t *c);  /* -(a*b)-c */

/* Multiply-add (integer) */
void vec_madd(vector_t *dst, const vector_t *a, const vector_t *b, const vector_t *c);
void vec_msub(vector_t *dst, const vector_t *a, const vector_t *b, const vector_t *c);

/***************************************************************************
 * Horizontal Operations
 ***************************************************************************/

/* Horizontal add: [a0+a1, a2+a3, ...] */
void vec_hadd(vector_t *dst, const vector_t *a, const vector_t *b);
void vec_hsub(vector_t *dst, const vector_t *a, const vector_t *b);
void vec_hadds(vector_t *dst, const vector_t *a, const vector_t *b);  /* Saturating */
void vec_hsubs(vector_t *dst, const vector_t *a, const vector_t *b);

/***************************************************************************
 * Reduction Operations (return scalar)
 ***************************************************************************/

/* Reduce all elements to single value */
void vec_reduce_add(void *result, const vector_t *vec);
void vec_reduce_mul(void *result, const vector_t *vec);
void vec_reduce_min(void *result, const vector_t *vec);
void vec_reduce_max(void *result, const vector_t *vec);
void vec_reduce_and(void *result, const vector_t *vec);
void vec_reduce_or(void *result, const vector_t *vec);
void vec_reduce_xor(void *result, const vector_t *vec);

/***************************************************************************
 * Dot Products
 ***************************************************************************/

/* Dot product of two vectors */
void vec_dot(void *result, const vector_t *a, const vector_t *b);

/* Configurable dot product (AVX style) */
void vec_dp(vector_t *dst, const vector_t *a, const vector_t *b, uint8_t mask);

/***************************************************************************
 * Bitwise Operations
 ***************************************************************************/

void vec_and(vector_t *dst, const vector_t *a, const vector_t *b);
void vec_or(vector_t *dst, const vector_t *a, const vector_t *b);
void vec_xor(vector_t *dst, const vector_t *a, const vector_t *b);
void vec_andn(vector_t *dst, const vector_t *a, const vector_t *b);  /* a & ~b */
void vec_not(vector_t *dst, const vector_t *src);

/* Shifts (element-wise) */
void vec_sll(vector_t *dst, const vector_t *a, const vector_t *shift);  /* Logical left */
void vec_srl(vector_t *dst, const vector_t *a, const vector_t *shift);  /* Logical right */
void vec_sra(vector_t *dst, const vector_t *a, const vector_t *shift);  /* Arithmetic right */

/* Shifts by immediate */
void vec_slli(vector_t *dst, const vector_t *src, int shift);
void vec_srli(vector_t *dst, const vector_t *src, int shift);
void vec_srai(vector_t *dst, const vector_t *src, int shift);

/* Rotates */
void vec_rol(vector_t *dst, const vector_t *a, const vector_t *shift);
void vec_ror(vector_t *dst, const vector_t *a, const vector_t *shift);
void vec_roli(vector_t *dst, const vector_t *src, int shift);
void vec_rori(vector_t *dst, const vector_t *src, int shift);

/***************************************************************************
 * Comparison Operations
 ***************************************************************************/

/* Comparisons return mask (all 1s or all 0s per element) */
void vec_cmpeq(vector_t *dst, const vector_t *a, const vector_t *b);
void vec_cmpne(vector_t *dst, const vector_t *a, const vector_t *b);
void vec_cmplt(vector_t *dst, const vector_t *a, const vector_t *b);
void vec_cmple(vector_t *dst, const vector_t *a, const vector_t *b);
void vec_cmpgt(vector_t *dst, const vector_t *a, const vector_t *b);
void vec_cmpge(vector_t *dst, const vector_t *a, const vector_t *b);

/* FP comparisons with ordering */
void vec_cmpord(vector_t *dst, const vector_t *a, const vector_t *b);
void vec_cmpunord(vector_t *dst, const vector_t *a, const vector_t *b);

/* Predicate comparisons (for SVE/AVX-512 style) */
void vec_cmpeq_pred(predicate_t *pred, const vector_t *a, const vector_t *b);
void vec_cmplt_pred(predicate_t *pred, const vector_t *a, const vector_t *b);

/***************************************************************************
 * Conversion Operations
 ***************************************************************************/

/* Integer <-> Float */
void vec_cvti2f(vector_t *dst, const vector_t *src);
void vec_cvtf2i(vector_t *dst, const vector_t *src);
void vec_cvtf2i_trunc(vector_t *dst, const vector_t *src);
void vec_cvtf2i_round(vector_t *dst, const vector_t *src);
void vec_cvtf2i_floor(vector_t *dst, const vector_t *src);
void vec_cvtf2i_ceil(vector_t *dst, const vector_t *src);

/* Float precision conversions */
void vec_cvtps2pd(vector_t *dst, const vector_t *src);  /* f32 -> f64 */
void vec_cvtpd2ps(vector_t *dst, const vector_t *src);  /* f64 -> f32 */
void vec_cvtph2ps(vector_t *dst, const vector_t *src);  /* fp16 -> f32 */
void vec_cvtps2ph(vector_t *dst, const vector_t *src, int rounding);  /* f32 -> fp16 */

/* BFloat16 conversions */
void vec_cvtbf16_f32(vector_t *dst, const vector_t *src);
void vec_cvtf32_bf16(vector_t *dst, const vector_t *src);

/* FP8 conversions */
void vec_cvtfp8_f32(vector_t *dst, const vector_t *src);
void vec_cvtf32_fp8(vector_t *dst, const vector_t *src);

/***************************************************************************
 * Pack/Unpack Operations
 ***************************************************************************/

/* Pack (narrow two vectors into one) */
void vec_pack(vector_t *dst, const vector_t *a, const vector_t *b);
void vec_packus(vector_t *dst, const vector_t *a, const vector_t *b);  /* Unsigned saturate */
void vec_packss(vector_t *dst, const vector_t *a, const vector_t *b);  /* Signed saturate */

/* Unpack (widen half of vector) */
void vec_unpacklo(vector_t *dst, const vector_t *src);
void vec_unpackhi(vector_t *dst, const vector_t *src);

/* Extend (sign/zero extend) */
void vec_extend_low_s(vector_t *dst, const vector_t *src);   /* Sign extend low */
void vec_extend_high_s(vector_t *dst, const vector_t *src);  /* Sign extend high */
void vec_extend_low_z(vector_t *dst, const vector_t *src);   /* Zero extend low */
void vec_extend_high_z(vector_t *dst, const vector_t *src);  /* Zero extend high */

/***************************************************************************
 * Shuffle/Permute Operations
 ***************************************************************************/

/* General shuffle with index vector */
void vec_shuffle(vector_t *dst, const vector_t *src, const vector_t *indices);
void vec_shuffle2(vector_t *dst, const vector_t *a, const vector_t *b, const vector_t *indices);

/* Permute (single vector) */
void vec_permute(vector_t *dst, const vector_t *src, const vector_t *control);

/* Blend with mask */
void vec_blend(vector_t *dst, const vector_t *a, const vector_t *b, const vector_t *mask);

/* Select with predicate */
void vec_select(vector_t *dst, const vector_t *a, const vector_t *b, const predicate_t *pred);

/* Extract/insert single element */
void vec_extract(void *result, const vector_t *vec, uint32_t index);
void vec_insert(vector_t *dst, const vector_t *src, const void *value, uint32_t index);

/* Broadcast/splat */
void vec_broadcast(vector_t *dst, const void *scalar);
void vec_splat(vector_t *dst, const vector_t *src, uint32_t index);

/* Byte shuffle */
void vec_pshufb(vector_t *dst, const vector_t *src, const vector_t *control);

/* Packed shuffles */
void vec_shufps(vector_t *dst, const vector_t *a, const vector_t *b, uint8_t imm);
void vec_shufpd(vector_t *dst, const vector_t *a, const vector_t *b, uint8_t imm);

/* Align (concatenate and extract) */
void vec_alignr(vector_t *dst, const vector_t *a, const vector_t *b, int count);

/***************************************************************************
 * Load/Store Operations
 ***************************************************************************/

/* Aligned/unaligned loads */
void vec_load(vector_t *dst, const void *ptr);
void vec_loadu(vector_t *dst, const void *ptr);
void vec_store(const vector_t *src, void *ptr);
void vec_storeu(const vector_t *src, void *ptr);

/* Load and splat */
void vec_load_splat(vector_t *dst, const void *ptr);

/* Load with zero extend */
void vec_load_zero(vector_t *dst, const void *ptr, uint32_t bytes);

/* Non-temporal (streaming) */
void vec_stream_load(vector_t *dst, const void *ptr);
void vec_stream_store(const vector_t *src, void *ptr);

/* Gather/scatter */
void vec_gather(vector_t *dst, const void *base, const vector_t *indices, int scale);
void vec_scatter(const vector_t *src, void *base, const vector_t *indices, int scale);

/* Masked (predicated) load/store */
void vec_masked_load(vector_t *dst, const void *ptr, const predicate_t *pred);
void vec_masked_store(const vector_t *src, void *ptr, const predicate_t *pred);

/***************************************************************************
 * FP Math Operations
 ***************************************************************************/

void vec_sqrt(vector_t *dst, const vector_t *src);
void vec_rsqrt(vector_t *dst, const vector_t *src);  /* Reciprocal sqrt */
void vec_rcp(vector_t *dst, const vector_t *src);    /* Reciprocal */

/* Rounding */
void vec_ceil(vector_t *dst, const vector_t *src);
void vec_floor(vector_t *dst, const vector_t *src);
void vec_trunc(vector_t *dst, const vector_t *src);
void vec_round(vector_t *dst, const vector_t *src);
void vec_rint(vector_t *dst, const vector_t *src);

/* Transcendental */
void vec_sin(vector_t *dst, const vector_t *src);
void vec_cos(vector_t *dst, const vector_t *src);
void vec_tan(vector_t *dst, const vector_t *src);
void vec_exp(vector_t *dst, const vector_t *src);
void vec_exp2(vector_t *dst, const vector_t *src);
void vec_log(vector_t *dst, const vector_t *src);
void vec_log2(vector_t *dst, const vector_t *src);
void vec_log10(vector_t *dst, const vector_t *src);
void vec_pow(vector_t *dst, const vector_t *a, const vector_t *b);
void vec_cbrt(vector_t *dst, const vector_t *src);

/***************************************************************************
 * Integer Bit Operations
 ***************************************************************************/

void vec_popcnt(vector_t *dst, const vector_t *src);
void vec_clz(vector_t *dst, const vector_t *src);
void vec_ctz(vector_t *dst, const vector_t *src);
void vec_bswap(vector_t *dst, const vector_t *src);
void vec_bitrev(vector_t *dst, const vector_t *src);
void vec_parity(vector_t *dst, const vector_t *src);

/***************************************************************************
 * Crypto Operations
 ***************************************************************************/

/* AES */
void vec_aes_enc(vector_t *dst, const vector_t *state, const vector_t *key);
void vec_aes_dec(vector_t *dst, const vector_t *state, const vector_t *key);
void vec_aes_enc_last(vector_t *dst, const vector_t *state, const vector_t *key);
void vec_aes_dec_last(vector_t *dst, const vector_t *state, const vector_t *key);
void vec_aes_imc(vector_t *dst, const vector_t *src);
void vec_aes_keygen(vector_t *dst, const vector_t *key, uint8_t rcon);

/* SHA */
void vec_sha1_msg1(vector_t *dst, const vector_t *src);
void vec_sha1_msg2(vector_t *dst, const vector_t *src);
void vec_sha1_nexte(vector_t *dst, const vector_t *src);
void vec_sha256_msg1(vector_t *dst, const vector_t *src);
void vec_sha256_msg2(vector_t *dst, const vector_t *src);

/* CRC */
void vec_crc32(uint32_t *dst, uint32_t crc, const vector_t *data);

/* Carry-less multiply */
void vec_pclmulqdq(vector_t *dst, const vector_t *a, const vector_t *b, uint8_t imm);

/***************************************************************************
 * SVE/RVV Specific Operations
 ***************************************************************************/

/* Predicate creation */
void vec_pred_while_lt(predicate_t *pred, uint64_t start, uint64_t end);
void vec_pred_ptrue(predicate_t *pred);
void vec_pred_pfalse(predicate_t *pred);

/* Scalable vector operations */
uint64_t vec_sve_len(const vector_t *vec);  /* Get actual vector length */
void vec_sve_compact(vector_t *dst, const vector_t *src, const predicate_t *pred);
void vec_sve_splice(vector_t *dst, const predicate_t *pred, const vector_t *a, const vector_t *b);
void vec_sve_rev(vector_t *dst, const vector_t *src);
void vec_sve_tbl(vector_t *dst, const vector_t *src, const vector_t *indices);

/***************************************************************************
 * Utility Functions
 ***************************************************************************/

/* Initialize vector with value */
void vec_set1(vector_t *vec, const void *scalar);
void vec_setzero(vector_t *vec);
void vec_undefined(vector_t *vec);

/* Move mask to integer */
uint64_t vec_movemask(const vector_t *vec);

/* Test operations */
int vec_testz(const vector_t *a, const vector_t *b);   /* All zeros */
int vec_testc(const vector_t *a, const vector_t *b);   /* All ones */
int vec_testnzc(const vector_t *a, const vector_t *b); /* Mixed */

#ifdef __cplusplus
}
#endif

#endif /* __LIBCPU_BACKEND_EMULATION_VECTOR_H__ */
