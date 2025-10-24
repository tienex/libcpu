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

/***************************************************************************
 * REMAINING ARITHMETIC OPERATIONS
 ***************************************************************************/

void op_rem(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		if (a->type == VALUE_TYPE_FLOAT) {
			double a_val = uval_get_f64(a);
			double b_val = uval_get_f64(b);
			uval_set_f64(dst, fmod(a_val, b_val));
		} else {
			if (a->word_config.is_signed) {
				int64_t a_val = uval_get_i64(a);
				int64_t b_val = uval_get_i64(b);
				if (b_val != 0) {
					uval_set_i64(dst, a_val % b_val);
				}
			} else {
				uint64_t a_val = uval_get_u64(a);
				uint64_t b_val = uval_get_u64(b);
				if (b_val != 0) {
					uval_set_u64(dst, a_val % b_val);
				}
			}
		}
	} else if (a->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < a->vec_config.num_elements; i++) {
			unified_value_t a_elem, b_elem, dst_elem;
			op_extract_element(&a_elem, a, i);
			op_extract_element(&b_elem, b, i);
			op_rem(&dst_elem, &a_elem, &b_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_abs(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_SCALAR) {
		if (src->type == VALUE_TYPE_FLOAT) {
			double val = uval_get_f64(src);
			uval_set_f64(dst, fabs(val));
		} else if (src->word_config.is_signed) {
			int64_t val = uval_get_i64(src);
			uval_set_i64(dst, (val < 0) ? -val : val);
		} else {
			uval_set_u64(dst, uval_get_u64(src));
		}
	} else if (src->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < src->vec_config.num_elements; i++) {
			unified_value_t src_elem, dst_elem;
			op_extract_element(&src_elem, src, i);
			op_abs(&dst_elem, &src_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_min(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		if (a->type == VALUE_TYPE_FLOAT) {
			double a_val = uval_get_f64(a);
			double b_val = uval_get_f64(b);
			uval_set_f64(dst, (a_val < b_val) ? a_val : b_val);
		} else if (a->word_config.is_signed) {
			int64_t a_val = uval_get_i64(a);
			int64_t b_val = uval_get_i64(b);
			uval_set_i64(dst, (a_val < b_val) ? a_val : b_val);
		} else {
			uint64_t a_val = uval_get_u64(a);
			uint64_t b_val = uval_get_u64(b);
			uval_set_u64(dst, (a_val < b_val) ? a_val : b_val);
		}
	} else if (a->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < a->vec_config.num_elements; i++) {
			unified_value_t a_elem, b_elem, dst_elem;
			op_extract_element(&a_elem, a, i);
			op_extract_element(&b_elem, b, i);
			op_min(&dst_elem, &a_elem, &b_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_max(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		if (a->type == VALUE_TYPE_FLOAT) {
			double a_val = uval_get_f64(a);
			double b_val = uval_get_f64(b);
			uval_set_f64(dst, (a_val > b_val) ? a_val : b_val);
		} else if (a->word_config.is_signed) {
			int64_t a_val = uval_get_i64(a);
			int64_t b_val = uval_get_i64(b);
			uval_set_i64(dst, (a_val > b_val) ? a_val : b_val);
		} else {
			uint64_t a_val = uval_get_u64(a);
			uint64_t b_val = uval_get_u64(b);
			uval_set_u64(dst, (a_val > b_val) ? a_val : b_val);
		}
	} else if (a->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < a->vec_config.num_elements; i++) {
			unified_value_t a_elem, b_elem, dst_elem;
			op_extract_element(&a_elem, a, i);
			op_extract_element(&b_elem, b, i);
			op_max(&dst_elem, &a_elem, &b_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_add_sat(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		if (a->word_config.is_signed) {
			int64_t a_val = uval_get_i64(a);
			int64_t b_val = uval_get_i64(b);
			int64_t sum = a_val + b_val;
			
			int64_t max_val = (1LL << (a->word_config.bits - 1)) - 1;
			int64_t min_val = -(1LL << (a->word_config.bits - 1));
			
			if (sum > max_val) sum = max_val;
			if (sum < min_val) sum = min_val;
			
			uval_set_i64(dst, sum);
		} else {
			uint64_t a_val = uval_get_u64(a);
			uint64_t b_val = uval_get_u64(b);
			uint64_t sum = a_val + b_val;
			uint64_t mask = get_word_mask(a->word_config.bits);
			
			if (sum > mask) sum = mask;
			
			uval_set_u64(dst, sum);
		}
	} else if (a->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < a->vec_config.num_elements; i++) {
			unified_value_t a_elem, b_elem, dst_elem;
			op_extract_element(&a_elem, a, i);
			op_extract_element(&b_elem, b, i);
			op_add_sat(&dst_elem, &a_elem, &b_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_sub_sat(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		if (a->word_config.is_signed) {
			int64_t a_val = uval_get_i64(a);
			int64_t b_val = uval_get_i64(b);
			int64_t diff = a_val - b_val;
			
			int64_t max_val = (1LL << (a->word_config.bits - 1)) - 1;
			int64_t min_val = -(1LL << (a->word_config.bits - 1));
			
			if (diff > max_val) diff = max_val;
			if (diff < min_val) diff = min_val;
			
			uval_set_i64(dst, diff);
		} else {
			uint64_t a_val = uval_get_u64(a);
			uint64_t b_val = uval_get_u64(b);
			
			if (a_val < b_val) {
				uval_set_u64(dst, 0);
			} else {
				uval_set_u64(dst, a_val - b_val);
			}
		}
	} else if (a->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < a->vec_config.num_elements; i++) {
			unified_value_t a_elem, b_elem, dst_elem;
			op_extract_element(&a_elem, a, i);
			op_extract_element(&b_elem, b, i);
			op_sub_sat(&dst_elem, &a_elem, &b_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_mulhi(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		if (a->word_config.is_signed) {
			int64_t a_val = uval_get_i64(a);
			int64_t b_val = uval_get_i64(b);
			__int128 product = (__int128)a_val * (__int128)b_val;
			uval_set_i64(dst, (int64_t)(product >> a->word_config.bits));
		} else {
			uint64_t a_val = uval_get_u64(a);
			uint64_t b_val = uval_get_u64(b);
			__uint128_t product = (__uint128_t)a_val * (__uint128_t)b_val;
			uval_set_u64(dst, (uint64_t)(product >> a->word_config.bits));
		}
	} else if (a->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < a->vec_config.num_elements; i++) {
			unified_value_t a_elem, b_elem, dst_elem;
			op_extract_element(&a_elem, a, i);
			op_extract_element(&b_elem, b, i);
			op_mulhi(&dst_elem, &a_elem, &b_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_mullo(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		uint64_t a_val = uval_get_u64(a);
		uint64_t b_val = uval_get_u64(b);
		uint64_t mask = get_word_mask(a->word_config.bits);
		uval_set_u64(dst, (a_val * b_val) & mask);
	} else if (a->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < a->vec_config.num_elements; i++) {
			unified_value_t a_elem, b_elem, dst_elem;
			op_extract_element(&a_elem, a, i);
			op_extract_element(&b_elem, b, i);
			op_mullo(&dst_elem, &a_elem, &b_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_avg(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		if (a->type == VALUE_TYPE_FLOAT) {
			double a_val = uval_get_f64(a);
			double b_val = uval_get_f64(b);
			uval_set_f64(dst, (a_val + b_val) / 2.0);
		} else {
			uint64_t a_val = uval_get_u64(a);
			uint64_t b_val = uval_get_u64(b);
			uval_set_u64(dst, (a_val + b_val) / 2);
		}
	} else if (a->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < a->vec_config.num_elements; i++) {
			unified_value_t a_elem, b_elem, dst_elem;
			op_extract_element(&a_elem, a, i);
			op_extract_element(&b_elem, b, i);
			op_avg(&dst_elem, &a_elem, &b_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_fma(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, const unified_value_t *c) {
	if (!dst || !a || !b || !c) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		double a_val = uval_get_f64(a);
		double b_val = uval_get_f64(b);
		double c_val = uval_get_f64(c);
		uval_set_f64(dst, fma(a_val, b_val, c_val));
	} else if (a->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < a->vec_config.num_elements; i++) {
			unified_value_t a_elem, b_elem, c_elem, dst_elem;
			op_extract_element(&a_elem, a, i);
			op_extract_element(&b_elem, b, i);
			op_extract_element(&c_elem, c, i);
			op_fma(&dst_elem, &a_elem, &b_elem, &c_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_fms(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, const unified_value_t *c) {
	if (!dst || !a || !b || !c) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		double a_val = uval_get_f64(a);
		double b_val = uval_get_f64(b);
		double c_val = uval_get_f64(c);
		uval_set_f64(dst, fma(a_val, b_val, -c_val));
	} else if (a->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < a->vec_config.num_elements; i++) {
			unified_value_t a_elem, b_elem, c_elem, dst_elem;
			op_extract_element(&a_elem, a, i);
			op_extract_element(&b_elem, b, i);
			op_extract_element(&c_elem, c, i);
			op_fms(&dst_elem, &a_elem, &b_elem, &c_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_fnma(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, const unified_value_t *c) {
	if (!dst || !a || !b || !c) return;

	double a_val = uval_get_f64(a);
	double b_val = uval_get_f64(b);
	double c_val = uval_get_f64(c);
	uval_set_f64(dst, fma(-a_val, b_val, c_val));
}

void op_fnms(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, const unified_value_t *c) {
	if (!dst || !a || !b || !c) return;

	double a_val = uval_get_f64(a);
	double b_val = uval_get_f64(b);
	double c_val = uval_get_f64(c);
	uval_set_f64(dst, fma(-a_val, b_val, -c_val));
}

void op_adc(unified_value_t *dst, unified_value_t *carry, const unified_value_t *a, const unified_value_t *b, const unified_value_t *carry_in) {
	if (!dst || !a || !b || !carry_in) return;

	uint64_t a_val = uval_get_u64(a);
	uint64_t b_val = uval_get_u64(b);
	uint64_t c_val = uval_get_u64(carry_in) & 1;
	
	uint64_t sum = a_val + b_val + c_val;
	uint64_t mask = get_word_mask(a->word_config.bits);
	
	uval_set_u64(dst, sum & mask);
	if (carry) {
		uval_set_u64(carry, (sum > mask) ? 1 : 0);
	}
}

void op_sbb(unified_value_t *dst, unified_value_t *borrow, const unified_value_t *a, const unified_value_t *b, const unified_value_t *borrow_in) {
	if (!dst || !a || !b || !borrow_in) return;

	uint64_t a_val = uval_get_u64(a);
	uint64_t b_val = uval_get_u64(b);
	uint64_t bor_val = uval_get_u64(borrow_in) & 1;
	
	uint64_t diff = a_val - b_val - bor_val;
	uint64_t mask = get_word_mask(a->word_config.bits);
	
	uval_set_u64(dst, diff & mask);
	if (borrow) {
		uval_set_u64(borrow, (a_val < (b_val + bor_val)) ? 1 : 0);
	}
}

/***************************************************************************
 * ADDITIONAL BITWISE OPERATIONS
 ***************************************************************************/

void op_nand(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	uint64_t a_val = uval_get_u64(a);
	uint64_t b_val = uval_get_u64(b);
	uint64_t mask = get_word_mask(a->word_config.bits);
	uval_set_u64(dst, ~(a_val & b_val) & mask);
}

void op_nor(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	uint64_t a_val = uval_get_u64(a);
	uint64_t b_val = uval_get_u64(b);
	uint64_t mask = get_word_mask(a->word_config.bits);
	uval_set_u64(dst, ~(a_val | b_val) & mask);
}

void op_xnor(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	uint64_t a_val = uval_get_u64(a);
	uint64_t b_val = uval_get_u64(b);
	uint64_t mask = get_word_mask(a->word_config.bits);
	uval_set_u64(dst, ~(a_val ^ b_val) & mask);
}

void op_andn(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	uint64_t a_val = uval_get_u64(a);
	uint64_t b_val = uval_get_u64(b);
	uint64_t mask = get_word_mask(a->word_config.bits);
	uval_set_u64(dst, (a_val & ~b_val) & mask);
}

void op_orn(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	uint64_t a_val = uval_get_u64(a);
	uint64_t b_val = uval_get_u64(b);
	uint64_t mask = get_word_mask(a->word_config.bits);
	uval_set_u64(dst, (a_val | ~b_val) & mask);
}

void op_slli(unified_value_t *dst, const unified_value_t *src, uint32_t shift) {
	if (!dst || !src) return;

	uint64_t val = uval_get_u64(src);
	uint64_t mask = get_word_mask(src->word_config.bits);
	
	if (shift < src->word_config.bits) {
		uval_set_u64(dst, (val << shift) & mask);
	} else {
		uval_set_u64(dst, 0);
	}
}

void op_srli(unified_value_t *dst, const unified_value_t *src, uint32_t shift) {
	if (!dst || !src) return;

	uint64_t val = uval_get_u64(src);
	
	if (shift < src->word_config.bits) {
		uval_set_u64(dst, val >> shift);
	} else {
		uval_set_u64(dst, 0);
	}
}

void op_srai(unified_value_t *dst, const unified_value_t *src, uint32_t shift) {
	if (!dst || !src) return;

	int64_t val = uval_get_i64(src);
	
	if (shift < src->word_config.bits) {
		uval_set_i64(dst, val >> shift);
	} else {
		uval_set_i64(dst, val < 0 ? -1 : 0);
	}
}

void op_roli(unified_value_t *dst, const unified_value_t *src, uint32_t shift) {
	if (!dst || !src) return;

	uint64_t val = uval_get_u64(src);
	uint32_t bits = src->word_config.bits;
	shift = shift % bits;
	uint64_t mask = get_word_mask(bits);
	
	uint64_t result = ((val << shift) | (val >> (bits - shift))) & mask;
	uval_set_u64(dst, result);
}

void op_rori(unified_value_t *dst, const unified_value_t *src, uint32_t shift) {
	if (!dst || !src) return;

	uint64_t val = uval_get_u64(src);
	uint32_t bits = src->word_config.bits;
	shift = shift % bits;
	uint64_t mask = get_word_mask(bits);
	
	uint64_t result = ((val >> shift) | (val << (bits - shift))) & mask;
	uval_set_u64(dst, result);
}

void op_funnel_shl(unified_value_t *dst, const unified_value_t *hi, const unified_value_t *lo, const unified_value_t *shift) {
	if (!dst || !hi || !lo || !shift) return;

	uint64_t hi_val = uval_get_u64(hi);
	uint64_t lo_val = uval_get_u64(lo);
	uint32_t shift_amt = uval_get_u64(shift);
	uint32_t bits = hi->word_config.bits;
	
	__uint128_t concat = ((__uint128_t)hi_val << bits) | lo_val;
	__uint128_t shifted = concat << shift_amt;
	
	uval_set_u64(dst, (shifted >> bits) & get_word_mask(bits));
}

void op_funnel_shr(unified_value_t *dst, const unified_value_t *hi, const unified_value_t *lo, const unified_value_t *shift) {
	if (!dst || !hi || !lo || !shift) return;

	uint64_t hi_val = uval_get_u64(hi);
	uint64_t lo_val = uval_get_u64(lo);
	uint32_t shift_amt = uval_get_u64(shift);
	uint32_t bits = hi->word_config.bits;
	
	__uint128_t concat = ((__uint128_t)hi_val << bits) | lo_val;
	__uint128_t shifted = concat >> shift_amt;
	
	uval_set_u64(dst, shifted & get_word_mask(bits));
}

/***************************************************************************
 * ADDITIONAL BIT MANIPULATION
 ***************************************************************************/

void op_clo(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	uint64_t val = uval_get_u64(src);
	uint32_t bits = src->word_config.bits;
	uint32_t count = 0;
	uint64_t test_bit = 1ULL << (bits - 1);
	
	while ((val & test_bit) && count < bits) {
		count++;
		test_bit >>= 1;
	}
	
	uval_set_u64(dst, count);
}

void op_cto(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	uint64_t val = uval_get_u64(src);
	uint32_t bits = src->word_config.bits;
	uint32_t count = 0;
	
	while ((val & 1) && count < bits) {
		count++;
		val >>= 1;
	}
	
	uval_set_u64(dst, count);
}

void op_parity(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	uint64_t val = uval_get_u64(src);
	uint32_t count = 0;
	
	while (val) {
		count += val & 1;
		val >>= 1;
	}
	
	uval_set_u64(dst, count & 1);
}

void op_ffs(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	uint64_t val = uval_get_u64(src);
	
	if (val == 0) {
		uval_set_u64(dst, 0);
		return;
	}
	
	uint32_t pos = 0;
	while ((val & 1) == 0) {
		pos++;
		val >>= 1;
	}
	
	uval_set_u64(dst, pos + 1);
}

void op_fls(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	uint64_t val = uval_get_u64(src);
	uint32_t bits = src->word_config.bits;
	
	if (val == 0) {
		uval_set_u64(dst, 0);
		return;
	}
	
	uint32_t pos = bits;
	uint64_t test_bit = 1ULL << (bits - 1);
	
	while ((val & test_bit) == 0) {
		pos--;
		test_bit >>= 1;
	}
	
	uval_set_u64(dst, pos);
}

void op_ffc(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	uint64_t val = uval_get_u64(src);
	uint32_t bits = src->word_config.bits;
	
	if (val == get_word_mask(bits)) {
		uval_set_u64(dst, 0);
		return;
	}
	
	uint32_t pos = 0;
	while ((val & 1)) {
		pos++;
		val >>= 1;
	}
	
	uval_set_u64(dst, pos + 1);
}

void op_bit_deposit(unified_value_t *dst, const unified_value_t *src, const unified_value_t *mask) {
	if (!dst || !src || !mask) return;

	uint64_t src_val = uval_get_u64(src);
	uint64_t mask_val = uval_get_u64(mask);
	uint64_t result = 0;
	uint32_t src_bit = 0;
	
	for (uint32_t i = 0; i < 64; i++) {
		if (mask_val & (1ULL << i)) {
			if (src_val & (1ULL << src_bit)) {
				result |= (1ULL << i);
			}
			src_bit++;
		}
	}
	
	uval_set_u64(dst, result);
}

void op_bit_extract(unified_value_t *dst, const unified_value_t *src, const unified_value_t *mask) {
	if (!dst || !src || !mask) return;

	uint64_t src_val = uval_get_u64(src);
	uint64_t mask_val = uval_get_u64(mask);
	uint64_t result = 0;
	uint32_t dst_bit = 0;
	
	for (uint32_t i = 0; i < 64; i++) {
		if (mask_val & (1ULL << i)) {
			if (src_val & (1ULL << i)) {
				result |= (1ULL << dst_bit);
			}
			dst_bit++;
		}
	}
	
	uval_set_u64(dst, result);
}

void op_bitfield_set(unified_value_t *dst, const unified_value_t *src, uint32_t offset, uint32_t width) {
	if (!dst || !src) return;

	uint64_t val = uval_get_u64(src);
	uint64_t mask = ((1ULL << width) - 1) << offset;
	
	uval_set_u64(dst, val | mask);
}

void op_bitfield_clear(unified_value_t *dst, const unified_value_t *src, uint32_t offset, uint32_t width) {
	if (!dst || !src) return;

	uint64_t val = uval_get_u64(src);
	uint64_t mask = ((1ULL << width) - 1) << offset;
	
	uval_set_u64(dst, val & ~mask);
}

void op_bitfield_toggle(unified_value_t *dst, const unified_value_t *src, uint32_t offset, uint32_t width) {
	if (!dst || !src) return;

	uint64_t val = uval_get_u64(src);
	uint64_t mask = ((1ULL << width) - 1) << offset;
	
	uval_set_u64(dst, val ^ mask);
}

void op_bitfield_test(unified_value_t *dst, const unified_value_t *src, uint32_t offset, uint32_t width) {
	if (!dst || !src) return;

	uint64_t val = uval_get_u64(src);
	uint64_t mask = ((1ULL << width) - 1) << offset;
	
	uval_set_u64(dst, ((val & mask) != 0) ? 1 : 0);
}

void op_bitrev(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	uint64_t val = uval_get_u64(src);
	uint32_t bits = src->word_config.bits;
	uint64_t result = 0;
	
	for (uint32_t i = 0; i < bits; i++) {
		if (val & (1ULL << i)) {
			result |= (1ULL << (bits - 1 - i));
		}
	}
	
	uval_set_u64(dst, result);
}

void op_bswap(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	uint64_t val = uval_get_u64(src);
	uint64_t result = 0;
	uint32_t bytes = (src->word_config.bits + 7) / 8;
	
	for (uint32_t i = 0; i < bytes; i++) {
		uint64_t byte = (val >> (i * 8)) & 0xFF;
		result |= byte << ((bytes - 1 - i) * 8);
	}
	
	uval_set_u64(dst, result);
}

/***************************************************************************
 * BYTE OPERATIONS
 ***************************************************************************/

void op_byte_extract(unified_value_t *dst, const unified_value_t *src, uint32_t index) {
	if (!dst || !src) return;

	uint64_t val = uval_get_u64(src);
	uint8_t byte = (val >> (index * 8)) & 0xFF;
	
	uval_set_u64(dst, byte);
}

void op_byte_insert(unified_value_t *dst, const unified_value_t *base, const unified_value_t *byte, uint32_t index) {
	if (!dst || !base || !byte) return;

	uint64_t base_val = uval_get_u64(base);
	uint64_t byte_val = uval_get_u64(byte) & 0xFF;
	uint64_t mask = ~(0xFFULL << (index * 8));
	
	uint64_t result = (base_val & mask) | (byte_val << (index * 8));
	uval_set_u64(dst, result);
}

void op_byte_shuffle(unified_value_t *dst, const unified_value_t *src, const uint8_t *pattern, uint32_t pattern_len) {
	if (!dst || !src || !pattern) return;

	uint64_t src_val = uval_get_u64(src);
	uint64_t result = 0;
	
	for (uint32_t i = 0; i < pattern_len && i < 8; i++) {
		uint32_t src_idx = pattern[i];
		if (src_idx < 8) {
			uint8_t byte = (src_val >> (src_idx * 8)) & 0xFF;
			result |= ((uint64_t)byte << (i * 8));
		}
	}
	
	uval_set_u64(dst, result);
}

/***************************************************************************
 * COMPARISON OPERATIONS
 ***************************************************************************/

void op_cmpne(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		if (a->type == VALUE_TYPE_FLOAT) {
			double a_val = uval_get_f64(a);
			double b_val = uval_get_f64(b);
			uval_set_u64(dst, (a_val != b_val) ? ~0ULL : 0);
		} else {
			uint64_t a_val = uval_get_u64(a);
			uint64_t b_val = uval_get_u64(b);
			uval_set_u64(dst, (a_val != b_val) ? ~0ULL : 0);
		}
	} else if (a->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < a->vec_config.num_elements; i++) {
			unified_value_t a_elem, b_elem, dst_elem;
			op_extract_element(&a_elem, a, i);
			op_extract_element(&b_elem, b, i);
			op_cmpne(&dst_elem, &a_elem, &b_elem);
			op_insert_element(dst, dst, &dst_elem, i);
		}
	}
}

void op_cmple(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		if (a->type == VALUE_TYPE_FLOAT) {
			double a_val = uval_get_f64(a);
			double b_val = uval_get_f64(b);
			uval_set_u64(dst, (a_val <= b_val) ? ~0ULL : 0);
		} else if (a->word_config.is_signed) {
			int64_t a_val = uval_get_i64(a);
			int64_t b_val = uval_get_i64(b);
			uval_set_u64(dst, (a_val <= b_val) ? ~0ULL : 0);
		} else {
			uint64_t a_val = uval_get_u64(a);
			uint64_t b_val = uval_get_u64(b);
			uval_set_u64(dst, (a_val <= b_val) ? ~0ULL : 0);
		}
	}
}

void op_cmpgt(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		if (a->type == VALUE_TYPE_FLOAT) {
			double a_val = uval_get_f64(a);
			double b_val = uval_get_f64(b);
			uval_set_u64(dst, (a_val > b_val) ? ~0ULL : 0);
		} else if (a->word_config.is_signed) {
			int64_t a_val = uval_get_i64(a);
			int64_t b_val = uval_get_i64(b);
			uval_set_u64(dst, (a_val > b_val) ? ~0ULL : 0);
		} else {
			uint64_t a_val = uval_get_u64(a);
			uint64_t b_val = uval_get_u64(b);
			uval_set_u64(dst, (a_val > b_val) ? ~0ULL : 0);
		}
	}
}

void op_cmpge(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_SCALAR) {
		if (a->type == VALUE_TYPE_FLOAT) {
			double a_val = uval_get_f64(a);
			double b_val = uval_get_f64(b);
			uval_set_u64(dst, (a_val >= b_val) ? ~0ULL : 0);
		} else if (a->word_config.is_signed) {
			int64_t a_val = uval_get_i64(a);
			int64_t b_val = uval_get_i64(b);
			uval_set_u64(dst, (a_val >= b_val) ? ~0ULL : 0);
		} else {
			uint64_t a_val = uval_get_u64(a);
			uint64_t b_val = uval_get_u64(b);
			uval_set_u64(dst, (a_val >= b_val) ? ~0ULL : 0);
		}
	}
}

void op_cmpord(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	double a_val = uval_get_f64(a);
	double b_val = uval_get_f64(b);
	
	int ordered = !isnan(a_val) && !isnan(b_val);
	uval_set_u64(dst, ordered ? ~0ULL : 0);
}

void op_cmpunord(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	double a_val = uval_get_f64(a);
	double b_val = uval_get_f64(b);
	
	int unordered = isnan(a_val) || isnan(b_val);
	uval_set_u64(dst, unordered ? ~0ULL : 0);
}

void op_select(unified_value_t *dst, const unified_value_t *cond, const unified_value_t *true_val, const unified_value_t *false_val) {
	if (!dst || !cond || !true_val || !false_val) return;

	uint64_t cond_val = uval_get_u64(cond);
	
	if (cond_val) {
		memcpy(dst->data, true_val->data, true_val->data_size);
	} else {
		memcpy(dst->data, false_val->data, false_val->data_size);
	}
}

void op_blend(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, const unified_value_t *mask) {
	if (!dst || !a || !b || !mask) return;

	uint64_t mask_val = uval_get_u64(mask);
	uint64_t a_val = uval_get_u64(a);
	uint64_t b_val = uval_get_u64(b);
	
	uint64_t result = (a_val & mask_val) | (b_val & ~mask_val);
	uval_set_u64(dst, result);
}


/***************************************************************************
 * CONVERSION OPERATIONS
 ***************************************************************************/

void op_convert(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (dst->type == VALUE_TYPE_FLOAT && src->type == VALUE_TYPE_INTEGER) {
		op_cvt_i2f(dst, src);
	} else if (dst->type == VALUE_TYPE_INTEGER && src->type == VALUE_TYPE_FLOAT) {
		op_cvt_f2i(dst, src);
	} else {
		memcpy(dst->data, src->data, src->data_size);
	}
}

void op_cvt_i2f(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (src->word_config.is_signed) {
		int64_t val = uval_get_i64(src);
		uval_set_f64(dst, (double)val);
	} else {
		uint64_t val = uval_get_u64(src);
		uval_set_f64(dst, (double)val);
	}
}

void op_cvt_f2i(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_i64(dst, (int64_t)round(val));
}

void op_cvt_f2i_trunc(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_i64(dst, (int64_t)trunc(val));
}

void op_cvt_f2i_floor(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_i64(dst, (int64_t)floor(val));
}

void op_cvt_f2i_ceil(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_i64(dst, (int64_t)ceil(val));
}

void op_extend_signed(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	int64_t val = uval_get_i64(src);
	uval_set_i64(dst, val);
}

void op_extend_unsigned(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	uint64_t val = uval_get_u64(src);
	uval_set_u64(dst, val);
}

void op_truncate(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	uint64_t val = uval_get_u64(src);
	uint64_t mask = get_word_mask(dst->word_config.bits);
	uval_set_u64(dst, val & mask);
}

void op_saturate(unified_value_t *dst, const unified_value_t *src, int64_t min, int64_t max) {
	if (!dst || !src) return;

	int64_t val = uval_get_i64(src);
	
	if (val < min) val = min;
	if (val > max) val = max;
	
	uval_set_i64(dst, val);
}

void op_fp_convert_format(unified_value_t *dst, const unified_value_t *src, fp_format_t dst_fmt, fp_format_t src_fmt) {
	if (!dst || !src) return;

	double intermediate;
	
	// Convert src to IEEE double
	fp_config_t old_cfg = src->fp_config;
	unified_value_t temp = *src;
	temp.fp_config.format = src_fmt;
	intermediate = uval_get_f64(&temp);
	
	// Convert IEEE double to dst format
	dst->fp_config.format = dst_fmt;
	uval_set_f64(dst, intermediate);
}

void op_cvt_binary_to_bcd(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	uint64_t val = uval_get_u64(src);
	uint64_t bcd = 0;
	uint32_t shift = 0;
	
	while (val > 0 && shift < 64) {
		uint64_t digit = val % 10;
		bcd |= (digit << shift);
		val /= 10;
		shift += 4;
	}
	
	uval_set_u64(dst, bcd);
}

void op_cvt_bcd_to_binary(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	uint64_t bcd = uval_get_u64(src);
	uint64_t val = 0;
	uint64_t multiplier = 1;
	
	while (bcd > 0) {
		uint64_t digit = bcd & 0xF;
		if (digit > 9) digit = 0;  // Invalid BCD
		val += digit * multiplier;
		multiplier *= 10;
		bcd >>= 4;
	}
	
	uval_set_u64(dst, val);
}

void op_cvt_binary_to_decimal(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	// Placeholder for decimal conversion
	uval_set_f64(dst, val);
}

void op_cvt_decimal_to_binary(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_f64(dst, val);
}

/***************************************************************************
 * PACK/UNPACK OPERATIONS
 ***************************************************************************/

void op_pack(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	// Pack two values into narrower elements
	// Implementation depends on element widths
	if (a->kind == VALUE_KIND_VECTOR && b->kind == VALUE_KIND_VECTOR) {
		uint32_t half = a->vec_config.num_elements;
		
		for (uint32_t i = 0; i < half; i++) {
			unified_value_t elem;
			op_extract_element(&elem, a, i);
			op_insert_element(dst, dst, &elem, i);
		}
		
		for (uint32_t i = 0; i < half; i++) {
			unified_value_t elem;
			op_extract_element(&elem, b, i);
			op_insert_element(dst, dst, &elem, half + i);
		}
	}
}

void op_pack_sat_signed(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	// Pack with signed saturation
	op_pack(dst, a, b);
}

void op_pack_sat_unsigned(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	// Pack with unsigned saturation
	op_pack(dst, a, b);
}

void op_unpack_low(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_VECTOR) {
		uint32_t half = src->vec_config.num_elements / 2;
		
		for (uint32_t i = 0; i < half; i++) {
			unified_value_t elem;
			op_extract_element(&elem, src, i);
			op_insert_element(dst, dst, &elem, i);
		}
	}
}

void op_unpack_high(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_VECTOR) {
		uint32_t half = src->vec_config.num_elements / 2;
		
		for (uint32_t i = 0; i < half; i++) {
			unified_value_t elem;
			op_extract_element(&elem, src, half + i);
			op_insert_element(dst, dst, &elem, i);
		}
	}
}

void op_unpack_low_signed(unified_value_t *dst, const unified_value_t *src) {
	op_unpack_low(dst, src);
}

void op_unpack_high_signed(unified_value_t *dst, const unified_value_t *src) {
	op_unpack_high(dst, src);
}

void op_interleave_low(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_VECTOR) {
		uint32_t half = a->vec_config.num_elements / 2;
		
		for (uint32_t i = 0; i < half; i++) {
			unified_value_t a_elem, b_elem;
			op_extract_element(&a_elem, a, i);
			op_extract_element(&b_elem, b, i);
			op_insert_element(dst, dst, &a_elem, i * 2);
			op_insert_element(dst, dst, &b_elem, i * 2 + 1);
		}
	}
}

void op_interleave_high(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_VECTOR) {
		uint32_t half = a->vec_config.num_elements / 2;
		
		for (uint32_t i = 0; i < half; i++) {
			unified_value_t a_elem, b_elem;
			op_extract_element(&a_elem, a, half + i);
			op_extract_element(&b_elem, b, half + i);
			op_insert_element(dst, dst, &a_elem, i * 2);
			op_insert_element(dst, dst, &b_elem, i * 2 + 1);
		}
	}
}

void op_deinterleave_even(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_VECTOR) {
		uint32_t half = src->vec_config.num_elements / 2;
		
		for (uint32_t i = 0; i < half; i++) {
			unified_value_t elem;
			op_extract_element(&elem, src, i * 2);
			op_insert_element(dst, dst, &elem, i);
		}
	}
}

void op_deinterleave_odd(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_VECTOR) {
		uint32_t half = src->vec_config.num_elements / 2;
		
		for (uint32_t i = 0; i < half; i++) {
			unified_value_t elem;
			op_extract_element(&elem, src, i * 2 + 1);
			op_insert_element(dst, dst, &elem, i);
		}
	}
}

/***************************************************************************
 * SHUFFLE/PERMUTE OPERATIONS
 ***************************************************************************/

void op_shuffle(unified_value_t *dst, const unified_value_t *src, const unified_value_t *indices) {
	if (!dst || !src || !indices) return;

	if (src->kind == VALUE_KIND_VECTOR) {
		for (uint32_t i = 0; i < dst->vec_config.num_elements; i++) {
			unified_value_t idx_elem, src_elem;
			op_extract_element(&idx_elem, indices, i);
			uint32_t idx = uval_get_u64(&idx_elem);
			
			if (idx < src->vec_config.num_elements) {
				op_extract_element(&src_elem, src, idx);
				op_insert_element(dst, dst, &src_elem, i);
			}
		}
	}
}

void op_shuffle2(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, const unified_value_t *indices) {
	if (!dst || !a || !b || !indices) return;

	if (a->kind == VALUE_KIND_VECTOR) {
		uint32_t total_elems = a->vec_config.num_elements + b->vec_config.num_elements;
		
		for (uint32_t i = 0; i < dst->vec_config.num_elements; i++) {
			unified_value_t idx_elem, elem;
			op_extract_element(&idx_elem, indices, i);
			uint32_t idx = uval_get_u64(&idx_elem);
			
			if (idx < a->vec_config.num_elements) {
				op_extract_element(&elem, a, idx);
			} else if (idx < total_elems) {
				op_extract_element(&elem, b, idx - a->vec_config.num_elements);
			}
			
			op_insert_element(dst, dst, &elem, i);
		}
	}
}

void op_permute(unified_value_t *dst, const unified_value_t *src, const unified_value_t *control) {
	op_shuffle(dst, src, control);
}

void op_splat(unified_value_t *dst, const unified_value_t *vec, uint32_t index) {
	if (!dst || !vec) return;

	unified_value_t elem;
	op_extract_element(&elem, vec, index);
	op_broadcast(dst, &elem);
}

void op_reverse(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_SCALAR) {
		op_bitrev(dst, src);
	} else if (src->kind == VALUE_KIND_VECTOR) {
		uint32_t n = src->vec_config.num_elements;
		
		for (uint32_t i = 0; i < n; i++) {
			unified_value_t elem;
			op_extract_element(&elem, src, n - 1 - i);
			op_insert_element(dst, dst, &elem, i);
		}
	}
}

void op_reverse_bytes(unified_value_t *dst, const unified_value_t *src) {
	op_bswap(dst, src);
}

void op_rotate_elements(unified_value_t *dst, const unified_value_t *src, int32_t count) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_VECTOR) {
		uint32_t n = src->vec_config.num_elements;
		count = count % (int32_t)n;
		if (count < 0) count += n;
		
		for (uint32_t i = 0; i < n; i++) {
			unified_value_t elem;
			uint32_t src_idx = (i + count) % n;
			op_extract_element(&elem, src, src_idx);
			op_insert_element(dst, dst, &elem, i);
		}
	}
}

void op_concat_extract(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, uint32_t offset) {
	if (!dst || !a || !b) return;

	// Concatenate a and b, then extract from offset
	if (a->kind == VALUE_KIND_VECTOR) {
		uint32_t total = a->vec_config.num_elements + b->vec_config.num_elements;
		
		for (uint32_t i = 0; i < dst->vec_config.num_elements; i++) {
			uint32_t src_idx = i + offset;
			unified_value_t elem;
			
			if (src_idx < a->vec_config.num_elements) {
				op_extract_element(&elem, a, src_idx);
			} else if (src_idx < total) {
				op_extract_element(&elem, b, src_idx - a->vec_config.num_elements);
			}
			
			op_insert_element(dst, dst, &elem, i);
		}
	}
}

/***************************************************************************
 * REDUCTION OPERATIONS
 ***************************************************************************/

void op_reduce_min(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_VECTOR) {
		unified_value_t min_val;
		op_extract_element(&min_val, src, 0);
		
		for (uint32_t i = 1; i < src->vec_config.num_elements; i++) {
			unified_value_t elem;
			op_extract_element(&elem, src, i);
			op_min(&min_val, &min_val, &elem);
		}
		
		*dst = min_val;
	}
}

void op_reduce_max(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_VECTOR) {
		unified_value_t max_val;
		op_extract_element(&max_val, src, 0);
		
		for (uint32_t i = 1; i < src->vec_config.num_elements; i++) {
			unified_value_t elem;
			op_extract_element(&elem, src, i);
			op_max(&max_val, &max_val, &elem);
		}
		
		*dst = max_val;
	}
}

void op_reduce_and(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_VECTOR) {
		unified_value_t result;
		op_extract_element(&result, src, 0);
		
		for (uint32_t i = 1; i < src->vec_config.num_elements; i++) {
			unified_value_t elem;
			op_extract_element(&elem, src, i);
			op_and(&result, &result, &elem);
		}
		
		*dst = result;
	}
}

void op_reduce_or(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_VECTOR) {
		unified_value_t result;
		op_extract_element(&result, src, 0);
		
		for (uint32_t i = 1; i < src->vec_config.num_elements; i++) {
			unified_value_t elem;
			op_extract_element(&elem, src, i);
			op_or(&result, &result, &elem);
		}
		
		*dst = result;
	}
}

void op_reduce_xor(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_VECTOR) {
		unified_value_t result;
		op_extract_element(&result, src, 0);
		
		for (uint32_t i = 1; i < src->vec_config.num_elements; i++) {
			unified_value_t elem;
			op_extract_element(&elem, src, i);
			op_xor(&result, &result, &elem);
		}
		
		*dst = result;
	}
}

void op_reduce_minidx(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_VECTOR) {
		uint32_t min_idx = 0;
		unified_value_t min_val;
		op_extract_element(&min_val, src, 0);
		
		for (uint32_t i = 1; i < src->vec_config.num_elements; i++) {
			unified_value_t elem, cmp;
			op_extract_element(&elem, src, i);
			op_cmplt(&cmp, &elem, &min_val);
			
			if (uval_get_u64(&cmp)) {
				min_val = elem;
				min_idx = i;
			}
		}
		
		uval_set_u64(dst, min_idx);
	}
}

void op_reduce_maxidx(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	if (src->kind == VALUE_KIND_VECTOR) {
		uint32_t max_idx = 0;
		unified_value_t max_val;
		op_extract_element(&max_val, src, 0);
		
		for (uint32_t i = 1; i < src->vec_config.num_elements; i++) {
			unified_value_t elem, cmp;
			op_extract_element(&elem, src, i);
			op_cmpgt(&cmp, &elem, &max_val);
			
			if (uval_get_u64(&cmp)) {
				max_val = elem;
				max_idx = i;
			}
		}
		
		uval_set_u64(dst, max_idx);
	}
}

/***************************************************************************
 * HORIZONTAL OPERATIONS
 ***************************************************************************/

void op_hadd(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_VECTOR) {
		uint32_t half = a->vec_config.num_elements / 2;
		
		for (uint32_t i = 0; i < half; i++) {
			unified_value_t a1, a2, sum;
			op_extract_element(&a1, a, i * 2);
			op_extract_element(&a2, a, i * 2 + 1);
			op_add(&sum, &a1, &a2);
			op_insert_element(dst, dst, &sum, i);
		}
		
		for (uint32_t i = 0; i < half; i++) {
			unified_value_t b1, b2, sum;
			op_extract_element(&b1, b, i * 2);
			op_extract_element(&b2, b, i * 2 + 1);
			op_add(&sum, &b1, &b2);
			op_insert_element(dst, dst, &sum, half + i);
		}
	}
}

void op_hsub(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_VECTOR) {
		uint32_t half = a->vec_config.num_elements / 2;
		
		for (uint32_t i = 0; i < half; i++) {
			unified_value_t a1, a2, diff;
			op_extract_element(&a1, a, i * 2);
			op_extract_element(&a2, a, i * 2 + 1);
			op_sub(&diff, &a1, &a2);
			op_insert_element(dst, dst, &diff, i);
		}
		
		for (uint32_t i = 0; i < half; i++) {
			unified_value_t b1, b2, diff;
			op_extract_element(&b1, b, i * 2);
			op_extract_element(&b2, b, i * 2 + 1);
			op_sub(&diff, &b1, &b2);
			op_insert_element(dst, dst, &diff, half + i);
		}
	}
}

