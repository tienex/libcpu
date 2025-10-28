/*
 * libcpu Unified Inline Emulation API
 *
 * Single unified API that combines all inline emulation layers:
 * - Base inline emulation (54 IBuilder operations)
 * - Extended operations (25 advanced math/bit operations)
 * - Comprehensive operations (400+ specialized operations)
 *
 * Plus additional features:
 * - Performance profiling and monitoring
 * - Backend capability auto-detection
 * - Optimization control
 * - Benchmarking framework
 * - Configuration presets
 */

#ifndef BACKEND_EMULATION_INLINE_UNIFIED_H
#define BACKEND_EMULATION_INLINE_UNIFIED_H

#include "backend.h"
#include "backend_emulation_inline.h"
#include "backend_emulation_inline_extended.h"
#include "backend_emulation_inline_comprehensive.h"

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************
 * Unified Builder Interface
 ***************************************************************************/

typedef struct IBuilderUnified IBuilderUnified;

struct IBuilderUnified {
	IBuilderComprehensive base;  /* All 479+ operations from all layers */

	/*************************************************************************
	 * Performance Monitoring and Profiling
	 *************************************************************************/

	/* Start/stop profiling to measure operation performance */
	void (*StartProfiling)(IBuilderUnified *self);
	void (*StopProfiling)(IBuilderUnified *self);
	void (*ResetProfile)(IBuilderUnified *self);

	/* Print profiling report to stdout */
	void (*PrintProfile)(IBuilderUnified *self);

	/* Export profiling data to JSON file */
	void (*ExportProfileJSON)(IBuilderUnified *self, const char *filename);

	/* Get specific profiling metrics */
	uint64_t (*GetTotalOperations)(IBuilderUnified *self);
	uint64_t (*GetInlineOperations)(IBuilderUnified *self);
	uint64_t (*GetNativeOperations)(IBuilderUnified *self);
	uint64_t (*GetFallbackOperations)(IBuilderUnified *self);

	/* Get estimated speedup vs helper functions */
	double (*GetSpeedup)(IBuilderUnified *self);

	/*************************************************************************
	 * Backend Capability Detection
	 *************************************************************************/

	/* Query backend for native instruction support */
	int (*HasNativeDivision)(IBuilderUnified *self);
	int (*HasNativeRemainder)(IBuilderUnified *self);
	int (*HasNativeSqrt)(IBuilderUnified *self);
	int (*HasNativeFMA)(IBuilderUnified *self);
	int (*HasNativeMinMax)(IBuilderUnified *self);
	int (*HasNativePOPCNT)(IBuilderUnified *self);
	int (*HasNativeCLZ)(IBuilderUnified *self);
	int (*HasNativeCTZ)(IBuilderUnified *self);
	int (*HasNativeBSWAP)(IBuilderUnified *self);
	int (*HasNativeAbs)(IBuilderUnified *self);
	int (*HasNativeVectorOps)(IBuilderUnified *self);
	int (*HasNativeAtomics)(IBuilderUnified *self);
	int (*HasNativeAESNI)(IBuilderUnified *self);
	int (*HasNativeSHA)(IBuilderUnified *self);

	/* Get vector width supported by backend (0 = none, 128, 256, 512) */
	int (*GetVectorWidth)(IBuilderUnified *self);

	/* Get all capabilities as bitmask */
	uint64_t (*GetCapabilities)(IBuilderUnified *self);

	/*************************************************************************
	 * Optimization Control
	 *************************************************************************/

	/* Enable/disable specific optimizations */
	void (*EnableConstantFolding)(IBuilderUnified *self, int enable);
	void (*EnableCommonSubexprElim)(IBuilderUnified *self, int enable);
	void (*EnableLoopUnrolling)(IBuilderUnified *self, int enable);
	void (*EnableVectorization)(IBuilderUnified *self, int enable);

	/* Set optimization level (0=none, 1=basic, 2=aggressive, 3=maximum) */
	void (*SetOptimizationLevel)(IBuilderUnified *self, uint32_t level);

	/* Control inline expansion budget */
	void (*SetInlineBudget)(IBuilderUnified *self, uint32_t max_instructions);

	/* Set accuracy level for math operations */
	void (*SetMathAccuracy)(IBuilderUnified *self, inline_accuracy_t accuracy);

	/*************************************************************************
	 * Statistics and Reporting
	 *************************************************************************/

	/* Get detailed statistics about code generation */
	emulation_stats_t (*GetStatistics)(IBuilderUnified *self);

