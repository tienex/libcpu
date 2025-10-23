## Unified Operations Framework

This document describes the architecture-agnostic unified operations framework that works on both scalars and vectors transparently.

## Design Philosophy

### Core Principles

1. **Semantic Operations**: Operations defined by WHAT they do, not which architecture they came from
2. **Scalar/Vector Transparency**: Every operation works on both scalars and vectors
3. **No Architecture Names**: Use `op_add()` not `x86_paddb()` or `neon_vaddq()`
4. **Predictable Behavior**: Same operation produces same results regardless of value type
5. **Type Safety**: Operations adapt to element types automatically

### Problem with Previous Approach

**Before (Architecture-Specific)**:
```c
// x86
_mm_add_ps(a, b);         // SSE
_mm256_add_ps(a, b);      // AVX

// ARM
vaddq_f32(a, b);          // NEON
svadd_f32_z(pred, a, b);  // SVE

// Each has different API, different types, different behavior
```

**After (Unified)**:
```c
// Works for scalars, vectors, any architecture
op_add(dst, a, b);
// - If a,b are scalars: scalar addition
// - If a,b are vectors: element-wise vector addition
// - Works on i8, i16, i32, i64, f32, f64, fp16, bf16, etc.
```

## Unified Value Type

### The `unified_value_t` Type

All operations work on `unified_value_t` which can represent:
- **Scalars**: Single integer or floating-point value
- **Vectors**: Multiple elements of same type
- **Matrices**: 2D tiles for matrix operations
- **Predicates**: Bit masks for conditional operations

```c
typedef struct unified_value {
    value_kind_t kind;  // SCALAR, VECTOR, MATRIX, PREDICATE
    union {
        struct { ... } scalar;
        vector_t *vector;
        matrix_tile_t *matrix;
        predicate_t *predicate;
    };
} unified_value_t;
```

### Creating Values

```c
// Scalars
unified_value_t *a = uval_scalar_i64(42);
unified_value_t *b = uval_scalar_f64(3.14);

// Vectors
vector_type_desc_t desc = vec_f32x4();  // 4× f32
unified_value_t *vec = uval_vector(&desc);

// Predicates
unified_value_t *pred = uval_predicate(64, 0);  // 64-bit mask
```

## Operation Categories

### 1. Arithmetic Operations

All work on both scalars and vectors:

```c
op_add(result, a, b);      // Addition
op_sub(result, a, b);      // Subtraction
op_mul(result, a, b);      // Multiplication
op_div(result, a, b);      // Division
op_rem(result, a, b);      // Remainder/modulo
op_abs(result, a);         // Absolute value
op_neg(result, a);         // Negation

// Saturating variants
op_add_sat(result, a, b);  // Add with saturation
op_sub_sat(result, a, b);  // Subtract with saturation

// Multiply variants
op_mulhi(result, a, b);    // High half of multiplication
op_mullo(result, a, b);    // Low half of multiplication

// Min/max
op_min(result, a, b);      // Minimum
op_max(result, a, b);      // Maximum
op_avg(result, a, b);      // Average (rounded)

// Fused operations
op_fma(result, a, b, c);   // a*b + c
op_fms(result, a, b, c);   // a*b - c
op_fnma(result, a, b, c);  // -(a*b) + c
op_fnms(result, a, b, c);  // -(a*b) - c
```

### 2. Bitwise Operations

```c
op_and(result, a, b);      // Bitwise AND
op_or(result, a, b);       // Bitwise OR
op_xor(result, a, b);      // Bitwise XOR
op_andn(result, a, b);     // AND-NOT (a & ~b)
op_not(result, a);         // Bitwise NOT

// Shifts
op_sll(result, a, shift);  // Shift left logical
op_srl(result, a, shift);  // Shift right logical
op_sra(result, a, shift);  // Shift right arithmetic
op_rol(result, a, shift);  // Rotate left
op_ror(result, a, shift);  // Rotate right

// Immediate shifts (faster)
op_slli(result, a, 3);     // Shift left by 3
op_srli(result, a, 5);     // Shift right by 5
```

### 3. Byte/Bit Manipulation

#### ALPHA-Style Byte Operations

