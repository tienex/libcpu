/*
 * static-recompile: Command-line tool for static recompilation
 *
 * Translates guest binaries to native code ahead-of-time
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "libcpu.h"
#include "static_recompiler.h"

static void
print_usage(const char *prog_name)
{
	printf("Usage: %s [OPTIONS] -i INPUT -o OUTPUT\n", prog_name);
	printf("\n");
	printf("Static recompilation tool for libcpu\n");
	printf("\n");
	printf("Required arguments:\n");
	printf("  -i, --input FILE          Input guest binary\n");
	printf("  -o, --output FILE         Output file\n");
	printf("  -a, --arch ARCH           Target architecture (arm, mips, m68k, etc.)\n");
	printf("\n");
	printf("Optional arguments:\n");
	printf("  -f, --format FORMAT       Output format (object, shared, executable, ir, bc)\n");
	printf("                            Default: object\n");
	printf("  -e, --entry ADDR          Entry point address (hex)\n");
	printf("  -s, --start ADDR          Code region start address (hex)\n");
	printf("  -E, --end ADDR            Code region end address (hex)\n");
	printf("  -O, --optimize            Enable optimizations (default)\n");
	printf("  -O0                       Disable optimizations\n");
	printf("  -v, --verbose             Verbose output\n");
	printf("  -g, --debug               Include debug information\n");
	printf("  --target TARGET           LLVM target triple\n");
	printf("  --standalone              Generate standalone executable\n");
	printf("  --runtime LIB             Path to runtime library\n");
	printf("  --endian big|little       Endianness (default: little)\n");
	printf("  -h, --help                Show this help\n");
	printf("\n");
	printf("Architectures:\n");
	printf("  6502      MOS 6502\n");
	printf("  m68k      Motorola 68000\n");
	printf("  mips      MIPS\n");
	printf("  m88k      Motorola 88000\n");
	printf("  arm       ARM\n");
	printf("  x86       x86 (8086)\n");
	printf("\n");
	printf("Output formats:\n");
	printf("  object      Object file (.o)\n");
	printf("  shared      Shared library (.so)\n");
	printf("  executable  Standalone executable\n");
	printf("  ir          LLVM IR (.ll)\n");
	printf("  bc          LLVM Bitcode (.bc)\n");
	printf("\n");
	printf("Examples:\n");
	printf("  # Compile ARM binary to object file\n");
	printf("  %s -i game.bin -o game.o -a arm -e 0x8000 -s 0x8000 -E 0x10000\n", prog_name);
	printf("\n");
	printf("  # Create standalone executable from MIPS binary\n");
	printf("  %s -i prog.bin -o prog -a mips -f executable --standalone\n", prog_name);
	printf("\n");
	printf("  # Generate LLVM IR for analysis\n");
	printf("  %s -i code.bin -o code.ll -a m68k -f ir -v\n", prog_name);
}

static cpu_arch_t
parse_arch(const char *arch_str)
{
	if (strcmp(arch_str, "6502") == 0)
		return CPU_ARCH_6502;
	if (strcmp(arch_str, "m68k") == 0)
		return CPU_ARCH_M68K;
	if (strcmp(arch_str, "mips") == 0)
		return CPU_ARCH_MIPS;
	if (strcmp(arch_str, "m88k") == 0)
		return CPU_ARCH_M88K;
	if (strcmp(arch_str, "arm") == 0)
		return CPU_ARCH_ARM;
	if (strcmp(arch_str, "x86") == 0 || strcmp(arch_str, "8086") == 0)
		return CPU_ARCH_8086;

	return CPU_ARCH_INVALID;
}

static static_output_format_t
parse_format(const char *format_str)
{
	if (strcmp(format_str, "object") == 0 || strcmp(format_str, "o") == 0)
		return STATIC_OUTPUT_OBJECT;
	if (strcmp(format_str, "shared") == 0 || strcmp(format_str, "so") == 0)
		return STATIC_OUTPUT_SHARED;
	if (strcmp(format_str, "executable") == 0 || strcmp(format_str, "exe") == 0)
		return STATIC_OUTPUT_EXECUTABLE;
	if (strcmp(format_str, "ir") == 0 || strcmp(format_str, "ll") == 0)
		return STATIC_OUTPUT_LLVM_IR;
	if (strcmp(format_str, "bc") == 0 || strcmp(format_str, "bitcode") == 0)
		return STATIC_OUTPUT_LLVM_BC;

	return STATIC_OUTPUT_OBJECT;
}

int
main(int argc, char **argv)
{
	static_recompile_options_t opts;
	cpu_arch_t arch = CPU_ARCH_INVALID;
	uint32_t cpu_flags = CPU_FLAG_ENDIAN_LITTLE;
	const char *input_file = NULL;
	const char *output_file = NULL;
	const char *arch_str = NULL;
	const char *format_str = "object";
	const char *endian_str = "little";
	addr_t entry_point = 0;
	addr_t code_start = 0;
	addr_t code_end = 0;
	bool has_entry = false;
	bool has_start = false;
	bool has_end = false;

	/* Initialize options */
	static_recompile_options_init(&opts);

	/* Parse command line */
	static struct option long_options[] = {
		{"input",      required_argument, 0, 'i'},
		{"output",     required_argument, 0, 'o'},
		{"arch",       required_argument, 0, 'a'},
		{"format",     required_argument, 0, 'f'},
		{"entry",      required_argument, 0, 'e'},
		{"start",      required_argument, 0, 's'},
		{"end",        required_argument, 0, 'E'},
		{"optimize",   no_argument,       0, 'O'},
		{"verbose",    no_argument,       0, 'v'},
		{"debug",      no_argument,       0, 'g'},
		{"target",     required_argument, 0, 't'},
		{"standalone", no_argument,       0, 'S'},
		{"runtime",    required_argument, 0, 'r'},
		{"endian",     required_argument, 0, 'n'},
		{"help",       no_argument,       0, 'h'},
		{0, 0, 0, 0}
	};

	int opt;
	int option_index = 0;

	while ((opt = getopt_long(argc, argv, "i:o:a:f:e:s:E:O0vgh",
	                          long_options, &option_index)) != -1) {
		switch (opt) {
			case 'i':
				input_file = optarg;
				break;
			case 'o':
				output_file = optarg;
				break;
			case 'a':
				arch_str = optarg;
				arch = parse_arch(optarg);
				break;
			case 'f':
				format_str = optarg;
				break;
			case 'e':
				entry_point = strtoull(optarg, NULL, 0);
				has_entry = true;
				break;
			case 's':
				code_start = strtoull(optarg, NULL, 0);
				has_start = true;
				break;
			case 'E':
				code_end = strtoull(optarg, NULL, 0);
				has_end = true;
				break;
			case 'O':
				opts.optimize = true;
				break;
			case '0':
				opts.optimize = false;
				break;
			case 'v':
				opts.verbose = true;
				break;
			case 'g':
				opts.include_debug_info = true;
				break;
			case 't':
				opts.target_triple = optarg;
				break;
			case 'S':
				opts.standalone = true;
				opts.format = STATIC_OUTPUT_EXECUTABLE;
				break;
			case 'r':
				opts.runtime_lib_path = optarg;
				break;
			case 'n':
				endian_str = optarg;
				break;
			case 'h':
				print_usage(argv[0]);
				return 0;
			default:
				print_usage(argv[0]);
				return 1;
		}
	}

	/* Validate required arguments */
	if (!input_file) {
		fprintf(stderr, "Error: Input file required (-i)\n");
		print_usage(argv[0]);
		return 1;
	}

	if (!output_file) {
		fprintf(stderr, "Error: Output file required (-o)\n");
		print_usage(argv[0]);
		return 1;
	}

	if (arch == CPU_ARCH_INVALID) {
		fprintf(stderr, "Error: Architecture required (-a)\n");
		print_usage(argv[0]);
		return 1;
	}

	/* Parse endianness */
	if (strcmp(endian_str, "big") == 0) {
		cpu_flags = CPU_FLAG_ENDIAN_BIG;
	} else if (strcmp(endian_str, "little") == 0) {
		cpu_flags = CPU_FLAG_ENDIAN_LITTLE;
	} else {
		fprintf(stderr, "Error: Invalid endianness: %s\n", endian_str);
		return 1;
	}

	/* Parse output format */
	opts.format = parse_format(format_str);

	if (opts.verbose) {
		printf("Static Recompiler\n");
		printf("=================\n");
		printf("Input:  %s\n", input_file);
		printf("Output: %s\n", output_file);
		printf("Arch:   %s\n", arch_str);
		printf("Format: %s\n", format_str);
		printf("\n");
	}

	/* Load input binary */
	uint8_t *binary_data = NULL;
	size_t binary_size = 0;

	if (static_recompile_load_binary(input_file, &binary_data, &binary_size) != 0) {
		return 1;
	}

	if (opts.verbose) {
		printf("Loaded %zu bytes from %s\n", binary_size, input_file);
	}

	/* Detect binary format */
	binary_format_t bin_format = static_recompile_detect_format(binary_data, binary_size);
	if (opts.verbose) {
		const char *format_names[] = {
			"Unknown", "Raw binary", "ELF", "PE", "Mach-O"
		};
		printf("Binary format: %s\n", format_names[bin_format]);
	}

	/* Set default code region if not specified */
	if (!has_start) {
		code_start = 0;
	}
	if (!has_end) {
		code_end = binary_size;
	}
	if (!has_entry) {
		entry_point = code_start;
	}

	/* Create CPU */
	cpu_t *cpu = cpu_new(arch, cpu_flags, 0);
	if (!cpu) {
		fprintf(stderr, "Error: Failed to create CPU\n");
		free(binary_data);
		return 1;
	}

	/* Set RAM */
	cpu_set_ram(cpu, binary_data);

	/* Configure options */
	opts.input_file = input_file;
	opts.output_file = output_file;
	opts.entry_point = entry_point;
	opts.code_start = code_start;
	opts.code_end = code_end;

	/* Perform static recompilation */
	int ret = static_recompile(cpu, &opts);

	/* Cleanup */
	cpu_free(cpu);
	free(binary_data);

	if (ret == 0) {
		if (opts.verbose) {
			printf("\nSuccess!\n");
		}
		return 0;
	} else {
		fprintf(stderr, "\nFailed!\n");
		return 1;
	}
}