	/* Print statistics report */
	void (*PrintStatistics)(IBuilderUnified *self);

	/* Get estimated code size in bytes */
	uint64_t (*GetCodeSize)(IBuilderUnified *self);

	/* Get instruction count */
	uint64_t (*GetInstructionCount)(IBuilderUnified *self);

	/*************************************************************************
	 * Benchmarking Support
	 *************************************************************************/

	/* Run benchmarks for different operation categories */
	void (*BenchmarkArithmetic)(IBuilderUnified *self);
	void (*BenchmarkMath)(IBuilderUnified *self);
	void (*BenchmarkBitManip)(IBuilderUnified *self);
	void (*BenchmarkVector)(IBuilderUnified *self);
	void (*BenchmarkAtomic)(IBuilderUnified *self);
	void (*BenchmarkCrypto)(IBuilderUnified *self);
	void (*BenchmarkMemory)(IBuilderUnified *self);
	void (*BenchmarkString)(IBuilderUnified *self);

	/* Run all benchmarks */
	void (*BenchmarkAll)(IBuilderUnified *self);

	/* Print benchmark results */
	void (*PrintBenchmarkResults)(IBuilderUnified *self);

	/* Export benchmark results to JSON */
	void (*ExportBenchmarkJSON)(IBuilderUnified *self, const char *filename);

	/*************************************************************************
	 * Configuration Presets
	 *************************************************************************/

	/* Apply predefined configuration presets */
	void (*ApplyPresetFastest)(IBuilderUnified *self);     /* Maximum performance */
	void (*ApplyPresetBalanced)(IBuilderUnified *self);    /* Balance speed/size */
	void (*ApplyPresetSmallest)(IBuilderUnified *self);    /* Minimum code size */
	void (*ApplyPresetAccurate)(IBuilderUnified *self);    /* Maximum accuracy */
	void (*ApplyPresetLowPower)(IBuilderUnified *self);    /* Energy efficient */

	/*************************************************************************
	 * Advanced Features
	 *************************************************************************/

	/* Generate optimization hints for backend */
	void (*AddOptimizationHint)(IBuilderUnified *self, const char *hint);

	/* Set target CPU features (e.g., "avx2", "neon", "sve") */
	void (*SetTargetFeatures)(IBuilderUnified *self, const char *features);

	/* Enable/disable specific instruction patterns */
	void (*EnablePattern)(IBuilderUnified *self, const char *pattern, int enable);

	/* Set code generation strategy for operation */
	void (*SetStrategy)(IBuilderUnified *self, const char *operation, inline_strategy_t strategy);

	/*************************************************************************
	 * Internal (do not access directly)
	 *************************************************************************/

	/* Internal implementation pointer */
	void *internal_impl;
};

/***************************************************************************
 * Unified Builder Creation
 ***************************************************************************/

/**
 * Create unified inline emulation builder with all features.
 *
 * This is the recommended way to create an inline emulation builder - it provides
 * all operations from all layers plus profiling and optimization features.
 *
 * @param wrapped_builder The backend builder to wrap
 * @param wrapped_module The backend module
 * @param auto_detect If 1, automatically detect backend capabilities
 * @return Unified builder with all 479+ operations
 */
IBuilderUnified* emulation_create_inline_unified_builder(IBuilder *wrapped_builder,
                                                          IModule *wrapped_module,
                                                          int auto_detect);

/**
 * Create unified builder with specific configuration preset.
 *
 * @param wrapped_builder The backend builder to wrap
 * @param wrapped_module The backend module
 * @param preset One of: "fastest", "balanced", "smallest", "accurate", "lowpower"
 * @return Configured unified builder
 */
IBuilderUnified* emulation_create_inline_unified_builder_preset(IBuilder *wrapped_builder,
                                                                 IModule *wrapped_module,
                                                                 const char *preset);

/***************************************************************************
 * Capability Detection
 ***************************************************************************/

