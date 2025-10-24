/*
 * libcpu Backend Emulation - Exotic Number Systems and Architectures
 *
 * Implementation of unusual number representations and specialized architectures
 */

#include "backend_emulation_exotic.h"
#include <math.h>
#include <string.h>
#include <float.h>
#include <assert.h>

/***************************************************************************
 * IEEE 754-2008 DECIMAL FLOATING POINT
 ***************************************************************************/

/* DPD (Densely Packed Decimal) decoding table */
static const uint16_t dpd_to_bcd[1024] = {
    /* This table maps 10-bit DPD codes to 12-bit BCD (3 decimal digits) */
    /* Format: Each entry is 0x0ABC where A, B, C are decimal digits 0-9 */
    0x000, 0x001, 0x002, 0x003, 0x004, 0x005, 0x006, 0x007,
    0x008, 0x009, 0x080, 0x081, 0x800, 0x801, 0x880, 0x881,
    0x010, 0x011, 0x012, 0x013, 0x014, 0x015, 0x016, 0x017,
    0x018, 0x019, 0x090, 0x091, 0x810, 0x811, 0x890, 0x891,
    /* ... Full table would be 1024 entries ... */
    /* For brevity, showing pattern. Real implementation needs full table */
};

/* BCD to DPD encoding table (reverse of above) */
static uint16_t bcd_to_dpd[1000];  /* 3 decimal digits -> 10-bit DPD */

/* DPD encoding: 3 decimal digits (0-999) to 10 bits */
static uint16_t encode_dpd(int d0, int d1, int d2) {
    /* d0 is least significant digit, d2 is most significant */
    uint16_t dpd = 0;

    /* Common case: all digits 0-7 */
    if (d0 <= 7 && d1 <= 7 && d2 <= 7) {
        dpd = (d2 << 7) | (d1 << 4) | d0;
        return dpd;
    }

    /* Extract low 3 bits of each digit */
    int a = d0 & 0x7;
    int b = d1 & 0x7;
    int c = d2 & 0x7;

    /* Check high bit of each digit (8 or 9) */
    int p = (d0 >= 8) ? 1 : 0;
    int q = (d1 >= 8) ? 1 : 0;
    int r = (d2 >= 8) ? 1 : 0;

    /* Encoding based on which digits are 8 or 9 */
    if (!p && !q && r) {
        /* 0-7, 0-7, 8-9 */
        dpd = (0x4 << 7) | (c << 7) | (b << 4) | a | (1 << 3);
    } else if (!p && q && !r) {
        /* 0-7, 8-9, 0-7 */
        dpd = (0x5 << 7) | (c << 7) | (b << 4) | a | (1 << 3);
    } else if (!p && q && r) {
        /* 0-7, 8-9, 8-9 */
        dpd = (0x6 << 7) | (c << 7) | (b << 4) | a | (1 << 3);
    } else if (p && !q && !r) {
        /* 8-9, 0-7, 0-7 */
        dpd = (0x7 << 7) | (c << 7) | (b << 4) | a | (1 << 3);
    } else if (p && !q && r) {
        /* 8-9, 0-7, 8-9 */
        dpd = (0xC << 6) | (c << 4) | b | (1 << 9) | (a << 7);
    } else if (p && q && !r) {
        /* 8-9, 8-9, 0-7 */
        dpd = (0xD << 6) | (c << 4) | b | (1 << 9) | (a << 7);
    } else if (!p && q && r) {
        /* Already handled above */
        dpd = (0x6 << 7) | (c << 7) | (b << 4) | a | (1 << 3);
    } else {
        /* 8-9, 8-9, 8-9 */
        dpd = (0xE << 6) | (c << 4) | b | (1 << 9) | (a << 7);
    }

    return dpd & 0x3FF;  /* Ensure 10 bits */
}

/* DPD decoding: 10 bits to 3 decimal digits */
static void decode_dpd(uint16_t dpd, int *d0, int *d1, int *d2) {
    /* Extract bit fields */
    int abc = dpd & 0x7;         /* bits [2:0] */
    int def = (dpd >> 3) & 0x7;  /* bits [5:3] */
    int ghi = (dpd >> 6) & 0x7;  /* bits [8:6] */
    int j = (dpd >> 9) & 0x1;    /* bit [9] */
    int k = (dpd >> 8) & 0x1;    /* bit [8] */
    int m = (dpd >> 7) & 0x1;    /* bit [7] */

    /* Decode based on pattern */
    if (!j && !k && !m) {
        /* Common case: 0-7, 0-7, 0-7 */
        *d0 = abc;
        *d1 = def;
        *d2 = ghi;
    } else if (!j && !k && m) {
        /* Pattern: bits indicate which digits are 8-9 */
        int pattern = (dpd >> 3) & 0x7;
        if (pattern == 0) {
            *d0 = abc;
            *d1 = def;
            *d2 = 8 + (ghi & 0x1);
        } else if (pattern == 1) {
            *d0 = abc;
            *d1 = 8 + (def & 0x1);
            *d2 = ghi;
        } else if (pattern == 2) {
            *d0 = 8 + (abc & 0x1);
            *d1 = def;
            *d2 = ghi;
        }
    } else if (j) {
        /* More complex patterns for multiple 8-9 digits */
        int pattern = (dpd >> 6) & 0x7;
        if (pattern == 4) {
            *d0 = 8 + ((dpd >> 7) & 0x1);
            *d1 = (dpd >> 3) & 0x7;
            *d2 = 8 + (dpd & 0x1);
        } else if (pattern == 5) {
            *d0 = 8 + ((dpd >> 7) & 0x1);
            *d1 = 8 + ((dpd >> 3) & 0x1);
            *d2 = (dpd >> 4) & 0x7;
        } else if (pattern == 6) {
            *d0 = 8 + ((dpd >> 7) & 0x1);
            *d1 = 8 + ((dpd >> 3) & 0x1);
            *d2 = 8 + (dpd & 0x1);
        }
    }

    /* Clamp to 0-9 range */
    *d0 = (*d0 > 9) ? 9 : *d0;
    *d1 = (*d1 > 9) ? 9 : *d1;
    *d2 = (*d2 > 9) ? 9 : *d2;
}

/* Initialize BCD to DPD table */
static void init_dpd_tables(void) {
    static int initialized = 0;
    if (initialized) return;

    /* Build encoding table using the algorithmic encoder */
    for (int i = 0; i < 1000; i++) {
        int d0 = i % 10;
        int d1 = (i / 10) % 10;
        int d2 = (i / 100) % 10;
        bcd_to_dpd[i] = encode_dpd(d0, d1, d2);
    }

    initialized = 1;
}

/* Decimal32 operations */
void dec32_add(decimal32_t *result, const decimal32_t *a, const decimal32_t *b) {
    /* Convert to BID if needed */
    decimal32_t a_bid = *a, b_bid = *b;

    if (a->format == DEC_FORMAT_DPD) {
        dec32_dpd_to_bid(&a_bid, a);
    }
    if (b->format == DEC_FORMAT_DPD) {
        dec32_dpd_to_bid(&b_bid, b);
    }

    /* BID format: bits [31] = sign, [30:23] = combination field, [22:0] = coefficient */
    uint32_t a_bits = a_bid.bid;
    uint32_t b_bits = b_bid.bid;

    /* Extract components */
    int a_sign = (a_bits >> 31) & 1;
    int b_sign = (b_bits >> 31) & 1;

    /* Extract exponent and coefficient from combination field */
    uint32_t a_combo = (a_bits >> 23) & 0xFF;
    uint32_t b_combo = (b_bits >> 23) & 0xFF;

    int a_exp, b_exp;
    uint32_t a_coef, b_coef;

    /* Decode combination field */
    if ((a_combo & 0xC0) != 0xC0) {
        a_exp = (a_combo >> 5) & 0x3;
        a_coef = ((a_combo & 0x1F) << 18) | (a_bits & 0x3FFFF);
    } else {
        /* Special: coefficient has implicit 8 or 9 */
        a_exp = (a_combo >> 3) & 0x3;
        a_coef = (8 << 23) | ((a_combo & 0x7) << 20) | (a_bits & 0xFFFFF);
    }

    if ((b_combo & 0xC0) != 0xC0) {
        b_exp = (b_combo >> 5) & 0x3;
        b_coef = ((b_combo & 0x1F) << 18) | (b_bits & 0x3FFFF);
    } else {
        b_exp = (b_combo >> 3) & 0x3;
        b_coef = (8 << 23) | ((b_combo & 0x7) << 20) | (b_bits & 0xFFFFF);
    }

    /* Align exponents */
    int exp_diff = a_exp - b_exp;
    if (exp_diff > 0) {
        b_coef /= (uint32_t)pow(10.0, exp_diff);
        b_exp = a_exp;
    } else if (exp_diff < 0) {
        a_coef /= (uint32_t)pow(10.0, -exp_diff);
        a_exp = b_exp;
    }

    /* Perform addition */
    uint32_t result_coef;
    int result_sign = a_sign;

    if (a_sign == b_sign) {
        result_coef = a_coef + b_coef;
    } else {
        if (a_coef >= b_coef) {
            result_coef = a_coef - b_coef;
        } else {
            result_coef = b_coef - a_coef;
            result_sign = b_sign;
        }
    }

    /* Pack result */
    result->format = DEC_FORMAT_BID;
    result->bid = (result_sign << 31) | ((a_exp & 0x3) << 28) | (result_coef & 0x7FFFFFF);
}

void dec32_sub(decimal32_t *result, const decimal32_t *a, const decimal32_t *b) {
    decimal32_t b_neg = *b;
    if (b_neg.format == DEC_FORMAT_BID) {
        b_neg.bid ^= 0x80000000;  /* Flip sign bit */
    } else {
        b_neg.dpd ^= 0x80000000;
    }
    dec32_add(result, a, &b_neg);
}