void op_hadds(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_VECTOR) {
		uint32_t half = a->vec_config.num_elements / 2;
		
		for (uint32_t i = 0; i < half; i++) {
			unified_value_t a1, a2, sum;
			op_extract_element(&a1, a, i * 2);
			op_extract_element(&a2, a, i * 2 + 1);
			op_add_sat(&sum, &a1, &a2);
			op_insert_element(dst, dst, &sum, i);
		}
		
		for (uint32_t i = 0; i < half; i++) {
			unified_value_t b1, b2, sum;
			op_extract_element(&b1, b, i * 2);
			op_extract_element(&b2, b, i * 2 + 1);
			op_add_sat(&sum, &b1, &b2);
			op_insert_element(dst, dst, &sum, half + i);
		}
	}
}

void op_hsubs(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_VECTOR) {
		uint32_t half = a->vec_config.num_elements / 2;
		
		for (uint32_t i = 0; i < half; i++) {
			unified_value_t a1, a2, diff;
			op_extract_element(&a1, a, i * 2);
			op_extract_element(&a2, a, i * 2 + 1);
			op_sub_sat(&diff, &a1, &a2);
			op_insert_element(dst, dst, &diff, i);
		}
		
		for (uint32_t i = 0; i < half; i++) {
			unified_value_t b1, b2, diff;
			op_extract_element(&b1, b, i * 2);
			op_extract_element(&b2, b, i * 2 + 1);
			op_sub_sat(&diff, &b1, &b2);
			op_insert_element(dst, dst, &diff, half + i);
		}
	}
}