**ZAP (Zero APha bytes)**: Zero out bytes where mask bit = 1
**ZAPNOT**: Zero out bytes where mask bit = 0

```c
// ALPHA ZAP: Zero bytes according to mask
// mask = 0x0F (binary 00001111) -> zero lower 4 bytes
unified_value_t *val = uval_scalar_u64(0x0123456789ABCDEFULL);
op_byte_zap(result, val, 0x0F);
// result = 0x0123456700000000

// ALPHA ZAPNOT: Keep bytes according to mask
op_byte_zapnot(result, val, 0x0F);
// result = 0x0000000089ABCDEF
```

**Why ZAP/ZAPNOT are useful**:
- Efficient byte masking without shifts
- Useful for string operations
- Graphics pixel manipulation
- Network packet processing
- Endian-independent byte selection

#### Bit Field Operations (from 68K, generalized)

```c
// Extract bit field
op_bitfield_extract(dst, src, offset, width);
// Example: extract bits 8-15 from value
op_bitfield_extract(result, val, 8, 8);

// Insert bit field
op_bitfield_insert(dst, base, src, offset, width);
// Example: insert 8 bits at position 16
op_bitfield_insert(result, base, value, 16, 8);

// Set/clear/toggle bits
op_bitfield_set(dst, src, offset, width);
op_bitfield_clear(dst, src, offset, width);
op_bitfield_toggle(dst, src, offset, width);
```

#### Bit Counting and Scanning

```c
op_popcnt(dst, src);       // Count 1 bits
op_clz(dst, src);          // Count leading zeros
op_ctz(dst, src);          // Count trailing zeros
op_parity(dst, src);       // Parity bit

// Find first/last set bit
op_find_first_set(dst, src);    // Index of first 1
op_find_last_set(dst, src);     // Index of last 1
op_find_first_clear(dst, src);  // Index of first 0

// Bit deposit/extract (BMI2 PDEP/PEXT style)
op_bit_deposit(dst, src, mask);
op_bit_extract(dst, src, mask);
```

#### Byte Manipulation

```c
op_bswap(dst, src);        // Byte swap (endian conversion)
op_bitrev(dst, src);       // Bit reversal
op_byte_extract(dst, src, mask);      // Extract bytes by mask
op_byte_shuffle(dst, src, pattern);   // Arbitrary byte permutation
```

### 4. Comparison Operations

```c
// Comparisons return mask (all 1s or 0s per element)
op_cmpeq(dst, a, b);       // Equal
op_cmpne(dst, a, b);       // Not equal
op_cmplt(dst, a, b);       // Less than
op_cmple(dst, a, b);       // Less or equal
op_cmpgt(dst, a, b);       // Greater than
op_cmpge(dst, a, b);       // Greater or equal

// FP comparisons
op_cmpord(dst, a, b);      // Ordered (no NaN)
op_cmpunord(dst, a, b);    // Unordered (has NaN)

// Predicate-producing (for SVE/RVV/AVX-512)
op_cmpeq_pred(pred, a, b); // Store result in predicate
op_cmplt_pred(pred, a, b);

// Find index of min/max
op_minidx(dst, vec);       // Index of minimum element
op_maxidx(dst, vec);       // Index of maximum element
```

### 5. Type Conversions

```c
// Automatic conversion based on types
op_convert(dst, src);

// Integer <-> Float
op_cvt_i2f(dst, src);           // int to float
op_cvt_f2i(dst, src);           // float to int (default rounding)
op_cvt_f2i_trunc(dst, src);     // truncate toward zero
op_cvt_f2i_floor(dst, src);     // round down
op_cvt_f2i_ceil(dst, src);      // round up
op_cvt_f2i_round(dst, src);     // round to nearest

// Extension
op_extend_signed(dst, src);     // Sign extend
op_extend_unsigned(dst, src);   // Zero extend
op_truncate(dst, src);          // Truncate to smaller type

// Saturation
op_saturate(dst, src, min, max);
```

### 6. Pack/Unpack Operations