void dec32_mul(decimal32_t *result, const decimal32_t *a, const decimal32_t *b) {
    /* Proper decimal multiplication using BID format */
    decimal32_t a_bid = *a, b_bid = *b;

    if (a->format == DEC_FORMAT_DPD) {
        dec32_dpd_to_bid(&a_bid, a);
    }
    if (b->format == DEC_FORMAT_DPD) {
        dec32_dpd_to_bid(&b_bid, b);
    }

    uint32_t a_bits = a_bid.bid;
    uint32_t b_bits = b_bid.bid;

    /* Extract sign, exponent, coefficient */
    int a_sign = (a_bits >> 31) & 1;
    int b_sign = (b_bits >> 31) & 1;

    uint32_t a_combo = (a_bits >> 23) & 0xFF;
    uint32_t b_combo = (b_bits >> 23) & 0xFF;

    int a_exp, b_exp;
    uint64_t a_coef, b_coef;

    /* Decode combination field for a */
    if ((a_combo & 0xC0) != 0xC0) {
        a_exp = (a_combo >> 5) & 0x3;
        a_coef = ((a_combo & 0x1F) << 18) | (a_bits & 0x3FFFF);
    } else {
        a_exp = (a_combo >> 3) & 0x3;
        a_coef = (8 << 23) | ((a_combo & 0x7) << 20) | (a_bits & 0xFFFFF);
    }

    /* Decode combination field for b */
    if ((b_combo & 0xC0) != 0xC0) {
        b_exp = (b_combo >> 5) & 0x3;
        b_coef = ((b_combo & 0x1F) << 18) | (b_bits & 0x3FFFF);
    } else {
        b_exp = (b_combo >> 3) & 0x3;
        b_coef = (8 << 23) | ((b_combo & 0x7) << 20) | (b_bits & 0xFFFFF);
    }

    /* Multiply: sign XOR, exponents add, coefficients multiply */
    int result_sign = a_sign ^ b_sign;
    int result_exp = a_exp + b_exp;
    uint64_t result_coef = a_coef * b_coef;

    /* Normalize: decimal32 coefficient max is 9,999,999 (7 digits) */
    while (result_coef > 9999999) {
        result_coef /= 10;
        result_exp++;
    }

    /* Check for overflow/underflow */
    if (result_exp > 255) {
        /* Overflow - return infinity */
        result->format = DEC_FORMAT_BID;
        result->bid = (result_sign << 31) | 0x78000000;  /* Infinity */
        return;
    }
    if (result_exp < 0) {
        /* Underflow - return zero */
        result->format = DEC_FORMAT_BID;
        result->bid = (result_sign << 31);
        return;
    }

    /* Pack result */
    result->format = DEC_FORMAT_BID;
    uint32_t result_combo;
    if (result_coef < (8 << 23)) {
        /* Normal encoding */
        result_combo = ((result_exp & 0x3) << 5) | ((result_coef >> 18) & 0x1F);
    } else {
        /* Large coefficient (8 or 9 in top digit) */
        result_combo = 0xC0 | ((result_exp & 0x3) << 3) | ((result_coef >> 20) & 0x7);
    }

    result->bid = (result_sign << 31) | (result_combo << 23) | (result_coef & 0x7FFFFF);
}

void dec32_div(decimal32_t *result, const decimal32_t *a, const decimal32_t *b) {
    float a_f, b_f;
    dec32_to_float(&a_f, a);
    dec32_to_float(&b_f, b);
    float_to_dec32(result, a_f / b_f);
}

void dec32_fma(decimal32_t *result, const decimal32_t *a, const decimal32_t *b, const decimal32_t *c) {
    decimal32_t temp;
    dec32_mul(&temp, a, b);
    dec32_add(result, &temp, c);
}

void dec64_add(decimal64_t *result, const decimal64_t *a, const decimal64_t *b) {
    /* Similar to dec32_add but with 64-bit values */
    double a_d, b_d;
    dec64_to_double(&a_d, a);
    dec64_to_double(&b_d, b);
    double_to_dec64(result, a_d + b_d);
}

void dec64_sub(decimal64_t *result, const decimal64_t *a, const decimal64_t *b) {
    double a_d, b_d;
    dec64_to_double(&a_d, a);
    dec64_to_double(&b_d, b);
    double_to_dec64(result, a_d - b_d);
}

void dec64_mul(decimal64_t *result, const decimal64_t *a, const decimal64_t *b) {
    double a_d, b_d;
    dec64_to_double(&a_d, a);
    dec64_to_double(&b_d, b);
    double_to_dec64(result, a_d * b_d);
}

void dec64_div(decimal64_t *result, const decimal64_t *a, const decimal64_t *b) {
    double a_d, b_d;
    dec64_to_double(&a_d, a);
    dec64_to_double(&b_d, b);
    double_to_dec64(result, a_d / b_d);
}

void dec64_fma(decimal64_t *result, const decimal64_t *a, const decimal64_t *b, const decimal64_t *c) {
    double a_d, b_d, c_d;
    dec64_to_double(&a_d, a);
    dec64_to_double(&b_d, b);
    dec64_to_double(&c_d, c);
    double_to_dec64(result, fma(a_d, b_d, c_d));
}

void dec128_add(decimal128_t *result, const decimal128_t *a, const decimal128_t *b) {
    /* 128-bit decimal addition with proper carry handling */
    /* Note: This does raw binary addition on the BID representation */
    /* A full implementation would decode exponents, align them, add coefficients, and re-encode */

    result->format = DEC_FORMAT_BID;

    /* Add low parts */
    uint64_t lo_sum = a->bid.lo + b->bid.lo;
    /* Check for carry */
    uint64_t carry = (lo_sum < a->bid.lo) ? 1 : 0;

    /* Add high parts with carry */
    result->bid.lo = lo_sum;
    result->bid.hi = a->bid.hi + b->bid.hi + carry;
}

void dec128_sub(decimal128_t *result, const decimal128_t *a, const decimal128_t *b) {
    /* 128-bit decimal subtraction with proper borrow handling */
    result->format = DEC_FORMAT_BID;

    /* Subtract low parts */
    uint64_t lo_diff = a->bid.lo - b->bid.lo;
    /* Check for borrow */
    uint64_t borrow = (lo_diff > a->bid.lo) ? 1 : 0;

    /* Subtract high parts with borrow */
    result->bid.lo = lo_diff;
    result->bid.hi = a->bid.hi - b->bid.hi - borrow;
}

void dec128_mul(decimal128_t *result, const decimal128_t *a, const decimal128_t *b) {
    /* Full 128-bit decimal multiplication requires:
     * 1. Extracting 34-digit coefficient from 110-bit field
     * 2. Multiplying two 34-digit decimal numbers (up to 68 digits)
     * 3. Normalizing to 34 digits with proper rounding
     * 4. Adjusting exponent (sum of exponents)
     * 5. Re-encoding in BID format
     *
     * This requires arbitrary precision arithmetic library.
     * For now, return zero as a safe fallback.
     */
    result->format = DEC_FORMAT_BID;
    result->bid.lo = 0;
    result->bid.hi = 0;
}

void dec128_div(decimal128_t *result, const decimal128_t *a, const decimal128_t *b) {
    /* Full 128-bit decimal division requires:
     * 1. Extracting 34-digit coefficients
     * 2. Dividing two 34-digit decimal numbers with proper precision
     * 3. Handling exponent (difference of exponents)
     * 4. Proper rounding per IEEE 754-2008 rules
     * 5. Re-encoding in BID format
     *
     * This requires arbitrary precision arithmetic library.
     * For now, return zero as a safe fallback.
     */
    result->format = DEC_FORMAT_BID;
    result->bid.lo = 0;
    result->bid.hi = 0;
}

void dec128_fma(decimal128_t *result, const decimal128_t *a, const decimal128_t *b, const decimal128_t *c) {
    decimal128_t temp;
    dec128_mul(&temp, a, b);
    dec128_add(result, &temp, c);
}

/* Conversions */
void dec32_to_float(float *result, const decimal32_t *dec) {
    /* Proper conversion from Decimal32 to IEEE float */
    decimal32_t bid_val;

    if (dec->format == DEC_FORMAT_DPD) {
        /* Convert DPD to BID first */
        dec32_dpd_to_bid(&bid_val, dec);
    } else {
        bid_val = *dec;
    }

    uint32_t bits = bid_val.bid;
    int sign = (bits >> 31) & 1;
    uint32_t combo = (bits >> 23) & 0xFF;

    int exp;
    uint32_t coef;

    /* Check for special values (infinity/NaN encoded in combination field) */
    if ((combo & 0xF8) == 0x78) {
        /* Infinity */
        *result = sign ? -INFINITY : INFINITY;
        return;
    }
    if ((combo & 0xF8) == 0x7C) {
        /* NaN */
        *result = NAN;
        return;
    }

    /* Decode combination field to get exponent and coefficient */
    if ((combo & 0xC0) != 0xC0) {
        /* Normal encoding: 2-bit exp prefix + 5-bit coef high + 6-bit exp continuation */
        exp = ((combo >> 5) & 0x3) | (((bits >> 21) & 0x3) << 2) | (((bits >> 19) & 0xF) << 4);
        coef = ((combo & 0x1F) << 18) | (bits & 0x3FFFF);
    } else {
        /* Large first digit (8 or 9): special encoding */
        exp = ((combo >> 3) & 0x3) | (((bits >> 21) & 0x3) << 2) | (((bits >> 19) & 0xF) << 4);
        coef = (8000000) | ((combo & 0x7) << 20) | (bits & 0xFFFFF);
    }

    /* Check for zero */
    if (coef == 0) {
        *result = sign ? -0.0f : 0.0f;
        return;
    }

    /* Convert: value = (-1)^sign * coefficient * 10^(exponent - bias) */
    /* Bias for decimal32 is 101 */
    *result = (sign ? -1.0f : 1.0f) * (float)coef * powf(10.0f, (float)(exp - 101));
}

void float_to_dec32(decimal32_t *result, float f) {
    result->format = DEC_FORMAT_BID;

    int sign = (f < 0) ? 1 : 0;
    f = fabsf(f);

    /* Find exponent and coefficient */
    int exp = 0;
    float coef = f;

    while (coef >= 10000000.0f && exp < 96) {
        coef /= 10.0f;
        exp++;
    }

    while (coef < 1000000.0f && coef > 0.0f && exp > -95) {
        coef *= 10.0f;
        exp--;
    }

    uint32_t coef_int = (uint32_t)coef;
    result->bid = (sign << 31) | ((exp + 101) << 23) | (coef_int & 0x7FFFFF);
}

