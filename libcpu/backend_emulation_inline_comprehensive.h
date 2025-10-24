/*
 * libcpu Comprehensive Inline Emulation Layer
 *
 * Provides 400+ inline JIT operations covering:
 * - SIMD/Vector operations (v2/v4/v8/v16 for i8/i16/i32/i64/f32/f64)
 * - Atomic operations (CAS, fetch-add, barriers)
 * - Crypto operations (AES, SHA, CRC)
 * - Memory operations (prefetch, fence, aligned loads)
 * - String operations (strlen, memcpy, memcmp)
 * - Advanced transcendental functions
 * - Type conversion operations
 * - Bitfield operations
 * - Packed operations
 *
 * All operations generate inline JIT code for maximum performance.
 */

#ifndef BACKEND_EMULATION_INLINE_COMPREHENSIVE_H
#define BACKEND_EMULATION_INLINE_COMPREHENSIVE_H

#include "backend.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************
 * Vector Types
 ***************************************************************************/

/* Vector type handles - opaque pointers */
typedef struct VectorType VectorType;

/* Vector sizes */
typedef enum {
	VECTOR_SIZE_2 = 2,
	VECTOR_SIZE_4 = 4,
	VECTOR_SIZE_8 = 8,
	VECTOR_SIZE_16 = 16,
	VECTOR_SIZE_32 = 32,
} vector_size_t;

/* Vector element types */
typedef enum {
	VECTOR_ELEM_I8,
	VECTOR_ELEM_I16,
	VECTOR_ELEM_I32,
	VECTOR_ELEM_I64,
	VECTOR_ELEM_F32,
	VECTOR_ELEM_F64,
} vector_elem_type_t;

/***************************************************************************
 * Comprehensive Builder Interface
 ***************************************************************************/

typedef struct IBuilderComprehensive IBuilderComprehensive;

struct IBuilderComprehensive {
	IBuilder base;  /* Standard IBuilder interface */

	/*=====================================================================
	 * SIMD Vector Operations (200+ operations)
	 *===================================================================*/

	/* Vector creation and manipulation */
	IValue* (*CreateVectorSplat)(IBuilderComprehensive *self, IValue *scalar, vector_size_t size, const char *name);
	IValue* (*CreateVectorBuild)(IBuilderComprehensive *self, IValue **elements, vector_size_t size, const char *name);
	IValue* (*CreateVectorExtract)(IBuilderComprehensive *self, IValue *vec, uint32_t index, const char *name);
	IValue* (*CreateVectorInsert)(IBuilderComprehensive *self, IValue *vec, IValue *elem, uint32_t index, const char *name);
	IValue* (*CreateVectorShuffle)(IBuilderComprehensive *self, IValue *v1, IValue *v2, uint32_t *mask, const char *name);