```c
// Pack (narrow): two inputs -> one output
op_pack(dst, a, b);
op_pack_sat_signed(dst, a, b);
op_pack_sat_unsigned(dst, a, b);

// Unpack (widen): one input -> one output
op_unpack_low(dst, src);
op_unpack_high(dst, src);

// Interleave
op_interleave_low(dst, a, b);
op_interleave_high(dst, a, b);
```

### 7. Shuffle/Permute/Select

```c
// General shuffle
op_shuffle(dst, src, indices);
op_shuffle2(dst, a, b, indices);

// Permute (reorder elements)
op_permute(dst, src, control);

// Blend/select
op_blend(dst, a, b, mask);
op_select(dst, a, b, cond);

// Element operations
op_extract_element(dst, vec, index);
op_insert_element(dst, vec, elem, index);

// Broadcast/splat
op_broadcast(dst, scalar);
op_splat(dst, vec, index);

// Reverse/rotate
op_reverse(dst, src);
op_rotate_elements(dst, src, count);

// Concatenate and extract
op_concat_extract(dst, a, b, offset);
```

### 8. Reduction Operations (Vector -> Scalar)

```c
op_reduce_add(dst, vec);   // Sum all elements
op_reduce_mul(dst, vec);   // Product of all elements
op_reduce_min(dst, vec);   // Minimum element
op_reduce_max(dst, vec);   // Maximum element
op_reduce_and(dst, vec);   // Bitwise AND all elements
op_reduce_or(dst, vec);    // Bitwise OR all elements
op_reduce_xor(dst, vec);   // Bitwise XOR all elements
```

### 9. Horizontal Operations

```c
op_hadd(dst, a, b);        // Horizontal add
op_hsub(dst, a, b);        // Horizontal subtract
op_hadds(dst, a, b);       // Horizontal add saturating
op_hsubs(dst, a, b);       // Horizontal subtract saturating
```

### 10. Dot Product

```c
op_dot(dst, a, b);         // Dot product
op_dot4(dst, a, b);        // 4-element dot product
```

### 11. Floating-Point Math

```c
// Basic
op_sqrt(dst, src);         // Square root
op_rsqrt(dst, src);        // Reciprocal sqrt (1/sqrt(x))
op_rcp(dst, src);          // Reciprocal (1/x)

// Rounding
op_ceil(dst, src);
op_floor(dst, src);
op_trunc(dst, src);
op_round(dst, src);
op_rint(dst, src);

// Trigonometric
op_sin(dst, src);
op_cos(dst, src);
op_tan(dst, src);
op_asin(dst, src);
op_acos(dst, src);
op_atan(dst, src);
op_atan2(dst, y, x);

// Exponential/Logarithmic
op_exp(dst, src);
op_exp2(dst, src);
op_exp10(dst, src);
op_expm1(dst, src);        // exp(x) - 1
op_log(dst, src);
op_log2(dst, src);
op_log10(dst, src);
op_log1p(dst, src);        // log(1 + x)

// Other
op_pow(dst, base, exp);
op_hypot(dst, a, b);       // sqrt(a² + b²)
op_cbrt(dst, src);         // Cube root
op_copysign(dst, mag, sgn);
op_signbit(dst, src);
```

### 12. Predicated Operations

```c
// Execute only where predicate is true
op_add_pred(dst, pred, a, b);
op_mul_pred(dst, pred, a, b);
op_load_pred(dst, pred, ptr);
op_store_pred(src, pred, ptr);

// Predicate operations
op_pred_and(dst, a, b);
op_pred_or(dst, a, b);
op_pred_xor(dst, a, b);
op_pred_not(dst, src);
op_pred_count(dst, pred);      // Count true bits

// Predicate creation
op_pred_while_lt(pred, start, end);
op_pred_all_true(pred);
op_pred_all_false(pred);
```

### 13. Load/Store

```c
op_load(dst, ptr);
op_store(src, ptr);
op_load_splat(dst, ptr);       // Load scalar and broadcast

// Gather/scatter
op_gather(dst, base, indices, scale);
op_scatter(src, base, indices, scale);
```

### 14. String/Block Operations

```c
op_block_move(dst, src, count);
op_block_fill(dst, value, count);
op_block_compare(result, a, b, count);

op_string_search(result, str, chr, count);
op_string_search_not(result, str, chr, count);
```