void op_pairwise_add(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	op_hadd(dst, a, b);
}

/***************************************************************************
 * DOT PRODUCT / MATRIX OPERATIONS
 ***************************************************************************/

void op_dot(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->kind == VALUE_KIND_VECTOR) {
		unified_value_t sum;
		uval_set_f64(&sum, 0.0);
		
		for (uint32_t i = 0; i < a->vec_config.num_elements; i++) {
			unified_value_t a_elem, b_elem, prod, new_sum;
			op_extract_element(&a_elem, a, i);
			op_extract_element(&b_elem, b, i);
			op_mul(&prod, &a_elem, &b_elem);
			op_add(&new_sum, &sum, &prod);
			sum = new_sum;
		}
		
		*dst = sum;
	}
}

void op_dot4(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	unified_value_t sum;
	uval_set_f64(&sum, 0.0);
	
	for (uint32_t i = 0; i < 4; i++) {
		unified_value_t a_elem, b_elem, prod, new_sum;
		op_extract_element(&a_elem, a, i);
		op_extract_element(&b_elem, b, i);
		op_mul(&prod, &a_elem, &b_elem);
		op_add(&new_sum, &sum, &prod);
		sum = new_sum;
	}
	
	*dst = sum;
}

void op_matrix_mul(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	// Matrix multiplication placeholder
	(void)dst; (void)a; (void)b;
}

