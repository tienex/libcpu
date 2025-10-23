/*
 * libcpu Backend Emulation - Extended Type System Implementation
 */

#include "backend_emulation_types.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/***************************************************************************
 * FP8 Conversions (E4M3 and E5M2)
 ***************************************************************************/

float fp8_e4m3_to_fp32(fp8_e4m3_t x)
{
	/* E4M3: 1 sign, 4 exponent, 3 mantissa */
	uint8_t sign = (x >> 7) & 1;
	uint8_t exp = (x >> 3) & 0xF;
	uint8_t mantissa = x & 0x7;

	if (exp == 0 && mantissa == 0) return sign ? -0.0f : 0.0f;
	if (exp == 0xF) return sign ? -INFINITY : INFINITY;  /* No NaN in E4M3 */

	/* Denormal handling */
	if (exp == 0) {
		float val = (float)mantissa / 8.0f;  /* Implicit leading 0 */
		val = ldexpf(val, -6);               /* Exponent bias = 7, min = -6 */
		return sign ? -val : val;
	}

	/* Normal number */
	float val = 1.0f + (float)mantissa / 8.0f;
	val = ldexpf(val, (int)exp - 7);  /* Bias = 7 */
	return sign ? -val : val;
}

fp8_e4m3_t fp32_to_fp8_e4m3(float x)
{
	if (x == 0.0f) return 0;
	if (isnan(x)) return 0x7F;  /* Map NaN to max */
	if (isinf(x)) return x > 0 ? 0x7E : 0xFE;  /* Max finite values */

	uint8_t sign = (x < 0) ? 0x80 : 0;
	x = fabsf(x);

	int exp;
	float mantissa = frexpf(x, &exp);
	exp--;  /* frexp returns [0.5, 1.0), we want [1.0, 2.0) */

	/* Bias = 7 */
	exp += 7;

	/* Clamp to valid range */
	if (exp <= 0) {
		/* Denormal or underflow */
		if (exp < -2) return sign;  /* Underflow to zero */
		mantissa = ldexpf(mantissa, exp);
		exp = 0;
	} else if (exp >= 15) {
		/* Overflow */
		return sign | 0x7E;  /* Max finite value */
	}

	uint8_t mant_bits = (uint8_t)roundf((mantissa - 1.0f) * 8.0f);
	return sign | (exp << 3) | (mant_bits & 0x7);
}

float fp8_e5m2_to_fp32(fp8_e5m2_t x)
{
	/* E5M2: 1 sign, 5 exponent, 2 mantissa */
	uint8_t sign = (x >> 7) & 1;
	uint8_t exp = (x >> 2) & 0x1F;
	uint8_t mantissa = x & 0x3;

	if (exp == 0 && mantissa == 0) return sign ? -0.0f : 0.0f;
	if (exp == 0x1F && mantissa == 0) return sign ? -INFINITY : INFINITY;
	if (exp == 0x1F) return NAN;

	if (exp == 0) {
		float val = (float)mantissa / 4.0f;
		val = ldexpf(val, -14);
		return sign ? -val : val;
	}

	float val = 1.0f + (float)mantissa / 4.0f;
	val = ldexpf(val, (int)exp - 15);  /* Bias = 15 */
	return sign ? -val : val;
}

fp8_e5m2_t fp32_to_fp8_e5m2(float x)
{
	if (x == 0.0f) return 0;
	if (isnan(x)) return 0x7F;
	if (isinf(x)) return x > 0 ? 0x7C : 0xFC;

	uint8_t sign = (x < 0) ? 0x80 : 0;
	x = fabsf(x);

	int exp;
	float mantissa = frexpf(x, &exp);
	exp--;

	exp += 15;  /* Bias = 15 */

	if (exp <= 0) {
		if (exp < -1) return sign;
		mantissa = ldexpf(mantissa, exp);
		exp = 0;
	} else if (exp >= 31) {
		return sign | 0x7C;  /* Infinity */
	}

	uint8_t mant_bits = (uint8_t)roundf((mantissa - 1.0f) * 4.0f);
	return sign | (exp << 2) | (mant_bits & 0x3);
}