/* Capability flags for GetCapabilities() */
typedef enum {
	INLINE_UNIFIED_CAP_DIVISION     = (1ULL << 0),
	INLINE_UNIFIED_CAP_REMAINDER    = (1ULL << 1),
	INLINE_UNIFIED_CAP_SQRT         = (1ULL << 2),
	INLINE_UNIFIED_CAP_FMA          = (1ULL << 3),
	INLINE_UNIFIED_CAP_MINMAX       = (1ULL << 4),
	INLINE_UNIFIED_CAP_POPCNT       = (1ULL << 5),
	INLINE_UNIFIED_CAP_CLZ          = (1ULL << 6),
	INLINE_UNIFIED_CAP_CTZ          = (1ULL << 7),
	INLINE_UNIFIED_CAP_BSWAP        = (1ULL << 8),
	INLINE_UNIFIED_CAP_ABS          = (1ULL << 9),
	INLINE_UNIFIED_CAP_VECTOR_128   = (1ULL << 10),
	INLINE_UNIFIED_CAP_VECTOR_256   = (1ULL << 11),
	INLINE_UNIFIED_CAP_VECTOR_512   = (1ULL << 12),
	INLINE_UNIFIED_CAP_ATOMICS      = (1ULL << 13),
	INLINE_UNIFIED_CAP_AESNI        = (1ULL << 14),
	INLINE_UNIFIED_CAP_SHA          = (1ULL << 15),
	INLINE_UNIFIED_CAP_CRC32        = (1ULL << 16),
	INLINE_UNIFIED_CAP_BMI1         = (1ULL << 17),
	INLINE_UNIFIED_CAP_BMI2         = (1ULL << 18),
	INLINE_UNIFIED_CAP_AVX          = (1ULL << 19),
	INLINE_UNIFIED_CAP_AVX2         = (1ULL << 20),
	INLINE_UNIFIED_CAP_AVX512       = (1ULL << 21),
	INLINE_UNIFIED_CAP_NEON         = (1ULL << 22),
	INLINE_UNIFIED_CAP_SVE          = (1ULL << 23),
} inline_unified_capability_t;

/**
 * Auto-detect backend capabilities.
 *
 * @param backend The backend to query
 * @return Bitmask of INLINE_UNIFIED_CAP_* flags
 */
uint64_t emulation_inline_detect_capabilities(IBackend *backend);

/**
 * Get capability name from flag.
 *
 * @param cap Capability flag
 * @return Human-readable name
 */
const char* emulation_inline_capability_name(inline_unified_capability_t cap);

/**
 * Print all detected capabilities.
 *
 * @param capabilities Bitmask from GetCapabilities()
 */
void emulation_inline_print_capabilities(uint64_t capabilities);

/***************************************************************************
 * Benchmarking
 ***************************************************************************/

typedef struct {
	const char *operation_name;
	const char *category;
	uint64_t iterations;
	uint64_t total_cycles;
	uint64_t cycles_per_op;
	uint64_t instructions_generated;
	double speedup_vs_helper;
	const char *strategy;  /* "native", "inline", "fallback" */
} inline_benchmark_result_t;

typedef struct {
	uint32_t result_count;
	inline_benchmark_result_t *results;
	uint64_t total_cycles;
	double average_speedup;
} inline_benchmark_report_t;

/**
 * Run comprehensive benchmark suite.
 *
 * @param builder The unified builder
 * @param iterations Number of iterations per operation
 * @return Benchmark report (caller must free with emulation_inline_free_benchmark_report)
 */
inline_benchmark_report_t* emulation_inline_run_benchmarks(IBuilderUnified *builder, uint64_t iterations);

/**
 * Free benchmark report.
 *
 * @param report Report to free
 */
void emulation_inline_free_benchmark_report(inline_benchmark_report_t *report);

/**
 * Print benchmark report.
 *
 * @param report Benchmark report
 */
void emulation_inline_print_benchmark_report(inline_benchmark_report_t *report);

/**
 * Export benchmark report to JSON.
 *
 * @param report Benchmark report
 * @param filename Output file
 */
void emulation_inline_export_benchmark_json(inline_benchmark_report_t *report, const char *filename);

/***************************************************************************
 * Profiling
 ***************************************************************************/

typedef struct {
	const char *operation_name;
	uint64_t call_count;
	uint64_t total_instructions;
	uint64_t avg_instructions;
	uint64_t min_instructions;
	uint64_t max_instructions;
	uint64_t native_count;
	uint64_t inline_count;
	uint64_t fallback_count;
	double percentage;
} inline_profile_entry_t;

typedef struct {
	uint32_t entry_count;
	inline_profile_entry_t *entries;
	uint64_t total_operations;
	uint64_t total_instructions;
	double total_time_seconds;
} inline_profile_report_t;

/**
 * Generate profiling report.
 *
 * @param builder The unified builder
 * @return Profile report (caller must free with emulation_inline_free_profile_report)
 */
inline_profile_report_t* emulation_inline_generate_profile_report(IBuilderUnified *builder);

/**
 * Free profile report.
 *
 * @param report Report to free
 */