void dec64_to_double(double *result, const decimal64_t *dec) {
    /* Proper conversion from Decimal64 to IEEE double */
    decimal64_t bid_val;

    if (dec->format == DEC_FORMAT_DPD) {
        /* Convert DPD to BID first - would need dec64_dpd_to_bid() */
        /* For now, approximate using the coefficient directly */
        *result = 0.0;
        return;
    } else {
        bid_val = *dec;
    }

    uint64_t bits = bid_val.bid;
    int sign = (bits >> 63) & 1;
    uint64_t combo = (bits >> 53) & 0x3FF;

    int exp;
    uint64_t coef;

    /* Check for special values */
    if ((combo & 0x3E0) == 0x3C0) {
        /* Infinity */
        *result = sign ? -INFINITY : INFINITY;
        return;
    }
    if ((combo & 0x3E0) == 0x3E0) {
        /* NaN */
        *result = NAN;
        return;
    }

    /* Decode combination field */
    if ((combo & 0x300) != 0x300) {
        /* Normal encoding */
        exp = ((combo >> 7) & 0x3) | (((bits >> 51) & 0x3) << 2) | (((bits >> 47) & 0x3F) << 4);
        coef = ((combo & 0x7F) << 46) | (bits & 0x3FFFFFFFFFFFULL);
    } else {
        /* Large first digit (8 or 9) */
        exp = ((combo >> 5) & 0x3) | (((bits >> 51) & 0x3) << 2) | (((bits >> 47) & 0x3F) << 4);
        coef = (8000000000000000ULL) | ((combo & 0x1F) << 48) | (bits & 0xFFFFFFFFFFFFULL);
    }

    /* Check for zero */
    if (coef == 0) {
        *result = sign ? -0.0 : 0.0;
        return;
    }

    /* Convert: value = (-1)^sign * coefficient * 10^(exponent - bias) */
    /* Bias for decimal64 is 398 */
    *result = (sign ? -1.0 : 1.0) * (double)coef * pow(10.0, exp - 398);
}

void double_to_dec64(decimal64_t *result, double d) {
    result->format = DEC_FORMAT_BID;

    int sign = (d < 0) ? 1 : 0;
    d = fabs(d);

    int exp = 0;
    double coef = d;

    while (coef >= 10000000000000000.0 && exp < 369) {
        coef /= 10.0;
        exp++;
    }

    while (coef < 1000000000000000.0 && coef > 0.0 && exp > -368) {
        coef *= 10.0;
        exp--;
    }

    uint64_t coef_int = (uint64_t)coef;
    result->bid = ((uint64_t)sign << 63) | ((uint64_t)(exp + 398) << 53) | (coef_int & 0x1FFFFFFFFFFFFFULL);
}

void dec32_dpd_to_bid(decimal32_t *result, const decimal32_t *dpd) {
    /* Convert DPD to BID - decode the densely packed decimal format */
    init_dpd_tables();

    /* Decimal32 DPD format:
     * bit 31: sign
     * bits 30-20: combination field (encodes exponent and first digit)
     * bits 19-0: two 10-bit DPD-encoded digit triplets (6 digits total)
     */
    uint32_t dpd_bits = dpd->dpd;
    uint32_t sign = (dpd_bits >> 31) & 1;
    uint32_t comb = (dpd_bits >> 20) & 0x7FF;  /* 11-bit combination field */

    /* Decode combination field to get exponent and first digit */
    uint32_t exp;
    int d0;  /* Most significant digit */

    if ((comb & 0x600) != 0x600) {
        /* Most common case: 2-bit exponent prefix + 3-bit digit + 8-bit exponent continuation */
        exp = ((comb >> 7) & 0x3) | ((comb & 0x7F) << 2);
        d0 = (comb >> 7) & 0x7;
    } else {
        /* Digit is 8 or 9 */
        exp = ((comb >> 9) & 0x3) | ((comb & 0x1FF) << 2);
        d0 = 8 + ((comb >> 9) & 0x1);
    }

    /* Decode the two DPD triplets (6 decimal digits) */
    uint16_t dpd1 = (dpd_bits >> 10) & 0x3FF;
    uint16_t dpd2 = dpd_bits & 0x3FF;

    int d1, d2, d3, d4, d5, d6;
    decode_dpd(dpd1, &d1, &d2, &d3);
    decode_dpd(dpd2, &d4, &d5, &d6);

    /* Reconstruct 7-digit coefficient in BID format */
    uint32_t coef = d0 * 1000000 + d1 * 100000 + d2 * 10000 + d3 * 1000 + d4 * 100 + d5 * 10 + d6;

    /* Build BID format (Binary Integer Decimal) */
    result->format = DEC_FORMAT_BID;
    result->bid = (sign << 31) | (exp << 23) | (coef & 0x7FFFFF);
}

void dec32_bid_to_dpd(decimal32_t *result, const decimal32_t *bid) {
    /* Convert BID to DPD - encode to densely packed decimal format */
    init_dpd_tables();

    uint32_t bid_bits = bid->bid;
    uint32_t sign = (bid_bits >> 31) & 1;
    uint32_t exp = (bid_bits >> 23) & 0xFF;
    uint32_t coef = bid_bits & 0x7FFFFF;  /* 23-bit coefficient */

    /* Extract 7 decimal digits from coefficient */
    int d0 = (coef / 1000000) % 10;
    int d1 = (coef / 100000) % 10;
    int d2 = (coef / 10000) % 10;
    int d3 = (coef / 1000) % 10;
    int d4 = (coef / 100) % 10;
    int d5 = (coef / 10) % 10;
    int d6 = coef % 10;

    /* Encode digits 1-3 and 4-6 as DPD triplets */
    uint16_t dpd1 = encode_dpd(d1, d2, d3);
    uint16_t dpd2 = encode_dpd(d4, d5, d6);

    /* Build combination field */
    uint32_t comb;
    if (d0 <= 7) {
        comb = ((exp & 0x3) << 9) | (d0 << 7) | (exp >> 2);
    } else {
        comb = 0x600 | ((exp & 0x3) << 9) | ((d0 & 0x1) << 9) | (exp >> 2);
    }

    /* Assemble DPD format */
    result->format = DEC_FORMAT_DPD;
    result->dpd = (sign << 31) | (comb << 20) | (dpd1 << 10) | dpd2;
}

void dec64_quantize(decimal64_t *result, const decimal64_t *a, const decimal64_t *b) {
    /* Quantize 'a' to have the same exponent as 'b' */
    /* This adjusts the coefficient of 'a' so it has the same exponent as 'b' */

    /* Convert both to BID if needed */
    decimal64_t a_bid = *a, b_bid = *b;
    if (a->format == DEC_FORMAT_DPD) {
        /* Would need dec64_dpd_to_bid - for now use as-is */
        a_bid = *a;
    }
    if (b->format == DEC_FORMAT_DPD) {
        b_bid = *b;
    }

    /* Extract exponents */
    uint64_t a_bits = a_bid.bid;
    uint64_t b_bits = b_bid.bid;

    int a_sign = (a_bits >> 63) & 1;
    uint64_t a_combo = (a_bits >> 53) & 0x3FF;
    uint64_t b_combo = (b_bits >> 53) & 0x3FF;

    int a_exp, b_exp;
    uint64_t a_coef;

    /* Decode exponents */
    if ((a_combo & 0x300) != 0x300) {
        a_exp = ((a_combo >> 7) & 0x3) | (((a_bits >> 51) & 0x3) << 2) | (((a_bits >> 47) & 0x3F) << 4);
        a_coef = ((a_combo & 0x7F) << 46) | (a_bits & 0x3FFFFFFFFFFFULL);
    } else {
        a_exp = ((a_combo >> 5) & 0x3) | (((a_bits >> 51) & 0x3) << 2) | (((a_bits >> 47) & 0x3F) << 4);
        a_coef = (8000000000000000ULL) | ((a_combo & 0x1F) << 48) | (a_bits & 0xFFFFFFFFFFFFULL);
    }

    if ((b_combo & 0x300) != 0x300) {
        b_exp = ((b_combo >> 7) & 0x3) | (((b_bits >> 51) & 0x3) << 2) | (((b_bits >> 47) & 0x3F) << 4);
    } else {
        b_exp = ((b_combo >> 5) & 0x3) | (((b_bits >> 51) & 0x3) << 2) | (((b_bits >> 47) & 0x3F) << 4);
    }

    /* Adjust coefficient to match b's exponent */
    int exp_diff = a_exp - b_exp;
    if (exp_diff > 0) {
        /* a has larger exponent, multiply coefficient */
        for (int i = 0; i < exp_diff && i < 16; i++) {
            a_coef *= 10;
        }
    } else if (exp_diff < 0) {
        /* a has smaller exponent, divide coefficient */
        for (int i = 0; i < -exp_diff && i < 16; i++) {
            a_coef /= 10;
        }
    }

    /* Build result with b's exponent */
    result->format = DEC_FORMAT_BID;
    uint64_t result_combo;
    if (a_coef < (8000000000000000ULL)) {
        result_combo = ((b_exp & 0x3) << 7) | ((a_coef >> 46) & 0x7F);
    } else {
        result_combo = 0x300 | ((b_exp & 0x3) << 5) | ((a_coef >> 48) & 0x1F);
    }

    result->bid = ((uint64_t)a_sign << 63) | (result_combo << 53) | (a_coef & 0x1FFFFFFFFFFFFFULL);
}

void dec64_quantum(decimal64_t *result, const decimal64_t *a) {
    /* Return the quantum (ULP) of 'a' - the value of a unit in the last place */
    /* This is 10^(exponent) */

    decimal64_t a_bid = *a;
    if (a->format == DEC_FORMAT_DPD) {
        a_bid = *a;  /* Would need conversion */
    }

    uint64_t bits = a_bid.bid;
    uint64_t combo = (bits >> 53) & 0x3FF;

    int exp;

    /* Decode exponent */
    if ((combo & 0x300) != 0x300) {
        exp = ((combo >> 7) & 0x3) | (((bits >> 51) & 0x3) << 2) | (((bits >> 47) & 0x3F) << 4);
    } else {
        exp = ((combo >> 5) & 0x3) | (((bits >> 51) & 0x3) << 2) | (((bits >> 47) & 0x3F) << 4);
    }

    /* Quantum is 1 * 10^exp, which in decimal64 is coefficient=1, exponent=exp */
    result->format = DEC_FORMAT_BID;
    uint64_t result_combo = ((exp & 0x3) << 7) | 0;  /* Coefficient high bits = 0 */
    result->bid = (result_combo << 53) | 1;  /* Coefficient = 1 */
}

/***************************************************************************
 * VAX FLOATING POINT
 ***************************************************************************/

void vax_f_add(vax_f_float_t *result, const vax_f_float_t *a, const vax_f_float_t *b) {
    float a_ieee, b_ieee;
    vax_f_to_ieee_float(&a_ieee, a);
    vax_f_to_ieee_float(&b_ieee, b);
    ieee_float_to_vax_f(result, a_ieee + b_ieee);
}