/***************************************************************************
 * FP16 Conversions
 ***************************************************************************/

float fp16_to_fp32(fp16_t x)
{
	uint16_t sign = (x >> 15) & 1;
	uint16_t exp = (x >> 10) & 0x1F;
	uint16_t mantissa = x & 0x3FF;

	if (exp == 0 && mantissa == 0) return sign ? -0.0f : 0.0f;
	if (exp == 0x1F && mantissa == 0) return sign ? -INFINITY : INFINITY;
	if (exp == 0x1F) return NAN;

	if (exp == 0) {
		/* Denormal */
		float val = (float)mantissa / 1024.0f;
		val = ldexpf(val, -14);
		return sign ? -val : val;
	}

	/* Normal */
	float val = 1.0f + (float)mantissa / 1024.0f;
	val = ldexpf(val, (int)exp - 15);
	return sign ? -val : val;
}

fp16_t fp32_to_fp16(float x)
{
	if (x == 0.0f) return 0;
	if (isnan(x)) return 0x7FFF;
	if (isinf(x)) return x > 0 ? 0x7C00 : 0xFC00;

	uint16_t sign = (x < 0) ? 0x8000 : 0;
	x = fabsf(x);

	int exp;
	float mantissa = frexpf(x, &exp);
	exp--;

	exp += 15;

	if (exp <= 0) {
		if (exp < -10) return sign;
		mantissa = ldexpf(mantissa, exp);
		exp = 0;
	} else if (exp >= 31) {
		return sign | 0x7C00;
	}

	uint16_t mant_bits = (uint16_t)roundf((mantissa - 1.0f) * 1024.0f);
	return sign | (exp << 10) | (mant_bits & 0x3FF);
}

double fp16_to_fp64(fp16_t x)
{
	return (double)fp16_to_fp32(x);
}

fp16_t fp64_to_fp16(double x)
{
	return fp32_to_fp16((float)x);
}

/***************************************************************************
 * BFloat16 Conversions
 ***************************************************************************/

float bf16_to_fp32(bfloat16_t x)
{
	/* BFloat16: 1 sign, 8 exponent, 7 mantissa (truncated FP32) */
	uint32_t bits = ((uint32_t)x) << 16;
	float result;
	memcpy(&result, &bits, sizeof(float));
	return result;
}

bfloat16_t fp32_to_bf16(float x)
{
	uint32_t bits;
	memcpy(&bits, &x, sizeof(float));

	/* Round to nearest even */
	uint32_t rounding = 0x7FFF + ((bits >> 16) & 1);
	bits += rounding;

	return (bfloat16_t)(bits >> 16);
}

/***************************************************************************
 * FP80 Conversions (x87 extended precision)
 ***************************************************************************/

double fp80_to_fp64(const fp80_t *x)
{
	/* Simplified conversion - full precision loss */
	uint16_t exp = x->exponent & 0x7FFF;
	int sign = (x->exponent >> 15) & 1;

	if (exp == 0x7FFF) {
		if (x->mantissa & 0x7FFFFFFFFFFFFFFFULL)
			return NAN;
		return sign ? -INFINITY : INFINITY;
	}

	if (exp == 0 && x->mantissa == 0)
		return sign ? -0.0 : 0.0;

	/* Convert mantissa and exponent */
	double mantissa = (double)(x->mantissa >> 11) / (1ULL << 52);
	double val = ldexp(mantissa, (int)exp - 16383);
	return sign ? -val : val;
}

void fp64_to_fp80(double x, fp80_t *out)
{
	if (x == 0.0) {
		out->mantissa = 0;
		out->exponent = signbit(x) ? 0x8000 : 0;
		return;
	}

	if (isnan(x)) {
		out->mantissa = 0xC000000000000000ULL;
		out->exponent = 0x7FFF;
		return;
	}

	if (isinf(x)) {
		out->mantissa = 0x8000000000000000ULL;
		out->exponent = (x < 0 ? 0x8000 : 0) | 0x7FFF;
		return;
	}

	int exp;
	double mantissa = frexp(fabs(x), &exp);

	out->mantissa = (uint64_t)(mantissa * (1ULL << 63));
	out->exponent = ((x < 0 ? 0x8000 : 0) | (uint16_t)(exp + 16382));
}