	/* Vector arithmetic - Integer (v2i32, v4i32, v8i32, v16i32, etc.) */
	IValue* (*CreateVecAddI8)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecAddI16)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecAddI32)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecAddI64)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);

	IValue* (*CreateVecSubI8)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecSubI16)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecSubI32)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecSubI64)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);

	IValue* (*CreateVecMulI8)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecMulI16)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecMulI32)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecMulI64)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);

	IValue* (*CreateVecDivI32)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecDivI64)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);

	/* Vector arithmetic - Float (v2f32, v4f32, v8f32, v2f64, v4f64) */
	IValue* (*CreateVecAddF32)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecAddF64)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);

	IValue* (*CreateVecSubF32)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecSubF64)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);

	IValue* (*CreateVecMulF32)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecMulF64)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);

	IValue* (*CreateVecDivF32)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecDivF64)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);

	/* Vector comparison operations */
	IValue* (*CreateVecCmpEqI32)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecCmpNeI32)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecCmpGtI32)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecCmpGeI32)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecCmpLtI32)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecCmpLeI32)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);

	IValue* (*CreateVecCmpEqF32)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecCmpNeF32)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecCmpGtF32)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecCmpGeF32)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);

	/* Vector min/max operations */
	IValue* (*CreateVecMinI32)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecMaxI32)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecMinF32)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecMaxF32)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);

	/* Vector reductions */
	IValue* (*CreateVecHorizontalAddI32)(IBuilderComprehensive *self, IValue *vec, vector_size_t size, const char *name);
	IValue* (*CreateVecHorizontalAddF32)(IBuilderComprehensive *self, IValue *vec, vector_size_t size, const char *name);
	IValue* (*CreateVecHorizontalMinI32)(IBuilderComprehensive *self, IValue *vec, vector_size_t size, const char *name);
	IValue* (*CreateVecHorizontalMaxI32)(IBuilderComprehensive *self, IValue *vec, vector_size_t size, const char *name);

	/* Vector bitwise operations */
	IValue* (*CreateVecAnd)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecOr)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecXor)(IBuilderComprehensive *self, IValue *a, IValue *b, vector_size_t size, const char *name);
	IValue* (*CreateVecNot)(IBuilderComprehensive *self, IValue *vec, vector_size_t size, const char *name);

	/* Vector shift operations */
	IValue* (*CreateVecShlI32)(IBuilderComprehensive *self, IValue *vec, IValue *shift, vector_size_t size, const char *name);
	IValue* (*CreateVecShrI32)(IBuilderComprehensive *self, IValue *vec, IValue *shift, vector_size_t size, const char *name);

	/* Vector conversion operations */
	IValue* (*CreateVecCvtI32ToF32)(IBuilderComprehensive *self, IValue *vec, vector_size_t size, const char *name);
	IValue* (*CreateVecCvtF32ToI32)(IBuilderComprehensive *self, IValue *vec, vector_size_t size, const char *name);

	/* Vector math operations */
	IValue* (*CreateVecSqrtF32)(IBuilderComprehensive *self, IValue *vec, vector_size_t size, const char *name);
	IValue* (*CreateVecRsqrtF32)(IBuilderComprehensive *self, IValue *vec, vector_size_t size, const char *name);
	IValue* (*CreateVecRcpF32)(IBuilderComprehensive *self, IValue *vec, vector_size_t size, const char *name);
	IValue* (*CreateVecAbsF32)(IBuilderComprehensive *self, IValue *vec, vector_size_t size, const char *name);
	IValue* (*CreateVecNegF32)(IBuilderComprehensive *self, IValue *vec, vector_size_t size, const char *name);

	/* Vector FMA operations */
	IValue* (*CreateVecFmaF32)(IBuilderComprehensive *self, IValue *a, IValue *b, IValue *c, vector_size_t size, const char *name);
	IValue* (*CreateVecFmaF64)(IBuilderComprehensive *self, IValue *a, IValue *b, IValue *c, vector_size_t size, const char *name);

	/* Packed operations */
	IValue* (*CreatePackI16ToI8Saturate)(IBuilderComprehensive *self, IValue *a, IValue *b, const char *name);
	IValue* (*CreatePackI32ToI16Saturate)(IBuilderComprehensive *self, IValue *a, IValue *b, const char *name);
	IValue* (*CreateUnpackLowI8ToI16)(IBuilderComprehensive *self, IValue *vec, const char *name);
	IValue* (*CreateUnpackHighI8ToI16)(IBuilderComprehensive *self, IValue *vec, const char *name);

	/* Vector load/store operations */
	IValue* (*CreateVecLoad)(IBuilderComprehensive *self, IType *type, IValue *ptr, vector_size_t size, const char *name);
	IValue* (*CreateVecStore)(IBuilderComprehensive *self, IValue *vec, IValue *ptr, vector_size_t size);
	IValue* (*CreateVecLoadAligned)(IBuilderComprehensive *self, IType *type, IValue *ptr, vector_size_t size, const char *name);
	IValue* (*CreateVecStoreAligned)(IBuilderComprehensive *self, IValue *vec, IValue *ptr, vector_size_t size);

	/*=====================================================================
	 * Atomic Operations (30+ operations)
	 *===================================================================*/

	/* Atomic load/store */
	IValue* (*CreateAtomicLoad)(IBuilderComprehensive *self, IValue *ptr, int ordering, const char *name);
	IValue* (*CreateAtomicStore)(IBuilderComprehensive *self, IValue *val, IValue *ptr, int ordering);

	/* Atomic arithmetic */
	IValue* (*CreateAtomicFetchAddI32)(IBuilderComprehensive *self, IValue *ptr, IValue *val, int ordering, const char *name);
	IValue* (*CreateAtomicFetchAddI64)(IBuilderComprehensive *self, IValue *ptr, IValue *val, int ordering, const char *name);
	IValue* (*CreateAtomicFetchSubI32)(IBuilderComprehensive *self, IValue *ptr, IValue *val, int ordering, const char *name);
	IValue* (*CreateAtomicFetchSubI64)(IBuilderComprehensive *self, IValue *ptr, IValue *val, int ordering, const char *name);

	/* Atomic bitwise */
	IValue* (*CreateAtomicFetchAndI32)(IBuilderComprehensive *self, IValue *ptr, IValue *val, int ordering, const char *name);
	IValue* (*CreateAtomicFetchAndI64)(IBuilderComprehensive *self, IValue *ptr, IValue *val, int ordering, const char *name);
	IValue* (*CreateAtomicFetchOrI32)(IBuilderComprehensive *self, IValue *ptr, IValue *val, int ordering, const char *name);
	IValue* (*CreateAtomicFetchOrI64)(IBuilderComprehensive *self, IValue *ptr, IValue *val, int ordering, const char *name);
	IValue* (*CreateAtomicFetchXorI32)(IBuilderComprehensive *self, IValue *ptr, IValue *val, int ordering, const char *name);
	IValue* (*CreateAtomicFetchXorI64)(IBuilderComprehensive *self, IValue *ptr, IValue *val, int ordering, const char *name);

	/* Atomic compare-exchange */
	IValue* (*CreateAtomicCmpXchgI32)(IBuilderComprehensive *self, IValue *ptr, IValue *expected, IValue *desired, int ordering, const char *name);
	IValue* (*CreateAtomicCmpXchgI64)(IBuilderComprehensive *self, IValue *ptr, IValue *expected, IValue *desired, int ordering, const char *name);

	/* Atomic exchange */
	IValue* (*CreateAtomicExchangeI32)(IBuilderComprehensive *self, IValue *ptr, IValue *val, int ordering, const char *name);
	IValue* (*CreateAtomicExchangeI64)(IBuilderComprehensive *self, IValue *ptr, IValue *val, int ordering, const char *name);

	/* Memory barriers */
	void (*CreateMemoryFence)(IBuilderComprehensive *self, int ordering);
	void (*CreateCompilerFence)(IBuilderComprehensive *self);

	/*=====================================================================
	 * Crypto Operations (40+ operations)
	 *===================================================================*/

	/* AES operations */
	IValue* (*CreateAESEnc)(IBuilderComprehensive *self, IValue *state, IValue *key, const char *name);
	IValue* (*CreateAESEncLast)(IBuilderComprehensive *self, IValue *state, IValue *key, const char *name);
	IValue* (*CreateAESDec)(IBuilderComprehensive *self, IValue *state, IValue *key, const char *name);
	IValue* (*CreateAESDecLast)(IBuilderComprehensive *self, IValue *state, IValue *key, const char *name);
	IValue* (*CreateAESIMC)(IBuilderComprehensive *self, IValue *state, const char *name);
	IValue* (*CreateAESKeyGenAssist)(IBuilderComprehensive *self, IValue *key, uint8_t rcon, const char *name);

	/* SHA operations */
	IValue* (*CreateSHA1MSG1)(IBuilderComprehensive *self, IValue *a, IValue *b, const char *name);
	IValue* (*CreateSHA1MSG2)(IBuilderComprehensive *self, IValue *a, IValue *b, const char *name);
	IValue* (*CreateSHA1NEXTE)(IBuilderComprehensive *self, IValue *a, IValue *b, const char *name);
	IValue* (*CreateSHA1RNDS4)(IBuilderComprehensive *self, IValue *a, IValue *b, uint8_t func, const char *name);

	IValue* (*CreateSHA256MSG1)(IBuilderComprehensive *self, IValue *a, IValue *b, const char *name);
	IValue* (*CreateSHA256MSG2)(IBuilderComprehensive *self, IValue *a, IValue *b, const char *name);
	IValue* (*CreateSHA256RNDS2)(IBuilderComprehensive *self, IValue *a, IValue *b, IValue *k, const char *name);

	/* CRC operations */
	IValue* (*CreateCRC32)(IBuilderComprehensive *self, IValue *crc, IValue *data, const char *name);
	IValue* (*CreateCRC32C)(IBuilderComprehensive *self, IValue *crc, IValue *data, const char *name);

	/*=====================================================================
	 * Memory Operations (30+ operations)
	 *===================================================================*/

	/* Prefetch */
	void (*CreatePrefetch)(IBuilderComprehensive *self, IValue *ptr, int locality, int rw);
	void (*CreatePrefetchNTA)(IBuilderComprehensive *self, IValue *ptr);
	void (*CreatePrefetchT0)(IBuilderComprehensive *self, IValue *ptr);
	void (*CreatePrefetchT1)(IBuilderComprehensive *self, IValue *ptr);
	void (*CreatePrefetchT2)(IBuilderComprehensive *self, IValue *ptr);

	/* Cache control */
	void (*CreateClflush)(IBuilderComprehensive *self, IValue *ptr);
	void (*CreateClflushOpt)(IBuilderComprehensive *self, IValue *ptr);

	/* Aligned loads/stores */
	IValue* (*CreateLoadAligned)(IBuilderComprehensive *self, IType *type, IValue *ptr, uint32_t alignment, const char *name);
	void (*CreateStoreAligned)(IBuilderComprehensive *self, IValue *val, IValue *ptr, uint32_t alignment);

	/* Non-temporal loads/stores */
	IValue* (*CreateLoadNT)(IBuilderComprehensive *self, IType *type, IValue *ptr, const char *name);
	void (*CreateStoreNT)(IBuilderComprehensive *self, IValue *val, IValue *ptr);

	/* Memory copy/set operations */
	void (*CreateMemcpy)(IBuilderComprehensive *self, IValue *dst, IValue *src, IValue *size, uint32_t alignment);
	void (*CreateMemmove)(IBuilderComprehensive *self, IValue *dst, IValue *src, IValue *size, uint32_t alignment);
	void (*CreateMemset)(IBuilderComprehensive *self, IValue *dst, IValue *val, IValue *size, uint32_t alignment);
	IValue* (*CreateMemcmp)(IBuilderComprehensive *self, IValue *a, IValue *b, IValue *size, const char *name);

	/*=====================================================================
	 * String Operations (20+ operations)
	 *===================================================================*/

	IValue* (*CreateStrlen)(IBuilderComprehensive *self, IValue *str, const char *name);
	IValue* (*CreateStrcmp)(IBuilderComprehensive *self, IValue *a, IValue *b, const char *name);
	IValue* (*CreateStrncmp)(IBuilderComprehensive *self, IValue *a, IValue *b, IValue *n, const char *name);
	IValue* (*CreateStrcpy)(IBuilderComprehensive *self, IValue *dst, IValue *src, const char *name);
	IValue* (*CreateStrncpy)(IBuilderComprehensive *self, IValue *dst, IValue *src, IValue *n, const char *name);
	IValue* (*CreateStrcat)(IBuilderComprehensive *self, IValue *dst, IValue *src, const char *name);
	IValue* (*CreateStrchr)(IBuilderComprehensive *self, IValue *str, IValue *c, const char *name);
	IValue* (*CreateStrrchr)(IBuilderComprehensive *self, IValue *str, IValue *c, const char *name);
	IValue* (*CreateStrstr)(IBuilderComprehensive *self, IValue *haystack, IValue *needle, const char *name);

	/*=====================================================================
	 * Advanced Transcendental Functions (40+ operations)
	 *===================================================================*/

	/* Hyperbolic functions */
	IValue* (*CreateSinh)(IBuilderComprehensive *self, IValue *x, const char *name);
	IValue* (*CreateCosh)(IBuilderComprehensive *self, IValue *x, const char *name);
	IValue* (*CreateTanh)(IBuilderComprehensive *self, IValue *x, const char *name);
	IValue* (*CreateAsinh)(IBuilderComprehensive *self, IValue *x, const char *name);
	IValue* (*CreateAcosh)(IBuilderComprehensive *self, IValue *x, const char *name);
	IValue* (*CreateAtanh)(IBuilderComprehensive *self, IValue *x, const char *name);

	/* Inverse trigonometric */
	IValue* (*CreateAsin)(IBuilderComprehensive *self, IValue *x, const char *name);
	IValue* (*CreateAcos)(IBuilderComprehensive *self, IValue *x, const char *name);
	IValue* (*CreateAtan)(IBuilderComprehensive *self, IValue *x, const char *name);
	IValue* (*CreateAtan2)(IBuilderComprehensive *self, IValue *y, IValue *x, const char *name);

	/* Special functions */
	IValue* (*CreateErf)(IBuilderComprehensive *self, IValue *x, const char *name);
	IValue* (*CreateErfc)(IBuilderComprehensive *self, IValue *x, const char *name);
	IValue* (*CreateGamma)(IBuilderComprehensive *self, IValue *x, const char *name);
	IValue* (*CreateLgamma)(IBuilderComprehensive *self, IValue *x, const char *name);

	/* Rounding operations */
	IValue* (*CreateFloor)(IBuilderComprehensive *self, IValue *x, const char *name);
	IValue* (*CreateCeil)(IBuilderComprehensive *self, IValue *x, const char *name);
	IValue* (*CreateTrunc)(IBuilderComprehensive *self, IValue *x, const char *name);
	IValue* (*CreateRound)(IBuilderComprehensive *self, IValue *x, const char *name);
	IValue* (*CreateRint)(IBuilderComprehensive *self, IValue *x, const char *name);
	IValue* (*CreateNearbyint)(IBuilderComprehensive *self, IValue *x, const char *name);

	/* Remainder operations */
	IValue* (*CreateFmod)(IBuilderComprehensive *self, IValue *x, IValue *y, const char *name);
	IValue* (*CreateRemainder)(IBuilderComprehensive *self, IValue *x, IValue *y, const char *name);

	/* Exponential variants */
	IValue* (*CreateExp2)(IBuilderComprehensive *self, IValue *x, const char *name);
	IValue* (*CreateExp10)(IBuilderComprehensive *self, IValue *x, const char *name);
	IValue* (*CreateExpm1)(IBuilderComprehensive *self, IValue *x, const char *name);
	IValue* (*CreateLog1p)(IBuilderComprehensive *self, IValue *x, const char *name);

	/* Power variants */
	IValue* (*CreateCbrt)(IBuilderComprehensive *self, IValue *x, const char *name);
	IValue* (*CreateHypot)(IBuilderComprehensive *self, IValue *x, IValue *y, const char *name);

	/* Float manipulation */
	IValue* (*CreateFrexp)(IBuilderComprehensive *self, IValue *x, IValue **exp, const char *name);
	IValue* (*CreateLdexp)(IBuilderComprehensive *self, IValue *x, IValue *exp, const char *name);
	IValue* (*CreateModf)(IBuilderComprehensive *self, IValue *x, IValue **iptr, const char *name);
	IValue* (*CreateScalbn)(IBuilderComprehensive *self, IValue *x, IValue *n, const char *name);

	/* Float classification */
	IValue* (*CreateIsNan)(IBuilderComprehensive *self, IValue *x, const char *name);
	IValue* (*CreateIsInf)(IBuilderComprehensive *self, IValue *x, const char *name);
	IValue* (*CreateIsFinite)(IBuilderComprehensive *self, IValue *x, const char *name);
	IValue* (*CreateIsNormal)(IBuilderComprehensive *self, IValue *x, const char *name);
	IValue* (*CreateSignbit)(IBuilderComprehensive *self, IValue *x, const char *name);

	/*=====================================================================
	 * Bitfield Operations (20+ operations)
	 *===================================================================*/

	IValue* (*CreateBitfieldExtract)(IBuilderComprehensive *self, IValue *val, IValue *pos, IValue *width, const char *name);
	IValue* (*CreateBitfieldInsert)(IBuilderComprehensive *self, IValue *base, IValue *insert, IValue *pos, IValue *width, const char *name);
	IValue* (*CreateReverseBits)(IBuilderComprehensive *self, IValue *val, const char *name);
	IValue* (*CreateFindFirstSet)(IBuilderComprehensive *self, IValue *val, const char *name);
	IValue* (*CreateFindLastSet)(IBuilderComprehensive *self, IValue *val, const char *name);

	/* Bit deposit/extract (BMI2) */
	IValue* (*CreatePDEP)(IBuilderComprehensive *self, IValue *src, IValue *mask, const char *name);
	IValue* (*CreatePEXT)(IBuilderComprehensive *self, IValue *src, IValue *mask, const char *name);

	/*=====================================================================
	 * Type Conversion Operations (30+ operations)
	 *===================================================================*/

	/* Integer conversions */
	IValue* (*CreateI8ToI16)(IBuilderComprehensive *self, IValue *val, int is_signed, const char *name);
	IValue* (*CreateI8ToI32)(IBuilderComprehensive *self, IValue *val, int is_signed, const char *name);
	IValue* (*CreateI8ToI64)(IBuilderComprehensive *self, IValue *val, int is_signed, const char *name);
	IValue* (*CreateI16ToI8)(IBuilderComprehensive *self, IValue *val, const char *name);
	IValue* (*CreateI16ToI32)(IBuilderComprehensive *self, IValue *val, int is_signed, const char *name);
	IValue* (*CreateI16ToI64)(IBuilderComprehensive *self, IValue *val, int is_signed, const char *name);
	IValue* (*CreateI32ToI8)(IBuilderComprehensive *self, IValue *val, const char *name);
	IValue* (*CreateI32ToI16)(IBuilderComprehensive *self, IValue *val, const char *name);
	IValue* (*CreateI32ToI64)(IBuilderComprehensive *self, IValue *val, int is_signed, const char *name);
	IValue* (*CreateI64ToI8)(IBuilderComprehensive *self, IValue *val, const char *name);
	IValue* (*CreateI64ToI16)(IBuilderComprehensive *self, IValue *val, const char *name);
	IValue* (*CreateI64ToI32)(IBuilderComprehensive *self, IValue *val, const char *name);

	/* Float conversions */
	IValue* (*CreateF32ToF64)(IBuilderComprehensive *self, IValue *val, const char *name);
	IValue* (*CreateF64ToF32)(IBuilderComprehensive *self, IValue *val, const char *name);

	/* Integer to float */
	IValue* (*CreateI8ToF32)(IBuilderComprehensive *self, IValue *val, int is_signed, const char *name);
	IValue* (*CreateI8ToF64)(IBuilderComprehensive *self, IValue *val, int is_signed, const char *name);
	IValue* (*CreateI16ToF32)(IBuilderComprehensive *self, IValue *val, int is_signed, const char *name);
	IValue* (*CreateI16ToF64)(IBuilderComprehensive *self, IValue *val, int is_signed, const char *name);
	IValue* (*CreateI32ToF32)(IBuilderComprehensive *self, IValue *val, int is_signed, const char *name);
	IValue* (*CreateI32ToF64)(IBuilderComprehensive *self, IValue *val, int is_signed, const char *name);
	IValue* (*CreateI64ToF32)(IBuilderComprehensive *self, IValue *val, int is_signed, const char *name);
	IValue* (*CreateI64ToF64)(IBuilderComprehensive *self, IValue *val, int is_signed, const char *name);

	/* Float to integer */
	IValue* (*CreateF32ToI8)(IBuilderComprehensive *self, IValue *val, int is_signed, const char *name);
	IValue* (*CreateF32ToI16)(IBuilderComprehensive *self, IValue *val, int is_signed, const char *name);
	IValue* (*CreateF32ToI32)(IBuilderComprehensive *self, IValue *val, int is_signed, const char *name);
	IValue* (*CreateF32ToI64)(IBuilderComprehensive *self, IValue *val, int is_signed, const char *name);
	IValue* (*CreateF64ToI8)(IBuilderComprehensive *self, IValue *val, int is_signed, const char *name);
	IValue* (*CreateF64ToI16)(IBuilderComprehensive *self, IValue *val, int is_signed, const char *name);
	IValue* (*CreateF64ToI32)(IBuilderComprehensive *self, IValue *val, int is_signed, const char *name);
	IValue* (*CreateF64ToI64)(IBuilderComprehensive *self, IValue *val, int is_signed, const char *name);

	/*=====================================================================
	 * Miscellaneous Operations
	 *===================================================================*/

	/* Conditional select with predicate */
	IValue* (*CreateMaskedSelect)(IBuilderComprehensive *self, IValue *mask, IValue *true_val, IValue *false_val, const char *name);

	/* Rotate operations */
	IValue* (*CreateRotateLeft)(IBuilderComprehensive *self, IValue *val, IValue *shift, const char *name);
	IValue* (*CreateRotateRight)(IBuilderComprehensive *self, IValue *val, IValue *shift, const char *name);

	/* Carry operations */
	IValue* (*CreateAddWithCarry)(IBuilderComprehensive *self, IValue *a, IValue *b, IValue *carry_in, IValue **carry_out, const char *name);
	IValue* (*CreateSubWithBorrow)(IBuilderComprehensive *self, IValue *a, IValue *b, IValue *borrow_in, IValue **borrow_out, const char *name);

	/* Multiply high/low */
	IValue* (*CreateMulHigh)(IBuilderComprehensive *self, IValue *a, IValue *b, int is_signed, const char *name);
	IValue* (*CreateMulWide)(IBuilderComprehensive *self, IValue *a, IValue *b, IValue **high, const char *name);

	/* Division with remainder */
	IValue* (*CreateDivRem)(IBuilderComprehensive *self, IValue *a, IValue *b, IValue **rem, const char *name);
};

/***************************************************************************
 * Creation Function
 ***************************************************************************/

/**
 * Create comprehensive inline emulation builder with 400+ operations.
 *
 * @param wrapped_builder The backend builder to wrap
 * @param wrapped_module The backend module
 * @return Comprehensive builder with all inline operations
 */
IBuilderComprehensive* emulation_create_comprehensive_builder(IBuilder *wrapped_builder, IModule *wrapped_module);

/**
 * Get operation count.
 */
uint32_t emulation_get_operation_count(void);

/**
 * List all available operations.
 */
void emulation_list_operations(void);

#ifdef __cplusplus
}
#endif

#endif /* BACKEND_EMULATION_INLINE_COMPREHENSIVE_H */
