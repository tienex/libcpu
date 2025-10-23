/*
 * libcpu Backend Emulation - Extended Type System
 *
 * Comprehensive type system supporting all modern CPU data types:
 * - Extended FP: fp8, fp16, bfloat16, fp32, fp64, fp80, fp128, double-double
 * - Vectors: Variable width (64-bit to 2048-bit), all element types
 * - Matrices: Tile-based matrix types for AMX/SME
 */

#ifndef __LIBCPU_BACKEND_EMULATION_TYPES_H__
#define __LIBCPU_BACKEND_EMULATION_TYPES_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************
 * Extended Floating Point Types
 ***************************************************************************/

/* 8-bit floating point (E4M3 and E5M2 variants) */
typedef uint8_t fp8_e4m3_t;  /* 1 sign, 4 exponent, 3 mantissa */
typedef uint8_t fp8_e5m2_t;  /* 1 sign, 5 exponent, 2 mantissa */

/* 16-bit floating point */
typedef uint16_t fp16_t;      /* IEEE 754 half precision */
typedef uint16_t bfloat16_t;  /* Brain float (1 sign, 8 exp, 7 mantissa) */

/* Standard types */
typedef float fp32_t;
typedef double fp64_t;

/* Extended precision (x87) */
typedef struct {
	uint64_t mantissa;
	uint16_t exponent;  /* Includes sign bit */
} fp80_t;

/* Quad precision */
typedef struct {
	uint64_t low;
	uint64_t high;
} fp128_t;

/* PowerPC double-double (two fp64) */
typedef struct {
	double high;
	double low;
} double_double_t;

/***************************************************************************
 * Vector Types - Semantic, not ISA-specific
 ***************************************************************************/

/* Vector element type */
typedef enum {
	VEC_ELEM_I8,
	VEC_ELEM_I16,
	VEC_ELEM_I32,
	VEC_ELEM_I64,
	VEC_ELEM_I128,
	VEC_ELEM_U8,
	VEC_ELEM_U16,
	VEC_ELEM_U32,
	VEC_ELEM_U64,
	VEC_ELEM_U128,
	VEC_ELEM_FP8_E4M3,
	VEC_ELEM_FP8_E5M2,
	VEC_ELEM_FP16,
	VEC_ELEM_BF16,
	VEC_ELEM_FP32,
	VEC_ELEM_FP64,
	VEC_ELEM_FP80,
	VEC_ELEM_FP128
} vector_element_type_t;

/* Vector descriptor - describes any vector type */
typedef struct {
	vector_element_type_t element_type;
	uint32_t num_elements;        /* Number of elements */
	uint32_t total_bits;          /* Total vector width in bits */
	int is_scalable;              /* For SVE/RVV - runtime-determined length */
	uint32_t scalable_factor;     /* Multiply by vlen for actual size */
} vector_type_desc_t;

/* Generic vector data container */
typedef struct {
	vector_type_desc_t desc;
	void *data;                   /* Actual vector data */
	size_t data_size;             /* Size in bytes */
} vector_t;

/* Common vector sizes (in bits) */
#define VEC_64BIT    64    /* MMX */
#define VEC_128BIT   128   /* SSE, NEON, VMX */
#define VEC_256BIT   256   /* AVX, AVX2 */
#define VEC_512BIT   512   /* AVX-512, SVE (fixed), Loongson LSX/LASX */
#define VEC_1024BIT  1024  /* SVE (extended) */
#define VEC_2048BIT  2048  /* SVE (maximum) */

/***************************************************************************
 * Matrix Types - For AMX, SME, etc.
 ***************************************************************************/

/* Matrix element type */
typedef enum {
	MAT_ELEM_I8,
	MAT_ELEM_I16,
	MAT_ELEM_I32,
	MAT_ELEM_I64,
	MAT_ELEM_U8,
	MAT_ELEM_U16,
	MAT_ELEM_U32,
	MAT_ELEM_U64,
	MAT_ELEM_FP16,
	MAT_ELEM_BF16,
	MAT_ELEM_FP32,
	MAT_ELEM_FP64
} matrix_element_type_t;

/* Matrix tile descriptor */
typedef struct {
	matrix_element_type_t element_type;
	uint32_t rows;
	uint32_t cols;
	uint32_t tile_id;             /* For AMX tile register number */
	int is_accumulator;           /* INT32 accumulator for INT8 ops */
} matrix_tile_desc_t;

/* Matrix tile container */
typedef struct {
	matrix_tile_desc_t desc;
	void *data;                   /* Row-major tile data */
	size_t data_size;
} matrix_tile_t;

/* AMX tile sizes */
#define AMX_TILE_MAX_ROWS  16
#define AMX_TILE_MAX_COLS  64  /* In bytes */

/* SME (Scalable Matrix Extension) */
#define SME_TILE_MAX  16   /* 16 tile registers */

/***************************************************************************
 * Predicate/Mask Types - For SVE, AVX-512, RVV
 ***************************************************************************/

typedef struct {
	uint32_t num_bits;            /* Number of predicate bits */
	int is_scalable;              /* For SVE/RVV */
	uint8_t *mask_data;           /* Bit mask data */
	size_t mask_bytes;
} predicate_t;