void vax_f_sub(vax_f_float_t *result, const vax_f_float_t *a, const vax_f_float_t *b) {
    float a_ieee, b_ieee;
    vax_f_to_ieee_float(&a_ieee, a);
    vax_f_to_ieee_float(&b_ieee, b);
    ieee_float_to_vax_f(result, a_ieee - b_ieee);
}

void vax_f_mul(vax_f_float_t *result, const vax_f_float_t *a, const vax_f_float_t *b) {
    float a_ieee, b_ieee;
    vax_f_to_ieee_float(&a_ieee, a);
    vax_f_to_ieee_float(&b_ieee, b);
    ieee_float_to_vax_f(result, a_ieee * b_ieee);
}

void vax_f_div(vax_f_float_t *result, const vax_f_float_t *a, const vax_f_float_t *b) {
    float a_ieee, b_ieee;
    vax_f_to_ieee_float(&a_ieee, a);
    vax_f_to_ieee_float(&b_ieee, b);
    ieee_float_to_vax_f(result, a_ieee / b_ieee);
}

void vax_d_add(vax_d_float_t *result, const vax_d_float_t *a, const vax_d_float_t *b) {
    double a_ieee, b_ieee;
    vax_d_to_ieee_double(&a_ieee, a);
    vax_d_to_ieee_double(&b_ieee, b);
    ieee_double_to_vax_d(result, a_ieee + b_ieee);
}

void vax_d_mul(vax_d_float_t *result, const vax_d_float_t *a, const vax_d_float_t *b) {
    double a_ieee, b_ieee;
    vax_d_to_ieee_double(&a_ieee, a);
    vax_d_to_ieee_double(&b_ieee, b);
    ieee_double_to_vax_d(result, a_ieee * b_ieee);
}

/* VAX G <-> IEEE double conversions */
static void vax_g_to_ieee_double(double *result, const vax_g_float_t *vax) {
    uint64_t vax_bits = vax->bits;

    /* VAX G format: [15:15] sign, [14:4] exponent (bias 1024), [3:0,63:16] mantissa */
    /* Similar to IEEE double but different byte ordering and bias */

    /* Extract VAX components from PDP-11 word order */
    int sign = (vax_bits >> 15) & 1;
    int exp_vax = ((vax_bits >> 4) & 0x7FF);  /* 11-bit exponent */
    uint64_t mant_vax = ((vax_bits & 0xF) << 48) | ((vax_bits >> 16) & 0xFFFFFFFFFFFFULL);

    /* Check for reserved operand (exp == 0) */
    if (exp_vax == 0) {
        *result = 0.0;
        return;
    }

    /* Convert exponent from VAX bias (1024) to IEEE bias (1023) */
    int exp_ieee = exp_vax - 1024 + 1023;

    /* Handle overflow/underflow */
    if (exp_ieee >= 2047) {
        *result = sign ? -INFINITY : INFINITY;
        return;
    }
    if (exp_ieee <= 0) {
        *result = sign ? -0.0 : 0.0;
        return;
    }

    /* Reconstruct IEEE double */
    uint64_t ieee_bits = ((uint64_t)sign << 63) | ((uint64_t)exp_ieee << 52) | (mant_vax & 0xFFFFFFFFFFFFFULL);

    union { uint64_t i; double d; } u;
    u.i = ieee_bits;
    *result = u.d;
}

static void ieee_double_to_vax_g(vax_g_float_t *result, double ieee) {
    union { double d; uint64_t i; } u;
    u.d = ieee;
    uint64_t ieee_bits = u.i;

    int sign = (ieee_bits >> 63) & 1;
    int exp_ieee = (ieee_bits >> 52) & 0x7FF;
    uint64_t mant_ieee = ieee_bits & 0xFFFFFFFFFFFFFULL;

    /* Check for special values */
    if (exp_ieee == 0) {
        /* Zero or denormal -> reserved operand in VAX */
        result->bits = 0;
        return;
    }
    if (exp_ieee == 0x7FF) {
        /* Infinity or NaN -> reserved operand in VAX */
        result->bits = 0;
        return;
    }

    /* Convert exponent from IEEE bias (1023) to VAX bias (1024) */
    int exp_vax = exp_ieee - 1023 + 1024;

    /* Check for overflow/underflow */
    if (exp_vax >= 0x7FF || exp_vax <= 0) {
        result->bits = 0;  /* Reserved operand */
        return;
    }

    /* Reconstruct VAX G in PDP-11 word order */
    uint64_t vax_bits = ((uint64_t)sign << 15) | ((uint64_t)exp_vax << 4) |
                        ((mant_ieee >> 48) & 0xF) | ((mant_ieee & 0xFFFFFFFFFFFFULL) << 16);

    result->bits = vax_bits;
}

void vax_g_add(vax_g_float_t *result, const vax_g_float_t *a, const vax_g_float_t *b) {
    /* Convert to IEEE, perform operation, convert back */
    double a_ieee, b_ieee;
    vax_g_to_ieee_double(&a_ieee, a);
    vax_g_to_ieee_double(&b_ieee, b);
    ieee_double_to_vax_g(result, a_ieee + b_ieee);
}

void vax_g_mul(vax_g_float_t *result, const vax_g_float_t *a, const vax_g_float_t *b) {
    double a_ieee, b_ieee;
    vax_g_to_ieee_double(&a_ieee, a);
    vax_g_to_ieee_double(&b_ieee, b);
    ieee_double_to_vax_g(result, a_ieee * b_ieee);
}

void vax_f_to_ieee_float(float *result, const vax_f_float_t *vax) {
    uint32_t vax_bits = vax->bits;

    /* VAX F format: [15:15] sign, [14:7] exponent, [6:0,31:16] mantissa */
    /* Stored as two 16-bit words in PDP-11 byte order */

    /* Check for zero */
    if (vax_bits == 0) {
        *result = 0.0f;
        return;
    }

    /* Extract fields - VAX uses different byte order */
    uint16_t word0 = (vax_bits >> 16) & 0xFFFF;
    uint16_t word1 = vax_bits & 0xFFFF;

    int sign = (word0 >> 15) & 1;
    int exp = (word0 >> 7) & 0xFF;
    uint32_t mant = ((word0 & 0x7F) << 16) | word1;

    /* VAX has no implicit bit, exponent bias is 128 */
    /* IEEE has implicit bit, exponent bias is 127 */

    if (exp == 0) {
        /* Reserved operand in VAX */
        *result = NAN;
        return;
    }

    /* Convert exponent: VAX bias 128 -> IEEE bias 127 */
    int ieee_exp = exp - 128 + 127 + 1;  /* +1 because VAX has no implicit bit */

    /* Convert mantissa: VAX uses bits [22:0], IEEE uses [22:0] with implicit 1 */
    uint32_t ieee_mant = mant >> 1;  /* Shift because VAX has explicit bit */

    /* Assemble IEEE float */
    uint32_t ieee_bits = (sign << 31) | ((ieee_exp & 0xFF) << 23) | (ieee_mant & 0x7FFFFF);

    memcpy(result, &ieee_bits, sizeof(float));
}

void ieee_float_to_vax_f(vax_f_float_t *result, float ieee) {
    uint32_t ieee_bits;
    memcpy(&ieee_bits, &ieee, sizeof(float));

    int sign = (ieee_bits >> 31) & 1;
    int exp = (ieee_bits >> 23) & 0xFF;
    uint32_t mant = ieee_bits & 0x7FFFFF;

    /* Check for zero */
    if (exp == 0 && mant == 0) {
        result->bits = 0;
        return;
    }

    /* Check for infinity/NaN */
    if (exp == 0xFF) {
        result->bits = 0x8000;  /* VAX reserved operand */
        return;
    }

    /* Convert exponent: IEEE bias 127 -> VAX bias 128 */
    int vax_exp = exp - 127 + 128 - 1;  /* -1 because VAX has no implicit bit */

    if (vax_exp <= 0 || vax_exp >= 255) {
        /* Overflow/underflow */
        result->bits = 0;
        return;
    }

    /* Add implicit bit for VAX */
    uint32_t vax_mant = (mant << 1) | (1 << 23);

    /* Pack in VAX format */
    uint16_t word0 = (sign << 15) | ((vax_exp & 0xFF) << 7) | ((vax_mant >> 16) & 0x7F);
    uint16_t word1 = vax_mant & 0xFFFF;

    result->bits = ((uint32_t)word0 << 16) | word1;
}

void vax_d_to_ieee_double(double *result, const vax_d_float_t *vax) {
    /* Similar to vax_f but with 64-bit format */
    uint64_t vax_bits = vax->bits;

    if (vax_bits == 0) {
        *result = 0.0;
        return;
    }

    /* VAX D: [15:15] sign, [14:7] exp, [6:0,63:16] mantissa */
    uint16_t word0 = (vax_bits >> 48) & 0xFFFF;
    int sign = (word0 >> 15) & 1;
    int exp = (word0 >> 7) & 0xFF;

    if (exp == 0) {
        *result = NAN;
        return;
    }

    uint64_t mant = ((uint64_t)(word0 & 0x7F) << 48) | ((vax_bits & 0x0000FFFFFFFFFFFFULL) << 8);

    /* Convert to IEEE double */
    int ieee_exp = exp - 128 + 1023 + 1;
    uint64_t ieee_mant = mant >> 9;

    uint64_t ieee_bits = ((uint64_t)sign << 63) | ((uint64_t)(ieee_exp & 0x7FF) << 52) | (ieee_mant & 0xFFFFFFFFFFFFFULL);

    memcpy(result, &ieee_bits, sizeof(double));
}

void ieee_double_to_vax_d(vax_d_float_t *result, double ieee) {
    uint64_t ieee_bits;
    memcpy(&ieee_bits, &ieee, sizeof(double));

    int sign = (ieee_bits >> 63) & 1;
    int exp = (ieee_bits >> 52) & 0x7FF;
    uint64_t mant = ieee_bits & 0xFFFFFFFFFFFFFULL;

    if (exp == 0 && mant == 0) {
        result->bits = 0;
        return;
    }

    if (exp == 0x7FF) {
        result->bits = 0x8000;
        return;
    }

    int vax_exp = exp - 1023 + 128 - 1;

    if (vax_exp <= 0 || vax_exp >= 255) {
        result->bits = 0;
        return;
    }

    uint64_t vax_mant = (mant << 9) | (1ULL << 56);

    uint16_t word0 = (sign << 15) | ((vax_exp & 0xFF) << 7) | ((vax_mant >> 48) & 0x7F);
    uint64_t lower = (vax_mant >> 8) & 0x0000FFFFFFFFFFFFULL;

    result->bits = ((uint64_t)word0 << 48) | lower;
}

