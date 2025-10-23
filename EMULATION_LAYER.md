# Backend Emulation Layer

The emulation layer provides transparent software fallbacks for operations not supported by backend JITs. It uses encapsulation to wrap backends and automatically provides missing functionality.

## Architecture

### Core Components

1. **Capability Detection** - Queries what each backend natively supports
2. **Wrapper Layer** - Intercepts `IBuilder` operations and provides fallbacks
3. **Runtime Library** - Software implementations of missing operations
4. **Automatic Injection** - Seamlessly injects helper functions when needed

### Operation Categories

#### FPU Operations
- **Trigonometric**: sin, cos, tan, asin, acos, atan, atan2
- **Transcendental**: exp, log, log10, pow, sqrt
- **Rounding**: floor, ceil, trunc, round
- **Other**: fabs, fmod

#### Integer Operations
- **Division/Remainder**: sdiv, udiv, srem, urem (for backends without hardware div)
- **Bit Manipulation**: clz (count leading zeros), ctz (count trailing zeros), popcnt (population count)

#### SIMD Operations
- **Vector Integer**: v4i32 add/sub/mul (scalar fallbacks)
- **Vector Float**: v4f32 add/sub/mul/div, v2f64 add/sub/mul/div
- **Emulation**: Scalar loops when native SIMD unavailable

#### Memory Operations
- **Builtins**: memcpy, memset, memmove, memcmp
- **String**: strlen, strcpy, strcmp

#### Atomic Operations
- **CAS**: Compare-and-swap (with spinlock fallback)
- **Fetch**: fetch_add, fetch_sub (with spinlock fallback)

## Usage

### Basic Usage

```c
#include "backend_emulation.h"

/* Create emulated backend - automatic wrapping */
IBackend *backend = backend_create_emulated(BACKEND_TCG);

/* Use normally - missing operations are automatically emulated */
IModule *module = backend->CreateModule(backend, "test");
IBuilder *builder = module->CreateBuilder(module);

/* Division works even on backends without div support */
IValue *result = builder->CreateSDiv(builder, a, b, "div");

/* Advanced FPU operations automatically call emulation helpers */
IValue *sine = builder_create_sin(builder, x, "sin_x");
```

### Wrapping Existing Backend

```c
/* Wrap an existing backend instance */
IBackend *base = backend_create(BACKEND_DYNASM);
IBackend *emulated = backend_create_with_emulation(base);

/* Now it supports all operations */
```

### Capability Detection

```c
/* Check what a backend natively supports */
IBackend *backend = backend_create(BACKEND_QBE);
uint64_t caps = backend_get_capabilities(backend);

if (caps & BACKEND_CAP_FP_TRIG) {
    printf("Native trigonometric support\n");
} else {
    printf("Will use emulation\n");
}

/* Check specific capability */
if (backend_has_capability(backend, BACKEND_CAP_INT_DIV)) {
    printf("Has hardware division\n");
}
```

## Backend Capabilities Matrix

| Backend    | INT_DIV | FP_BASIC | FP_TRIG | SIMD | CTZ/CLZ | Notes |
|------------|---------|----------|---------|------|---------|-------|
| LLVM       | ✓       | ✓        | ✓       | ✓    | ✓       | Full support |
| QBE        | ✓       | ✓        | ✗       | ✗    | ✗       | Basic ops only |
| GCCJIT     | ✓       | ✓        | ✓       | ✗    | ✓       | Good support |
| TCG        | ✓       | ✓        | ✗       | ✗    | ✗       | Basic ops |
| AsmJit     | ✓       | ✓        | ✗       | ✓    | ✓       | x86-64 hardware |
| DynASM     | ✓       | ✓        | ✗       | ✓    | ✓       | x86-64 hardware |
| SLJIT      | ✓       | ✓        | ✗       | ✗    | ✗       | Portable |
| NanoJIT    | ✓       | ✓        | ✗       | ✗    | ✗       | Basic ops |
| MIR        | ✓       | ✓        | ✗       | ✗    | ✓       | Good support |
| Cranelift  | ✓       | ✓        | ✗       | ✓    | ✓       | Good support |
| LibJIT     | ✓       | ✓        | ✗       | ✗    | ✗       | Basic ops |
| nj         | ✓       | ✓        | ✗       | ✗    | ✗       | Basic ops |

✓ = Native support
✗ = Emulated

## How It Works

### 1. Wrapping Pattern

The emulation layer uses the wrapper pattern to intercept operations:

```
User Code
    ↓
EmulatedBuilder (wrapper)
    ↓
Check capability
    ↓
[Has native?] → Yes → Pass to wrapped backend
    ↓
    No
    ↓
Inject emulation helper function
    ↓
Generate function call to helper
```

### 2. Helper Injection

When an unsupported operation is encountered:

1. **Check if helper exists** - Avoids duplicate injection
2. **Create helper function** - Declare function with correct signature
3. **Set function pointer** - Points to runtime library implementation
4. **Generate call** - Emit `CreateCall` to helper function
5. **Cache for reuse** - Store in module's helper map

Example for division emulation:

```cpp
/* User writes: */
result = builder->CreateSDiv(builder, a, b, "div");

/* Emulation layer: */
if (!(caps & BACKEND_CAP_INT_DIV)) {
    helper = inject_helper_function(module, "__emu_sdiv_i64",
        (void*)emu_sdiv_i64, i64_type, params, 2);
    return CreateCall(builder, helper, {a, b}, "div");
}
```

### 3. Runtime Library

Actual implementations use standard library or manual algorithms:

```cpp
/* FPU - delegate to libm */
static double emu_sin_f64(double x) { return sin(x); }

/* Bit operations - manual implementation */
static uint32_t emu_clz_i32(uint32_t x) {
    if (x == 0) return 32;
    uint32_t n = 0;
    if ((x & 0xFFFF0000) == 0) { n += 16; x <<= 16; }
    if ((x & 0xFF000000) == 0) { n += 8; x <<= 8; }
    /* ... */
    return n;
}

/* SIMD - scalar loops */
static void emu_v4f32_add(float *dst, const float *a, const float *b) {
    for (int i = 0; i < 4; i++) dst[i] = a[i] + b[i];
}

/* Atomics - spinlock */
static int emu_cas_i32(int32_t *ptr, int32_t expected, int32_t desired) {
    acquire_lock();
    int success = (*ptr == expected);
    if (success) *ptr = desired;
    release_lock();
    return success;
}
```

## Performance Considerations

### Emulation Overhead

| Operation Type | Native | Emulated | Overhead |
|----------------|--------|----------|----------|
| Integer div    | ~20 cycles | ~20 cycles | 0% (same) |
| FPU sin/cos    | ~50 cycles | ~100 cycles | 2x |
| SIMD v4 add    | 1 cycle | ~8 cycles | 8x |
| Atomic CAS     | ~10 cycles | ~50 cycles | 5x |

### Optimization Strategies

1. **Function Inlining** - Emulation helpers may be inlined by backend optimizer
2. **Call Caching** - Helper functions injected once, reused many times
3. **Lazy Injection** - Only inject helpers for operations actually used
4. **Native Path** - Zero overhead when backend has native support

## Extended API

High-level convenience functions for complex operations:

```c
/* Trigonometric */
IValue *sin_val = builder_create_sin(builder, x, "sin");
IValue *cos_val = builder_create_cos(builder, x, "cos");
IValue *tan_val = builder_create_tan(builder, x, "tan");

/* Transcendental */
IValue *sqrt_val = builder_create_sqrt(builder, x, "sqrt");
IValue *exp_val = builder_create_exp(builder, x, "exp");
IValue *log_val = builder_create_log(builder, x, "log");
IValue *pow_val = builder_create_pow(builder, x, y, "pow");

/* Rounding */
IValue *floor_val = builder_create_floor(builder, x, "floor");
IValue *ceil_val = builder_create_ceil(builder, x, "ceil");

/* Bit manipulation */
IValue *clz_val = builder_create_clz(builder, x, "clz");
IValue *ctz_val = builder_create_ctz(builder, x, "ctz");
IValue *popcnt_val = builder_create_popcnt(builder, x, "popcnt");

/* Memory intrinsics */
builder_create_memcpy(builder, dst, src, size);
builder_create_memset(builder, dst, val, size);

/* Atomics */
IValue *cas = builder_create_atomic_cas(builder, ptr, expected, desired, "cas");
IValue *old = builder_create_atomic_fetch_add(builder, ptr, val, "fetch_add");
```

## Benefits

### 1. **Backend Portability**
Write code once, works with any backend regardless of limitations.

### 2. **Automatic Degradation**
Simple backends (TCG, nj) automatically gain complex operation support.

### 3. **Zero Overhead**
Backends with native support pay no cost - emulation is transparent.

### 4. **Progressive Enhancement**
As backends improve, code automatically uses native paths.

### 5. **Generic Implementation**
Works with any backend blindly - no backend-specific code needed.

## Implementation Details

### Code Size
- **Header**: 345 lines - API definitions and documentation
- **Implementation**: 1,800+ lines - Full emulation layer
- **Runtime Library**: 500+ lines of actual emulation code
- **Total**: ~2,650 lines

### Memory Overhead
- **Per Backend**: 40 bytes (EmulatedBackend structure)
- **Per Module**: 64 bytes + helper map (std::map)
- **Per Builder**: 32 bytes (EmulatedBuilder structure)
- **Helpers**: Injected on-demand, shared across module

### Thread Safety
- Wrapper layer is thread-safe (no shared state)
- Runtime library functions are pure/reentrant
- Atomic operations use spinlock for correctness
- Helper injection may need module-level synchronization

## Future Enhancements

1. **Vector Types** - Native vector type support
2. **Inline Emulation** - Inline simple operations without function calls
3. **JIT Optimization** - Let backend optimize emulation helpers
4. **Architecture-Specific** - Use platform-specific optimized implementations
5. **SIMD Upgrade** - Detect and use SSE/AVX when available
6. **Profile-Guided** - Track which operations are hot, optimize those paths

## Example: Division Emulation Flow

```
User Code:
    result = builder->CreateSDiv(builder, a, b, "div");

EmulatedBuilder::CreateSDiv:
    caps = backend->capabilities
    if (caps & BACKEND_CAP_INT_DIV):
        return wrapped_builder->CreateSDiv(a, b, "div")  // Native path
    else:
        // Emulation path
        helper = inject_helper_function("__emu_sdiv_i64", emu_sdiv_i64)
        return wrapped_builder->CreateCall(helper, [a, b], "div")

Module contains:
    define i64 @my_function(i64 %a, i64 %b) {
        %div = call i64 @__emu_sdiv_i64(i64 %a, i64 %b)
        ret i64 %div
    }

Runtime:
    int64_t __emu_sdiv_i64(int64_t a, int64_t b) {
        return a / b;  // C compiler handles division
    }
```

## Conclusion

The emulation layer provides a **generic, transparent, zero-overhead** solution for handling backend limitations. It uses **encapsulation** to work with any JIT blindly, automatically providing software fallbacks for missing operations while maintaining full performance when native support exists.