/***************************************************************************
 * Type Conversion Operations
 ***************************************************************************/

/* FP8 conversions */
float fp8_e4m3_to_fp32(fp8_e4m3_t x);
fp8_e4m3_t fp32_to_fp8_e4m3(float x);
float fp8_e5m2_to_fp32(fp8_e5m2_t x);
fp8_e5m2_t fp32_to_fp8_e5m2(float x);

/* FP16 conversions */
float fp16_to_fp32(fp16_t x);
fp16_t fp32_to_fp16(float x);
double fp16_to_fp64(fp16_t x);
fp16_t fp64_to_fp16(double x);

/* BFloat16 conversions */
float bf16_to_fp32(bfloat16_t x);
bfloat16_t fp32_to_bf16(float x);

/* FP80 conversions */
double fp80_to_fp64(const fp80_t *x);
void fp64_to_fp80(double x, fp80_t *out);

/* FP128 conversions */
double fp128_to_fp64(const fp128_t *x);
void fp64_to_fp128(double x, fp128_t *out);

/* Double-double conversions */
double dd_to_fp64(const double_double_t *x);
void fp64_to_dd(double x, double_double_t *out);

/* Cross-conversions */
fp16_t bf16_to_fp16(bfloat16_t x);
bfloat16_t fp16_to_bf16(fp16_t x);

/***************************************************************************
 * Vector Type Construction
 ***************************************************************************/

/* Create vector type descriptor */
vector_type_desc_t vector_type_create(vector_element_type_t elem_type, uint32_t num_elems);
vector_type_desc_t vector_type_create_scalable(vector_element_type_t elem_type, uint32_t factor);

/* Allocate vector storage */
vector_t* vector_alloc(const vector_type_desc_t *desc);
void vector_free(vector_t *vec);

/* Common vector type constructors */
vector_type_desc_t vec_i8x16(void);    /* 16x i8 = 128-bit */
vector_type_desc_t vec_i16x8(void);    /* 8x i16 = 128-bit */
vector_type_desc_t vec_i32x4(void);    /* 4x i32 = 128-bit */
vector_type_desc_t vec_i64x2(void);    /* 2x i64 = 128-bit */
vector_type_desc_t vec_f32x4(void);    /* 4x f32 = 128-bit */
vector_type_desc_t vec_f64x2(void);    /* 2x f64 = 128-bit */

vector_type_desc_t vec_i32x8(void);    /* 8x i32 = 256-bit (AVX2) */
vector_type_desc_t vec_f32x8(void);    /* 8x f32 = 256-bit (AVX) */
vector_type_desc_t vec_f64x4(void);    /* 4x f64 = 256-bit (AVX) */

vector_type_desc_t vec_i32x16(void);   /* 16x i32 = 512-bit (AVX-512) */
vector_type_desc_t vec_f32x16(void);   /* 16x f32 = 512-bit (AVX-512) */
vector_type_desc_t vec_f64x8(void);    /* 8x f64 = 512-bit (AVX-512) */

/***************************************************************************
 * Matrix Type Construction
 ***************************************************************************/

/* Create matrix tile descriptor */
matrix_tile_desc_t matrix_tile_create(matrix_element_type_t elem_type,
                                      uint32_t rows, uint32_t cols);

/* Allocate matrix tile storage */
matrix_tile_t* matrix_tile_alloc(const matrix_tile_desc_t *desc);
void matrix_tile_free(matrix_tile_t *tile);

/* AMX tile constructors */
matrix_tile_desc_t amx_tile_i8(uint32_t rows, uint32_t cols);    /* INT8 tile */
matrix_tile_desc_t amx_tile_i32_acc(uint32_t rows, uint32_t cols); /* INT32 accumulator */
matrix_tile_desc_t amx_tile_fp16(uint32_t rows, uint32_t cols);  /* FP16 tile */
matrix_tile_desc_t amx_tile_bf16(uint32_t rows, uint32_t cols);  /* BF16 tile */
matrix_tile_desc_t amx_tile_fp32_acc(uint32_t rows, uint32_t cols); /* FP32 accumulator */

/***************************************************************************
 * Predicate Type Construction
 ***************************************************************************/

/* Create predicate/mask */
predicate_t* predicate_alloc(uint32_t num_bits, int is_scalable);
void predicate_free(predicate_t *pred);

/* Set/get predicate bits */
void predicate_set_bit(predicate_t *pred, uint32_t index, int value);
int predicate_get_bit(const predicate_t *pred, uint32_t index);

/* Predicate operations */
void predicate_and(predicate_t *dst, const predicate_t *a, const predicate_t *b);
void predicate_or(predicate_t *dst, const predicate_t *a, const predicate_t *b);
void predicate_not(predicate_t *dst, const predicate_t *src);

/***************************************************************************
 * Element Size Helpers
 ***************************************************************************/

/* Get size in bytes of element type */
size_t vector_element_size(vector_element_type_t type);
size_t matrix_element_size(matrix_element_type_t type);

/* Get element type name */
const char* vector_element_name(vector_element_type_t type);
const char* matrix_element_name(matrix_element_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* __LIBCPU_BACKEND_EMULATION_TYPES_H__ */