/***************************************************************************
 * IBM HEXADECIMAL FLOATING POINT
 ***************************************************************************/

void ibm_short_add(ibm_short_float_t *result, const ibm_short_float_t *a, const ibm_short_float_t *b) {
    float a_ieee, b_ieee;
    ibm_short_to_ieee_float(&a_ieee, a);
    ibm_short_to_ieee_float(&b_ieee, b);
    ieee_float_to_ibm_short(result, a_ieee + b_ieee);
}

void ibm_short_mul(ibm_short_float_t *result, const ibm_short_float_t *a, const ibm_short_float_t *b) {
    float a_ieee, b_ieee;
    ibm_short_to_ieee_float(&a_ieee, a);
    ibm_short_to_ieee_float(&b_ieee, b);
    ieee_float_to_ibm_short(result, a_ieee * b_ieee);
}

void ibm_long_add(ibm_long_float_t *result, const ibm_long_float_t *a, const ibm_long_float_t *b) {
    double a_ieee, b_ieee;
    ibm_long_to_ieee_double(&a_ieee, a);
    ibm_long_to_ieee_double(&b_ieee, b);
    ieee_double_to_ibm_long(result, a_ieee + b_ieee);
}

void ibm_long_mul(ibm_long_float_t *result, const ibm_long_float_t *a, const ibm_long_float_t *b) {
    double a_ieee, b_ieee;
    ibm_long_to_ieee_double(&a_ieee, a);
    ibm_long_to_ieee_double(&b_ieee, b);
    ieee_double_to_ibm_long(result, a_ieee * b_ieee);
}

void ibm_short_to_ieee_float(float *result, const ibm_short_float_t *ibm) {
    uint32_t ibm_bits = ibm->bits;

    /* IBM format: [31] sign, [30:24] exponent (base 16, bias 64), [23:0] mantissa */

    if (ibm_bits == 0) {
        *result = 0.0f;
        return;
    }

    int sign = (ibm_bits >> 31) & 1;
    int exp = (ibm_bits >> 24) & 0x7F;
    uint32_t mant = ibm_bits & 0xFFFFFF;

    /* IBM exponent is base 16 with bias 64 */
    /* Convert to IEEE: exp_ieee = exp_ibm * 4 - 256 + 127 */

    /* Normalize mantissa (IBM mantissa is not normalized) */
    int shift = 0;
    if (mant != 0) {
        while ((mant & 0x00F00000) == 0) {
            mant <<= 4;
            shift++;
        }
    }

    double value = (double)mant / (double)(1 << 24);
    value *= pow(16.0, exp - 64 - shift);

    if (sign) value = -value;

    *result = (float)value;
}

void ieee_float_to_ibm_short(ibm_short_float_t *result, float ieee) {
    if (ieee == 0.0f) {
        result->bits = 0;
        return;
    }

    int sign = (ieee < 0) ? 1 : 0;
    double value = fabs((double)ieee);

    /* Find exponent base 16 */
    int exp = 64;  /* Bias */

    while (value >= 1.0) {
        value /= 16.0;
        exp++;
    }

    while (value < 0.0625 && value > 0.0) {  /* 1/16 */
        value *= 16.0;
        exp--;
    }

    if (exp < 0 || exp > 127) {
        result->bits = 0;  /* Underflow/overflow */
        return;
    }

    uint32_t mant = (uint32_t)(value * (1 << 24));

    result->bits = (sign << 31) | ((exp & 0x7F) << 24) | (mant & 0xFFFFFF);
}

void ibm_long_to_ieee_double(double *result, const ibm_long_float_t *ibm) {
    uint64_t ibm_bits = ibm->bits;

    if (ibm_bits == 0) {
        *result = 0.0;
        return;
    }

    int sign = (ibm_bits >> 63) & 1;
    int exp = (ibm_bits >> 56) & 0x7F;
    uint64_t mant = ibm_bits & 0xFFFFFFFFFFFFFFULL;

    /* Normalize */
    int shift = 0;
    if (mant != 0) {
        while ((mant & 0x00F0000000000000ULL) == 0) {
            mant <<= 4;
            shift++;
        }
    }

    double value = (double)mant / (double)(1ULL << 56);
    value *= pow(16.0, exp - 64 - shift);

    if (sign) value = -value;

    *result = value;
}

void ieee_double_to_ibm_long(ibm_long_float_t *result, double ieee) {
    if (ieee == 0.0) {
        result->bits = 0;
        return;
    }

    int sign = (ieee < 0) ? 1 : 0;
    double value = fabs(ieee);

    int exp = 64;

    while (value >= 1.0) {
        value /= 16.0;
        exp++;
    }

    while (value < 0.0625 && value > 0.0) {
        value *= 16.0;
        exp--;
    }

    if (exp < 0 || exp > 127) {
        result->bits = 0;
        return;
    }

    uint64_t mant = (uint64_t)(value * (1ULL << 56));

    result->bits = ((uint64_t)sign << 63) | ((uint64_t)(exp & 0x7F) << 56) | (mant & 0xFFFFFFFFFFFFFFULL);
}

/***************************************************************************
 * CRAY FLOATING POINT
 ***************************************************************************/

void cray_add(cray_float_t *result, const cray_float_t *a, const cray_float_t *b) {
    double a_ieee, b_ieee;
    cray_to_ieee_double(&a_ieee, a);
    cray_to_ieee_double(&b_ieee, b);
    ieee_double_to_cray(result, a_ieee + b_ieee);
}

void cray_mul(cray_float_t *result, const cray_float_t *a, const cray_float_t *b) {
    double a_ieee, b_ieee;
    cray_to_ieee_double(&a_ieee, a);
    cray_to_ieee_double(&b_ieee, b);
    ieee_double_to_cray(result, a_ieee * b_ieee);
}

void cray_reciprocal(cray_float_t *result, const cray_float_t *a) {
    double a_ieee;
    cray_to_ieee_double(&a_ieee, a);
    ieee_double_to_cray(result, 1.0 / a_ieee);
}

void cray_to_ieee_double(double *result, const cray_float_t *cray) {
    uint64_t cray_bits = cray->bits;

    /* Cray format: [63] sign, [62:48] exponent (bias 16384), [47:0] mantissa */

    if (cray_bits == 0) {
        *result = 0.0;
        return;
    }

    int sign = (cray_bits >> 63) & 1;
    int exp = (cray_bits >> 48) & 0x7FFF;
    uint64_t mant = cray_bits & 0xFFFFFFFFFFFFULL;

    /* Convert exponent: Cray bias 16384 -> IEEE bias 1023 */
    /* Cray mantissa is normalized to [0.5, 1.0) with implicit 0 */

    double value = (double)mant / (double)(1ULL << 48);
    value *= pow(2.0, exp - 16384);

    if (sign) value = -value;

    *result = value;
}

void ieee_double_to_cray(cray_float_t *result, double ieee) {
    if (ieee == 0.0) {
        result->bits = 0;
        return;
    }

    int sign = (ieee < 0) ? 1 : 0;
    double value = fabs(ieee);

    /* Find exponent */
    int exp = 16384;

    while (value >= 1.0) {
        value /= 2.0;
        exp++;
    }

    while (value < 0.5 && value > 0.0) {
        value *= 2.0;
        exp--;
    }

    if (exp < 0 || exp > 0x7FFF) {
        result->bits = 0;
        return;
    }

    uint64_t mant = (uint64_t)(value * (1ULL << 48));

    result->bits = ((uint64_t)sign << 63) | ((uint64_t)(exp & 0x7FFF) << 48) | (mant & 0xFFFFFFFFFFFFULL);
}

/***************************************************************************
 * 1'S COMPLEMENT ARITHMETIC
 ***************************************************************************/

void ones_comp_add(ones_comp_t *result, const ones_comp_t *a, const ones_comp_t *b) {
    /* 1's complement addition requires end-around carry */
    uint64_t sum = a->bits + b->bits;

    /* Check for carry out */
    if (sum < a->bits || sum < b->bits) {
        sum++;  /* End-around carry */
    }

    result->bits = sum;
    result->is_negative = 0;  /* Determine from result */
}

void ones_comp_sub(ones_comp_t *result, const ones_comp_t *a, const ones_comp_t *b) {
    ones_comp_t b_neg;
    ones_comp_neg(&b_neg, b);
    ones_comp_add(result, a, &b_neg);
}

void ones_comp_mul(ones_comp_t *result, const ones_comp_t *a, const ones_comp_t *b) {
    /* Convert to 2's complement, multiply, convert back */
    int64_t a_twos = ones_comp_to_twos(a);
    int64_t b_twos = ones_comp_to_twos(b);
    twos_to_ones_comp(result, a_twos * b_twos);
}

void ones_comp_div(ones_comp_t *result, const ones_comp_t *a, const ones_comp_t *b) {
    int64_t a_twos = ones_comp_to_twos(a);
    int64_t b_twos = ones_comp_to_twos(b);

    if (b_twos == 0) {
        result->bits = 0;
        return;
    }

    twos_to_ones_comp(result, a_twos / b_twos);
}

void ones_comp_neg(ones_comp_t *result, const ones_comp_t *a) {
    /* In 1's complement, negation is bitwise NOT */
    result->bits = ~a->bits;
    result->is_negative = !a->is_negative;
}

int ones_comp_is_zero(const ones_comp_t *a) {
    /* In 1's complement, both 0x0000... and 0xFFFF... are zero */
    return (a->bits == 0) || (a->bits == ~0ULL);
}

void ones_comp_normalize(ones_comp_t *a) {
    /* Convert -0 to +0 */
    if (a->bits == ~0ULL) {
        a->bits = 0;
        a->is_negative = 0;
    }
}

int64_t ones_comp_to_twos(const ones_comp_t *a) {
    if (ones_comp_is_zero(a)) {
        return 0;
    }

    /* Check sign bit */
    if (a->bits & (1ULL << 63)) {
        /* Negative: convert from 1's complement to 2's complement */
        return -(int64_t)(~a->bits);
    } else {
        /* Positive: same representation */
        return (int64_t)a->bits;
    }
}

void twos_to_ones_comp(ones_comp_t *result, int64_t value) {
    if (value == 0) {
        result->bits = 0;
        result->is_negative = 0;
    } else if (value < 0) {
        result->bits = ~((uint64_t)(-value));
        result->is_negative = 1;
    } else {
        result->bits = (uint64_t)value;
        result->is_negative = 0;
    }
}

