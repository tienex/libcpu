/*
 * libcpu Backend Emulation - Exotic Number Systems and Architectures
 *
 * Support for unusual number representations and specialized architectures:
 * - IEEE 754-2008 Decimal Floating Point (DPD and BID)
 * - VAX, IBM, Cray floating point formats
 * - 1's complement arithmetic (PDP-1, CDC, UNIVAC)
 * - Non-power-of-2 word sizes (PDP-10 36-bit, CDC 60-bit)
 * - RVV 0.9 (pre-1.0 RISC-V vector)
 * - WMMX (Wireless MMX)
 * - MIPS MSA2, Loongson LMMX
 * - ARM FPA (original ARM floating point)
 */

#ifndef __LIBCPU_BACKEND_EMULATION_EXOTIC_H__
#define __LIBCPU_BACKEND_EMULATION_EXOTIC_H__

#include "backend_emulation_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************
 * IEEE 754-2008 DECIMAL FLOATING POINT
 ***************************************************************************/

/* Decimal encoding formats */
typedef enum {
	DEC_FORMAT_DPD,    /* Densely Packed Decimal (hardware encoding) */
	DEC_FORMAT_BID     /* Binary Integer Decimal (software friendly) */
} decimal_format_t;

/* Decimal32 (7 decimal digits) */
typedef struct {
	decimal_format_t format;
	union {
		uint32_t dpd;  /* DPD encoding */
		uint32_t bid;  /* BID encoding */
	};
} decimal32_t;

/* Decimal64 (16 decimal digits) */
typedef struct {
	decimal_format_t format;
	union {
		uint64_t dpd;
		uint64_t bid;
	};
} decimal64_t;

/* Decimal128 (34 decimal digits) */
typedef struct {
	decimal_format_t format;
	union {
		struct { uint64_t lo; uint64_t hi; } dpd;
		struct { uint64_t lo; uint64_t hi; } bid;
	};
} decimal128_t;

/* Decimal FP operations (IEEE 754-2008 compliant) */
void dec32_add(decimal32_t *result, const decimal32_t *a, const decimal32_t *b);
void dec32_sub(decimal32_t *result, const decimal32_t *a, const decimal32_t *b);
void dec32_mul(decimal32_t *result, const decimal32_t *a, const decimal32_t *b);
void dec32_div(decimal32_t *result, const decimal32_t *a, const decimal32_t *b);
void dec32_fma(decimal32_t *result, const decimal32_t *a, const decimal32_t *b, const decimal32_t *c);

void dec64_add(decimal64_t *result, const decimal64_t *a, const decimal64_t *b);
void dec64_sub(decimal64_t *result, const decimal64_t *a, const decimal64_t *b);
void dec64_mul(decimal64_t *result, const decimal64_t *a, const decimal64_t *b);
void dec64_div(decimal64_t *result, const decimal64_t *a, const decimal64_t *b);
void dec64_fma(decimal64_t *result, const decimal64_t *a, const decimal64_t *b, const decimal64_t *c);

void dec128_add(decimal128_t *result, const decimal128_t *a, const decimal128_t *b);
void dec128_sub(decimal128_t *result, const decimal128_t *a, const decimal128_t *b);
void dec128_mul(decimal128_t *result, const decimal128_t *a, const decimal128_t *b);
void dec128_div(decimal128_t *result, const decimal128_t *a, const decimal128_t *b);
void dec128_fma(decimal128_t *result, const decimal128_t *a, const decimal128_t *b, const decimal128_t *c);

/* Decimal <-> Binary conversions */
void dec32_to_float(float *result, const decimal32_t *dec);
void float_to_dec32(decimal32_t *result, float f);
void dec64_to_double(double *result, const decimal64_t *dec);
void double_to_dec64(decimal64_t *result, double d);

/* DPD <-> BID conversions */
void dec32_dpd_to_bid(decimal32_t *result, const decimal32_t *dpd);
void dec32_bid_to_dpd(decimal32_t *result, const decimal32_t *bid);

/* Quantum operations (IEEE 754-2008) */
void dec64_quantize(decimal64_t *result, const decimal64_t *a, const decimal64_t *b);
void dec64_quantum(decimal64_t *result, const decimal64_t *a);

