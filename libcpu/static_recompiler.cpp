/*
 * libcpu: static_recompiler.cpp
 *
 * Static/ahead-of-time recompilation implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "llvm/IR/Module.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/MC/TargetRegistry.h"

#include "libcpu.h"
#include "libcpu_llvm.h"
#include "static_recompiler.h"
#include "tag.h"
#include "translate_all.h"
#include "optimize.h"

/* Initialize options with defaults */
void
static_recompile_options_init(static_recompile_options_t *opts)
{
	memset(opts, 0, sizeof(static_recompile_options_t));

	opts->format = STATIC_OUTPUT_OBJECT;
	opts->optimize = true;
	opts->verbose = false;
	opts->include_debug_info = false;
	opts->target_triple = NULL;  /* Use host triple by default */
	opts->extract_functions = false;
	opts->standalone = false;
	opts->include_runtime = true;
}

/* Create static recompilation context */
static_recompile_context_t *
static_recompile_create(cpu_t *cpu, static_recompile_options_t *opts)
{
	static_recompile_context_t *ctx;

	ctx = (static_recompile_context_t *)calloc(1, sizeof(static_recompile_context_t));
	if (!ctx) return NULL;

	ctx->cpu = cpu;
	ctx->options = *opts;

	/* Initialize statistics */
	ctx->bytes_translated = 0;
	ctx->instructions_translated = 0;
	ctx->basic_blocks = 0;

	LOG("Static recompiler context created\n");
	return ctx;
}

/* Free static recompilation context */
void
static_recompile_free(static_recompile_context_t *ctx)
{
	if (!ctx) return;

	/* Free functions */
	if (ctx->functions) {
		for (size_t i = 0; i < ctx->num_functions; i++) {
			if (ctx->functions[i].name) {
				free((void *)ctx->functions[i].name);
			}
		}
		free(ctx->functions);
	}

	/* Free symbols */
	if (ctx->symbols) {
		for (size_t i = 0; i < ctx->num_symbols; i++) {
			if (ctx->symbols[i].symbol_name) {
				free((void *)ctx->symbols[i].symbol_name);
			}
		}
		free(ctx->symbols);
	}

	free(ctx);
	LOG("Static recompiler context freed\n");
}

/* Analyze binary and discover functions */
int
static_recompile_analyze(static_recompile_context_t *ctx)
{
	cpu_t *cpu = ctx->cpu;

	if (ctx->options.verbose) {
		printf("Analyzing binary...\n");
		printf("  Code region: 0x%llx - 0x%llx\n",
		       (unsigned long long)ctx->options.code_start,
		       (unsigned long long)ctx->options.code_end);
	}

	/* Set code region */
	cpu->code_start = ctx->options.code_start;
	cpu->code_end = ctx->options.code_end;
	cpu->code_entry = ctx->options.entry_point;

	/* Tag code starting from entry point */
	tag_start(cpu, ctx->options.entry_point);

	/* If specific functions provided, tag those as well */
	if (ctx->options.extract_functions && ctx->options.function_addrs) {
		for (size_t i = 0; i < ctx->options.num_functions; i++) {
			addr_t func_addr = ctx->options.function_addrs[i];
			tag_start(cpu, func_addr);

			/* Add to symbol table */
			char func_name[64];
			snprintf(func_name, sizeof(func_name), "func_%llx",
			         (unsigned long long)func_addr);
			static_recompile_add_symbol(ctx, func_addr, func_name);
		}
	}

	/* Count basic blocks for statistics */
	ctx->basic_blocks = 0;
	for (addr_t pc = cpu->code_start; pc < cpu->code_end; pc++) {
		if (is_start_of_basicblock(cpu, pc)) {
			ctx->basic_blocks++;
		}
	}

	if (ctx->options.verbose) {
		printf("  Discovered %zu basic blocks\n", ctx->basic_blocks);
		printf("  Tagged %zu bytes of code\n",
		       (size_t)(cpu->code_end - cpu->code_start));
	}

	return 0;
}