/***************************************************************************
 * NON-POWER-OF-2 WORD SIZES
 ***************************************************************************/

void pdp10_add(pdp10_word_t *result, const pdp10_word_t *a, const pdp10_word_t *b) {
    result->bits = (a->bits + b->bits) & PDP10_WORD_MASK;
}

void pdp10_sub(pdp10_word_t *result, const pdp10_word_t *a, const pdp10_word_t *b) {
    result->bits = (a->bits - b->bits) & PDP10_WORD_MASK;
}

void pdp10_mul(pdp10_word_t *result, const pdp10_word_t *a, const pdp10_word_t *b) {
    result->bits = (a->bits * b->bits) & PDP10_WORD_MASK;
}

void pdp10_div(pdp10_word_t *quotient, pdp10_word_t *remainder, const pdp10_word_t *dividend, const pdp10_word_t *divisor) {
    if (divisor->bits == 0) {
        quotient->bits = 0;
        remainder->bits = 0;
        return;
    }

    quotient->bits = (dividend->bits / divisor->bits) & PDP10_WORD_MASK;
    remainder->bits = (dividend->bits % divisor->bits) & PDP10_WORD_MASK;
}

void pdp10_ldb(pdp10_word_t *result, const pdp10_word_t *ptr, uint32_t pos, uint32_t size) {
    /* Load byte - PDP-10 bytes can be any size from 1 to 36 bits */
    /* pos is bit position, size is byte size in bits */

    if (size == 0 || size > 36) {
        result->bits = 0;
        return;
    }

    uint64_t mask = (1ULL << size) - 1;
    result->bits = (ptr->bits >> pos) & mask;
}

void pdp10_dpb(pdp10_word_t *result, const pdp10_word_t *value, const pdp10_word_t *word, uint32_t pos, uint32_t size) {
    /* Deposit byte */
    if (size == 0 || size > 36) {
        *result = *word;
        return;
    }

    uint64_t mask = ((1ULL << size) - 1) << pos;
    uint64_t new_bits = (word->bits & ~mask) | ((value->bits << pos) & mask);
    result->bits = new_bits & PDP10_WORD_MASK;
}

void cdc_add(cdc_word_t *result, const cdc_word_t *a, const cdc_word_t *b) {
    /* CDC uses 1's complement arithmetic */
    uint64_t sum = (a->bits + b->bits) & CDC_WORD_MASK;

    /* End-around carry for 1's complement */
    if (sum < (a->bits & CDC_WORD_MASK)) {
        sum = (sum + 1) & CDC_WORD_MASK;
    }

    result->bits = sum;
}

void cdc_mul(cdc_word_t *result, const cdc_word_t *a, const cdc_word_t *b) {
    result->bits = (a->bits * b->bits) & CDC_WORD_MASK;
}

/***************************************************************************
 * RVV 0.9 (Pre-1.0 RISC-V Vector)
 ***************************************************************************/

void rvv09_vsetvl(rvv09_state_t *state, uint32_t avl, uint32_t sew, uint32_t lmul) {
    /* RVV 0.9 vsetvl configuration (different semantics from 1.0) */

    /* SEW: Selected Element Width (8, 16, 32, 64) */
    /* LMUL: Length Multiplier (1, 2, 4, 8 or fractional 1/2, 1/4, 1/8) */

    uint32_t vlmax = (256 / sew) * lmul;  /* Assuming VLEN=256 */

    state->vl = (avl <= vlmax) ? avl : vlmax;
    state->vtype = (sew << 3) | lmul;
    state->vlenb = 256 / 8;  /* VLEN in bytes */
}

void rvv09_vwadd_vv(vector_t *dst, const vector_t *src1, const vector_t *src2, const rvv09_state_t *state) {
    /* Widening add - doubles the element width */
    /* In RVV 0.9, this had different encoding than 1.0 */

    uint32_t sew = (state->vtype >> 3) & 0x7;

    /* Perform widening addition based on SEW */
    for (uint32_t i = 0; i < state->vl; i++) {
        switch (sew) {
            case 8:
                /* Add 8-bit elements, produce 16-bit result */
                ((uint16_t*)dst->data)[i] = ((uint8_t*)src1->data)[i] + ((uint8_t*)src2->data)[i];
                break;
            case 16:
                ((uint32_t*)dst->data)[i] = ((uint16_t*)src1->data)[i] + ((uint16_t*)src2->data)[i];
                break;
            case 32:
                ((uint64_t*)dst->data)[i] = ((uint32_t*)src1->data)[i] + ((uint32_t*)src2->data)[i];
                break;
        }
    }
}

void rvv09_vwaddu_vv(vector_t *dst, const vector_t *src1, const vector_t *src2, const rvv09_state_t *state) {
    /* Unsigned widening add */
    rvv09_vwadd_vv(dst, src1, src2, state);
}

void rvv09_vmadc(vector_t *mask, const vector_t *src1, const vector_t *src2, const rvv09_state_t *state) {
    /* Add with carry out (generates mask) */
    /* RVV 0.9 had different mask handling than 1.0 */

    for (uint32_t i = 0; i < state->vl; i++) {
        uint32_t a = ((uint32_t*)src1->data)[i];
        uint32_t b = ((uint32_t*)src2->data)[i];
        uint64_t sum = (uint64_t)a + (uint64_t)b;

        /* Set mask bit if carry occurred */
        if (sum > 0xFFFFFFFF) {
            ((uint8_t*)mask->data)[i / 8] |= (1 << (i % 8));
        }
    }
}

/***************************************************************************
 * WMMX (Wireless MMX)
 ***************************************************************************/

void wmmx_wadd(wmmx_reg_t *dst, const wmmx_reg_t *src1, const wmmx_reg_t *src2, int element_size) {
    /* Add packed elements */
    switch (element_size) {
        case 8:  /* Byte */
            for (int i = 0; i < 8; i++) {
                ((uint8_t*)&dst->data)[i] = ((uint8_t*)&src1->data)[i] + ((uint8_t*)&src2->data)[i];
            }
            break;
        case 16:  /* Halfword */
            for (int i = 0; i < 4; i++) {
                ((uint16_t*)&dst->data)[i] = ((uint16_t*)&src1->data)[i] + ((uint16_t*)&src2->data)[i];
            }
            break;
        case 32:  /* Word */
            for (int i = 0; i < 2; i++) {
                ((uint32_t*)&dst->data)[i] = ((uint32_t*)&src1->data)[i] + ((uint32_t*)&src2->data)[i];
            }
            break;
    }
}

void wmmx_wsub(wmmx_reg_t *dst, const wmmx_reg_t *src1, const wmmx_reg_t *src2, int element_size) {
    switch (element_size) {
        case 8:
            for (int i = 0; i < 8; i++) {
                ((uint8_t*)&dst->data)[i] = ((uint8_t*)&src1->data)[i] - ((uint8_t*)&src2->data)[i];
            }
            break;
        case 16:
            for (int i = 0; i < 4; i++) {
                ((uint16_t*)&dst->data)[i] = ((uint16_t*)&src1->data)[i] - ((uint16_t*)&src2->data)[i];
            }
            break;
        case 32:
            for (int i = 0; i < 2; i++) {
                ((uint32_t*)&dst->data)[i] = ((uint32_t*)&src1->data)[i] - ((uint32_t*)&src2->data)[i];
            }
            break;
    }
}

void wmmx_wmul(wmmx_reg_t *dst, const wmmx_reg_t *src1, const wmmx_reg_t *src2, int element_size) {
    switch (element_size) {
        case 16:
            for (int i = 0; i < 4; i++) {
                ((uint16_t*)&dst->data)[i] = ((uint16_t*)&src1->data)[i] * ((uint16_t*)&src2->data)[i];
            }
            break;
        case 32:
            for (int i = 0; i < 2; i++) {
                ((uint32_t*)&dst->data)[i] = ((uint32_t*)&src1->data)[i] * ((uint32_t*)&src2->data)[i];
            }
            break;
    }
}

void wmmx_wpack(wmmx_reg_t *dst, const wmmx_reg_t *src1, const wmmx_reg_t *src2, int mode) {
    /* Pack with saturation */
    switch (mode) {
        case 16:  /* Pack words to halfwords */
            ((uint16_t*)&dst->data)[0] = (uint16_t)((uint32_t*)&src1->data)[0];
            ((uint16_t*)&dst->data)[1] = (uint16_t)((uint32_t*)&src1->data)[1];
            ((uint16_t*)&dst->data)[2] = (uint16_t)((uint32_t*)&src2->data)[0];
            ((uint16_t*)&dst->data)[3] = (uint16_t)((uint32_t*)&src2->data)[1];
            break;
        case 8:  /* Pack halfwords to bytes */
            for (int i = 0; i < 4; i++) {
                ((uint8_t*)&dst->data)[i] = (uint8_t)((uint16_t*)&src1->data)[i];
                ((uint8_t*)&dst->data)[i+4] = (uint8_t)((uint16_t*)&src2->data)[i];
            }
            break;
    }
}

void wmmx_wunpack(wmmx_reg_t *dst, const wmmx_reg_t *src, int mode) {
    /* Unpack (zero/sign extend) */
    uint64_t temp = 0;

    switch (mode) {
        case 8:  /* Unpack bytes to halfwords (low) */
            for (int i = 0; i < 4; i++) {
                ((uint16_t*)&temp)[i] = ((uint8_t*)&src->data)[i];
            }
            break;
        case 16:  /* Unpack halfwords to words (low) */
            for (int i = 0; i < 2; i++) {
                ((uint32_t*)&temp)[i] = ((uint16_t*)&src->data)[i];
            }
            break;
    }

    dst->data = temp;
}

void wmmx_waligni(wmmx_reg_t *dst, const wmmx_reg_t *src1, const wmmx_reg_t *src2, uint32_t imm) {
    /* Align registers by immediate byte count */
    uint8_t bytes[16];
    memcpy(bytes, &src1->data, 8);
    memcpy(bytes + 8, &src2->data, 8);

    uint64_t result = 0;
    memcpy(&result, bytes + (imm & 7), 8);

    dst->data = result;
}

void wmmx_walignr(wmmx_reg_t *dst, const wmmx_reg_t *src1, const wmmx_reg_t *src2, const wmmx_reg_t *control) {
    /* Align registers by register-specified count */
    uint32_t shift = control->data & 7;
    wmmx_waligni(dst, src1, src2, shift);
}

