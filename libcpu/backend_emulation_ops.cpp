/*
 * libcpu Backend Emulation - Unified Generic Operations Implementation
 *
 * Implementation of architecture-agnostic operations that can emulate ANY system.
 */

#include "backend_emulation_ops.h"
#include "backend_emulation_exotic.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

/***************************************************************************
 * VALUE CREATION / DESTRUCTION
 ***************************************************************************/

unified_value_t* uval_create_scalar(value_type_t type, const word_config_t *config) {
	unified_value_t *val = (unified_value_t*)calloc(1, sizeof(unified_value_t));
	val->kind = VALUE_KIND_SCALAR;
	val->type = type;
	val->word_config = *config;

	/* Allocate data storage based on word size */
	val->data_size = (config->bits + 7) / 8;
	val->data = calloc(1, val->data_size);

	return val;
}

unified_value_t* uval_create_vector(value_type_t type, const vector_config_t *vec_config, const word_config_t *word_config) {
	unified_value_t *val = (unified_value_t*)calloc(1, sizeof(unified_value_t));
	val->kind = VALUE_KIND_VECTOR;
	val->type = type;
	val->word_config = *word_config;
	val->vec_config = *vec_config;

	/* Allocate data for all elements */
	val->data_size = (vec_config->num_elements * vec_config->element_bits + 7) / 8;
	val->data = calloc(1, val->data_size);

	return val;
}

unified_value_t* uval_create_matrix(uint32_t rows, uint32_t cols, value_type_t type, const word_config_t *config) {
	unified_value_t *val = (unified_value_t*)calloc(1, sizeof(unified_value_t));
	val->kind = VALUE_KIND_MATRIX;
	val->type = type;
	val->word_config = *config;

	val->data_size = rows * cols * ((config->bits + 7) / 8);
	val->data = calloc(1, val->data_size);

	return val;
}

unified_value_t* uval_create_predicate(uint32_t num_bits, int is_scalable) {
	unified_value_t *val = (unified_value_t*)calloc(1, sizeof(unified_value_t));
	val->kind = VALUE_KIND_PREDICATE;
	val->type = VALUE_TYPE_INTEGER;

	val->vec_config.num_elements = num_bits;
	val->vec_config.is_scalable = is_scalable;

	val->data_size = (num_bits + 7) / 8;
	val->data = calloc(1, val->data_size);

	return val;
}

unified_value_t* uval_create_pointer(uint32_t address_bits, uint32_t position_bits, uint32_t size_bits) {
	unified_value_t *val = (unified_value_t*)calloc(1, sizeof(unified_value_t));
	val->kind = VALUE_KIND_POINTER;
	val->type = VALUE_TYPE_INTEGER;

	/* PDP-10 style byte pointer: address + position + size */
	uint32_t total_bits = address_bits + position_bits + size_bits;
	val->data_size = (total_bits + 7) / 8;
	val->data = calloc(1, val->data_size);

	return val;
}

void uval_free(unified_value_t *val) {
	if (val) {
		if (val->data) {
			free(val->data);
		}
		free(val);
	}
}

/***************************************************************************
 * VALUE ACCESS
 ***************************************************************************/

void uval_set_i64(unified_value_t *val, int64_t value) {
	if (!val || !val->data) return;

	/* Mask to word size */
	uint64_t mask = get_word_mask(val->word_config.bits);
	uint64_t masked = (uint64_t)value & mask;

	memcpy(val->data, &masked, (val->word_config.bits + 7) / 8);
}

void uval_set_u64(unified_value_t *val, uint64_t value) {
	if (!val || !val->data) return;

	uint64_t mask = get_word_mask(val->word_config.bits);
	uint64_t masked = value & mask;

	memcpy(val->data, &masked, (val->word_config.bits + 7) / 8);
}

void uval_set_f64(unified_value_t *val, double value) {
	if (!val || !val->data) return;

	/* Convert to appropriate FP format */
	switch (val->fp_config.format) {
		case FP_FORMAT_IEEE_BINARY32: {
			float f = (float)value;
			memcpy(val->data, &f, sizeof(float));
			break;
		}
		case FP_FORMAT_IEEE_BINARY64: {
			memcpy(val->data, &value, sizeof(double));
			break;
		}
		case FP_FORMAT_VAX_F: {
			vax_f_float_t vax;
			ieee_float_to_vax_f(&vax, (float)value);
			memcpy(val->data, &vax, sizeof(vax));
			break;
		}
		case FP_FORMAT_VAX_D: {
			vax_d_float_t vax;
			ieee_double_to_vax_d(&vax, value);
			memcpy(val->data, &vax, sizeof(vax));
			break;
		}
		case FP_FORMAT_IBM_SHORT: {
			ibm_short_float_t ibm;
			ieee_float_to_ibm_short(&ibm, (float)value);
			memcpy(val->data, &ibm, sizeof(ibm));
			break;
		}
		case FP_FORMAT_IBM_LONG: {
			ibm_long_float_t ibm;
			ieee_double_to_ibm_long(&ibm, value);
			memcpy(val->data, &ibm, sizeof(ibm));
			break;
		}
		case FP_FORMAT_CRAY: {
			cray_float_t cray;
			ieee_double_to_cray(&cray, value);
			memcpy(val->data, &cray, sizeof(cray));
			break;
		}
		default:
			memcpy(val->data, &value, sizeof(double));
			break;
	}
}