/***************************************************************************
 * FP128 Conversions (quad precision)
 ***************************************************************************/

double fp128_to_fp64(const fp128_t *x)
{
	/* Simplified - assumes IEEE 754 quad format */
	/* This is a placeholder - full implementation needs 128-bit math */
	uint64_t sign_exp = x->high >> 48;
	if ((sign_exp & 0x7FFF) == 0x7FFF)
		return (sign_exp & 0x8000) ? -INFINITY : INFINITY;

	/* Approximate conversion */
	return 0.0;  /* TODO: Implement full conversion */
}

void fp64_to_fp128(double x, fp128_t *out)
{
	/* Placeholder */
	out->low = 0;
	out->high = 0;
}

/***************************************************************************
 * Double-Double Conversions (PowerPC)
 ***************************************************************************/

double dd_to_fp64(const double_double_t *x)
{
	/* Return high part (most significant) */
	return x->high;
}

void fp64_to_dd(double x, double_double_t *out)
{
	out->high = x;
	out->low = 0.0;
}

/***************************************************************************
 * Cross-conversions
 ***************************************************************************/

fp16_t bf16_to_fp16(bfloat16_t x)
{
	return fp32_to_fp16(bf16_to_fp32(x));
}

bfloat16_t fp16_to_bf16(fp16_t x)
{
	return fp32_to_bf16(fp16_to_fp32(x));
}

/***************************************************************************
 * Vector Type Construction
 ***************************************************************************/

vector_type_desc_t vector_type_create(vector_element_type_t elem_type, uint32_t num_elems)
{
	vector_type_desc_t desc;
	desc.element_type = elem_type;
	desc.num_elements = num_elems;
	desc.total_bits = vector_element_size(elem_type) * 8 * num_elems;
	desc.is_scalable = 0;
	desc.scalable_factor = 0;
	return desc;
}

vector_type_desc_t vector_type_create_scalable(vector_element_type_t elem_type, uint32_t factor)
{
	vector_type_desc_t desc;
	desc.element_type = elem_type;
	desc.num_elements = 0;  /* Runtime-determined */
	desc.total_bits = 0;
	desc.is_scalable = 1;
	desc.scalable_factor = factor;
	return desc;
}

vector_t* vector_alloc(const vector_type_desc_t *desc)
{
	vector_t *vec = (vector_t*)malloc(sizeof(vector_t));
	vec->desc = *desc;

	if (desc->is_scalable) {
		/* Assume 128-bit SVE/RVV for now */
		vec->data_size = 16 * desc->scalable_factor;
	} else {
		vec->data_size = desc->total_bits / 8;
	}

	vec->data = malloc(vec->data_size);
	memset(vec->data, 0, vec->data_size);
	return vec;
}

void vector_free(vector_t *vec)
{
	if (vec) {
		free(vec->data);
		free(vec);
	}
}

/* Common constructors */
vector_type_desc_t vec_i8x16(void) { return vector_type_create(VEC_ELEM_I8, 16); }
vector_type_desc_t vec_i16x8(void) { return vector_type_create(VEC_ELEM_I16, 8); }
vector_type_desc_t vec_i32x4(void) { return vector_type_create(VEC_ELEM_I32, 4); }
vector_type_desc_t vec_i64x2(void) { return vector_type_create(VEC_ELEM_I64, 2); }
vector_type_desc_t vec_f32x4(void) { return vector_type_create(VEC_ELEM_FP32, 4); }
vector_type_desc_t vec_f64x2(void) { return vector_type_create(VEC_ELEM_FP64, 2); }