/***************************************************************************
 * VAX FLOATING POINT (1977-2000)
 ***************************************************************************/

/* VAX F_floating (32-bit, similar to IEEE single but different) */
typedef struct {
	uint32_t bits;  /* 1 sign, 8 exponent (bias 128), 23 mantissa (no implicit bit) */
} vax_f_float_t;

/* VAX D_floating (64-bit) */
typedef struct {
	uint64_t bits;  /* 1 sign, 8 exponent (bias 128), 55 mantissa */
} vax_d_float_t;

/* VAX G_floating (64-bit, extended range) */
typedef struct {
	uint64_t bits;  /* 1 sign, 11 exponent (bias 1024), 52 mantissa */
} vax_g_float_t;

/* VAX H_floating (128-bit) */
typedef struct {
	uint64_t lo;
	uint64_t hi;    /* 1 sign, 15 exponent (bias 16384), 112 mantissa */
} vax_h_float_t;

/* VAX FP operations */
void vax_f_add(vax_f_float_t *result, const vax_f_float_t *a, const vax_f_float_t *b);
void vax_f_sub(vax_f_float_t *result, const vax_f_float_t *a, const vax_f_float_t *b);
void vax_f_mul(vax_f_float_t *result, const vax_f_float_t *a, const vax_f_float_t *b);
void vax_f_div(vax_f_float_t *result, const vax_f_float_t *a, const vax_f_float_t *b);

void vax_d_add(vax_d_float_t *result, const vax_d_float_t *a, const vax_d_float_t *b);
void vax_d_mul(vax_d_float_t *result, const vax_d_float_t *a, const vax_d_float_t *b);
void vax_g_add(vax_g_float_t *result, const vax_g_float_t *a, const vax_g_float_t *b);
void vax_g_mul(vax_g_float_t *result, const vax_g_float_t *a, const vax_g_float_t *b);

/* VAX <-> IEEE conversions */
void vax_f_to_ieee_float(float *result, const vax_f_float_t *vax);
void ieee_float_to_vax_f(vax_f_float_t *result, float ieee);
void vax_d_to_ieee_double(double *result, const vax_d_float_t *vax);
void ieee_double_to_vax_d(vax_d_float_t *result, double ieee);

/***************************************************************************
 * IBM HEXADECIMAL FLOATING POINT (1960s-1990s)
 ***************************************************************************/

/* IBM short (32-bit) */
typedef struct {
	uint32_t bits;  /* 1 sign, 7 exponent (base 16, bias 64), 24 mantissa */
} ibm_short_float_t;

/* IBM long (64-bit) */
typedef struct {
	uint64_t bits;  /* 1 sign, 7 exponent (base 16, bias 64), 56 mantissa */
} ibm_long_float_t;

/* IBM extended (128-bit) - two 64-bit halves */
typedef struct {
	ibm_long_float_t hi;
	ibm_long_float_t lo;
} ibm_extended_float_t;

/* IBM FP operations */
void ibm_short_add(ibm_short_float_t *result, const ibm_short_float_t *a, const ibm_short_float_t *b);
void ibm_short_mul(ibm_short_float_t *result, const ibm_short_float_t *a, const ibm_short_float_t *b);
void ibm_long_add(ibm_long_float_t *result, const ibm_long_float_t *a, const ibm_long_float_t *b);
void ibm_long_mul(ibm_long_float_t *result, const ibm_long_float_t *a, const ibm_long_float_t *b);

/* IBM <-> IEEE conversions */
void ibm_short_to_ieee_float(float *result, const ibm_short_float_t *ibm);
void ieee_float_to_ibm_short(ibm_short_float_t *result, float ieee);
void ibm_long_to_ieee_double(double *result, const ibm_long_float_t *ibm);
void ieee_double_to_ibm_long(ibm_long_float_t *result, double ieee);

/***************************************************************************
 * CRAY FLOATING POINT (1976-2000s)
 ***************************************************************************/

/* Cray used unusual formats optimized for vector processing */

/* Cray 64-bit floating point (no exponent bias, different layout) */
typedef struct {
	uint64_t bits;  /* 1 sign, 15 exponent (base 2, bias 16384), 48 mantissa */
} cray_float_t;