int64_t uval_get_i64(const unified_value_t *val) {
	if (!val || !val->data) return 0;

	uint64_t raw = 0;
	memcpy(&raw, val->data, (val->word_config.bits + 7) / 8);

	/* Mask to word size */
	uint64_t mask = get_word_mask(val->word_config.bits);
	raw &= mask;

	/* Sign extend if signed */
	if (val->word_config.is_signed) {
		uint64_t sign_bit = 1ULL << (val->word_config.bits - 1);
		if (raw & sign_bit) {
			raw |= ~mask;  /* Sign extend */
		}
	}

	return (int64_t)raw;
}

uint64_t uval_get_u64(const unified_value_t *val) {
	if (!val || !val->data) return 0;

	uint64_t raw = 0;
	memcpy(&raw, val->data, (val->word_config.bits + 7) / 8);

	return raw & get_word_mask(val->word_config.bits);
}

double uval_get_f64(const unified_value_t *val) {
	if (!val || !val->data) return 0.0;

	double result = 0.0;

	switch (val->fp_config.format) {
		case FP_FORMAT_IEEE_BINARY32: {
			float f;
			memcpy(&f, val->data, sizeof(float));
			result = (double)f;
			break;
		}
		case FP_FORMAT_IEEE_BINARY64: {
			memcpy(&result, val->data, sizeof(double));
			break;
		}
		case FP_FORMAT_VAX_F: {
			vax_f_float_t vax;
			memcpy(&vax, val->data, sizeof(vax));
			float f;
			vax_f_to_ieee_float(&f, &vax);
			result = (double)f;
			break;
		}
		case FP_FORMAT_VAX_D: {
			vax_d_float_t vax;
			memcpy(&vax, val->data, sizeof(vax));
			vax_d_to_ieee_double(&result, &vax);
			break;
		}
		case FP_FORMAT_IBM_SHORT: {
			ibm_short_float_t ibm;
			memcpy(&ibm, val->data, sizeof(ibm));
			float f;
			ibm_short_to_ieee_float(&f, &ibm);
			result = (double)f;
			break;
		}
		case FP_FORMAT_IBM_LONG: {
			ibm_long_float_t ibm;
			memcpy(&ibm, val->data, sizeof(ibm));
			ibm_long_to_ieee_double(&result, &ibm);
			break;
		}
		case FP_FORMAT_CRAY: {
			cray_float_t cray;
			memcpy(&cray, val->data, sizeof(cray));
			cray_to_ieee_double(&result, &cray);
			break;
		}
		default:
			memcpy(&result, val->data, sizeof(double));
			break;
	}

	return result;
}

/***************************************************************************
 * ARITHMETIC OPERATIONS - Generic for all word sizes and modes
 ***************************************************************************/