vector_type_desc_t vec_i32x8(void) { return vector_type_create(VEC_ELEM_I32, 8); }
vector_type_desc_t vec_f32x8(void) { return vector_type_create(VEC_ELEM_FP32, 8); }
vector_type_desc_t vec_f64x4(void) { return vector_type_create(VEC_ELEM_FP64, 4); }

vector_type_desc_t vec_i32x16(void) { return vector_type_create(VEC_ELEM_I32, 16); }
vector_type_desc_t vec_f32x16(void) { return vector_type_create(VEC_ELEM_FP32, 16); }
vector_type_desc_t vec_f64x8(void) { return vector_type_create(VEC_ELEM_FP64, 8); }

/***************************************************************************
 * Matrix Type Construction
 ***************************************************************************/

matrix_tile_desc_t matrix_tile_create(matrix_element_type_t elem_type,
                                      uint32_t rows, uint32_t cols)
{
	matrix_tile_desc_t desc;
	desc.element_type = elem_type;
	desc.rows = rows;
	desc.cols = cols;
	desc.tile_id = 0;
	desc.is_accumulator = 0;
	return desc;
}

matrix_tile_t* matrix_tile_alloc(const matrix_tile_desc_t *desc)
{
	matrix_tile_t *tile = (matrix_tile_t*)malloc(sizeof(matrix_tile_t));
	tile->desc = *desc;
	tile->data_size = desc->rows * desc->cols * matrix_element_size(desc->element_type);
	tile->data = malloc(tile->data_size);
	memset(tile->data, 0, tile->data_size);
	return tile;
}

void matrix_tile_free(matrix_tile_t *tile)
{
	if (tile) {
		free(tile->data);
		free(tile);
	}
}

matrix_tile_desc_t amx_tile_i8(uint32_t rows, uint32_t cols)
{
	return matrix_tile_create(MAT_ELEM_I8, rows, cols);
}

matrix_tile_desc_t amx_tile_i32_acc(uint32_t rows, uint32_t cols)
{
	matrix_tile_desc_t desc = matrix_tile_create(MAT_ELEM_I32, rows, cols);
	desc.is_accumulator = 1;
	return desc;
}

matrix_tile_desc_t amx_tile_fp16(uint32_t rows, uint32_t cols)
{
	return matrix_tile_create(MAT_ELEM_FP16, rows, cols);
}

matrix_tile_desc_t amx_tile_bf16(uint32_t rows, uint32_t cols)
{
	return matrix_tile_create(MAT_ELEM_BF16, rows, cols);
}

matrix_tile_desc_t amx_tile_fp32_acc(uint32_t rows, uint32_t cols)
{
	matrix_tile_desc_t desc = matrix_tile_create(MAT_ELEM_FP32, rows, cols);
	desc.is_accumulator = 1;
	return desc;
}

/***************************************************************************
 * Predicate Operations
 ***************************************************************************/

predicate_t* predicate_alloc(uint32_t num_bits, int is_scalable)
{
	predicate_t *pred = (predicate_t*)malloc(sizeof(predicate_t));
	pred->num_bits = num_bits;
	pred->is_scalable = is_scalable;
	pred->mask_bytes = (num_bits + 7) / 8;
	pred->mask_data = (uint8_t*)calloc(pred->mask_bytes, 1);
	return pred;
}

void predicate_free(predicate_t *pred)
{
	if (pred) {
		free(pred->mask_data);
		free(pred);
	}
}

void predicate_set_bit(predicate_t *pred, uint32_t index, int value)
{
	if (index >= pred->num_bits) return;
	uint32_t byte = index / 8;
	uint32_t bit = index % 8;
	if (value)
		pred->mask_data[byte] |= (1 << bit);
	else
		pred->mask_data[byte] &= ~(1 << bit);
}

int predicate_get_bit(const predicate_t *pred, uint32_t index)
{
	if (index >= pred->num_bits) return 0;
	uint32_t byte = index / 8;
	uint32_t bit = index % 8;
	return (pred->mask_data[byte] >> bit) & 1;
}

void predicate_and(predicate_t *dst, const predicate_t *a, const predicate_t *b)
{
	for (size_t i = 0; i < dst->mask_bytes; i++)
		dst->mask_data[i] = a->mask_data[i] & b->mask_data[i];
}