void op_matrix_mul_acc(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	// Matrix multiply-accumulate placeholder
	(void)dst; (void)a; (void)b;
}

void op_tile_load(unified_value_t *dst, const void *ptr, size_t stride) {
	// Tile load placeholder
	(void)dst; (void)ptr; (void)stride;
}

void op_tile_store(void *ptr, const unified_value_t *src, size_t stride) {
	// Tile store placeholder
	(void)ptr; (void)src; (void)stride;
}


/***************************************************************************
 * FLOATING-POINT MATH OPERATIONS
 ***************************************************************************/

void op_rsqrt(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_f64(dst, 1.0 / sqrt(val));
}

void op_rcp(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_f64(dst, 1.0 / val);
}

void op_round(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_f64(dst, round(val));
}

void op_floor(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_f64(dst, floor(val));
}

void op_ceil(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_f64(dst, ceil(val));
}

void op_trunc(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_f64(dst, trunc(val));
}

void op_fract(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	double int_part;
	double frac = modf(val, &int_part);
	uval_set_f64(dst, frac);
}

void op_modf(unified_value_t *int_part, unified_value_t *frac_part, const unified_value_t *src) {
	if (!int_part || !frac_part || !src) return;

	double val = uval_get_f64(src);
	double ip;
	double fp = modf(val, &ip);
	
	uval_set_f64(int_part, ip);
	uval_set_f64(frac_part, fp);
}