void emulation_inline_free_profile_report(inline_profile_report_t *report);

/**
 * Print profile report.
 *
 * @param report Profile report
 */
void emulation_inline_print_profile_report(inline_profile_report_t *report);

/**
 * Export profile report to JSON.
 *
 * @param report Profile report
 * @param filename Output file
 */
void emulation_inline_export_profile_json(inline_profile_report_t *report, const char *filename);

/***************************************************************************
 * Configuration Presets
 ***************************************************************************/

typedef struct {
	inline_accuracy_t math_accuracy;
	uint32_t optimization_level;
	uint32_t inline_budget;
	int enable_constant_folding;
	int enable_cse;
	int enable_loop_unrolling;
	int enable_vectorization;
	int enable_range_reduction;
} inline_emulation_config_t;

/* Predefined configuration presets */
extern const inline_emulation_config_t INLINE_EMULATION_PRESET_FASTEST;
extern const inline_emulation_config_t INLINE_EMULATION_PRESET_BALANCED;
extern const inline_emulation_config_t INLINE_EMULATION_PRESET_SMALLEST;
extern const inline_emulation_config_t INLINE_EMULATION_PRESET_ACCURATE;
extern const inline_emulation_config_t INLINE_EMULATION_PRESET_LOWPOWER;

/**
 * Apply configuration to builder.
 *
 * @param builder The unified builder
 * @param config Configuration to apply
 */
void emulation_inline_apply_config(IBuilderUnified *builder, const inline_emulation_config_t *config);

/**
 * Get current configuration.
 *
 * @param builder The unified builder
 * @return Current configuration
 */
inline_emulation_config_t emulation_inline_get_config(IBuilderUnified *builder);

/***************************************************************************
 * Utility Functions
 ***************************************************************************/

/**
 * Print summary of all available operations across all inline emulation layers.
 */
void emulation_inline_print_operation_summary(void);

/**
 * Get total number of available operations.
 *
 * @return Number of operations (base + extended + comprehensive)
 */
uint32_t emulation_inline_get_operation_count(void);

/**
 * Check if operation is available.
 *
 * @param operation_name Name of operation (e.g., "CreateSqrt")
 * @return 1 if available, 0 otherwise
 */
int emulation_inline_has_operation(const char *operation_name);

/**
 * Get operation category.
 *
 * @param operation_name Name of operation
 * @return Category name (e.g., "Math", "Vector", "Atomic")
 */
const char* emulation_inline_get_operation_category(const char *operation_name);

/**
 * Print help for specific operation.
 *
 * @param operation_name Name of operation
 */
void emulation_inline_print_operation_help(const char *operation_name);

/***************************************************************************
 * Version Information
 ***************************************************************************/

#define INLINE_EMULATION_VERSION_MAJOR 1
#define INLINE_EMULATION_VERSION_MINOR 0
#define INLINE_EMULATION_VERSION_PATCH 0

typedef struct {
	uint32_t major;
	uint32_t minor;
	uint32_t patch;
	const char *version_string;
	const char *build_date;
	const char *features;  /* Comma-separated feature list */
} inline_emulation_version_info_t;

/**
 * Get version information.
 *
 * @return Version info structure
 */
inline_emulation_version_info_t emulation_inline_get_version(void);

/**
 * Print version information.
 */
void emulation_inline_print_version(void);

/***************************************************************************
 * Error Handling
 ***************************************************************************/

typedef enum {
	INLINE_EMULATION_ERROR_NONE = 0,
	INLINE_EMULATION_ERROR_NULL_POINTER,
	INLINE_EMULATION_ERROR_INVALID_OPERATION,
	INLINE_EMULATION_ERROR_UNSUPPORTED_TYPE,
	INLINE_EMULATION_ERROR_BACKEND_FAILURE,
	INLINE_EMULATION_ERROR_OUT_OF_MEMORY,
	INLINE_EMULATION_ERROR_INVALID_CONFIG,
	INLINE_EMULATION_ERROR_FILE_IO,
} inline_emulation_error_t;

/**
 * Get last error code.
 *
 * @return Error code
 */
inline_emulation_error_t emulation_inline_get_last_error(void);

/**
 * Get last error message.
 *
 * @return Error message string
 */
const char* emulation_inline_get_last_error_message(void);

/**
 * Clear last error.
 */
void emulation_inline_clear_error(void);

#ifdef __cplusplus
}
#endif

#endif /* BACKEND_EMULATION_INLINE_UNIFIED_H */