/* Cray FP operations */
void cray_add(cray_float_t *result, const cray_float_t *a, const cray_float_t *b);
void cray_mul(cray_float_t *result, const cray_float_t *a, const cray_float_t *b);
void cray_reciprocal(cray_float_t *result, const cray_float_t *a);

/* Cray <-> IEEE conversions */
void cray_to_ieee_double(double *result, const cray_float_t *cray);
void ieee_double_to_cray(cray_float_t *result, double ieee);

/***************************************************************************
 * 1'S COMPLEMENT ARITHMETIC (PDP-1, CDC, UNIVAC)
 ***************************************************************************/

/* In 1's complement, negative numbers are bitwise NOT of positive
 * This results in two representations of zero: +0 and -0
 */

typedef struct {
	uint64_t bits;     /* Raw bit representation */
	int is_negative;   /* For determining which zero */
} ones_comp_t;

/* 1's complement operations */
void ones_comp_add(ones_comp_t *result, const ones_comp_t *a, const ones_comp_t *b);
void ones_comp_sub(ones_comp_t *result, const ones_comp_t *a, const ones_comp_t *b);
void ones_comp_mul(ones_comp_t *result, const ones_comp_t *a, const ones_comp_t *b);
void ones_comp_div(ones_comp_t *result, const ones_comp_t *a, const ones_comp_t *b);
void ones_comp_neg(ones_comp_t *result, const ones_comp_t *a);

/* Test for zero (must check both +0 and -0) */
int ones_comp_is_zero(const ones_comp_t *a);

/* Normalize (convert -0 to +0) */
void ones_comp_normalize(ones_comp_t *a);

/* Convert to/from 2's complement */
int64_t ones_comp_to_twos(const ones_comp_t *a);
void twos_to_ones_comp(ones_comp_t *result, int64_t value);

/***************************************************************************
 * NON-POWER-OF-2 WORD SIZES
 ***************************************************************************/

/* PDP-10: 36-bit words (very common in 1960s-1970s mainframes) */
typedef struct {
	uint64_t bits;     /* Use 64-bit storage, only 36 bits valid */
	                   /* Bits 0-35 are valid, 36-63 are zero */
} pdp10_word_t;

#define PDP10_WORD_MASK 0x0000000FFFFFFFFFULL

/* PDP-10 operations */
void pdp10_add(pdp10_word_t *result, const pdp10_word_t *a, const pdp10_word_t *b);
void pdp10_sub(pdp10_word_t *result, const pdp10_word_t *a, const pdp10_word_t *b);
void pdp10_mul(pdp10_word_t *result, const pdp10_word_t *a, const pdp10_word_t *b);
void pdp10_div(pdp10_word_t *quotient, pdp10_word_t *remainder, const pdp10_word_t *dividend, const pdp10_word_t *divisor);

/* PDP-10 byte extraction (variable byte sizes!) */
void pdp10_ldb(pdp10_word_t *result, const pdp10_word_t *ptr, uint32_t pos, uint32_t size);
void pdp10_dpb(pdp10_word_t *result, const pdp10_word_t *value, const pdp10_word_t *word, uint32_t pos, uint32_t size);

/* CDC 6600/7600: 60-bit words */
typedef struct {
	uint64_t bits;     /* Use 64-bit storage, only 60 bits valid */
} cdc_word_t;

#define CDC_WORD_MASK 0x0FFFFFFFFFFFFFFFULL

/* CDC operations (1's complement) */
void cdc_add(cdc_word_t *result, const cdc_word_t *a, const cdc_word_t *b);
void cdc_mul(cdc_word_t *result, const cdc_word_t *a, const cdc_word_t *b);

/***************************************************************************
 * RVV 0.9 (Pre-1.0 RISC-V Vector)
 ***************************************************************************/

/* RVV 0.9 had different operations and semantics from 1.0 */

typedef struct {
	uint32_t vl;        /* Vector length */
	uint32_t vtype;     /* Vector type register */
	uint32_t vlenb;     /* Vector length in bytes */
} rvv09_state_t;

/* RVV 0.9 configuration (different from 1.0) */
void rvv09_vsetvl(rvv09_state_t *state, uint32_t avl, uint32_t sew, uint32_t lmul);