void predicate_or(predicate_t *dst, const predicate_t *a, const predicate_t *b)
{
	for (size_t i = 0; i < dst->mask_bytes; i++)
		dst->mask_data[i] = a->mask_data[i] | b->mask_data[i];
}

void predicate_not(predicate_t *dst, const predicate_t *src)
{
	for (size_t i = 0; i < dst->mask_bytes; i++)
		dst->mask_data[i] = ~src->mask_data[i];
}

/***************************************************************************
 * Element Size Helpers
 ***************************************************************************/

size_t vector_element_size(vector_element_type_t type)
{
	switch (type) {
	case VEC_ELEM_I8: case VEC_ELEM_U8: return 1;
	case VEC_ELEM_FP8_E4M3: case VEC_ELEM_FP8_E5M2: return 1;
	case VEC_ELEM_I16: case VEC_ELEM_U16: return 2;
	case VEC_ELEM_FP16: case VEC_ELEM_BF16: return 2;
	case VEC_ELEM_I32: case VEC_ELEM_U32: return 4;
	case VEC_ELEM_FP32: return 4;
	case VEC_ELEM_I64: case VEC_ELEM_U64: return 8;
	case VEC_ELEM_FP64: return 8;
	case VEC_ELEM_I128: case VEC_ELEM_U128: return 16;
	case VEC_ELEM_FP80: return 10;
	case VEC_ELEM_FP128: return 16;
	default: return 0;
	}
}

size_t matrix_element_size(matrix_element_type_t type)
{
	switch (type) {
	case MAT_ELEM_I8: case MAT_ELEM_U8: return 1;
	case MAT_ELEM_I16: case MAT_ELEM_U16: return 2;
	case MAT_ELEM_FP16: case MAT_ELEM_BF16: return 2;
	case MAT_ELEM_I32: case MAT_ELEM_U32: return 4;
	case MAT_ELEM_FP32: return 4;
	case MAT_ELEM_I64: case MAT_ELEM_U64: return 8;
	case MAT_ELEM_FP64: return 8;
	default: return 0;
	}
}

const char* vector_element_name(vector_element_type_t type)
{
	switch (type) {
	case VEC_ELEM_I8: return "i8";
	case VEC_ELEM_I16: return "i16";
	case VEC_ELEM_I32: return "i32";
	case VEC_ELEM_I64: return "i64";
	case VEC_ELEM_I128: return "i128";
	case VEC_ELEM_U8: return "u8";
	case VEC_ELEM_U16: return "u16";
	case VEC_ELEM_U32: return "u32";
	case VEC_ELEM_U64: return "u64";
	case VEC_ELEM_U128: return "u128";
	case VEC_ELEM_FP8_E4M3: return "fp8_e4m3";
	case VEC_ELEM_FP8_E5M2: return "fp8_e5m2";
	case VEC_ELEM_FP16: return "fp16";
	case VEC_ELEM_BF16: return "bf16";
	case VEC_ELEM_FP32: return "f32";
	case VEC_ELEM_FP64: return "f64";
	case VEC_ELEM_FP80: return "fp80";
	case VEC_ELEM_FP128: return "fp128";
	default: return "unknown";
	}
}

const char* matrix_element_name(matrix_element_type_t type)
{
	switch (type) {
	case MAT_ELEM_I8: return "i8";
	case MAT_ELEM_I16: return "i16";
	case MAT_ELEM_I32: return "i32";
	case MAT_ELEM_I64: return "i64";
	case MAT_ELEM_U8: return "u8";
	case MAT_ELEM_U16: return "u16";
	case MAT_ELEM_U32: return "u32";
	case MAT_ELEM_U64: return "u64";
	case MAT_ELEM_FP16: return "fp16";
	case MAT_ELEM_BF16: return "bf16";
	case MAT_ELEM_FP32: return "f32";
	case MAT_ELEM_FP64: return "f64";
	default: return "unknown";
	}
}