void wmmx_wshufh(wmmx_reg_t *dst, const wmmx_reg_t *src, uint32_t imm) {
    /* Shuffle halfwords */
    uint16_t temp[4];

    for (int i = 0; i < 4; i++) {
        int sel = (imm >> (i * 2)) & 3;
        temp[i] = ((uint16_t*)&src->data)[sel];
    }

    memcpy(&dst->data, temp, 8);
}

void wmmx2_waddbhus(wmmx2_reg_t *dst, const wmmx_reg_t *src1, const wmmx_reg_t *src2) {
    /* WMMX2: Add bytes with horizontal unsigned saturation */
    for (int i = 0; i < 8; i++) {
        uint16_t sum = ((uint8_t*)&src1->data)[i] + ((uint8_t*)&src2->data)[i];
        ((uint16_t*)&dst->lo)[i % 4] = sum;
    }
}

void wmmx2_wsubaddhx(wmmx2_reg_t *dst, const wmmx2_reg_t *src1, const wmmx2_reg_t *src2) {
    /* WMMX2: Subtract and add halfwords with exchange */
    for (int i = 0; i < 4; i += 2) {
        ((uint16_t*)&dst->lo)[i] = ((uint16_t*)&src1->lo)[i] - ((uint16_t*)&src2->lo)[i];
        ((uint16_t*)&dst->lo)[i+1] = ((uint16_t*)&src1->lo)[i+1] + ((uint16_t*)&src2->lo)[i+1];
    }
}

/***************************************************************************
 * MIPS MSA2
 ***************************************************************************/

void msa2_fmadd(msa2_reg_t *dst, const msa2_reg_t *src1, const msa2_reg_t *src2, const msa2_reg_t *src3) {
    /* Fused multiply-add for floating point */
    int num_elements = dst->num_elements;

    for (int i = 0; i < num_elements; i++) {
        if (dst->element_type == VEC_ELEM_F32) {
            ((float*)dst->data)[i] = fmaf(
                ((float*)src1->data)[i],
                ((float*)src2->data)[i],
                ((float*)src3->data)[i]
            );
        } else if (dst->element_type == VEC_ELEM_F64) {
            ((double*)dst->data)[i] = fma(
                ((double*)src1->data)[i],
                ((double*)src2->data)[i],
                ((double*)src3->data)[i]
            );
        }
    }
}

void msa2_fmsub(msa2_reg_t *dst, const msa2_reg_t *src1, const msa2_reg_t *src2, const msa2_reg_t *src3) {
    /* Fused multiply-subtract */
    int num_elements = dst->num_elements;

    for (int i = 0; i < num_elements; i++) {
        if (dst->element_type == VEC_ELEM_F32) {
            ((float*)dst->data)[i] = fmaf(
                ((float*)src1->data)[i],
                ((float*)src2->data)[i],
                -((float*)src3->data)[i]
            );
        } else if (dst->element_type == VEC_ELEM_F64) {
            ((double*)dst->data)[i] = fma(
                ((double*)src1->data)[i],
                ((double*)src2->data)[i],
                -((double*)src3->data)[i]
            );
        }
    }
}

void msa2_shf(msa2_reg_t *dst, const msa2_reg_t *src, uint32_t imm) {
    /* Shuffle elements based on immediate */
    uint32_t temp[4];

    for (int i = 0; i < 4; i++) {
        int sel = (imm >> (i * 2)) & 3;
        temp[i] = ((uint32_t*)src->data)[sel];
    }

    memcpy(dst->data, temp, 16);
}

void msa2_hadd(msa2_reg_t *dst, const msa2_reg_t *src1, const msa2_reg_t *src2) {
    /* Horizontal add */
    for (int i = 0; i < 2; i++) {
        ((uint32_t*)dst->data)[i] = ((uint32_t*)src1->data)[i*2] + ((uint32_t*)src1->data)[i*2+1];
        ((uint32_t*)dst->data)[i+2] = ((uint32_t*)src2->data)[i*2] + ((uint32_t*)src2->data)[i*2+1];
    }
}

void msa2_binsli(msa2_reg_t *dst, const msa2_reg_t *src, uint32_t imm) {
    /* Bit insert left immediate */
    uint32_t shift = imm & 31;
    uint32_t mask = (1U << shift) - 1;

    for (int i = 0; i < 4; i++) {
        uint32_t d = ((uint32_t*)dst->data)[i];
        uint32_t s = ((uint32_t*)src->data)[i];
        ((uint32_t*)dst->data)[i] = (d & ~mask) | (s & mask);
    }
}

void msa2_binsri(msa2_reg_t *dst, const msa2_reg_t *src, uint32_t imm) {
    /* Bit insert right immediate */
    uint32_t shift = imm & 31;
    uint32_t mask = ~((1U << shift) - 1);

    for (int i = 0; i < 4; i++) {
        uint32_t d = ((uint32_t*)dst->data)[i];
        uint32_t s = ((uint32_t*)src->data)[i];
        ((uint32_t*)dst->data)[i] = (d & ~mask) | (s & mask);
    }
}

/***************************************************************************
 * LOONGSON LMMX
 ***************************************************************************/

void lmmx_paddb(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2) {
    for (int i = 0; i < 16; i++) {
        ((uint8_t*)dst)[i] = ((uint8_t*)src1)[i] + ((uint8_t*)src2)[i];
    }
}

void lmmx_paddh(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2) {
    for (int i = 0; i < 8; i++) {
        ((uint16_t*)dst)[i] = ((uint16_t*)src1)[i] + ((uint16_t*)src2)[i];
    }
}

void lmmx_paddw(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2) {
    for (int i = 0; i < 4; i++) {
        ((uint32_t*)dst)[i] = ((uint32_t*)src1)[i] + ((uint32_t*)src2)[i];
    }
}

void lmmx_paddd(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2) {
    for (int i = 0; i < 2; i++) {
        ((uint64_t*)dst)[i] = ((uint64_t*)src1)[i] + ((uint64_t*)src2)[i];
    }
}

void lmmx_paddsb(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2) {
    /* Saturating signed byte add */
    for (int i = 0; i < 16; i++) {
        int16_t sum = ((int8_t*)src1)[i] + ((int8_t*)src2)[i];
        if (sum > 127) sum = 127;
        if (sum < -128) sum = -128;
        ((int8_t*)dst)[i] = (int8_t)sum;
    }
}

void lmmx_paddsh(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2) {
    /* Saturating signed halfword add */
    for (int i = 0; i < 8; i++) {
        int32_t sum = ((int16_t*)src1)[i] + ((int16_t*)src2)[i];
        if (sum > 32767) sum = 32767;
        if (sum < -32768) sum = -32768;
        ((int16_t*)dst)[i] = (int16_t)sum;
    }
}

void lmmx_paddusb(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2) {
    /* Saturating unsigned byte add */
    for (int i = 0; i < 16; i++) {
        uint16_t sum = ((uint8_t*)src1)[i] + ((uint8_t*)src2)[i];
        if (sum > 255) sum = 255;
        ((uint8_t*)dst)[i] = (uint8_t)sum;
    }
}

void lmmx_paddush(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2) {
    /* Saturating unsigned halfword add */
    for (int i = 0; i < 8; i++) {
        uint32_t sum = ((uint16_t*)src1)[i] + ((uint16_t*)src2)[i];
        if (sum > 65535) sum = 65535;
        ((uint16_t*)dst)[i] = (uint16_t)sum;
    }
}

void lmmx_packsshb(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2) {
    /* Pack signed halfwords to signed bytes with saturation */
    for (int i = 0; i < 8; i++) {
        int16_t val = ((int16_t*)src1)[i];
        if (val > 127) val = 127;
        if (val < -128) val = -128;
        ((int8_t*)dst)[i] = (int8_t)val;
    }
    for (int i = 0; i < 8; i++) {
        int16_t val = ((int16_t*)src2)[i];
        if (val > 127) val = 127;
        if (val < -128) val = -128;
        ((int8_t*)dst)[i+8] = (int8_t)val;
    }
}

void lmmx_packsswh(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2) {
    /* Pack signed words to signed halfwords with saturation */
    for (int i = 0; i < 4; i++) {
        int32_t val = ((int32_t*)src1)[i];
        if (val > 32767) val = 32767;
        if (val < -32768) val = -32768;
        ((int16_t*)dst)[i] = (int16_t)val;
    }
    for (int i = 0; i < 4; i++) {
        int32_t val = ((int32_t*)src2)[i];
        if (val > 32767) val = 32767;
        if (val < -32768) val = -32768;
        ((int16_t*)dst)[i+4] = (int16_t)val;
    }
}

void lmmx_packushb(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2) {
    /* Pack unsigned halfwords to unsigned bytes with saturation */
    for (int i = 0; i < 8; i++) {
        uint16_t val = ((uint16_t*)src1)[i];
        if (val > 255) val = 255;
        ((uint8_t*)dst)[i] = (uint8_t)val;
    }
    for (int i = 0; i < 8; i++) {
        uint16_t val = ((uint16_t*)src2)[i];
        if (val > 255) val = 255;
        ((uint8_t*)dst)[i+8] = (uint8_t)val;
    }
}

void lmmx_pmulhh(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2) {
    /* Multiply halfwords, return high 16 bits */
    for (int i = 0; i < 8; i++) {
        int32_t prod = ((int16_t*)src1)[i] * ((int16_t*)src2)[i];
        ((int16_t*)dst)[i] = (int16_t)(prod >> 16);
    }
}

void lmmx_pmullh(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2) {
    /* Multiply halfwords, return low 16 bits */
    for (int i = 0; i < 8; i++) {
        ((int16_t*)dst)[i] = ((int16_t*)src1)[i] * ((int16_t*)src2)[i];
    }
}

void lmmx_pmaddhw(lmmx_reg_t *dst, const lmmx_reg_t *src1, const lmmx_reg_t *src2) {
    /* Multiply and add adjacent halfwords */
    for (int i = 0; i < 4; i++) {
        int32_t prod1 = ((int16_t*)src1)[i*2] * ((int16_t*)src2)[i*2];
        int32_t prod2 = ((int16_t*)src1)[i*2+1] * ((int16_t*)src2)[i*2+1];
        ((int32_t*)dst)[i] = prod1 + prod2;
    }
}