void op_add(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_SCALAR && b->kind == VALUE_KIND_SCALAR) {
		/* Scalar arithmetic */
		if (a->type == VALUE_TYPE_FLOAT) {
			double a_val = uval_get_f64(a);
			double b_val = uval_get_f64(b);
			uval_set_f64(dst, a_val + b_val);
		} else {
			/* Integer arithmetic based on mode */
			switch (a->word_config.arith_mode) {
				case ARITH_TWOS_COMPLEMENT: {
					uint64_t a_val = uval_get_u64(a);
					uint64_t b_val = uval_get_u64(b);
					uint64_t sum = a_val + b_val;
					uval_set_u64(dst, sum);
					break;
				}
				case ARITH_ONES_COMPLEMENT: {
					/* 1's complement: end-around carry */
					uint64_t a_val = uval_get_u64(a);
					uint64_t b_val = uval_get_u64(b);
					uint64_t sum = a_val + b_val;
					uint64_t mask = get_word_mask(a->word_config.bits);

					/* Check for carry out */
					if (sum > mask) {
						sum = (sum & mask) + 1;  /* End-around carry */
					}

					uval_set_u64(dst, sum);
					break;
				}
				case ARITH_SIGN_MAGNITUDE: {
					/* Sign-magnitude arithmetic */
					int64_t a_val = uval_get_i64(a);
					int64_t b_val = uval_get_i64(b);
					uval_set_i64(dst, a_val + b_val);
					break;
				}
				case ARITH_BCD: {
					/* BCD addition */
					op_bcd_add(dst, a, b);
					break;
				}
				default: {
					uint64_t a_val = uval_get_u64(a);
					uint64_t b_val = uval_get_u64(b);
					uval_set_u64(dst, a_val + b_val);
					break;
				}
			}
		}
	} else if (a->kind == VALUE_KIND_VECTOR && b->kind == VALUE_KIND_VECTOR) {
		/* Vector arithmetic - element-wise */
		uint32_t num_elem = a->vec_config.num_elements;

		for (uint32_t i = 0; i < num_elem; i++) {
			unified_value_t a_elem, b_elem, dst_elem;

			/* Extract elements */
			op_extract_element(&a_elem, a, i);
			op_extract_element(&b_elem, b, i);

			/* Add elements */
			op_add(&dst_elem, &a_elem, &b_elem);

			/* Insert result */
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_sub(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		if (a->type == VALUE_TYPE_FLOAT) {
			double a_val = uval_get_f64(a);
			double b_val = uval_get_f64(b);
			uval_set_f64(dst, a_val - b_val);
		} else {
			switch (a->word_config.arith_mode) {
				case ARITH_TWOS_COMPLEMENT: {
					uint64_t a_val = uval_get_u64(a);
					uint64_t b_val = uval_get_u64(b);
					uval_set_u64(dst, a_val - b_val);
					break;
				}
				case ARITH_ONES_COMPLEMENT: {
					/* Negate b and add */
					unified_value_t b_neg;
					op_neg(&b_neg, b);
					op_add(dst, a, &b_neg);
					break;
				}
				default: {
					uint64_t a_val = uval_get_u64(a);
					uint64_t b_val = uval_get_u64(b);
					uval_set_u64(dst, a_val - b_val);
					break;
				}
			}
		}
	} else if (a->kind == VALUE_KIND_VECTOR) {
		/* Vector subtraction */
		for (uint32_t i = 0; i < a->vec_config.num_elements; i++) {
			unified_value_t a_elem, b_elem, dst_elem;
			op_extract_element(&a_elem, a, i);
			op_extract_element(&b_elem, b, i);
			op_sub(&dst_elem, &a_elem, &b_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_mul(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		if (a->type == VALUE_TYPE_FLOAT) {
			double a_val = uval_get_f64(a);
			double b_val = uval_get_f64(b);
			uval_set_f64(dst, a_val * b_val);
		} else {
			/* Integer multiply */
			if (a->word_config.is_signed) {
				int64_t a_val = uval_get_i64(a);
				int64_t b_val = uval_get_i64(b);
				uval_set_i64(dst, a_val * b_val);
			} else {
				uint64_t a_val = uval_get_u64(a);
				uint64_t b_val = uval_get_u64(b);
				uval_set_u64(dst, a_val * b_val);
			}
		}
	} else if (a->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < a->vec_config.num_elements; i++) {
			unified_value_t a_elem, b_elem, dst_elem;
			op_extract_element(&a_elem, a, i);
			op_extract_element(&b_elem, b, i);
			op_mul(&dst_elem, &a_elem, &b_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_div(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		if (a->type == VALUE_TYPE_FLOAT) {
			double a_val = uval_get_f64(a);
			double b_val = uval_get_f64(b);
			uval_set_f64(dst, a_val / b_val);
		} else {
			if (a->word_config.is_signed) {
				int64_t a_val = uval_get_i64(a);
				int64_t b_val = uval_get_i64(b);
				if (b_val != 0) {
					uval_set_i64(dst, a_val / b_val);
				}
			} else {
				uint64_t a_val = uval_get_u64(a);
				uint64_t b_val = uval_get_u64(b);
				if (b_val != 0) {
					uval_set_u64(dst, a_val / b_val);
				}
			}
		}
	} else if (a->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < a->vec_config.num_elements; i++) {
			unified_value_t a_elem, b_elem, dst_elem;
			op_extract_element(&a_elem, a, i);
			op_extract_element(&b_elem, b, i);
			op_div(&dst_elem, &a_elem, &b_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_neg(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_SCALAR) {
		switch (src->word_config.arith_mode) {
			case ARITH_TWOS_COMPLEMENT: {
				uint64_t val = uval_get_u64(src);
				uint64_t mask = get_word_mask(src->word_config.bits);
				uval_set_u64(dst, (~val + 1) & mask);
				break;
			}
			case ARITH_ONES_COMPLEMENT: {
				/* In 1's complement, negation is bitwise NOT */
				uint64_t val = uval_get_u64(src);
				uint64_t mask = get_word_mask(src->word_config.bits);
				uval_set_u64(dst, (~val) & mask);
				break;
			}
			case ARITH_SIGN_MAGNITUDE: {
				/* Flip sign bit */
				uint64_t val = uval_get_u64(src);
				uint64_t sign_bit = 1ULL << (src->word_config.bits - 1);
				uval_set_u64(dst, val ^ sign_bit);
				break;
			}
			default: {
				int64_t val = uval_get_i64(src);
				uval_set_i64(dst, -val);
				break;
			}
		}
	} else if (src->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < src->vec_config.num_elements; i++) {
			unified_value_t src_elem, dst_elem;
			op_extract_element(&src_elem, src, i);
			op_neg(&dst_elem, &src_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

/***************************************************************************
 * BITWISE OPERATIONS
 ***************************************************************************/

void op_and(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		uint64_t a_val = uval_get_u64(a);
		uint64_t b_val = uval_get_u64(b);
		uval_set_u64(dst, a_val & b_val);
	} else if (a->kind == VALUE_KIND_VECTOR) {
		/* Bitwise AND on vector data */
		size_t bytes = (a->vec_config.num_elements * a->vec_config.element_bits + 7) / 8;
		uint8_t *a_data = (uint8_t*)a->data;
		uint8_t *b_data = (uint8_t*)b->data;
		uint8_t *dst_data = (uint8_t*)dst->data;

		for (size_t i = 0; i < bytes; i++) {
			dst_data[i] = a_data[i] & b_data[i];
		}
	}
}

void op_or(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		uint64_t a_val = uval_get_u64(a);
		uint64_t b_val = uval_get_u64(b);
		uval_set_u64(dst, a_val | b_val);
	} else if (a->kind == VALUE_KIND_VECTOR) {
		size_t bytes = (a->vec_config.num_elements * a->vec_config.element_bits + 7) / 8;
		uint8_t *a_data = (uint8_t*)a->data;
		uint8_t *b_data = (uint8_t*)b->data;
		uint8_t *dst_data = (uint8_t*)dst->data;

		for (size_t i = 0; i < bytes; i++) {
			dst_data[i] = a_data[i] | b_data[i];
		}
	}
}

void op_xor(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		uint64_t a_val = uval_get_u64(a);
		uint64_t b_val = uval_get_u64(b);
		uval_set_u64(dst, a_val ^ b_val);
	} else if (a->kind == VALUE_KIND_VECTOR) {
		size_t bytes = (a->vec_config.num_elements * a->vec_config.element_bits + 7) / 8;
		uint8_t *a_data = (uint8_t*)a->data;
		uint8_t *b_data = (uint8_t*)b->data;
		uint8_t *dst_data = (uint8_t*)dst->data;

		for (size_t i = 0; i < bytes; i++) {
			dst_data[i] = a_data[i] ^ b_data[i];
		}
	}
}

void op_not(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_SCALAR) {
		uint64_t val = uval_get_u64(src);
		uint64_t mask = get_word_mask(src->word_config.bits);
		uval_set_u64(dst, (~val) & mask);
	} else if (src->kind == VALUE_KIND_VECTOR) {
		size_t bytes = (src->vec_config.num_elements * src->vec_config.element_bits + 7) / 8;
		uint8_t *src_data = (uint8_t*)src->data;
		uint8_t *dst_data = (uint8_t*)dst->data;

		for (size_t i = 0; i < bytes; i++) {
			dst_data[i] = ~src_data[i];
		}
	}
}

/***************************************************************************
 * SHIFT OPERATIONS - Work with any word size
 ***************************************************************************/

void op_sll(unified_value_t *dst, const unified_value_t *a, const unified_value_t *shift) {
	if (!dst || !a || !shift) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		uint64_t val = uval_get_u64(a);
		uint64_t shift_amt = uval_get_u64(shift);
		uint64_t mask = get_word_mask(a->word_config.bits);

		if (shift_amt < a->word_config.bits) {
			uval_set_u64(dst, (val << shift_amt) & mask);
		} else {
			uval_set_u64(dst, 0);
		}
	} else if (a->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < a->vec_config.num_elements; i++) {
			unified_value_t a_elem, shift_elem, dst_elem;
			op_extract_element(&a_elem, a, i);
			op_extract_element(&shift_elem, shift, i);
			op_sll(&dst_elem, &a_elem, &shift_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_srl(unified_value_t *dst, const unified_value_t *a, const unified_value_t *shift) {
	if (!dst || !a || !shift) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		uint64_t val = uval_get_u64(a);
		uint64_t shift_amt = uval_get_u64(shift);

		if (shift_amt < a->word_config.bits) {
			uval_set_u64(dst, val >> shift_amt);
		} else {
			uval_set_u64(dst, 0);
		}
	} else if (a->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < a->vec_config.num_elements; i++) {
			unified_value_t a_elem, shift_elem, dst_elem;
			op_extract_element(&a_elem, a, i);
			op_extract_element(&shift_elem, shift, i);
			op_srl(&dst_elem, &a_elem, &shift_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_sra(unified_value_t *dst, const unified_value_t *a, const unified_value_t *shift) {
	if (!dst || !a || !shift) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		int64_t val = uval_get_i64(a);
		uint64_t shift_amt = uval_get_u64(shift);

		if (shift_amt < a->word_config.bits) {
			uval_set_i64(dst, val >> shift_amt);
		} else {
			/* Shift in sign bit */
			uval_set_i64(dst, val < 0 ? -1 : 0);
		}
	} else if (a->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < a->vec_config.num_elements; i++) {
			unified_value_t a_elem, shift_elem, dst_elem;
			op_extract_element(&a_elem, a, i);
			op_extract_element(&shift_elem, shift, i);
			op_sra(&dst_elem, &a_elem, &shift_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_rol(unified_value_t *dst, const unified_value_t *a, const unified_value_t *shift) {
	if (!dst || !a || !shift) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		uint64_t val = uval_get_u64(a);
		uint64_t shift_amt = uval_get_u64(shift) % a->word_config.bits;
		uint64_t mask = get_word_mask(a->word_config.bits);

		uint64_t result = ((val << shift_amt) | (val >> (a->word_config.bits - shift_amt))) & mask;
		uval_set_u64(dst, result);
	} else if (a->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < a->vec_config.num_elements; i++) {
			unified_value_t a_elem, shift_elem, dst_elem;
			op_extract_element(&a_elem, a, i);
			op_extract_element(&shift_elem, shift, i);
			op_rol(&dst_elem, &a_elem, &shift_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_ror(unified_value_t *dst, const unified_value_t *a, const unified_value_t *shift) {
	if (!dst || !a || !shift) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		uint64_t val = uval_get_u64(a);
		uint64_t shift_amt = uval_get_u64(shift) % a->word_config.bits;
		uint64_t mask = get_word_mask(a->word_config.bits);

		uint64_t result = ((val >> shift_amt) | (val << (a->word_config.bits - shift_amt))) & mask;
		uval_set_u64(dst, result);
	} else if (a->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < a->vec_config.num_elements; i++) {
			unified_value_t a_elem, shift_elem, dst_elem;
			op_extract_element(&a_elem, a, i);
			op_extract_element(&shift_elem, shift, i);
			op_ror(&dst_elem, &a_elem, &shift_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

/***************************************************************************
 * BIT COUNTING - Works with any word size
 ***************************************************************************/

void op_popcnt(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_SCALAR) {
		uint64_t val = uval_get_u64(src);
		uint64_t mask = get_word_mask(src->word_config.bits);
		val &= mask;

		/* Count set bits */
		uint32_t count = 0;
		while (val) {
			count += val & 1;
			val >>= 1;
		}

		uval_set_u64(dst, count);
	} else if (src->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < src->vec_config.num_elements; i++) {
			unified_value_t src_elem, dst_elem;
			op_extract_element(&src_elem, src, i);
			op_popcnt(&dst_elem, &src_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_clz(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_SCALAR) {
		uint64_t val = uval_get_u64(src);
		uint32_t bits = src->word_config.bits;

		if (val == 0) {
			uval_set_u64(dst, bits);
			return;
		}

		uint32_t count = 0;
		uint64_t test_bit = 1ULL << (bits - 1);

		while ((val & test_bit) == 0 && count < bits) {
			count++;
			test_bit >>= 1;
		}

		uval_set_u64(dst, count);
	} else if (src->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < src->vec_config.num_elements; i++) {
			unified_value_t src_elem, dst_elem;
			op_extract_element(&src_elem, src, i);
			op_clz(&dst_elem, &src_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_ctz(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_SCALAR) {
		uint64_t val = uval_get_u64(src);
		uint32_t bits = src->word_config.bits;

		if (val == 0) {
			uval_set_u64(dst, bits);
			return;
		}

		uint32_t count = 0;
		while ((val & 1) == 0 && count < bits) {
			count++;
			val >>= 1;
		}

		uval_set_u64(dst, count);
	} else if (src->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < src->vec_config.num_elements; i++) {
			unified_value_t src_elem, dst_elem;
			op_extract_element(&src_elem, src, i);
			op_ctz(&dst_elem, &src_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

/***************************************************************************
 * BYTE OPERATIONS - Variable size bytes (for PDP-10, CDC, etc.)
 ***************************************************************************/

void op_byte_load(unified_value_t *dst, const unified_value_t *src, uint32_t position, uint32_t size) {
	if (!dst || !src) return;

	/* Extract byte of given size at given bit position */
	uint64_t val = uval_get_u64(src);
	uint64_t mask = (1ULL << size) - 1;
	uint64_t byte = (val >> position) & mask;

	uval_set_u64(dst, byte);
}

void op_byte_store(unified_value_t *dst, const unified_value_t *src, const unified_value_t *byte, uint32_t position, uint32_t size) {
	if (!dst || !src || !byte) return;

	/* Insert byte of given size at given bit position */
	uint64_t val = uval_get_u64(src);
	uint64_t byte_val = uval_get_u64(byte);
	uint64_t mask = ((1ULL << size) - 1) << position;

	uint64_t result = (val & ~mask) | ((byte_val << position) & mask);

	uval_set_u64(dst, result);
}

void op_byte_zap(unified_value_t *dst, const unified_value_t *src, uint64_t mask) {
	if (!dst || !src) return;

	/* Zero bytes where mask bit = 1 (ALPHA ZAP) */
	uint64_t val = uval_get_u64(src);
	uint64_t result = val;

	for (int i = 0; i < 8; i++) {
		if (mask & (1 << i)) {
			result &= ~(0xFFULL << (i * 8));
		}
	}

	uval_set_u64(dst, result);
}

void op_byte_zapnot(unified_value_t *dst, const unified_value_t *src, uint64_t mask) {
	if (!dst || !src) return;

	/* Zero bytes where mask bit = 0 (ALPHA ZAPNOT) */
	uint64_t val = uval_get_u64(src);
	uint64_t result = val;

	for (int i = 0; i < 8; i++) {
		if (!(mask & (1 << i))) {
			result &= ~(0xFFULL << (i * 8));
		}
	}

	uval_set_u64(dst, result);
}

/***************************************************************************
 * BIT FIELD OPERATIONS - Generalized for any word size
 ***************************************************************************/

void op_bitfield_extract(unified_value_t *dst, const unified_value_t *src, uint32_t offset, uint32_t width) {
	if (!dst || !src) return;

	uint64_t val = uval_get_u64(src);
	uint64_t mask = (1ULL << width) - 1;
	uint64_t field = (val >> offset) & mask;

	uval_set_u64(dst, field);
}

void op_bitfield_extract_signed(unified_value_t *dst, const unified_value_t *src, uint32_t offset, uint32_t width) {
	if (!dst || !src) return;

	uint64_t val = uval_get_u64(src);
	uint64_t mask = (1ULL << width) - 1;
	uint64_t field = (val >> offset) & mask;

	/* Sign extend */
	if (field & (1ULL << (width - 1))) {
		field |= ~mask;
	}

	uval_set_u64(dst, field);
}

void op_bitfield_insert(unified_value_t *dst, const unified_value_t *base, const unified_value_t *src, uint32_t offset, uint32_t width) {
	if (!dst || !base || !src) return;

	uint64_t base_val = uval_get_u64(base);
	uint64_t src_val = uval_get_u64(src);
	uint64_t mask = ((1ULL << width) - 1) << offset;

	uint64_t result = (base_val & ~mask) | ((src_val << offset) & mask);

	uval_set_u64(dst, result);
}

/***************************************************************************
 * COMPARISON OPERATIONS
 ***************************************************************************/

void op_cmpeq(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		if (a->type == VALUE_TYPE_FLOAT) {
			double a_val = uval_get_f64(a);
			double b_val = uval_get_f64(b);
			uval_set_u64(dst, (a_val == b_val) ? ~0ULL : 0);
		} else {
			uint64_t a_val = uval_get_u64(a);
			uint64_t b_val = uval_get_u64(b);
			uval_set_u64(dst, (a_val == b_val) ? ~0ULL : 0);
		}
	} else if (a->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < a->vec_config.num_elements; i++) {
			unified_value_t a_elem, b_elem, dst_elem;
			op_extract_element(&a_elem, a, i);
			op_extract_element(&b_elem, b, i);
			op_cmpeq(&dst_elem, &a_elem, &b_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_cmplt(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		if (a->type == VALUE_TYPE_FLOAT) {
			double a_val = uval_get_f64(a);
			double b_val = uval_get_f64(b);
			uval_set_u64(dst, (a_val < b_val) ? ~0ULL : 0);
		} else if (a->word_config.is_signed) {
			int64_t a_val = uval_get_i64(a);
			int64_t b_val = uval_get_i64(b);
			uval_set_u64(dst, (a_val < b_val) ? ~0ULL : 0);
		} else {
			uint64_t a_val = uval_get_u64(a);
			uint64_t b_val = uval_get_u64(b);
			uval_set_u64(dst, (a_val < b_val) ? ~0ULL : 0);
		}
	} else if (a->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < a->vec_config.num_elements; i++) {
			unified_value_t a_elem, b_elem, dst_elem;
			op_extract_element(&a_elem, a, i);
			op_extract_element(&b_elem, b, i);
			op_cmplt(&dst_elem, &a_elem, &b_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

/***************************************************************************
 * VECTOR/ELEMENT OPERATIONS
 ***************************************************************************/

void op_extract_element(unified_value_t *dst, const unified_value_t *src, uint32_t index) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_VECTOR) {
		uint32_t elem_bytes = (src->vec_config.element_bits + 7) / 8;
		uint8_t *src_data = (uint8_t*)src->data + (index * elem_bytes);

		/* Copy element data */
		memcpy(dst->data, src_data, elem_bytes);
		dst->kind = VALUE_KIND_SCALAR;
		dst->word_config.bits = src->vec_config.element_bits;
	}
}

void op_insert_element(unified_value_t *dst, const unified_value_t *vec, const unified_value_t *elem, uint32_t index) {
	if (!dst || !vec || !elem) return;

	if (vec->kind == VALUE_KIND_VECTOR) {
		/* Copy vector to dst first if not same */
		if (dst != vec) {
			memcpy(dst->data, vec->data, vec->data_size);
			dst->kind = vec->kind;
			dst->vec_config = vec->vec_config;
		}

		uint32_t elem_bytes = (vec->vec_config.element_bits + 7) / 8;
		uint8_t *dst_data = (uint8_t*)dst->data + (index * elem_bytes);

		/* Copy element data */
		memcpy(dst_data, elem->data, elem_bytes);
	}
}

void op_broadcast(unified_value_t *dst, const unified_value_t *scalar) {
	if (!dst || !scalar) return;

	if (dst->kind == VALUE_KIND_VECTOR && scalar->kind == VALUE_KIND_SCALAR) {
		for (uint32_t i = 0; i < dst->vec_config.num_elements; i++) {
			op_insert_element(dst, dst, scalar, i);
		}
	}
}

/***************************************************************************
 * REDUCTION OPERATIONS
 ***************************************************************************/

void op_reduce_add(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_VECTOR) {
		unified_value_t sum = *dst;
		uval_set_u64(&sum, 0);

		for (uint32_t i = 0; i < src->vec_config.num_elements; i++) {
			unified_value_t elem;
			op_extract_element(&elem, src, i);
			op_add(&sum, &sum, &elem);
		}

		*dst = sum;
	}
}

void op_reduce_mul(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_VECTOR) {
		unified_value_t prod = *dst;
		uval_set_u64(&prod, 1);

		for (uint32_t i = 0; i < src->vec_config.num_elements; i++) {
			unified_value_t elem;
			op_extract_element(&elem, src, i);
			op_mul(&prod, &prod, &elem);
		}

		*dst = prod;
	}
}

/***************************************************************************
 * TRANSCENDENTAL FUNCTIONS
 ***************************************************************************/

void op_sin(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_SCALAR) {
		double val = uval_get_f64(src);
		uval_set_f64(dst, sin(val));
	} else if (src->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < src->vec_config.num_elements; i++) {
			unified_value_t src_elem, dst_elem;
			op_extract_element(&src_elem, src, i);
			op_sin(&dst_elem, &src_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_cos(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_SCALAR) {
		double val = uval_get_f64(src);
		uval_set_f64(dst, cos(val));
	} else if (src->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < src->vec_config.num_elements; i++) {
			unified_value_t src_elem, dst_elem;
			op_extract_element(&src_elem, src, i);
			op_cos(&dst_elem, &src_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_sqrt(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_SCALAR) {
		double val = uval_get_f64(src);
		uval_set_f64(dst, sqrt(val));
	} else if (src->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < src->vec_config.num_elements; i++) {
			unified_value_t src_elem, dst_elem;
			op_extract_element(&src_elem, src, i);
			op_sqrt(&dst_elem, &src_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

/***************************************************************************
 * UTILITY FUNCTIONS - Configurations for common systems
 ***************************************************************************/

uint64_t get_word_mask(uint32_t bits) {
	if (bits >= 64) {
		return ~0ULL;
	}
	return (1ULL << bits) - 1;
}

word_config_t get_config_pdp1(void) {
	word_config_t cfg = {0};
	cfg.bits = 18;
	cfg.arith_mode = ARITH_ONES_COMPLEMENT;
	cfg.endian = ENDIAN_BIG;
	cfg.is_signed = 1;
	return cfg;
}

word_config_t get_config_pdp6(void) {
	word_config_t cfg = {0};
	cfg.bits = 36;
	cfg.arith_mode = ARITH_ONES_COMPLEMENT;
	cfg.endian = ENDIAN_BIG;
	cfg.is_signed = 1;
	return cfg;
}

word_config_t get_config_pdp10(void) {
	word_config_t cfg = {0};
	cfg.bits = 36;
	cfg.arith_mode = ARITH_TWOS_COMPLEMENT;
	cfg.endian = ENDIAN_BIG;
	cfg.is_signed = 1;
	return cfg;
}

word_config_t get_config_pdp11(void) {
	word_config_t cfg = {0};
	cfg.bits = 16;
	cfg.arith_mode = ARITH_TWOS_COMPLEMENT;
	cfg.endian = ENDIAN_LITTLE;
	cfg.is_signed = 1;
	return cfg;
}

word_config_t get_config_univac1100(void) {
	word_config_t cfg = {0};
	cfg.bits = 36;
	cfg.arith_mode = ARITH_ONES_COMPLEMENT;
	cfg.endian = ENDIAN_BIG;
	cfg.is_signed = 1;
	return cfg;
}

word_config_t get_config_univac2200(void) {
	word_config_t cfg = {0};
	cfg.bits = 36;
	cfg.arith_mode = ARITH_ONES_COMPLEMENT;
	cfg.endian = ENDIAN_BIG;
	cfg.is_signed = 1;
	return cfg;
}

word_config_t get_config_cdc6600(void) {
	word_config_t cfg = {0};
	cfg.bits = 60;
	cfg.arith_mode = ARITH_ONES_COMPLEMENT;
	cfg.endian = ENDIAN_BIG;
	cfg.is_signed = 1;
	return cfg;
}

word_config_t get_config_vax(void) {
	word_config_t cfg = {0};
	cfg.bits = 32;
	cfg.arith_mode = ARITH_TWOS_COMPLEMENT;
	cfg.endian = ENDIAN_LITTLE;
	cfg.is_signed = 1;
	return cfg;
}

word_config_t get_config_ibm360(void) {
	word_config_t cfg = {0};
	cfg.bits = 32;
	cfg.arith_mode = ARITH_TWOS_COMPLEMENT;
	cfg.endian = ENDIAN_BIG;
	cfg.is_signed = 1;
	return cfg;
}

word_config_t get_config_modern_32(void) {
	word_config_t cfg = {0};
	cfg.bits = 32;
	cfg.arith_mode = ARITH_TWOS_COMPLEMENT;
	cfg.endian = ENDIAN_LITTLE;
	cfg.is_signed = 1;
	return cfg;
}

word_config_t get_config_modern_64(void) {
	word_config_t cfg = {0};
	cfg.bits = 64;
	cfg.arith_mode = ARITH_TWOS_COMPLEMENT;
	cfg.endian = ENDIAN_LITTLE;
	cfg.is_signed = 1;
	return cfg;
}

fp_config_t get_fp_config_ieee(void) {
	fp_config_t cfg = {0};
	cfg.format = FP_FORMAT_IEEE_BINARY64;
	cfg.rounding = ROUND_NEAREST_EVEN;
	return cfg;
}

fp_config_t get_fp_config_vax(void) {
	fp_config_t cfg = {0};
	cfg.format = FP_FORMAT_VAX_D;
	cfg.rounding = ROUND_NEAREST_EVEN;
	return cfg;
}

fp_config_t get_fp_config_ibm(void) {
	fp_config_t cfg = {0};
	cfg.format = FP_FORMAT_IBM_LONG;
	cfg.rounding = ROUND_TOWARD_ZERO;
	return cfg;
}

fp_config_t get_fp_config_cray(void) {
	fp_config_t cfg = {0};
	cfg.format = FP_FORMAT_CRAY;
	cfg.rounding = ROUND_TOWARD_ZERO;
	return cfg;
}

int is_zero_in_mode(const unified_value_t *val) {
	if (!val) return 1;

	uint64_t raw = uval_get_u64(val);

	switch (val->word_config.arith_mode) {
		case ARITH_ONES_COMPLEMENT: {
			/* In 1's complement, both 0x0 and all-bits-1 are zero */
			uint64_t mask = get_word_mask(val->word_config.bits);
			return (raw == 0) || (raw == mask);
		}
		default:
			return (raw == 0);
	}
}

void normalize_value(unified_value_t *val) {
	if (!val) return;

	/* For 1's complement, convert -0 to +0 */
	if (val->word_config.arith_mode == ARITH_ONES_COMPLEMENT) {
		uint64_t raw = uval_get_u64(val);
		uint64_t mask = get_word_mask(val->word_config.bits);

		if (raw == mask) {  /* -0 (all bits set) */
			uval_set_u64(val, 0);  /* Convert to +0 */
		}
	}
}

/* BCD placeholder implementations */
void op_bcd_add(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	/* BCD addition would be implemented here */
	(void)dst; (void)a; (void)b;
}

void op_bcd_sub(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	(void)dst; (void)a; (void)b;
}

void op_bcd_mul(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	(void)dst; (void)a; (void)b;
}

void op_bcd_div(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	(void)dst; (void)a; (void)b;
}
