/*
 * libcpu Backend Emulation - Matrix Operations API
 *
 * Semantic matrix operations for tile-based accelerators:
 * - Intel AMX (Advanced Matrix Extensions)
 * - ARM SME (Scalable Matrix Extension)
 *
 * Provides software emulation without reimplementing ISAs.
 */

#ifndef __LIBCPU_BACKEND_EMULATION_MATRIX_H__
#define __LIBCPU_BACKEND_EMULATION_MATRIX_H__

#include "backend_emulation_types.h"
#include "backend_emulation_vector.h"

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************
 * Matrix Configuration
 ***************************************************************************/

/* Tile configuration for AMX */
typedef struct {
	uint8_t palette_id;
	uint8_t start_row;
	uint16_t colsb[8];  /* Columns in bytes per tile */
	uint8_t rows[8];    /* Rows per tile */
} amx_tile_config_t;

/* Configure AMX tiles */
void mat_amx_config(const amx_tile_config_t *config);
void mat_amx_release(void);

/* SME configuration */
void mat_sme_start(void);
void mat_sme_stop(void);

/***************************************************************************
 * Matrix Load/Store
 ***************************************************************************/

/* Load matrix tile from memory */
void mat_load(matrix_tile_t *tile, const void *ptr, size_t stride);

/* Store matrix tile to memory */
void mat_store(const matrix_tile_t *tile, void *ptr, size_t stride);

/* Load row into tile */
void mat_load_row(matrix_tile_t *tile, uint32_t row, const void *ptr);

/* Store row from tile */
void mat_store_row(const matrix_tile_t *tile, uint32_t row, void *ptr);

/* Zero tile */
void mat_zero(matrix_tile_t *tile);

/***************************************************************************
 * Matrix Multiply Operations
 ***************************************************************************/

/* General matrix multiply: C = A * B (+ C for accumulate variants) */
void mat_mul(matrix_tile_t *c, const matrix_tile_t *a, const matrix_tile_t *b);

/* Matrix multiply-accumulate: C += A * B */
void mat_mul_acc(matrix_tile_t *c, const matrix_tile_t *a, const matrix_tile_t *b);

/***************************************************************************
 * AMX INT8 Operations
 ***************************************************************************/

/* Dot product of INT8 -> INT32 accumulator
 * Computes 4 int8 dot products accumulating into int32
 */
void mat_dpbssd(matrix_tile_t *dst, const matrix_tile_t *a, const matrix_tile_t *b);

/* Signed-unsigned dot product: int8 * uint8 -> int32 */
void mat_dpbsud(matrix_tile_t *dst, const matrix_tile_t *a, const matrix_tile_t *b);

/* Unsigned-signed dot product: uint8 * int8 -> int32 */
void mat_dpbusd(matrix_tile_t *dst, const matrix_tile_t *a, const matrix_tile_t *b);

/* Unsigned dot product: uint8 * uint8 -> int32 */
void mat_dpbuud(matrix_tile_t *dst, const matrix_tile_t *a, const matrix_tile_t *b);

/***************************************************************************
 * AMX BF16 Operations
 ***************************************************************************/

/* BF16 dot product -> FP32 accumulator
 * Each BF16 element pair is multiplied and accumulated into FP32
 */
void mat_dpbf16ps(matrix_tile_t *dst, const matrix_tile_t *a, const matrix_tile_t *b);

/***************************************************************************
 * AMX FP16 Operations
 ***************************************************************************/

/* FP16 dot product -> FP32 accumulator */
void mat_dpfp16ps(matrix_tile_t *dst, const matrix_tile_t *a, const matrix_tile_t *b);

/***************************************************************************
 * ARM SME Operations
 ***************************************************************************/