### 15. BCD Operations

```c
op_bcd_add(dst, a, b);
op_bcd_sub(dst, a, b);
op_bcd_adjust_add(dst, src);
```

### 16. Fixed-Point Operations

```c
op_fixed_add(dst, a, b, fraction_bits);
op_fixed_mul(dst, a, b, fraction_bits);
op_fixed_div(dst, a, b, fraction_bits);
```

### 17. Conditional Select

```c
// Select true_val if a == b, else false_val
op_select_eq(dst, true_val, false_val, a, b);
op_select_ne(dst, true_val, false_val, a, b);
op_select_lt(dst, true_val, false_val, a, b);
op_select_le(dst, true_val, false_val, a, b);
op_select_gt(dst, true_val, false_val, a, b);
op_select_ge(dst, true_val, false_val, a, b);
```

### 18. Crypto Operations

```c
op_aes_encrypt(dst, state, key);
op_aes_decrypt(dst, state, key);
op_aes_keygen(dst, key, round);
op_sha256_update(dst, state, data);
op_crc32_update(dst, crc, data);
```

### 19. Matrix Operations

```c
op_matrix_mul(c, a, b);          // C = A * B
op_matrix_mul_acc(c, a, b);      // C += A * B
op_matrix_transpose(dst, src);
```

### 20. Utilities

```c
op_set_all(dst, scalar);         // Broadcast to all elements
op_set_zero(dst);                // Set to zero

op_movemask(src);                // Extract comparison mask to integer

op_test_all_zeros(a, b);         // Test if (a & b) all zeros
op_test_all_ones(a, b);          // Test if (a & b) all ones
```

## Architecture Mapping

The unified framework provides inline helpers to map architecture-specific names:

```c
// ALPHA
alpha_zap(dst, src, mask);          -> op_byte_zap(dst, src, mask)
alpha_zapnot(dst, src, mask);       -> op_byte_zapnot(dst, src, mask)

// x86
x86_paddb(dst, a, b);               -> op_add(dst, a, b)
x86_psubb(dst, a, b);               -> op_sub(dst, a, b)
x86_pshufb(dst, src, control);      -> op_permute(dst, src, control)

// ARM NEON
neon_vaddq(dst, a, b);              -> op_add(dst, a, b)
neon_vsubq(dst, a, b);              -> op_sub(dst, a, b)

// SPARC VIS
vis_fpack16(dst, src);              -> op_pack_sat_unsigned(dst, src, src)

// VAX
vax_movc3(dst, src, count);         -> op_block_move(dst, src, count)

// 68K
m68k_bfextu(dst, src, off, width);  -> op_bitfield_extract(dst, src, off, width)

// Z80
z80_ldir(dst, src, count);          -> op_block_move(dst, src, count)
```

## Usage Examples

### Example 1: Vector Addition (works for any element type)

```c
// Create vectors
vector_type_desc_t desc = vec_f32x4();
unified_value_t *a = uval_vector(&desc);
unified_value_t *b = uval_vector(&desc);
unified_value_t *result = uval_vector(&desc);

// Initialize
float vals_a[4] = {1, 2, 3, 4};
float vals_b[4] = {5, 6, 7, 8};
memcpy(a->vector->data, vals_a, sizeof(vals_a));
memcpy(b->vector->data, vals_b, sizeof(vals_b));

// Add - same code works for any backend
op_add(result, a, b);  // result = [6, 8, 10, 12]
```

### Example 2: ALPHA ZAP in Action

```c
// Extract specific bytes from a 64-bit value
unified_value_t *val = uval_scalar_u64(0x123456789ABCDEF0ULL);
unified_value_t *result = uval_scalar_u64(0);

// Keep bytes 0, 1, 4, 5 (mask = 0x33 = 0b00110011)
op_byte_zapnot(result, val, 0x33);
// result = 0x000000009ABC00F0

// Zero out bytes 2, 3, 6, 7 (mask = 0xCC = 0b11001100)
op_byte_zap(result, val, 0xCC);
// result = 0x0034007800EF0000
```

### Example 3: Works with Scalars Too