/* RVV 0.9 specific operations that changed in 1.0 */
void rvv09_vwadd_vv(vector_t *dst, const vector_t *src1, const vector_t *src2, const rvv09_state_t *state);
void rvv09_vwaddu_vv(vector_t *dst, const vector_t *src1, const vector_t *src2, const rvv09_state_t *state);

/* RVV 0.9 had different mask handling */
void rvv09_vmadc(vector_t *mask, const vector_t *src1, const vector_t *src2, const rvv09_state_t *state);

/***************************************************************************
 * WMMX (Wireless MMX) - Intel XScale
 ***************************************************************************/

/* 64-bit WMMX registers (wR0-wR15) */
typedef struct {
	uint64_t data;
} wmmx_reg_t;

/* 128-bit WMMX2 registers */
typedef struct {
	uint64_t lo;
	uint64_t hi;
} wmmx2_reg_t;

/* WMMX operations (optimized for wireless/multimedia) */
void wmmx_wadd(wmmx_reg_t *dst, const wmmx_reg_t *src1, const wmmx_reg_t *src2, int element_size);
void wmmx_wsub(wmmx_reg_t *dst, const wmmx_reg_t *src1, const wmmx_reg_t *src2, int element_size);
void wmmx_wmul(wmmx_reg_t *dst, const wmmx_reg_t *src1, const wmmx_reg_t *src2, int element_size);

/* WMMX pack/unpack */
void wmmx_wpack(wmmx_reg_t *dst, const wmmx_reg_t *src1, const wmmx_reg_t *src2, int mode);
void wmmx_wunpack(wmmx_reg_t *dst, const wmmx_reg_t *src, int mode);

/* WMMX alignment */
void wmmx_waligni(wmmx_reg_t *dst, const wmmx_reg_t *src1, const wmmx_reg_t *src2, uint32_t imm);
void wmmx_walignr(wmmx_reg_t *dst, const wmmx_reg_t *src1, const wmmx_reg_t *src2, const wmmx_reg_t *control);

/* WMMX shuffle */
void wmmx_wshufh(wmmx_reg_t *dst, const wmmx_reg_t *src, uint32_t imm);

/* WMMX2 operations (128-bit) */
void wmmx2_waddbhus(wmmx2_reg_t *dst, const wmmx_reg_t *src1, const wmmx_reg_t *src2);
void wmmx2_wsubaddhx(wmmx2_reg_t *dst, const wmmx2_reg_t *src1, const wmmx2_reg_t *src2);

/***************************************************************************
 * MIPS MSA2 (MIPS SIMD Architecture 2)
 ***************************************************************************/

/* MSA2 extended operations beyond MSA */

/* MSA2 vector registers (same as MSA, but new operations) */
typedef vector_t msa2_reg_t;

/* MSA2 fused operations */
void msa2_fmadd(msa2_reg_t *dst, const msa2_reg_t *src1, const msa2_reg_t *src2, const msa2_reg_t *src3);
void msa2_fmsub(msa2_reg_t *dst, const msa2_reg_t *src1, const msa2_reg_t *src2, const msa2_reg_t *src3);

/* MSA2 extended shuffle */
void msa2_shf(msa2_reg_t *dst, const msa2_reg_t *src, uint32_t imm);

/* MSA2 horizontal operations */
void msa2_hadd(msa2_reg_t *dst, const msa2_reg_t *src1, const msa2_reg_t *src2);

/* MSA2 bit manipulation */
void msa2_binsli(msa2_reg_t *dst, const msa2_reg_t *src, uint32_t imm);
void msa2_binsri(msa2_reg_t *dst, const msa2_reg_t *src, uint32_t imm);

/***************************************************************************
 * LOONGSON LMMX (Loongson Multimedia Extensions)
 ***************************************************************************/

/* Loongson specific SIMD (different from standard MIPS MSA) */

/* LMMX uses MMI registers (same as PS2 Emotion Engine heritage) */
typedef struct {
	uint64_t lo;
	uint64_t hi;
} lmmx_reg_t;

/* LMMX operations (optimized for Chinese market applications) */
void lmmx_paddb(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2);
void lmmx_paddh(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2);
void lmmx_paddw(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2);
void lmmx_paddd(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2);