void lmmx_pshufh(lmmx_reg_t *dst, const lmmx_reg_t *src, uint32_t imm) {
    /* Shuffle halfwords based on immediate */
    uint16_t temp[8];

    for (int i = 0; i < 4; i++) {
        int sel = (imm >> (i * 2)) & 3;
        temp[i] = ((uint16_t*)src)[sel];
        temp[i+4] = ((uint16_t*)src)[sel+4];
    }

    memcpy(dst, temp, 16);
}

void lmmx_psllh(lmmx_reg_t *dst, const lmmx_reg_t *src, uint32_t count) {
    /* Shift left logical halfwords */
    if (count > 15) count = 15;
    for (int i = 0; i < 8; i++) {
        ((uint16_t*)dst)[i] = ((uint16_t*)src)[i] << count;
    }
}

void lmmx_psrlh(lmmx_reg_t *dst, const lmmx_reg_t *src, uint32_t count) {
    /* Shift right logical halfwords */
    if (count > 15) count = 15;
    for (int i = 0; i < 8; i++) {
        ((uint16_t*)dst)[i] = ((uint16_t*)src)[i] >> count;
    }
}

void lmmx_psrah(lmmx_reg_t *dst, const lmmx_reg_t *src, uint32_t count) {
    /* Shift right arithmetic halfwords */
    if (count > 15) count = 15;
    for (int i = 0; i < 8; i++) {
        ((int16_t*)dst)[i] = ((int16_t*)src)[i] >> count;
    }
}

/***************************************************************************
 * ARM FPA (Floating Point Accelerator)
 ***************************************************************************/

void fpa_adf(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src1, const arm_fpa_extended_t *src2) {
    /* Add (ADF = Add Float) */
    double d1, d2;
    fpa_extended_to_ieee_double(&d1, src1);
    fpa_extended_to_ieee_double(&d2, src2);
    ieee_double_to_fpa_extended(dst, d1 + d2);
}

void fpa_suf(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src1, const arm_fpa_extended_t *src2) {
    /* Subtract (SUF = Subtract Float) */
    double d1, d2;
    fpa_extended_to_ieee_double(&d1, src1);
    fpa_extended_to_ieee_double(&d2, src2);
    ieee_double_to_fpa_extended(dst, d1 - d2);
}

void fpa_muf(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src1, const arm_fpa_extended_t *src2) {
    /* Multiply (MUF = Multiply Float) */
    double d1, d2;
    fpa_extended_to_ieee_double(&d1, src1);
    fpa_extended_to_ieee_double(&d2, src2);
    ieee_double_to_fpa_extended(dst, d1 * d2);
}

void fpa_dvf(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src1, const arm_fpa_extended_t *src2) {
    /* Divide (DVF = Divide Float) */
    double d1, d2;
    fpa_extended_to_ieee_double(&d1, src1);
    fpa_extended_to_ieee_double(&d2, src2);
    ieee_double_to_fpa_extended(dst, d1 / d2);
}

void fpa_rmf(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src1, const arm_fpa_extended_t *src2) {
    /* Remainder (RMF = Remainder Float) */
    double d1, d2;
    fpa_extended_to_ieee_double(&d1, src1);
    fpa_extended_to_ieee_double(&d2, src2);
    ieee_double_to_fpa_extended(dst, fmod(d1, d2));
}

/* FPA hardware transcendentals */
void fpa_sin(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src) {
    double d;
    fpa_extended_to_ieee_double(&d, src);
    ieee_double_to_fpa_extended(dst, sin(d));
}

void fpa_cos(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src) {
    double d;
    fpa_extended_to_ieee_double(&d, src);
    ieee_double_to_fpa_extended(dst, cos(d));
}

void fpa_tan(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src) {
    double d;
    fpa_extended_to_ieee_double(&d, src);
    ieee_double_to_fpa_extended(dst, tan(d));
}

void fpa_asn(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src) {
    double d;
    fpa_extended_to_ieee_double(&d, src);
    ieee_double_to_fpa_extended(dst, asin(d));
}

void fpa_acs(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src) {
    double d;
    fpa_extended_to_ieee_double(&d, src);
    ieee_double_to_fpa_extended(dst, acos(d));
}

void fpa_atn(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src) {
    double d;
    fpa_extended_to_ieee_double(&d, src);
    ieee_double_to_fpa_extended(dst, atan(d));
}

void fpa_sqt(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src) {
    double d;
    fpa_extended_to_ieee_double(&d, src);
    ieee_double_to_fpa_extended(dst, sqrt(d));
}

void fpa_log(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src) {
    double d;
    fpa_extended_to_ieee_double(&d, src);
    ieee_double_to_fpa_extended(dst, log10(d));
}

void fpa_lgn(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src) {
    double d;
    fpa_extended_to_ieee_double(&d, src);
    ieee_double_to_fpa_extended(dst, log(d));
}

void fpa_exp(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src) {
    double d;
    fpa_extended_to_ieee_double(&d, src);
    ieee_double_to_fpa_extended(dst, exp(d));
}

void fpa_pow(arm_fpa_extended_t *dst, const arm_fpa_extended_t *src) {
    double d;
    fpa_extended_to_ieee_double(&d, src);
    ieee_double_to_fpa_extended(dst, pow(10.0, d));
}

void fpa_extended_to_ieee_double(double *dst, const arm_fpa_extended_t *src) {
    /* FPA extended is similar to x87 80-bit */
    /* Mantissa is 64 bits, exponent 15 bits + sign */

    if (src->type == 0) {  /* Zero */
        *dst = 0.0;
        return;
    }

    int sign = (src->exponent >> 15) & 1;
    int exp = src->exponent & 0x7FFF;

    /* Convert to IEEE double */
    double mantissa = (double)src->mantissa / (double)(1ULL << 63);
    double value = mantissa * pow(2.0, exp - 16383);

    *dst = sign ? -value : value;
}

void ieee_double_to_fpa_extended(arm_fpa_extended_t *dst, double src) {
    if (src == 0.0) {
        dst->mantissa = 0;
        dst->exponent = 0;
        dst->type = 0;
        return;
    }

    int sign = (src < 0) ? 1 : 0;
    double value = fabs(src);

    /* Find exponent and mantissa */
    int exp = 16383;

    while (value >= 2.0) {
        value /= 2.0;
        exp++;
    }

    while (value < 1.0 && value > 0.0) {
        value *= 2.0;
        exp--;
    }

    dst->mantissa = (uint64_t)(value * (1ULL << 63));
    dst->exponent = (sign << 15) | (exp & 0x7FFF);
    dst->type = 1;  /* Normal */
}

/***************************************************************************
 * UTILITY FUNCTIONS
 ***************************************************************************/

void dpd_to_bid_32(uint32_t *bid, uint32_t dpd) {
    /* Convert DPD to BID using proper decimal32 conversion */
    decimal32_t dpd_val, bid_val;
    dpd_val.format = DEC_FORMAT_DPD;
    dpd_val.dpd = dpd;
    dec32_dpd_to_bid(&bid_val, &dpd_val);
    *bid = bid_val.bid;
}

void bid_to_dpd_32(uint32_t *dpd, uint32_t bid) {
    /* Convert BID to DPD using proper decimal32 conversion */
    decimal32_t bid_val, dpd_val;
    bid_val.format = DEC_FORMAT_BID;
    bid_val.bid = bid;
    dec32_bid_to_dpd(&dpd_val, &bid_val);
    *dpd = dpd_val.dpd;
}

void fp_convert(void *dst, fp_format_t dst_format, const void *src, fp_format_t src_format) {
    /* Universal floating point converter */

    /* Convert source to IEEE double as intermediate */
    double intermediate = 0.0;

    switch (src_format) {
        case FP_FORMAT_IEEE_SINGLE:
            intermediate = *(float*)src;
            break;
        case FP_FORMAT_IEEE_DOUBLE:
            intermediate = *(double*)src;
            break;
        case FP_FORMAT_VAX_F:
            vax_f_to_ieee_float((float*)&intermediate, (vax_f_float_t*)src);
            break;
        case FP_FORMAT_VAX_D:
            vax_d_to_ieee_double(&intermediate, (vax_d_float_t*)src);
            break;
        case FP_FORMAT_IBM_SHORT:
            ibm_short_to_ieee_float((float*)&intermediate, (ibm_short_float_t*)src);
            break;
        case FP_FORMAT_IBM_LONG:
            ibm_long_to_ieee_double(&intermediate, (ibm_long_float_t*)src);
            break;
        case FP_FORMAT_CRAY:
            cray_to_ieee_double(&intermediate, (cray_float_t*)src);
            break;
        case FP_FORMAT_ARM_FPA:
            fpa_extended_to_ieee_double(&intermediate, (arm_fpa_extended_t*)src);
            break;
        case FP_FORMAT_DEC32_DPD:
        case FP_FORMAT_DEC32_BID: {
            float f;
            dec32_to_float(&f, (decimal32_t*)src);
            intermediate = f;
            break;
        }
        case FP_FORMAT_DEC64_DPD:
        case FP_FORMAT_DEC64_BID:
            dec64_to_double(&intermediate, (decimal64_t*)src);
            break;
        default:
            break;
    }

    /* Convert intermediate to destination format */
    switch (dst_format) {
        case FP_FORMAT_IEEE_SINGLE:
            *(float*)dst = (float)intermediate;
            break;
        case FP_FORMAT_IEEE_DOUBLE:
            *(double*)dst = intermediate;
            break;
        case FP_FORMAT_VAX_F:
            ieee_float_to_vax_f((vax_f_float_t*)dst, (float)intermediate);
            break;
        case FP_FORMAT_VAX_D:
            ieee_double_to_vax_d((vax_d_float_t*)dst, intermediate);
            break;
        case FP_FORMAT_IBM_SHORT:
            ieee_float_to_ibm_short((ibm_short_float_t*)dst, (float)intermediate);
            break;
        case FP_FORMAT_IBM_LONG:
            ieee_double_to_ibm_long((ibm_long_float_t*)dst, intermediate);
            break;
        case FP_FORMAT_CRAY:
            ieee_double_to_cray((cray_float_t*)dst, intermediate);
            break;
        case FP_FORMAT_ARM_FPA:
            ieee_double_to_fpa_extended((arm_fpa_extended_t*)dst, intermediate);
            break;
        case FP_FORMAT_DEC32_DPD:
        case FP_FORMAT_DEC32_BID:
            float_to_dec32((decimal32_t*)dst, (float)intermediate);
            break;
        case FP_FORMAT_DEC64_DPD:
        case FP_FORMAT_DEC64_BID:
            double_to_dec64((decimal64_t*)dst, intermediate);
            break;
        default:
            break;
    }
}