```c
// Same operations work on scalars
unified_value_t *a = uval_scalar_i32(10);
unified_value_t *b = uval_scalar_i32(20);
unified_value_t *sum = uval_scalar_i32(0);

op_add(sum, a, b);  // sum = 30

// Bitfield operations on scalars
unified_value_t *val = uval_scalar_u32(0x12345678);
unified_value_t *field = uval_scalar_u32(0);

op_bitfield_extract(field, val, 8, 8);  // Extract bits 8-15
// field = 0x56
```

### Example 4: Predicated Operations

```c
// SVE/RVV/AVX-512 style predication
vector_type_desc_t desc = vec_i32x8();
unified_value_t *vec = uval_vector(&desc);
unified_value_t *result = uval_vector(&desc);

// Create predicate: only first 4 elements active
unified_value_t *pred = uval_predicate(8, 0);
for (int i = 0; i < 4; i++)
    pred->predicate->mask_data[0] |= (1 << i);

// Predicated addition - only affects first 4 elements
op_add_pred(result, pred, vec, vec);
```

### Example 5: Matrix Multiply (AMX/SME style)

```c
// Works for both Intel AMX and ARM SME
matrix_tile_desc_t desc_a = amx_tile_i8(16, 64);
matrix_tile_desc_t desc_b = amx_tile_i8(16, 64);
matrix_tile_desc_t desc_c = amx_tile_i32_acc(16, 16);

unified_value_t *a = uval_matrix(&desc_a);
unified_value_t *b = uval_matrix(&desc_b);
unified_value_t *c = uval_matrix(&desc_c);

// Matrix multiply - backend chooses AMX or SME or emulation
op_matrix_mul_acc(c, a, b);  // C += A * B
```

## Benefits

### 1. **Architecture Independence**
Write `op_add()` once, works on x86, ARM, RISC-V, POWER, etc.

### 2. **Scalar/Vector Transparency**
Same operation works on both scalars and vectors.

### 3. **Type Flexibility**
Automatically adapts to i8, i16, i32, i64, f32, f64, fp16, bf16, etc.

### 4. **Predictable Behavior**
Same semantic meaning regardless of backend.

### 5. **Easy to Extend**
Add new operations without changing existing code.

### 6. **Reduced Cognitive Load**
Learn one API, not dozens of architecture-specific APIs.

### 7. **Automatic Optimization**
Backend can choose best implementation (native or emulated).

## Comparison

### Before (Architecture-Specific)

```c
// Different code for each architecture
#ifdef __x86_64__
    __m128 result = _mm_add_ps(a, b);
#elif __aarch64__
    float32x4_t result = vaddq_f32(a, b);
#elif __riscv
    vfloat32m1_t result = vfadd_vv_f32m1(a, b, vl);
#endif
```

### After (Unified)

```c
// Same code everywhere
op_add(result, a, b);
```

## Implementation Status

| Category | Operations | Status |
|----------|-----------|--------|
| Arithmetic | 20+ | API defined |
| Bitwise | 15+ | API defined |
| Byte/Bit manipulation | 20+ | API defined |
| Comparisons | 10+ | API defined |
| Conversions | 15+ | API defined |
| Pack/Unpack | 10+ | API defined |
| Shuffle/Select | 15+ | API defined |
| Reductions | 10+ | API defined |
| Horizontal ops | 4+ | API defined |
| Dot products | 2+ | API defined |
| FP math | 30+ | API defined |
| Predicated ops | 10+ | API defined |
| Load/Store | 6+ | API defined |
| String/Block | 5+ | API defined |
| BCD | 3+ | API defined |
| Fixed-point | 3+ | API defined |
| Conditional select | 6+ | API defined |
| Crypto | 5+ | API defined |
| Matrix | 3+ | API defined |
| Utilities | 5+ | API defined |

**Total: 200+ unified operations**

## Conclusion

The unified operations framework provides:
- **One API** for all architectures
- **Scalar and vector** support
- **Semantic operations** not encoding-specific
- **Easy to use** and extend
- **Comprehensive coverage** of all modern and legacy operations

This is the RIGHT way to build a portable, maintainable emulation layer.