/* Translate all discovered code to LLVM IR */
int
static_recompile_translate(static_recompile_context_t *ctx)
{
	cpu_t *cpu = ctx->cpu;

	if (ctx->options.verbose) {
		printf("Translating to LLVM IR...\n");
	}

	/* Create main translation function */
	BasicBlock *bb_ret, *bb_trap, *label_entry;
	cpu->cur_func = cpu_create_function(cpu, "guest_main", &bb_ret, &bb_trap, &label_entry);

	/* Translate all reachable code */
	BasicBlock *bb_start = cpu_translate_all(cpu, bb_ret, bb_trap);

	/* Finish entry basic block */
	BranchInst::Create(bb_start, label_entry);

	/* Verify the function */
	if (verifyFunction(*cpu->cur_func, &llvm::errs())) {
		fprintf(stderr, "Error: Function verification failed\n");
		return -1;
	}

	if (ctx->options.verbose) {
		printf("  Translation complete\n");
		printf("  Function: %s\n", cpu->cur_func->getName().str().c_str());
	}

	/* Update statistics */
	ctx->bytes_translated = cpu->code_end - cpu->code_start;
	ctx->instructions_translated = ctx->basic_blocks; /* Approximation */

	return 0;
}

/* Optimize translated code */
int
static_recompile_optimize(static_recompile_context_t *ctx)
{
	if (!ctx->options.optimize) {
		if (ctx->options.verbose) {
			printf("Optimization disabled\n");
		}
		return 0;
	}

	if (ctx->options.verbose) {
		printf("Optimizing...\n");
	}

	optimize(ctx->cpu);

	if (ctx->options.verbose) {
		printf("  Optimization complete\n");
	}

	return 0;
}

/* Generate LLVM IR output */
int
static_recompile_generate_ir(static_recompile_context_t *ctx, const char *output_path)
{
	std::error_code ec;
	llvm::raw_fd_ostream out(output_path, ec, llvm::sys::fs::OF_None);

	if (ec) {
		fprintf(stderr, "Error: Could not open output file: %s\n",
		        ec.message().c_str());
		return -1;
	}

	ctx->cpu->mod->print(out, nullptr);
	out.flush();

	if (ctx->options.verbose) {
		printf("LLVM IR written to: %s\n", output_path);
	}

	return 0;
}

/* Generate LLVM bitcode output */
int
static_recompile_generate_bitcode(static_recompile_context_t *ctx, const char *output_path)
{
	std::error_code ec;
	llvm::raw_fd_ostream out(output_path, ec, llvm::sys::fs::OF_None);

	if (ec) {
		fprintf(stderr, "Error: Could not open output file: %s\n",
		        ec.message().c_str());
		return -1;
	}

	llvm::WriteBitcodeToFile(*ctx->cpu->mod, out);
	out.flush();

	if (ctx->options.verbose) {
		printf("LLVM bitcode written to: %s\n", output_path);
	}

	return 0;
}

/* Generate object file */
int
static_recompile_generate_object(static_recompile_context_t *ctx, const char *output_path)
{
	llvm::InitializeAllTargetInfos();
	llvm::InitializeAllTargets();
	llvm::InitializeAllTargetMCs();
	llvm::InitializeAllAsmParsers();
	llvm::InitializeAllAsmPrinters();

	/* Get target triple */
	std::string target_triple;
	if (ctx->options.target_triple) {
		target_triple = ctx->options.target_triple;
	} else {
		target_triple = llvm::sys::getDefaultTargetTriple();
	}

	ctx->cpu->mod->setTargetTriple(target_triple);

	/* Lookup target */
	std::string error;
	auto target = llvm::TargetRegistry::lookupTarget(target_triple, error);

	if (!target) {
		fprintf(stderr, "Error: %s\n", error.c_str());
		return -1;
	}

	/* Create target machine */
	auto cpu_name = "generic";
	auto features = "";

	llvm::TargetOptions opt;
	auto RM = llvm::Optional<llvm::Reloc::Model>();
	auto target_machine = target->createTargetMachine(
	    target_triple, cpu_name, features, opt, RM);

	ctx->cpu->mod->setDataLayout(target_machine->createDataLayout());

	/* Open output file */
	std::error_code ec;
	llvm::raw_fd_ostream dest(output_path, ec, llvm::sys::fs::OF_None);

	if (ec) {
		fprintf(stderr, "Error: Could not open file: %s\n", ec.message().c_str());
		return -1;
	}

	/* Emit object file */
	llvm::legacy::PassManager pass;
	auto file_type = llvm::CGFT_ObjectFile;

	if (target_machine->addPassesToEmitFile(pass, dest, nullptr, file_type)) {
		fprintf(stderr, "Error: TargetMachine can't emit object file\n");
		return -1;
	}

	pass.run(*ctx->cpu->mod);
	dest.flush();

	if (ctx->options.verbose) {
		printf("Object file written to: %s\n", output_path);
	}

	return 0;
}