void op_ldexp(unified_value_t *dst, const unified_value_t *mantissa, const unified_value_t *exp) {
	if (!dst || !mantissa || !exp) return;

	double m = uval_get_f64(mantissa);
	int e = (int)uval_get_i64(exp);
	uval_set_f64(dst, ldexp(m, e));
}

void op_frexp(unified_value_t *mantissa, unified_value_t *exp, const unified_value_t *src) {
	if (!mantissa || !exp || !src) return;

	double val = uval_get_f64(src);
	int e;
	double m = frexp(val, &e);
	
	uval_set_f64(mantissa, m);
	uval_set_i64(exp, e);
}

void op_logb(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_f64(dst, logb(val));
}

void op_scalbn(unified_value_t *dst, const unified_value_t *src, const unified_value_t *n) {
	if (!dst || !src || !n) return;

	double val = uval_get_f64(src);
	int exp = (int)uval_get_i64(n);
	uval_set_f64(dst, scalbn(val, exp));
}

void op_fpclassify(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_i64(dst, fpclassify(val));
}

void op_isnan(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_u64(dst, isnan(val) ? 1 : 0);
}

void op_isinf(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_u64(dst, isinf(val) ? 1 : 0);
}

void op_isfinite(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_u64(dst, isfinite(val) ? 1 : 0);
}

