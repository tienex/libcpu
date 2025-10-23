/*
 * libcpu: static_recompiler.h
 *
 * Static/ahead-of-time recompilation support
 * Translates guest binaries to native executables
 */

#ifndef _STATIC_RECOMPILER_H_
#define _STATIC_RECOMPILER_H_

#include "libcpu.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Static recompilation output formats */
typedef enum {
	STATIC_OUTPUT_OBJECT,      /* Object file (.o) */
	STATIC_OUTPUT_SHARED,      /* Shared library (.so/.dylib) */
	STATIC_OUTPUT_EXECUTABLE,  /* Standalone executable */
	STATIC_OUTPUT_LLVM_IR,     /* LLVM IR (.ll) */
	STATIC_OUTPUT_LLVM_BC      /* LLVM Bitcode (.bc) */
} static_output_format_t;

/* Static recompilation options */
typedef struct static_recompile_options {
	/* Input */
	const char *input_file;           /* Guest binary input */
	addr_t entry_point;               /* Entry point address */
	addr_t code_start;                /* Code region start */
	addr_t code_end;                  /* Code region end */

	/* Output */
	const char *output_file;          /* Output file path */
	static_output_format_t format;    /* Output format */

	/* Compilation options */
	bool optimize;                    /* Enable optimizations */
	bool verbose;                     /* Verbose output */
	bool include_debug_info;          /* Include debug symbols */
	const char *target_triple;        /* LLVM target triple */

	/* Function extraction */
	bool extract_functions;           /* Extract as separate functions */
	addr_t *function_addrs;           /* Array of function addresses */
	size_t num_functions;             /* Number of functions */

	/* Runtime options */
	bool standalone;                  /* Generate standalone executable */
	bool include_runtime;             /* Include runtime support */
	const char *runtime_lib_path;     /* Path to runtime library */
} static_recompile_options_t;

/* Function descriptor for static compilation */
typedef struct static_function {
	addr_t guest_addr;                /* Guest address */
	const char *name;                 /* Function name */
	void *native_code;                /* Pointer to native code */
	size_t code_size;                 /* Size of native code */
	bool is_entry;                    /* Is entry point */
} static_function_t;

/* Static recompilation context */
typedef struct static_recompile_context {
	cpu_t *cpu;                       /* CPU context */
	static_recompile_options_t options; /* Options */

	/* Analysis results */
	static_function_t *functions;     /* Extracted functions */
	size_t num_functions;             /* Number of functions */

	/* Symbol table */
	struct {
		addr_t guest_addr;
		const char *symbol_name;
	} *symbols;
	size_t num_symbols;

	/* Statistics */
	size_t bytes_translated;          /* Bytes of guest code */
	size_t instructions_translated;   /* Number of instructions */
	size_t basic_blocks;              /* Number of basic blocks */

} static_recompile_context_t;

/* Initialize static recompilation options with defaults */
void static_recompile_options_init(static_recompile_options_t *opts);

/* Create static recompilation context */
static_recompile_context_t *static_recompile_create(cpu_t *cpu,
                                                     static_recompile_options_t *opts);

/* Free static recompilation context */
void static_recompile_free(static_recompile_context_t *ctx);

/* Analyze binary and discover functions */
int static_recompile_analyze(static_recompile_context_t *ctx);

/* Translate all discovered code to LLVM IR */
int static_recompile_translate(static_recompile_context_t *ctx);

/* Optimize translated code */
int static_recompile_optimize(static_recompile_context_t *ctx);

/* Generate output file */
int static_recompile_generate(static_recompile_context_t *ctx);

/* Complete static recompilation pipeline (all-in-one) */
int static_recompile(cpu_t *cpu, static_recompile_options_t *opts);

/* Extract single function */
int static_recompile_function(static_recompile_context_t *ctx,
                               addr_t func_addr,
                               const char *func_name);

/* Add symbol to symbol table */
void static_recompile_add_symbol(static_recompile_context_t *ctx,
                                 addr_t guest_addr,
                                 const char *symbol_name);

/* Lookup symbol by guest address */
const char *static_recompile_lookup_symbol(static_recompile_context_t *ctx,
                                           addr_t guest_addr);

/* Print statistics */
void static_recompile_print_stats(static_recompile_context_t *ctx);

/* Generate standalone executable wrapper */
int static_recompile_generate_standalone(static_recompile_context_t *ctx,
                                         const char *output_path);

/* Generate shared library */
int static_recompile_generate_shared_lib(static_recompile_context_t *ctx,
                                         const char *output_path);

/* Generate object file */
int static_recompile_generate_object(static_recompile_context_t *ctx,
                                     const char *output_path);

/* Generate LLVM IR */
int static_recompile_generate_ir(static_recompile_context_t *ctx,
                                 const char *output_path);

/* Generate LLVM bitcode */
int static_recompile_generate_bitcode(static_recompile_context_t *ctx,
                                      const char *output_path);

/* Helper: Load binary file into memory */
int static_recompile_load_binary(const char *path,
                                 uint8_t **data_out,
                                 size_t *size_out);

/* Helper: Detect binary format (ELF, PE, Mach-O, raw) */
typedef enum {
	BINARY_FORMAT_UNKNOWN,
	BINARY_FORMAT_RAW,
	BINARY_FORMAT_ELF,
	BINARY_FORMAT_PE,
	BINARY_FORMAT_MACHO
} binary_format_t;

binary_format_t static_recompile_detect_format(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* _STATIC_RECOMPILER_H_ */