/* Generate shared library */
int
static_recompile_generate_shared_lib(static_recompile_context_t *ctx, const char *output_path)
{
	/* First generate object file */
	char obj_path[1024];
	snprintf(obj_path, sizeof(obj_path), "%s.o", output_path);

	if (static_recompile_generate_object(ctx, obj_path) != 0) {
		return -1;
	}

	/* Link into shared library using system linker */
	char cmd[2048];
	snprintf(cmd, sizeof(cmd), "gcc -shared -o %s %s", output_path, obj_path);

	if (ctx->options.verbose) {
		printf("Linking: %s\n", cmd);
	}

	int ret = system(cmd);
	if (ret != 0) {
		fprintf(stderr, "Error: Linking failed\n");
		return -1;
	}

	/* Clean up temporary object file */
	unlink(obj_path);

	if (ctx->options.verbose) {
		printf("Shared library written to: %s\n", output_path);
	}

	return 0;
}

/* Generate standalone executable */
int
static_recompile_generate_standalone(static_recompile_context_t *ctx, const char *output_path)
{
	/* First generate object file */
	char obj_path[1024];
	snprintf(obj_path, sizeof(obj_path), "%s.o", output_path);

	if (static_recompile_generate_object(ctx, obj_path) != 0) {
		return -1;
	}

	/* Link into executable using system linker */
	char cmd[2048];

	if (ctx->options.include_runtime && ctx->options.runtime_lib_path) {
		snprintf(cmd, sizeof(cmd), "gcc -o %s %s %s",
		         output_path, obj_path, ctx->options.runtime_lib_path);
	} else {
		snprintf(cmd, sizeof(cmd), "gcc -o %s %s",
		         output_path, obj_path);
	}

	if (ctx->options.verbose) {
		printf("Linking: %s\n", cmd);
	}

	int ret = system(cmd);
	if (ret != 0) {
		fprintf(stderr, "Error: Linking failed\n");
		return -1;
	}

	/* Clean up temporary object file */
	unlink(obj_path);

	if (ctx->options.verbose) {
		printf("Executable written to: %s\n", output_path);
	}

	return 0;
}

/* Generate output file based on format */
int
static_recompile_generate(static_recompile_context_t *ctx)
{
	const char *output_path = ctx->options.output_file;

	if (!output_path) {
		fprintf(stderr, "Error: No output file specified\n");
		return -1;
	}

	switch (ctx->options.format) {
		case STATIC_OUTPUT_LLVM_IR:
			return static_recompile_generate_ir(ctx, output_path);

		case STATIC_OUTPUT_LLVM_BC:
			return static_recompile_generate_bitcode(ctx, output_path);

		case STATIC_OUTPUT_OBJECT:
			return static_recompile_generate_object(ctx, output_path);

		case STATIC_OUTPUT_SHARED:
			return static_recompile_generate_shared_lib(ctx, output_path);

		case STATIC_OUTPUT_EXECUTABLE:
			return static_recompile_generate_standalone(ctx, output_path);

		default:
			fprintf(stderr, "Error: Unknown output format\n");
			return -1;
	}
}