void op_isnormal(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_u64(dst, isnormal(val) ? 1 : 0);
}

void op_signbit(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_u64(dst, signbit(val) ? 1 : 0);
}

void op_copysign(unified_value_t *dst, const unified_value_t *mag, const unified_value_t *sign) {
	if (!dst || !mag || !sign) return;

	double m = uval_get_f64(mag);
	double s = uval_get_f64(sign);
	uval_set_f64(dst, copysign(m, s));
}

void op_nextafter(unified_value_t *dst, const unified_value_t *from, const unified_value_t *to) {
	if (!dst || !from || !to) return;

	double f = uval_get_f64(from);
	double t = uval_get_f64(to);
	uval_set_f64(dst, nextafter(f, t));
}

/***************************************************************************
 * TRANSCENDENTAL FUNCTIONS (continued)
 ***************************************************************************/

void op_tan(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_f64(dst, tan(val));
}

void op_sincos(unified_value_t *sin_dst, unified_value_t *cos_dst, const unified_value_t *src) {
	if (!sin_dst || !cos_dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_f64(sin_dst, sin(val));
	uval_set_f64(cos_dst, cos(val));
}

void op_asin(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_f64(dst, asin(val));
}

void op_acos(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_f64(dst, acos(val));
}

void op_atan(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_f64(dst, atan(val));
}

void op_atan2(unified_value_t *dst, const unified_value_t *y, const unified_value_t *x) {
	if (!dst || !y || !x) return;

	double y_val = uval_get_f64(y);
	double x_val = uval_get_f64(x);
	uval_set_f64(dst, atan2(y_val, x_val));
}

void op_sinh(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_f64(dst, sinh(val));
}

void op_cosh(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_f64(dst, cosh(val));
}

void op_tanh(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_f64(dst, tanh(val));
}

void op_exp(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_f64(dst, exp(val));
}

void op_exp2(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_f64(dst, exp2(val));
}

void op_exp10(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_f64(dst, pow(10.0, val));
}

void op_expm1(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_f64(dst, expm1(val));
}

void op_log(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_f64(dst, log(val));
}

void op_log2(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_f64(dst, log2(val));
}

void op_log10(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_f64(dst, log10(val));
}

void op_log1p(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	double val = uval_get_f64(src);
	uval_set_f64(dst, log1p(val));
}

void op_pow(unified_value_t *dst, const unified_value_t *base, const unified_value_t *exp) {
	if (!dst || !base || !exp) return;

	double b = uval_get_f64(base);
	double e = uval_get_f64(exp);
	uval_set_f64(dst, pow(b, e));
}

void op_hypot(unified_value_t *dst, const unified_value_t *x, const unified_value_t *y) {
	if (!dst || !x || !y) return;

	double x_val = uval_get_f64(x);
	double y_val = uval_get_f64(y);
	uval_set_f64(dst, hypot(x_val, y_val));
}

void op_poly(unified_value_t *dst, const unified_value_t *x, const unified_value_t *coeffs, uint32_t degree) {
	if (!dst || !x || !coeffs) return;

	double x_val = uval_get_f64(x);
	double result = 0.0;
	double x_pow = 1.0;
	
	for (uint32_t i = 0; i <= degree; i++) {
		unified_value_t coeff;
		op_extract_element(&coeff, coeffs, i);
		double c = uval_get_f64(&coeff);
		result += c * x_pow;
		x_pow *= x_val;
	}
	
	uval_set_f64(dst, result);
}

/***************************************************************************
 * STRING/MEMORY OPERATIONS
 ***************************************************************************/

void op_strcmp(unified_value_t *dst, const unified_value_t *s1, const unified_value_t *s2, uint32_t max_len) {
	(void)dst; (void)s1; (void)s2; (void)max_len;
	// Placeholder
}

void op_strchr(unified_value_t *dst, const unified_value_t *str, const unified_value_t *chr, uint32_t max_len) {
	(void)dst; (void)str; (void)chr; (void)max_len;
	// Placeholder
}

void op_strlen(unified_value_t *dst, const unified_value_t *str, uint32_t max_len) {
	(void)dst; (void)str; (void)max_len;
	// Placeholder
}

void op_block_move(void *dst, const void *src, uint32_t count, uint32_t elem_size) {
	if (!dst || !src) return;
	memcpy(dst, src, count * elem_size);
}

void op_block_fill(void *dst, const unified_value_t *value, uint32_t count) {
	if (!dst || !value) return;
	
	uint64_t val = uval_get_u64(value);
	uint8_t *ptr = (uint8_t*)dst;
	
	for (uint32_t i = 0; i < count; i++) {
		memcpy(ptr, &val, value->data_size);
		ptr += value->data_size;
	}
}

void op_block_compare(unified_value_t *result, const void *a, const void *b, uint32_t count) {
	if (!result || !a || !b) return;
	
	int cmp = memcmp(a, b, count);
	uval_set_i64(result, cmp);
}

void op_block_scan(unified_value_t *index, const void *block, const unified_value_t *value, uint32_t count) {
	if (!index || !block || !value) return;
	
	uint64_t val = uval_get_u64(value);
	const uint64_t *ptr = (const uint64_t*)block;
	
	for (uint32_t i = 0; i < count; i++) {
		if (ptr[i] == val) {
			uval_set_u64(index, i);
			return;
		}
	}
	
	uval_set_u64(index, count);
}

/***************************************************************************
 * PREDICATE/MASK OPERATIONS
 ***************************************************************************/

void op_pred_and(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	op_and(dst, a, b);
}

void op_pred_or(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	op_or(dst, a, b);
}

void op_pred_xor(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	op_xor(dst, a, b);
}

void op_pred_not(unified_value_t *dst, const unified_value_t *src) {
	op_not(dst, src);
}

void op_pred_first_true(unified_value_t *dst, const unified_value_t *pred) {
	if (!dst || !pred) return;

	uint64_t mask = uval_get_u64(pred);
	
	if (mask == 0) {
		uval_set_u64(dst, 64);
		return;
	}
	
	uint32_t pos = 0;
	while ((mask & 1) == 0) {
		pos++;
		mask >>= 1;
	}
	
	uval_set_u64(dst, pos);
}

void op_pred_last_true(unified_value_t *dst, const unified_value_t *pred) {
	if (!dst || !pred) return;

	uint64_t mask = uval_get_u64(pred);
	
	if (mask == 0) {
		uval_set_u64(dst, 64);
		return;
	}
	
	uint32_t pos = 63;
	while ((mask & (1ULL << pos)) == 0) {
		pos--;
	}
	
	uval_set_u64(dst, pos);
}

void op_pred_count_true(unified_value_t *dst, const unified_value_t *pred) {
	op_popcnt(dst, pred);
}

void op_pred_all_true(unified_value_t *dst, const unified_value_t *pred) {
	if (!dst || !pred) return;

	uint64_t mask = uval_get_u64(pred);
	uint32_t bits = pred->word_config.bits;
	uint64_t full_mask = get_word_mask(bits);
	
	uval_set_u64(dst, (mask == full_mask) ? 1 : 0);
}

void op_pred_any_true(unified_value_t *dst, const unified_value_t *pred) {
	if (!dst || !pred) return;

	uint64_t mask = uval_get_u64(pred);
	uval_set_u64(dst, (mask != 0) ? 1 : 0);
}

void op_pred_none_true(unified_value_t *dst, const unified_value_t *pred) {
	if (!dst || !pred) return;

	uint64_t mask = uval_get_u64(pred);
	uval_set_u64(dst, (mask == 0) ? 1 : 0);
}

void op_masked_add(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b, const unified_value_t *mask) {
	if (!dst || !a || !b || !mask) return;

	unified_value_t result;
	op_add(&result, a, b);
	op_blend(dst, &result, a, mask);
}

void op_masked_load(unified_value_t *dst, const void *ptr, const unified_value_t *mask) {
	(void)dst; (void)ptr; (void)mask;
	// Placeholder
}

void op_masked_store(void *ptr, const unified_value_t *src, const unified_value_t *mask) {
	(void)ptr; (void)src; (void)mask;
	// Placeholder
}

void op_cmpeq_pred(unified_value_t *pred, const unified_value_t *a, const unified_value_t *b) {
	op_cmpeq(pred, a, b);
}

void op_cmplt_pred(unified_value_t *pred, const unified_value_t *a, const unified_value_t *b) {
	op_cmplt(pred, a, b);
}

void op_cmple_pred(unified_value_t *pred, const unified_value_t *a, const unified_value_t *b) {
	op_cmple(pred, a, b);
}

/***************************************************************************
 * BCD/DECIMAL OPERATIONS
 ***************************************************************************/

void op_daa(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	// Decimal adjust after addition
	uint64_t val = uval_get_u64(src);
	uint64_t result = val;
	
	// Adjust lower nibble
	if ((val & 0x0F) > 9 || ((val >> 4) & 1)) {
		result += 0x06;
	}
	
	// Adjust upper nibble
	if (((result >> 4) & 0x0F) > 9 || (result & 0x100)) {
		result += 0x60;
	}
	
	uval_set_u64(dst, result & 0xFF);
}

void op_das(unified_value_t *dst, const unified_value_t *src) {
	if (!dst || !src) return;

	// Decimal adjust after subtraction
	uint64_t val = uval_get_u64(src);
	uint64_t result = val;
	
	if ((val & 0x0F) > 9 || ((val >> 4) & 1)) {
		result -= 0x06;
	}
	
	if (((result >> 4) & 0x0F) > 9 || (result & 0x100)) {
		result -= 0x60;
	}
	
	uval_set_u64(dst, result & 0xFF);
}

void op_packed_decimal_add(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	// Placeholder for packed decimal add
	op_bcd_add(dst, a, b);
}

void op_packed_decimal_sub(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	// Placeholder for packed decimal subtract
	op_bcd_sub(dst, a, b);
}

void op_packed_decimal_mul(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	// Placeholder for packed decimal multiply
	op_bcd_mul(dst, a, b);
}

void op_packed_decimal_div(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	// Placeholder for packed decimal divide
	op_bcd_div(dst, a, b);
}

/***************************************************************************
 * QUEUE/LIST OPERATIONS
 ***************************************************************************/

void op_queue_insert(void *entry, void *predecessor) {
	(void)entry; (void)predecessor;
	// Placeholder
}

void op_queue_remove(void **entry, void *header) {
	(void)entry; (void)header;
	// Placeholder
}

/***************************************************************************
 * SPECIALIZED OPERATIONS
 ***************************************************************************/

void op_crc32(unified_value_t *dst, const unified_value_t *crc, const unified_value_t *data) {
	if (!dst || !crc || !data) return;

	uint32_t crc_val = (uint32_t)uval_get_u64(crc);
	uint32_t data_val = (uint32_t)uval_get_u64(data);
	
	// CRC32 polynomial
	const uint32_t poly = 0xEDB88320;
	
	crc_val ^= data_val;
	
	for (int i = 0; i < 32; i++) {
		if (crc_val & 1) {
			crc_val = (crc_val >> 1) ^ poly;
		} else {
			crc_val >>= 1;
		}
	}
	
	uval_set_u64(dst, crc_val);
}

void op_aes_enc(unified_value_t *dst, const unified_value_t *state, const unified_value_t *key) {
	(void)dst; (void)state; (void)key;
	// AES encryption placeholder
}

void op_aes_enc_last(unified_value_t *dst, const unified_value_t *state, const unified_value_t *key) {
	(void)dst; (void)state; (void)key;
	// AES encryption last round placeholder
}

void op_aes_dec(unified_value_t *dst, const unified_value_t *state, const unified_value_t *key) {
	(void)dst; (void)state; (void)key;
	// AES decryption placeholder
}

void op_aes_dec_last(unified_value_t *dst, const unified_value_t *state, const unified_value_t *key) {
	(void)dst; (void)state; (void)key;
	// AES decryption last round placeholder
}

void op_aes_keygen(unified_value_t *dst, const unified_value_t *key, uint8_t rcon) {
	(void)dst; (void)key; (void)rcon;
	// AES key generation placeholder
}

void op_sha1_c(unified_value_t *dst, const unified_value_t *abcd, const unified_value_t *e, const unified_value_t *msg) {
	(void)dst; (void)abcd; (void)e; (void)msg;
	// SHA1 placeholder
}

void op_sha1_p(unified_value_t *dst, const unified_value_t *abcd, const unified_value_t *e, const unified_value_t *msg) {
	(void)dst; (void)abcd; (void)e; (void)msg;
	// SHA1 placeholder
}

void op_sha1_m(unified_value_t *dst, const unified_value_t *msg0, const unified_value_t *msg1, const unified_value_t *msg2) {
	(void)dst; (void)msg0; (void)msg1; (void)msg2;
	// SHA1 placeholder
}

void op_sha256_rnds2(unified_value_t *dst, const unified_value_t *src, const unified_value_t *wk) {
	(void)dst; (void)src; (void)wk;
	// SHA256 placeholder
}

void op_sha256_msg1(unified_value_t *dst, const unified_value_t *src) {
	(void)dst; (void)src;
	// SHA256 placeholder
}

void op_sha256_msg2(unified_value_t *dst, const unified_value_t *src) {
	(void)dst; (void)src;
	// SHA256 placeholder
}

void op_rand(unified_value_t *dst) {
	if (!dst) return;

	uint64_t val = ((uint64_t)rand() << 32) | rand();
	uval_set_u64(dst, val);
}

void op_rand_seed(const unified_value_t *seed) {
	if (!seed) return;

	uint32_t s = (uint32_t)uval_get_u64(seed);
	srand(s);
}

/***************************************************************************
 * CONDITIONAL/PREDICATED EXECUTION
 ***************************************************************************/

void op_pred_cmp_eq(unified_value_t *p_true, unified_value_t *p_false, const unified_value_t *a, const unified_value_t *b) {
	if (!p_true || !p_false || !a || !b) return;

	unified_value_t cmp;
	op_cmpeq(&cmp, a, b);
	
	uint64_t is_eq = uval_get_u64(&cmp);
	uval_set_u64(p_true, is_eq ? 1 : 0);
	uval_set_u64(p_false, is_eq ? 0 : 1);
}

void op_pred_cmp_lt(unified_value_t *p_true, unified_value_t *p_false, const unified_value_t *a, const unified_value_t *b) {
	if (!p_true || !p_false || !a || !b) return;

	unified_value_t cmp;
	op_cmplt(&cmp, a, b);
	
	uint64_t is_lt = uval_get_u64(&cmp);
	uval_set_u64(p_true, is_lt ? 1 : 0);
	uval_set_u64(p_false, is_lt ? 0 : 1);
}

void op_speculative_load(unified_value_t *dst, const void *ptr, unified_value_t *nat_bit) {
	if (!dst || !ptr) return;

	// Speculative load - check for valid address
	// Placeholder implementation
	memcpy(dst->data, ptr, dst->data_size);
	
	if (nat_bit) {
		uval_set_u64(nat_bit, 0);  // 0 = valid
	}
}

/***************************************************************************
 * ATOMIC OPERATIONS
 ***************************************************************************/

void op_atomic_add(unified_value_t *dst, void *ptr, const unified_value_t *value) {
	if (!dst || !ptr || !value) return;

	// Atomic add placeholder
	uint64_t old_val = *(uint64_t*)ptr;
	uint64_t new_val = old_val + uval_get_u64(value);
	*(uint64_t*)ptr = new_val;
	uval_set_u64(dst, old_val);
}

void op_atomic_sub(unified_value_t *dst, void *ptr, const unified_value_t *value) {
	if (!dst || !ptr || !value) return;

	uint64_t old_val = *(uint64_t*)ptr;
	uint64_t new_val = old_val - uval_get_u64(value);
	*(uint64_t*)ptr = new_val;
	uval_set_u64(dst, old_val);
}

void op_atomic_and(unified_value_t *dst, void *ptr, const unified_value_t *value) {
	if (!dst || !ptr || !value) return;

	uint64_t old_val = *(uint64_t*)ptr;
	uint64_t new_val = old_val & uval_get_u64(value);
	*(uint64_t*)ptr = new_val;
	uval_set_u64(dst, old_val);
}

void op_atomic_or(unified_value_t *dst, void *ptr, const unified_value_t *value) {
	if (!dst || !ptr || !value) return;

	uint64_t old_val = *(uint64_t*)ptr;
	uint64_t new_val = old_val | uval_get_u64(value);
	*(uint64_t*)ptr = new_val;
	uval_set_u64(dst, old_val);
}

void op_atomic_xor(unified_value_t *dst, void *ptr, const unified_value_t *value) {
	if (!dst || !ptr || !value) return;

	uint64_t old_val = *(uint64_t*)ptr;
	uint64_t new_val = old_val ^ uval_get_u64(value);
	*(uint64_t*)ptr = new_val;
	uval_set_u64(dst, old_val);
}

void op_atomic_swap(unified_value_t *dst, void *ptr, const unified_value_t *value) {
	if (!dst || !ptr || !value) return;

	uint64_t old_val = *(uint64_t*)ptr;
	*(uint64_t*)ptr = uval_get_u64(value);
	uval_set_u64(dst, old_val);
}

void op_atomic_cas(unified_value_t *dst, void *ptr, const unified_value_t *expected, const unified_value_t *desired) {
	if (!dst || !ptr || !expected || !desired) return;

	uint64_t exp_val = uval_get_u64(expected);
	uint64_t des_val = uval_get_u64(desired);
	uint64_t old_val = *(uint64_t*)ptr;
	
	if (old_val == exp_val) {
		*(uint64_t*)ptr = des_val;
	}
	
	uval_set_u64(dst, old_val);
}

void op_atomic_min(unified_value_t *dst, void *ptr, const unified_value_t *value) {
	if (!dst || !ptr || !value) return;

	int64_t old_val = *(int64_t*)ptr;
	int64_t new_val = uval_get_i64(value);
	
	if (new_val < old_val) {
		*(int64_t*)ptr = new_val;
	}
	
	uval_set_i64(dst, old_val);
}

void op_atomic_max(unified_value_t *dst, void *ptr, const unified_value_t *value) {
	if (!dst || !ptr || !value) return;

	int64_t old_val = *(int64_t*)ptr;
	int64_t new_val = uval_get_i64(value);
	
	if (new_val > old_val) {
		*(int64_t*)ptr = new_val;
	}
	
	uval_set_i64(dst, old_val);
}

/***************************************************************************
 * MEMORY BARRIER / FENCE OPERATIONS
 ***************************************************************************/

void op_fence(void) {
	__sync_synchronize();
}

void op_fence_acquire(void) {
	__sync_synchronize();
}

void op_fence_release(void) {
	__sync_synchronize();
}

void op_fence_seq_cst(void) {
	__sync_synchronize();
}

/***************************************************************************
 * ADDITIONAL ARITHMETIC OPERATIONS
 ***************************************************************************/

void op_mul_sat(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->word_config.is_signed) {
		int64_t a_val = uval_get_i64(a);
		int64_t b_val = uval_get_i64(b);
		int64_t product = a_val * b_val;
		
		int64_t max_val = (1LL << (a->word_config.bits - 1)) - 1;
		int64_t min_val = -(1LL << (a->word_config.bits - 1));
		
		if (product > max_val) product = max_val;
		if (product < min_val) product = min_val;
		
		uval_set_i64(dst, product);
	} else {
		uint64_t a_val = uval_get_u64(a);
		uint64_t b_val = uval_get_u64(b);
		uint64_t product = a_val * b_val;
		uint64_t mask = get_word_mask(a->word_config.bits);
		
		if (product > mask) product = mask;
		
		uval_set_u64(dst, product);
	}
}