/* LMMX saturating operations */
void lmmx_paddsb(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2);
void lmmx_paddsh(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2);
void lmmx_paddusb(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2);
void lmmx_paddush(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2);

/* LMMX pack/unpack */
void lmmx_packsshb(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2);
void lmmx_packsswh(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2);
void lmmx_packushb(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2);

/* LMMX multiply */
void lmmx_pmulhh(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2);
void lmmx_pmullh(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2);
void lmmx_pmaddhw(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2);

/* LMMX shuffle/permute */
void lmmx_pshufh(lmmx_reg_t *dst, const lmmx_reg_t *src, uint32_t imm);
void lmmx_psllh(lmmx_reg_t *dst, const lmmx_reg_t *src, uint32_t count);
void lmmx_psrlh(lmmx_reg_t *dst, const lmmx_reg_t *src, uint32_t count);
void lmmx_psrah(lmmx_reg_t *dst, const lmmx_reg_t *src, uint32_t count);

/***************************************************************************
 * ARM FPA (Floating Point Accelerator) - Original ARM FP
 ***************************************************************************/

/* ARM FPA used 80-bit extended precision (similar to x87) */
typedef struct {
	uint64_t mantissa;
	uint16_t exponent;  /* Includes sign */
	uint8_t type;       /* Normal, denormal, infinity, NaN, zero */
} arm_fpa_extended_t;

/* FPA register format markers */
#define FPA_TYPE_SINGLE    0x01
#define FPA_TYPE_DOUBLE    0x02
#define FPA_TYPE_EXTENDED  0x03
#define FPA_TYPE_PACKED    0x04

/* FPA operations */
void fpa_adf(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src1, const arm_fpa_extended_t *src2);  /* Add */
void fpa_suf(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src1, const arm_fpa_extended_t *src2);  /* Subtract */
void fpa_muf(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src1, const arm_fpa_extended_t *src2);  /* Multiply */
void fpa_dvf(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src1, const arm_fpa_extended_t *src2);  /* Divide */
void fpa_rmf(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src1, const arm_fpa_extended_t *src2);  /* Remainder */

/* FPA transcendental (hardware accelerated) */
void fpa_sin(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src);
void fpa_cos(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src);
void fpa_tan(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src);
void fpa_asn(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src);  /* Arc sine */
void fpa_acs(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src);  /* Arc cosine */
void fpa_atn(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src);  /* Arc tangent */
void fpa_sqt(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src);  /* Square root */
void fpa_log(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src);
void fpa_lgn(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src);  /* Log natural */
void fpa_exp(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src);
void fpa_pow(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src);

/* FPA conversions */
void fpa_extended_to_ieee_double(double *dst, const arm_fpa_extended_t *src);
void ieee_double_to_fpa_extended(arm_fpa_extended_t *dst, double src);

/***************************************************************************
 * UTILITY FUNCTIONS
 ***************************************************************************/

/* Convert between different decimal formats */
void dpd_to_bid_32(uint32_t *bid, uint32_t dpd);
void bid_to_dpd_32(uint32_t *dpd, uint32_t bid);

/* Detect and convert floating point formats automatically */
typedef enum {
	FP_FORMAT_IEEE_SINGLE,
	FP_FORMAT_IEEE_DOUBLE,
	FP_FORMAT_IEEE_QUAD,
	FP_FORMAT_VAX_F,
	FP_FORMAT_VAX_D,
	FP_FORMAT_VAX_G,
	FP_FORMAT_IBM_SHORT,
	FP_FORMAT_IBM_LONG,
	FP_FORMAT_CRAY,
	FP_FORMAT_ARM_FPA,
	FP_FORMAT_DEC32_DPD,
	FP_FORMAT_DEC64_DPD,
	FP_FORMAT_DEC32_BID,
	FP_FORMAT_DEC64_BID
} fp_format_t;

/* Universal FP conversion (detects format and converts) */
void fp_convert(void *dst, fp_format_t dst_format, const void *src, fp_format_t src_format);

#ifdef __cplusplus
}
#endif

#endif /* __LIBCPU_BACKEND_EMULATION_EXOTIC_H__ */