/* Complete static recompilation pipeline */
int
static_recompile(cpu_t *cpu, static_recompile_options_t *opts)
{
	static_recompile_context_t *ctx;
	int ret = 0;

	/* Create context */
	ctx = static_recompile_create(cpu, opts);
	if (!ctx) {
		fprintf(stderr, "Error: Failed to create context\n");
		return -1;
	}

	/* Analyze */
	if (static_recompile_analyze(ctx) != 0) {
		fprintf(stderr, "Error: Analysis failed\n");
		ret = -1;
		goto cleanup;
	}

	/* Translate */
	if (static_recompile_translate(ctx) != 0) {
		fprintf(stderr, "Error: Translation failed\n");
		ret = -1;
		goto cleanup;
	}

	/* Optimize */
	if (static_recompile_optimize(ctx) != 0) {
		fprintf(stderr, "Error: Optimization failed\n");
		ret = -1;
		goto cleanup;
	}

	/* Generate output */
	if (static_recompile_generate(ctx) != 0) {
		fprintf(stderr, "Error: Code generation failed\n");
		ret = -1;
		goto cleanup;
	}

	/* Print statistics */
	if (opts->verbose) {
		static_recompile_print_stats(ctx);
	}

cleanup:
	static_recompile_free(ctx);
	return ret;
}

/* Add symbol to symbol table */
void
static_recompile_add_symbol(static_recompile_context_t *ctx,
                            addr_t guest_addr,
                            const char *symbol_name)
{
	/* Reallocate symbol table */
	size_t new_size = (ctx->num_symbols + 1) * sizeof(*ctx->symbols);
	ctx->symbols = (typeof(ctx->symbols))realloc(ctx->symbols, new_size);

	/* Add new symbol */
	ctx->symbols[ctx->num_symbols].guest_addr = guest_addr;
	ctx->symbols[ctx->num_symbols].symbol_name = strdup(symbol_name);
	ctx->num_symbols++;
}

/* Lookup symbol by guest address */
const char *
static_recompile_lookup_symbol(static_recompile_context_t *ctx, addr_t guest_addr)
{
	for (size_t i = 0; i < ctx->num_symbols; i++) {
		if (ctx->symbols[i].guest_addr == guest_addr) {
			return ctx->symbols[i].symbol_name;
		}
	}
	return NULL;
}

/* Print statistics */
void
static_recompile_print_stats(static_recompile_context_t *ctx)
{
	printf("\nStatic Recompilation Statistics:\n");
	printf("  Bytes translated:        %zu\n", ctx->bytes_translated);
	printf("  Instructions translated: %zu (approx)\n", ctx->instructions_translated);
	printf("  Basic blocks:            %zu\n", ctx->basic_blocks);
	printf("  Symbols defined:         %zu\n", ctx->num_symbols);
	printf("  Functions extracted:     %zu\n", ctx->num_functions);
}

/* Load binary file into memory */
int
static_recompile_load_binary(const char *path, uint8_t **data_out, size_t *size_out)
{
	FILE *fp = fopen(path, "rb");
	if (!fp) {
		fprintf(stderr, "Error: Could not open file: %s\n", path);
		return -1;
	}

	/* Get file size */
	fseek(fp, 0, SEEK_END);
	size_t size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	/* Allocate buffer */
	uint8_t *data = (uint8_t *)malloc(size);
	if (!data) {
		fclose(fp);
		fprintf(stderr, "Error: Out of memory\n");
		return -1;
	}

	/* Read file */
	if (fread(data, 1, size, fp) != size) {
		free(data);
		fclose(fp);
		fprintf(stderr, "Error: Failed to read file\n");
		return -1;
	}

	fclose(fp);

	*data_out = data;
	*size_out = size;
	return 0;
}

/* Detect binary format */
binary_format_t
static_recompile_detect_format(const uint8_t *data, size_t size)
{
	if (size < 4) {
		return BINARY_FORMAT_UNKNOWN;
	}

	/* ELF magic: 0x7f 'E' 'L' 'F' */
	if (data[0] == 0x7f && data[1] == 'E' && data[2] == 'L' && data[3] == 'F') {
		return BINARY_FORMAT_ELF;
	}

	/* PE magic: 'M' 'Z' */
	if (data[0] == 'M' && data[1] == 'Z') {
		return BINARY_FORMAT_PE;
	}

	/* Mach-O magic: 0xfeedface, 0xfeedfacf, 0xcefaedfe, 0xcffaedfe */
	uint32_t magic = *(uint32_t *)data;
	if (magic == 0xfeedface || magic == 0xfeedfacf ||
	    magic == 0xcefaedfe || magic == 0xcffaedfe) {
		return BINARY_FORMAT_MACHO;
	}

	/* Default to raw binary */
	return BINARY_FORMAT_RAW;
}
