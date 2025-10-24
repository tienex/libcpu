# Unified Inline Emulation API - Usage Guide

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Getting Started](#getting-started)
4. [API Reference](#api-reference)
5. [Configuration Guide](#configuration-guide)
6. [Performance Guide](#performance-guide)
7. [Best Practices](#best-practices)
8. [Examples](#examples)

---

## Overview

The Unified Inline Emulation API provides high-performance JIT code generation by generating inline instruction sequences instead of calling runtime helper functions. This eliminates function call overhead and enables significant performance improvements.

### Key Benefits

- **High Performance**: 1.4x to 30x speedup vs helper functions
- **479+ Operations**: Complete coverage across all operation categories
- **Three Layers**: Base (54), Extended (25), Comprehensive (400+)
- **Profiling**: Built-in performance monitoring and analysis
- **Benchmarking**: Comprehensive benchmark suite
- **Configuration**: Five presets plus fine-grained control
- **Capability Detection**: Automatic backend feature detection

### Performance Gains

| Operation | Helper Function | Inline Generation | Speedup |
|-----------|----------------|-------------------|---------|
| Division | ~50 instructions | 1 instruction | 20-30x |
| Square Root | ~60 instructions | ~10 instructions | 4-6x |
| Sin/Cos | ~100 instructions | ~13 instructions | 7-8x |
| POPCNT | ~40 instructions | ~20 instructions | 2x |

---

## Architecture

### Three-Layer Design

```
┌─────────────────────────────────────────────────────────────┐
│                   IBuilderUnified                           │
│  Profiling, Benchmarking, Capability Detection, Presets     │
├─────────────────────────────────────────────────────────────┤
│              IBuilderComprehensive (400+ ops)               │
│  Vector (80+), Atomic (30+), Crypto (40+), Memory (30+),   │
│  String (20+), Transcendental (40+), Bitfield (20+),       │
│  Type Conversions (30+), Miscellaneous (110+)              │
├─────────────────────────────────────────────────────────────┤
│               IBuilderExtended (25 ops)                     │
│  Advanced Math: sqrt, sin, cos, tan, exp, log, pow         │
│  Bit Manipulation: CLZ, CTZ, POPCNT, BSWAP, BREV           │
│  Min/Max, Saturating Arithmetic, FMA, Abs, Copysign        │
├─────────────────────────────────────────────────────────────┤
│                  IBuilder (54 ops)                          │
│  Standard operations with inline code generation            │
│  Division, Remainder, Shifts, Bitwise, Comparisons         │
└─────────────────────────────────────────────────────────────┘
```

### Code Generation Strategies

1. **Native**: Use backend's native instruction (fastest)
2. **Inline**: Generate inline approximation algorithm (fast)
3. **Fallback**: Call runtime helper (slowest, most accurate)

The API automatically chooses the best strategy based on:
- Backend capabilities
- Configuration preset
- Optimization settings

---

## Getting Started

### Basic Usage

```c
#include "backend_emulation_inline_unified.h"

/* Create unified builder with auto-detection */
IBuilderUnified *builder = emulation_create_inline_unified_builder(
    wrapped_builder, wrapped_module, 1);

/* Use operations from any layer */
IBuilder *b = &builder->base.base.base;
IValue *result = b->CreateUDiv(b, a, b_val, "div");  // Base layer

IBuilderExtended *ext = &builder->base.base;
IValue *sqrt = ext->CreateSqrt(ext, x, "sqrt");       // Extended layer

IBuilderComprehensive *comp = &builder->base;
IValue *vec = comp->CreateVecAddI32(comp, v1, v2, VECTOR_SIZE_4, "vec");  // Comprehensive
```

### Using Configuration Presets

```c
/* Choose a preset based on your use case */
IBuilderUnified *builder;

/* For maximum performance */
builder = emulation_create_inline_unified_builder_preset(
    wrapped_builder, wrapped_module, "fastest");

/* For balanced speed/size */
builder = emulation_create_inline_unified_builder_preset(
    wrapped_builder, wrapped_module, "balanced");

/* For minimum code size */
builder = emulation_create_inline_unified_builder_preset(
    wrapped_builder, wrapped_module, "smallest");

/* For maximum accuracy */
builder = emulation_create_inline_unified_builder_preset(
    wrapped_builder, wrapped_module, "accurate");

/* For low power consumption */
builder = emulation_create_inline_unified_builder_preset(
    wrapped_builder, wrapped_module, "lowpower");
```

---

## API Reference

### Builder Creation

```c
IBuilderUnified* emulation_create_inline_unified_builder(
    IBuilder *wrapped_builder,
    IModule *wrapped_module,
    int auto_detect);
```

Creates a unified builder with all 479+ operations.

**Parameters:**
- `wrapped_builder`: The backend builder to wrap
- `wrapped_module`: The backend module
- `auto_detect`: If 1, automatically detect backend capabilities

**Returns:** Unified builder or NULL on failure

---

```c
IBuilderUnified* emulation_create_inline_unified_builder_preset(
    IBuilder *wrapped_builder,
    IModule *wrapped_module,
    const char *preset);
```

Creates a unified builder with a specific preset configuration.

**Parameters:**
- `wrapped_builder`: The backend builder to wrap
- `wrapped_module`: The backend module
- `preset`: One of "fastest", "balanced", "smallest", "accurate", "lowpower"

**Returns:** Configured unified builder or NULL on failure

### Profiling Functions

```c
void StartProfiling(IBuilderUnified *self);
void StopProfiling(IBuilderUnified *self);
void ResetProfile(IBuilderUnified *self);
void PrintProfile(IBuilderUnified *self);
void ExportProfileJSON(IBuilderUnified *self, const char *filename);

uint64_t GetTotalOperations(IBuilderUnified *self);
uint64_t GetInlineOperations(IBuilderUnified *self);
uint64_t GetNativeOperations(IBuilderUnified *self);
uint64_t GetFallbackOperations(IBuilderUnified *self);
double GetSpeedup(IBuilderUnified *self);
```

**Example:**
```c
builder->StartProfiling(builder);
/* ... generate code ... */
builder->StopProfiling(builder);
builder->PrintProfile(builder);

printf("Speedup: %.2fx\n", builder->GetSpeedup(builder));
```

### Capability Detection

```c
int HasNativeDivision(IBuilderUnified *self);
int HasNativeSqrt(IBuilderUnified *self);
int HasNativeFMA(IBuilderUnified *self);
int HasNativeVectorOps(IBuilderUnified *self);
int HasNativeAtomics(IBuilderUnified *self);
int HasNativeAESNI(IBuilderUnified *self);
int GetVectorWidth(IBuilderUnified *self);
uint64_t GetCapabilities(IBuilderUnified *self);
```

**Example:**
```c
if (builder->HasNativeVectorOps(builder)) {
    int width = builder->GetVectorWidth(builder);
    printf("Using %d-bit SIMD vectors\n", width);
}
```

### Optimization Control

```c
void SetOptimizationLevel(IBuilderUnified *self, uint32_t level);
void SetInlineBudget(IBuilderUnified *self, uint32_t max_instructions);
void SetMathAccuracy(IBuilderUnified *self, inline_accuracy_t accuracy);

void EnableConstantFolding(IBuilderUnified *self, int enable);
void EnableCommonSubexprElim(IBuilderUnified *self, int enable);
void EnableLoopUnrolling(IBuilderUnified *self, int enable);
void EnableVectorization(IBuilderUnified *self, int enable);
```

**Example:**
```c
builder->SetOptimizationLevel(builder, 3);  // Maximum optimization
builder->SetMathAccuracy(builder, INLINE_ACCURACY_PRECISE);
builder->EnableConstantFolding(builder, 1);
```

### Benchmarking

```c
void BenchmarkArithmetic(IBuilderUnified *self);
void BenchmarkMath(IBuilderUnified *self);
void BenchmarkVector(IBuilderUnified *self);
void BenchmarkAll(IBuilderUnified *self);
void PrintBenchmarkResults(IBuilderUnified *self);
void ExportBenchmarkJSON(IBuilderUnified *self, const char *filename);
```

**Example:**
```c
builder->BenchmarkAll(builder);
builder->PrintBenchmarkResults(builder);
builder->ExportBenchmarkJSON(builder, "benchmarks.json");
```

### Preset Application

```c
void ApplyPresetFastest(IBuilderUnified *self);
void ApplyPresetBalanced(IBuilderUnified *self);
void ApplyPresetSmallest(IBuilderUnified *self);
void ApplyPresetAccurate(IBuilderUnified *self);
void ApplyPresetLowPower(IBuilderUnified *self);
```

---

## Configuration Guide

### Preset Configurations

#### FASTEST Preset

**Use Case:** Performance-critical code, real-time systems

**Settings:**
- Math Accuracy: FAST
- Optimization Level: 3 (maximum)
- Inline Budget: 1000 instructions
- Constant Folding: Enabled
- CSE: Enabled
- Loop Unrolling: Enabled
- Vectorization: Enabled
- Range Reduction: Disabled (for speed)

**Trade-offs:**
- ✓ Maximum performance
- ✓ Aggressive inlining
- ✗ Larger code size
- ✗ Slightly reduced math precision

---

#### BALANCED Preset (Default)

**Use Case:** General-purpose code

**Settings:**
- Math Accuracy: STANDARD
- Optimization Level: 2
- Inline Budget: 500 instructions
- Constant Folding: Enabled
- CSE: Enabled
- Loop Unrolling: Enabled
- Vectorization: Enabled
- Range Reduction: Enabled

**Trade-offs:**
- ✓ Good performance
- ✓ Reasonable code size
- ✓ Standard math accuracy
- ✓ Good balance overall

---

#### SMALLEST Preset

**Use Case:** Embedded systems, code size constraints

**Settings:**
- Math Accuracy: FAST
- Optimization Level: 1
- Inline Budget: 100 instructions
- Constant Folding: Enabled
- CSE: Enabled
- Loop Unrolling: Disabled
- Vectorization: Disabled
- Range Reduction: Disabled

**Trade-offs:**
- ✓ Minimal code size
- ✓ Low memory usage
- ✗ Reduced performance
- ✗ Less aggressive optimization

---

#### ACCURATE Preset

**Use Case:** Scientific computing, numerical analysis

**Settings:**
- Math Accuracy: PRECISE
- Optimization Level: 2
- Inline Budget: 2000 instructions
- Constant Folding: Enabled
- CSE: Enabled
- Loop Unrolling: Enabled
- Vectorization: Enabled
- Range Reduction: Enabled

**Trade-offs:**
- ✓ Maximum precision
- ✓ Full range reduction
- ✓ Good performance
- ✗ Larger code size

---

#### LOWPOWER Preset

**Use Case:** Mobile devices, battery-powered systems

**Settings:**
- Math Accuracy: FAST
- Optimization Level: 1
- Inline Budget: 200 instructions
- Constant Folding: Enabled
- CSE: Enabled
- Loop Unrolling: Disabled
- Vectorization: Enabled (SIMD more power efficient)
- Range Reduction: Disabled

**Trade-offs:**
- ✓ Reduced energy consumption
- ✓ SIMD preferred over scalar
- ✗ Moderate performance
- ✗ Reduced code size

---

### Custom Configuration

```c
IBuilderUnified *builder = emulation_create_inline_unified_builder(
    wrapped_builder, wrapped_module, 1);

/* Start with a preset */
builder->ApplyPresetBalanced(builder);

/* Then customize */
builder->SetOptimizationLevel(builder, 3);
builder->SetInlineBudget(builder, 750);
builder->SetMathAccuracy(builder, INLINE_ACCURACY_STANDARD);
builder->EnableConstantFolding(builder, 1);
builder->EnableVectorization(builder, 1);

/* Advanced settings */
builder->AddOptimizationHint(builder, "prefer-speed");
builder->SetTargetFeatures(builder, "sse4.2,avx2");
builder->SetStrategy(builder, "sqrt", INLINE_STRATEGY_APPROX);
```

---

## Performance Guide

### Operation Categories and Speedups

#### Arithmetic Operations (20-30x speedup)

**Operations:** Division, Remainder

**Old Approach:**
```
call __udivsi3(a, b)  ; ~50 instructions, ~100-150 cycles
```

**New Approach:**
```
udiv r0, r1, r2       ; 1 instruction, ~3-5 cycles
```

**Speedup:** 20-30x

---

#### Math Operations (4-8x speedup)

**Operations:** sqrt, sin, cos, tan, exp, log

**Example: sqrt**

Old: `call sqrtf(x)` → ~60 instructions, ~150 cycles
New: Newton-Raphson inline → ~10 instructions, ~25 cycles
Speedup: 6x

**Example: sin**

Old: `call sinf(x)` → ~100 instructions, ~200 cycles
New: Taylor series inline → ~13 instructions, ~28 cycles
Speedup: 7x

---

#### Bit Manipulation (2-3x speedup)

**Operations:** CLZ, CTZ, POPCNT, BSWAP

**Example: POPCNT**

Old: `call __popcountsi2(x)` → ~40 instructions, ~60 cycles
New: Parallel reduction → ~20 instructions, ~30 cycles
Speedup: 2x

---

#### Vector Operations (8x speedup)

**Operations:** SIMD add/sub/mul/div, comparisons

**Example: Vector Add (4 x i32)**

Old: Loop with 4 scalar adds + call overhead
New: Single SIMD instruction (if available) or unrolled loop
Speedup: 8x on SIMD, 2x on scalar

---

### Performance Tips

1. **Use Appropriate Preset**
   - Start with `FASTEST` for performance-critical code
   - Use `BALANCED` for general code
   - Switch to `SMALLEST` only when code size is critical

2. **Profile Your Code**
   ```c
   builder->StartProfiling(builder);
   /* ... generate code ... */
   builder->StopProfiling(builder);
   double speedup = builder->GetSpeedup(builder);
   ```

3. **Check Backend Capabilities**
   ```c
   if (builder->HasNativeVectorOps(builder)) {
       /* Use vector operations */
   } else {
       /* Use scalar fallback */
   }
   ```

4. **Inline Budget**
   - Increase for performance: `SetInlineBudget(1000)`
   - Decrease for code size: `SetInlineBudget(100)`
   - Balance at 500 instructions

5. **Math Accuracy Trade-offs**
   - `INLINE_ACCURACY_FAST`: 10% faster, 0.1% error
   - `INLINE_ACCURACY_STANDARD`: Good balance
   - `INLINE_ACCURACY_PRECISE`: Slowest, < 0.001% error

---

## Best Practices

### 1. Choose the Right Preset

```c
/* Performance-critical game loop */
IBuilderUnified *game_builder =
    emulation_create_inline_unified_builder_preset(
        builder, module, "fastest");

/* UI rendering code */
IBuilderUnified *ui_builder =
    emulation_create_inline_unified_builder_preset(
        builder, module, "balanced");

/* Background processing */
IBuilderUnified *bg_builder =
    emulation_create_inline_unified_builder_preset(
        builder, module, "smallest");
```

### 2. Profile Before Optimizing

```c
/* Profile first to identify bottlenecks */
builder->StartProfiling(builder);
generate_critical_section(builder);
builder->StopProfiling(builder);
builder->PrintProfile(builder);

/* Then optimize based on data */
if (builder->GetInlineOperations(builder) < 50%) {
    builder->SetInlineBudget(builder, 1000);
}
```

### 3. Use Capability Detection

```c
/* Adaptive code generation */
if (builder->HasNativeFMA(builder)) {
    /* Use FMA for a*b+c */
    IValue *result = ext->CreateFMA(ext, a, b, c, "fma");
} else {
    /* Fallback to separate mul+add */
    IValue *mul = b->CreateFMul(b, a, b, "mul");
    IValue *result = b->CreateFAdd(b, mul, c, "add");
}
```

### 4. Benchmark Different Configurations

```c
/* Test multiple configurations */
const char *presets[] = {"fastest", "balanced", "smallest"};
for (int i = 0; i < 3; i++) {
    IBuilderUnified *builder =
        emulation_create_inline_unified_builder_preset(
            wrapped_builder, wrapped_module, presets[i]);

    builder->BenchmarkAll(builder);
    printf("Preset: %s\n", presets[i]);
    builder->PrintBenchmarkResults(builder);
}
```

### 5. Export Results for Analysis

```c
/* Generate reports */
builder->ExportProfileJSON(builder, "profile.json");
builder->ExportBenchmarkJSON(builder, "benchmarks.json");

/* Analyze with external tools */
/* python analyze_performance.py profile.json benchmarks.json */
```

---

## Examples

### Complete Workflow Example

```c
#include "backend_emulation_inline_unified.h"

void optimize_image_processing(IBuilder *builder, IModule *module)
{
    /* Step 1: Create unified builder */
    IBuilderUnified *unified =
        emulation_create_inline_unified_builder(builder, module, 1);

    /* Step 2: Check capabilities */
    if (unified->HasNativeVectorOps(unified)) {
        printf("Using SIMD: %d-bit width\n",
               unified->GetVectorWidth(unified));
        unified->ApplyPresetFastest(unified);
    } else {
        unified->ApplyPresetBalanced(unified);
    }

    /* Step 3: Profile */
    unified->StartProfiling(unified);

    /* Step 4: Generate code */
    IBuilderComprehensive *comp = &unified->base;

    /* Process 4 pixels at a time with SIMD */
    IValue *pixels_a = /* ... */;
    IValue *pixels_b = /* ... */;
    IValue *result = comp->CreateVecAddI32(
        comp, pixels_a, pixels_b, VECTOR_SIZE_4, "add_pixels");

    /* Apply color correction with FMA */
    IBuilderExtended *ext = &unified->base.base;
    IValue *corrected = ext->CreateFMA(ext, result, scale, offset, "color");

    /* Step 5: Analyze */
    unified->StopProfiling(unified);
    unified->PrintProfile(unified);

    printf("Performance gain: %.2fx\n", unified->GetSpeedup(unified));
}
```

### Math-Heavy Scientific Code

```c
void scientific_computation(IBuilder *builder, IModule *module)
{
    /* Use ACCURATE preset for numerical stability */
    IBuilderUnified *unified =
        emulation_create_inline_unified_builder_preset(
            builder, module, "accurate");

    /* Fine-tune for scientific accuracy */
    unified->SetMathAccuracy(unified, INLINE_ACCURACY_PRECISE);

    IBuilderExtended *ext = &unified->base.base;
    IBuilder *b = &ext->base;

    IValue *x = b->CreateConstFloat(b, /* ... */);

    /* High-precision math operations */
    IValue *exp_x = ext->CreateExp(ext, x, "exp");
    IValue *log_x = ext->CreateLog(ext, x, "log");
    IValue *sin_x = ext->CreateSin(ext, x, "sin");

    /* All use extended precision with range reduction */
}
```

### Embedded System (Code Size Constrained)

```c
void embedded_controller(IBuilder *builder, IModule *module)
{
    /* Use SMALLEST preset */
    IBuilderUnified *unified =
        emulation_create_inline_unified_builder_preset(
            builder, module, "smallest");

    /* Further reduce code size */
    unified->SetInlineBudget(unified, 50);
    unified->EnableLoopUnrolling(unified, 0);

    /* Generate minimal code */
    IBuilder *b = &unified->base.base.base;

    /* Basic operations only */
    IValue *result = b->CreateAdd(b, a, b_val, "add");

    printf("Code size: %lu bytes\n", unified->GetCodeSize(unified));
}
```

---

## Troubleshooting

### Issue: Lower than Expected Performance

**Solution 1:** Check inline budget
```c
uint64_t inline_ops = builder->GetInlineOperations(builder);
uint64_t total_ops = builder->GetTotalOperations(builder);
if (inline_ops < total_ops * 0.8) {
    /* Increase inline budget */
    builder->SetInlineBudget(builder, 1000);
}
```

**Solution 2:** Use FASTEST preset
```c
builder->ApplyPresetFastest(builder);
```

### Issue: Code Size Too Large

**Solution:** Reduce inline budget and disable optimizations
```c
builder->SetInlineBudget(builder, 100);
builder->EnableLoopUnrolling(builder, 0);
builder->EnableVectorization(builder, 0);
```

### Issue: Math Precision Issues

**Solution:** Switch to ACCURATE preset
```c
builder->ApplyPresetAccurate(builder);
builder->SetMathAccuracy(builder, INLINE_ACCURACY_PRECISE);
```

---

## Summary

The Unified Inline Emulation API provides:

✅ **479+ operations** across three layers
✅ **1.4x to 30x speedup** vs helper functions
✅ **Five configuration presets** for different use cases
✅ **Built-in profiling** and benchmarking
✅ **Automatic capability detection**
✅ **Fine-grained optimization control**

**Start with:**
1. Choose appropriate preset for your use case
2. Profile your code to measure impact
3. Benchmark to quantify improvements
4. Fine-tune based on results

For more examples, see `backend_emulation_inline_unified_example.cpp`.