/* Outer product and accumulate (MOPA) */
void mat_sme_mopa_i8(matrix_tile_t *za, const vector_t *zn, const vector_t *zm);
void mat_sme_mopa_i16(matrix_tile_t *za, const vector_t *zn, const vector_t *zm);
void mat_sme_mopa_i32(matrix_tile_t *za, const vector_t *zn, const vector_t *zm);
void mat_sme_mopa_i64(matrix_tile_t *za, const vector_t *zn, const vector_t *zm);
void mat_sme_mopa_f16(matrix_tile_t *za, const vector_t *zn, const vector_t *zm);
void mat_sme_mopa_bf16(matrix_tile_t *za, const vector_t *zn, const vector_t *zm);
void mat_sme_mopa_f32(matrix_tile_t *za, const vector_t *zn, const vector_t *zm);
void mat_sme_mopa_f64(matrix_tile_t *za, const vector_t *zn, const vector_t *zm);

/* Outer product and subtract (MOPS) */
void mat_sme_mops_i8(matrix_tile_t *za, const vector_t *zn, const vector_t *zm);
void mat_sme_mops_f32(matrix_tile_t *za, const vector_t *zn, const vector_t *zm);

/* Load/store tile slice (row/column) */
void mat_sme_ldr(matrix_tile_t *za, const void *ptr);
void mat_sme_str(const matrix_tile_t *za, void *ptr);

/* Tile slice operations */
void mat_sme_read_horiz(vector_t *dst, const matrix_tile_t *za, uint32_t slice);
void mat_sme_read_vert(vector_t *dst, const matrix_tile_t *za, uint32_t slice);
void mat_sme_write_horiz(matrix_tile_t *za, uint32_t slice, const vector_t *src);
void mat_sme_write_vert(matrix_tile_t *za, uint32_t slice, const vector_t *src);

/* Insert/extract vectors */
void mat_sme_insert_row(matrix_tile_t *za, uint32_t row, const vector_t *vec);
void mat_sme_insert_col(matrix_tile_t *za, uint32_t col, const vector_t *vec);
void mat_sme_extract_row(vector_t *vec, const matrix_tile_t *za, uint32_t row);
void mat_sme_extract_col(vector_t *vec, const matrix_tile_t *za, uint32_t col);

/* Zero tile array */
void mat_sme_zero(matrix_tile_t *za);

/***************************************************************************
 * Matrix Transpose
 ***************************************************************************/

void mat_transpose(matrix_tile_t *dst, const matrix_tile_t *src);

/***************************************************************************
 * Matrix Element Operations
 ***************************************************************************/

/* Get/set matrix element */
void mat_get_elem(void *result, const matrix_tile_t *tile, uint32_t row, uint32_t col);
void mat_set_elem(matrix_tile_t *tile, uint32_t row, uint32_t col, const void *value);

/***************************************************************************
 * Matrix Utility Functions
 ***************************************************************************/

/* Initialize tile */
void mat_init(matrix_tile_t *tile, const void *scalar);  /* Fill with value */
void mat_identity(matrix_tile_t *tile);  /* Identity matrix */

/* Copy tile */
void mat_copy(matrix_tile_t *dst, const matrix_tile_t *src);

/* Add/subtract tiles (element-wise) */
void mat_add(matrix_tile_t *dst, const matrix_tile_t *a, const matrix_tile_t *b);
void mat_sub(matrix_tile_t *dst, const matrix_tile_t *a, const matrix_tile_t *b);

/* Scale tile */
void mat_scale(matrix_tile_t *dst, const matrix_tile_t *src, const void *scalar);

/* Compare tiles */
int mat_equal(const matrix_tile_t *a, const matrix_tile_t *b);

/***************************************************************************
 * Debugging/Utility
 ***************************************************************************/

/* Print matrix tile (for debugging) */
void mat_print(const matrix_tile_t *tile);

/* Get tile info */
uint32_t mat_get_rows(const matrix_tile_t *tile);
uint32_t mat_get_cols(const matrix_tile_t *tile);
size_t mat_get_size_bytes(const matrix_tile_t *tile);

#ifdef __cplusplus
}
#endif

#endif /* __LIBCPU_BACKEND_EMULATION_MATRIX_H__ */