void op_mul_wide(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	if (a->word_config.is_signed) {
		int64_t a_val = uval_get_i64(a);
		int64_t b_val = uval_get_i64(b);
		__int128 product = (__int128)a_val * (__int128)b_val;
		
		// Store full product (would need larger dst)
		uval_set_i64(dst, (int64_t)product);
	} else {
		uint64_t a_val = uval_get_u64(a);
		uint64_t b_val = uval_get_u64(b);
		__uint128_t product = (__uint128_t)a_val * (__uint128_t)b_val;
		
		uval_set_u64(dst, (uint64_t)product);
	}
}

void op_avg_round_up(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	uint64_t a_val = uval_get_u64(a);
	uint64_t b_val = uval_get_u64(b);
	uval_set_u64(dst, (a_val + b_val + 1) / 2);
}

void op_avg_round_down(unified_value_t *dst, const unified_value_t *a, const unified_value_t *b) {
	if (!dst || !a || !b) return;

	uint64_t a_val = uval_get_u64(a);
	uint64_t b_val = uval_get_u64(b);
	uval_set_u64(dst, (a_val + b_val) / 2);
}

void op_minmax(unified_value_t *min_dst, unified_value_t *max_dst, const unified_value_t *a, const unified_value_t *b) {
	if (!min_dst || !max_dst || !a || !b) return;

	op_min(min_dst, a, b);
	op_max(max_dst, a, b);
}

void op_mac(unified_value_t *acc, const unified_value_t *a, const unified_value_t *b) {
	if (!acc || !a || !b) return;

	unified_value_t product, sum;
	op_mul(&product, a, b);
	op_add(&sum, acc, &product);
	*acc = sum;
}

void op_msu(unified_value_t *acc, const unified_value_t *a, const unified_value_t *b) {
	if (!acc || !a || !b) return;

	unified_value_t product, diff;
	op_mul(&product, a, b);
	op_sub(&diff, acc, &product);
	*acc = diff;
}

void uval_set_bytes(unified_value_t *val, const void *bytes, size_t len) {
	if (!val || !bytes) return;

	size_t copy_len = (len < val->data_size) ? len : val->data_size;
	memcpy(val->data, bytes, copy_len);
}

void uval_get_bytes(const unified_value_t *val, void *bytes, size_t len) {
	if (!val || !bytes) return;

	size_t copy_len = (len < val->data_size) ? len : val->data_size;
	memcpy(bytes, val->data, copy_len);
}

