/*
 * libcpu: idbg_enhanced.cpp
 *
 * Enhanced interactive debugger implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "libcpu.h"
#include "idbg_enhanced.h"

/* Initialize enhanced debugger */
void
idbg_enhanced_init(idbg_enhanced_t *ctx, cpu_t *cpu)
{
	memset(ctx, 0, sizeof(idbg_enhanced_t));
	ctx->cpu = cpu;
	ctx->next_bp_id = 1;
	ctx->next_wp_id = 1;
	ctx->trace_enabled = false;
	ctx->verbose = false;
	ctx->step_count = 0;
	ctx->instr_count = 0;

	printf("Enhanced debugger initialized\n");
}

/* Cleanup enhanced debugger */
void
idbg_enhanced_free(idbg_enhanced_t *ctx)
{
	/* Free breakpoint conditions and commands */
	for (int i = 0; i < ctx->num_breakpoints; i++) {
		if (ctx->breakpoints[i].condition)
			free((void *)ctx->breakpoints[i].condition);
		if (ctx->breakpoints[i].commands)
			free((void *)ctx->breakpoints[i].commands);
	}

	/* Free watchpoint expressions */
	for (int i = 0; i < ctx->num_watchpoints; i++) {
		if (ctx->watchpoints[i].expression)
			free((void *)ctx->watchpoints[i].expression);
	}

	/* Free symbols */
	if (ctx->symbols) {
		for (int i = 0; i < ctx->num_symbols; i++) {
			if (ctx->symbols[i].name)
				free((void *)ctx->symbols[i].name);
		}
		free(ctx->symbols);
	}

	/* Free history */
	if (ctx->history) {
		for (int i = 0; i < ctx->history_count; i++) {
			free(ctx->history[i]);
		}
		free(ctx->history);
	}

	if (ctx->script_file) {
		fclose(ctx->script_file);
	}
}

/* ========== Breakpoint Functions ========== */

int
idbg_add_breakpoint(idbg_enhanced_t *ctx, breakpoint_type_t type, addr_t address)
{
	if (ctx->num_breakpoints >= MAX_BREAKPOINTS) {
		fprintf(stderr, "Error: Maximum number of breakpoints reached\n");
		return -1;
	}

	breakpoint_t *bp = &ctx->breakpoints[ctx->num_breakpoints];
	bp->id = ctx->next_bp_id++;
	bp->type = type;
	bp->address = address;
	bp->enabled = true;
	bp->temporary = false;
	bp->hit_count = 0;
	bp->condition = NULL;
	bp->commands = NULL;

	ctx->num_breakpoints++;

	const char *type_names[] = {"exec", "read", "write", "access"};
	printf("Breakpoint %d: %s at 0x%llx\n", bp->id, type_names[type],
	       (unsigned long long)address);

	return bp->id;
}

int
idbg_remove_breakpoint(idbg_enhanced_t *ctx, int id)
{
	for (int i = 0; i < ctx->num_breakpoints; i++) {
		if (ctx->breakpoints[i].id == id) {
			/* Free resources */
			if (ctx->breakpoints[i].condition)
				free((void *)ctx->breakpoints[i].condition);
			if (ctx->breakpoints[i].commands)
				free((void *)ctx->breakpoints[i].commands);

			/* Shift array */
			memmove(&ctx->breakpoints[i], &ctx->breakpoints[i+1],
			        (ctx->num_breakpoints - i - 1) * sizeof(breakpoint_t));
			ctx->num_breakpoints--;

			printf("Breakpoint %d deleted\n", id);
			return 0;
		}
	}

	fprintf(stderr, "Error: Breakpoint %d not found\n", id);
	return -1;
}

int
idbg_enable_breakpoint(idbg_enhanced_t *ctx, int id)
{
	for (int i = 0; i < ctx->num_breakpoints; i++) {
		if (ctx->breakpoints[i].id == id) {
			ctx->breakpoints[i].enabled = true;
			printf("Breakpoint %d enabled\n", id);
			return 0;
		}
	}
	return -1;
}

int
idbg_disable_breakpoint(idbg_enhanced_t *ctx, int id)
{
	for (int i = 0; i < ctx->num_breakpoints; i++) {
		if (ctx->breakpoints[i].id == id) {
			ctx->breakpoints[i].enabled = false;
			printf("Breakpoint %d disabled\n", id);
			return 0;
		}
	}
	return -1;
}

void
idbg_list_breakpoints(idbg_enhanced_t *ctx)
{
	if (ctx->num_breakpoints == 0) {
		printf("No breakpoints\n");
		return;
	}

	printf("Breakpoints:\n");
	printf("ID   Type    Address          Enabled  Hits\n");
	printf("---  ------  ---------------  -------  -----\n");

	const char *type_names[] = {"exec", "read", "write", "access"};
	for (int i = 0; i < ctx->num_breakpoints; i++) {
		breakpoint_t *bp = &ctx->breakpoints[i];
		printf("%-3d  %-6s  0x%-13llx  %-7s  %llu\n",
		       bp->id,
		       type_names[bp->type],
		       (unsigned long long)bp->address,
		       bp->enabled ? "yes" : "no",
		       (unsigned long long)bp->hit_count);
	}
}

bool
idbg_check_breakpoints(idbg_enhanced_t *ctx, addr_t pc)
{
	for (int i = 0; i < ctx->num_breakpoints; i++) {
		breakpoint_t *bp = &ctx->breakpoints[i];
		if (bp->enabled && bp->type == BP_TYPE_EXEC && bp->address == pc) {
			bp->hit_count++;
			printf("\nBreakpoint %d hit at 0x%llx (hit count: %llu)\n",
			       bp->id, (unsigned long long)pc,
			       (unsigned long long)bp->hit_count);

			/* Execute commands if any */
			if (bp->commands) {
				printf("Executing: %s\n", bp->commands);
			}

			/* Remove if temporary */
			if (bp->temporary) {
				idbg_remove_breakpoint(ctx, bp->id);
			}

			return true;
		}
	}
	return false;
}

/* ========== Watchpoint Functions ========== */

int
idbg_add_watchpoint(idbg_enhanced_t *ctx, addr_t address, size_t size, const char *expr)
{
	if (ctx->num_watchpoints >= MAX_WATCHPOINTS) {
		fprintf(stderr, "Error: Maximum number of watchpoints reached\n");
		return -1;
	}

	watchpoint_t *wp = &ctx->watchpoints[ctx->num_watchpoints];
	wp->id = ctx->next_wp_id++;
	wp->address = address;
	wp->size = size;
	wp->enabled = true;
	wp->hit_count = 0;
	wp->expression = expr ? strdup(expr) : NULL;

	/* Read initial value */
	wp->old_value = 0;
	if (size <= 8) {
		memcpy(&wp->old_value, ctx->cpu->RAM + address, size);
	}

	ctx->num_watchpoints++;

	printf("Watchpoint %d: watch 0x%llx (size %zu)\n",
	       wp->id, (unsigned long long)address, size);

	return wp->id;
}

int
idbg_remove_watchpoint(idbg_enhanced_t *ctx, int id)
{
	for (int i = 0; i < ctx->num_watchpoints; i++) {
		if (ctx->watchpoints[i].id == id) {
			if (ctx->watchpoints[i].expression)
				free((void *)ctx->watchpoints[i].expression);

			memmove(&ctx->watchpoints[i], &ctx->watchpoints[i+1],
			        (ctx->num_watchpoints - i - 1) * sizeof(watchpoint_t));
			ctx->num_watchpoints--;

			printf("Watchpoint %d deleted\n", id);
			return 0;
		}
	}
	return -1;
}

int
idbg_enable_watchpoint(idbg_enhanced_t *ctx, int id)
{
	for (int i = 0; i < ctx->num_watchpoints; i++) {
		if (ctx->watchpoints[i].id == id) {
			ctx->watchpoints[i].enabled = true;
			printf("Watchpoint %d enabled\n", id);
			return 0;
		}
	}
	return -1;
}

int
idbg_disable_watchpoint(idbg_enhanced_t *ctx, int id)
{
	for (int i = 0; i < ctx->num_watchpoints; i++) {
		if (ctx->watchpoints[i].id == id) {
			ctx->watchpoints[i].enabled = false;
			printf("Watchpoint %d disabled\n", id);
			return 0;
		}
	}
	return -1;
}

void
idbg_list_watchpoints(idbg_enhanced_t *ctx)
{
	if (ctx->num_watchpoints == 0) {
		printf("No watchpoints\n");
		return;
	}

	printf("Watchpoints:\n");
	printf("ID   Address          Size  Enabled  Hits    Expression\n");
	printf("---  ---------------  ----  -------  ------  ----------\n");

	for (int i = 0; i < ctx->num_watchpoints; i++) {
		watchpoint_t *wp = &ctx->watchpoints[i];
		printf("%-3d  0x%-13llx  %-4zu  %-7s  %-6llu  %s\n",
		       wp->id,
		       (unsigned long long)wp->address,
		       wp->size,
		       wp->enabled ? "yes" : "no",
		       (unsigned long long)wp->hit_count,
		       wp->expression ? wp->expression : "");
	}
}

bool
idbg_check_watchpoints(idbg_enhanced_t *ctx)
{
	bool hit = false;

	for (int i = 0; i < ctx->num_watchpoints; i++) {
		watchpoint_t *wp = &ctx->watchpoints[i];
		if (!wp->enabled)
			continue;

		/* Read current value */
		uint64_t new_value = 0;
		if (wp->size <= 8) {
			memcpy(&new_value, ctx->cpu->RAM + wp->address, wp->size);
		}

		/* Check if changed */
		if (new_value != wp->old_value) {
			wp->hit_count++;
			printf("\nWatchpoint %d triggered at 0x%llx\n",
			       wp->id, (unsigned long long)wp->address);
			printf("  Old value: 0x%llx\n", (unsigned long long)wp->old_value);
			printf("  New value: 0x%llx\n", (unsigned long long)new_value);

			wp->old_value = new_value;
			hit = true;
		}
	}

	return hit;
}

/* ========== Memory Inspection ========== */

void
idbg_hexdump(idbg_enhanced_t *ctx, addr_t address, size_t length)
{
	const uint8_t *data = ctx->cpu->RAM + address;

	for (size_t i = 0; i < length; i += 16) {
		/* Address */
		printf("%08llx:  ", (unsigned long long)(address + i));

		/* Hex bytes */
		for (size_t j = 0; j < 16; j++) {
			if (i + j < length) {
				printf("%02x ", data[i + j]);
			} else {
				printf("   ");
			}
			if (j == 7) printf(" ");
		}

		printf(" |");

		/* ASCII */
		for (size_t j = 0; j < 16 && i + j < length; j++) {
			unsigned char c = data[i + j];
			printf("%c", (c >= 32 && c < 127) ? c : '.');
		}

		printf("|\n");
	}
}

void
idbg_smart_disasm(idbg_enhanced_t *ctx, addr_t address, int count)
{
	cpu_t *cpu = ctx->cpu;
	char buf[256];

	for (int i = 0; i < count; i++) {
		/* Get symbol if available */
		const char *symbol = idbg_lookup_symbol(ctx, address);

		if (symbol) {
			printf("\n<%s>:\n", symbol);
		}

		/* Disassemble */
		printf("0x%08llx:  ", (unsigned long long)address);

		int bytes = cpu->f.disasm_instr(cpu, address, buf, sizeof(buf));
		printf("%s\n", buf);

		address += bytes;
	}
}

void
idbg_search_memory(idbg_enhanced_t *ctx, addr_t start, addr_t end,
                   const uint8_t *pattern, size_t pattern_len)
{
	printf("Searching 0x%llx - 0x%llx for pattern...\n",
	       (unsigned long long)start, (unsigned long long)end);

	int found = 0;
	for (addr_t addr = start; addr <= end - pattern_len; addr++) {
		if (memcmp(ctx->cpu->RAM + addr, pattern, pattern_len) == 0) {
			printf("Found at 0x%llx\n", (unsigned long long)addr);
			found++;
			if (found >= 100) {
				printf("... (too many matches, stopping)\n");
				break;
			}
		}
	}

	if (found == 0) {
		printf("Pattern not found\n");
	} else {
		printf("Total matches: %d\n", found);
	}
}

/* ========== Symbol Table ========== */

const char *
idbg_lookup_symbol(idbg_enhanced_t *ctx, addr_t address)
{
	/* Find exact match first */
	for (int i = 0; i < ctx->num_symbols; i++) {
		if (ctx->symbols[i].address == address) {
			return ctx->symbols[i].name;
		}
	}

	/* Find nearest symbol before address */
	const char *nearest = NULL;
	addr_t nearest_dist = (addr_t)-1;

	for (int i = 0; i < ctx->num_symbols; i++) {
		if (ctx->symbols[i].address <= address) {
			addr_t dist = address - ctx->symbols[i].address;
			if (dist < nearest_dist && dist < 256) { /* Within 256 bytes */
				nearest_dist = dist;
				nearest = ctx->symbols[i].name;
			}
		}
	}

	return nearest;
}

addr_t
idbg_lookup_address(idbg_enhanced_t *ctx, const char *symbol)
{
	for (int i = 0; i < ctx->num_symbols; i++) {
		if (strcmp(ctx->symbols[i].name, symbol) == 0) {
			return ctx->symbols[i].address;
		}
	}
	return 0;
}

void
idbg_list_symbols(idbg_enhanced_t *ctx, const char *pattern)
{
	if (ctx->num_symbols == 0) {
		printf("No symbols loaded\n");
		return;
	}

	printf("Symbols:\n");
	printf("Address          Type  Name\n");
	printf("---------------  ----  ----\n");

	int shown = 0;
	for (int i = 0; i < ctx->num_symbols; i++) {
		/* Filter by pattern if provided */
		if (pattern && strstr(ctx->symbols[i].name, pattern) == NULL) {
			continue;
		}

		printf("0x%-13llx  %c     %s\n",
		       (unsigned long long)ctx->symbols[i].address,
		       ctx->symbols[i].type,
		       ctx->symbols[i].name);

		shown++;
		if (shown >= 100) {
			printf("... (showing first 100 matches)\n");
			break;
		}
	}

	printf("Total symbols: %d\n", ctx->num_symbols);
}

/* ========== Statistics ========== */

void
idbg_print_statistics(idbg_enhanced_t *ctx)
{
	printf("\nDebugger Statistics:\n");
	printf("  Steps executed:       %llu\n", (unsigned long long)ctx->step_count);
	printf("  Instructions:         %llu\n", (unsigned long long)ctx->instr_count);
	printf("  Breakpoints set:      %d\n", ctx->num_breakpoints);
	printf("  Watchpoints set:      %d\n", ctx->num_watchpoints);
	printf("  Symbols loaded:       %d\n", ctx->num_symbols);

	/* Breakpoint hit counts */
	uint64_t total_bp_hits = 0;
	for (int i = 0; i < ctx->num_breakpoints; i++) {
		total_bp_hits += ctx->breakpoints[i].hit_count;
	}
	printf("  Total BP hits:        %llu\n", (unsigned long long)total_bp_hits);

	/* Watchpoint hit counts */
	uint64_t total_wp_hits = 0;
	for (int i = 0; i < ctx->num_watchpoints; i++) {
		total_wp_hits += ctx->watchpoints[i].hit_count;
	}
	printf("  Total WP triggers:    %llu\n", (unsigned long long)total_wp_hits);
}

/* ========== Enhanced Help ========== */

void
idbg_print_help_enhanced(const char *command)
{
	if (command == NULL || *command == '\0') {
		printf("Enhanced Debugger Commands:\n");
		printf("\n");
		printf("Execution:\n");
		printf("  s[tep]              - Single step\n");
		printf("  n[ext]              - Step over (simplified: same as step)\n");
		printf("  c[ontinue]          - Continue execution\n");
		printf("  finish              - Run until function returns (not yet implemented)\n");
		printf("\n");
		printf("Breakpoints:\n");
		printf("  b[reak] ADDR        - Set breakpoint at address\n");
		printf("  tbreak ADDR         - Set temporary breakpoint (deleted after hit)\n");
		printf("  d[elete] ID         - Delete breakpoint\n");
		printf("  enable ID           - Enable breakpoint\n");
		printf("  disable ID          - Disable breakpoint\n");
		printf("  info b[reakpoints]  - List all breakpoints\n");
		printf("\n");
		printf("Watchpoints:\n");
		printf("  watch ADDR [SIZE]   - Set watchpoint\n");
		printf("  delete w ID         - Delete watchpoint\n");
		printf("  info w[atchpoints]  - List all watchpoints\n");
		printf("\n");
		printf("Memory:\n");
		printf("  x/FMT ADDR          - Examine memory (see 'help x')\n");
		printf("  dump ADDR SIZE      - Hex dump memory\n");
		printf("  disasm ADDR [N]     - Disassemble N instructions\n");
		printf("  search START END PAT - Search for pattern\n");
		printf("\n");
		printf("Registers:\n");
		printf("  info r[egisters]    - Show all registers\n");
		printf("  p[rint] $REG        - Print register\n");
		printf("  set $REG = VALUE    - Set register\n");
		printf("\n");
		printf("Symbols:\n");
		printf("  symbol-file FILE    - Load symbol table\n");
		printf("  info symbols [PAT]  - List symbols\n");
		printf("\n");
		printf("Stack:\n");
		printf("  bt / backtrace      - Print call stack\n");
		printf("  frame N             - Select stack frame\n");
		printf("\n");
		printf("Misc:\n");
		printf("  help [CMD]          - Show help\n");
		printf("  quit / q            - Exit debugger\n");
		printf("  source FILE         - Execute commands from file\n");
		printf("  set trace on/off    - Enable/disable tracing\n");
		printf("  info stats          - Show statistics\n");
		printf("\n");
		printf("Type 'help COMMAND' for detailed help on a command\n");
	} else {
		printf("Detailed help for '%s' not yet implemented\n", command);
	}
}

/* ========== Call Stack Functions ========== */

void
idbg_update_call_stack(idbg_enhanced_t *ctx, addr_t pc, addr_t sp)
{
	/* Simple heuristic: if PC jumped significantly, might be a call */
	/* This is architecture-dependent and simplified */

	/* For now, just track PC and SP */
	if (ctx->call_stack_depth < MAX_CALL_STACK_DEPTH) {
		call_frame_t *frame = &ctx->call_stack[ctx->call_stack_depth];
		frame->pc = pc;
		frame->sp = sp;
		frame->function = idbg_lookup_symbol(ctx, pc);
		ctx->call_stack_depth++;
	}
}

void
idbg_print_backtrace(idbg_enhanced_t *ctx)
{
	if (ctx->call_stack_depth == 0) {
		printf("No call stack available\n");
		return;
	}

	printf("Call stack:\n");
	printf("#   PC               SP               Function\n");
	printf("--  ---------------  ---------------  --------\n");

	for (int i = ctx->call_stack_depth - 1; i >= 0; i--) {
		call_frame_t *frame = &ctx->call_stack[i];
		printf("%-2d  0x%-13llx  0x%-13llx  %s\n",
		       ctx->call_stack_depth - 1 - i,
		       (unsigned long long)frame->pc,
		       (unsigned long long)frame->sp,
		       frame->function ? frame->function : "??");
	}
}

void
idbg_print_frame(idbg_enhanced_t *ctx, int frame_num)
{
	if (frame_num < 0 || frame_num >= ctx->call_stack_depth) {
		fprintf(stderr, "Error: Invalid frame number\n");
		return;
	}

	int idx = ctx->call_stack_depth - 1 - frame_num;
	call_frame_t *frame = &ctx->call_stack[idx];

	printf("Frame #%d:\n", frame_num);
	printf("  PC: 0x%llx\n", (unsigned long long)frame->pc);
	printf("  SP: 0x%llx\n", (unsigned long long)frame->sp);
	printf("  Function: %s\n", frame->function ? frame->function : "??");
}

/* ========== Symbol Loading ========== */

int
idbg_load_symbols(idbg_enhanced_t *ctx, const char *filename)
{
	FILE *f = fopen(filename, "r");
	if (!f) {
		fprintf(stderr, "Error: Cannot open symbol file: %s\n", filename);
		return -1;
	}

	/* Free existing symbols */
	if (ctx->symbols) {
		for (int i = 0; i < ctx->num_symbols; i++) {
			if (ctx->symbols[i].name)
				free((void *)ctx->symbols[i].name);
		}
		free(ctx->symbols);
		ctx->symbols = NULL;
		ctx->num_symbols = 0;
	}

	/* Count symbols first */
	int count = 0;
	char line[512];
	while (fgets(line, sizeof(line), f)) {
		if (line[0] != '#' && line[0] != '\n')
			count++;
	}

	/* Allocate symbol table */
	ctx->symbols = (symbol_t *)calloc(count, sizeof(symbol_t));
	if (!ctx->symbols) {
		fclose(f);
		return -1;
	}

	/* Parse symbols - format: ADDRESS TYPE NAME */
	rewind(f);
	int idx = 0;
	while (fgets(line, sizeof(line), f) && idx < count) {
		/* Skip comments and empty lines */
		if (line[0] == '#' || line[0] == '\n')
			continue;

		unsigned long long addr;
		char type;
		char name[256];

		/* Parse line: "ADDRESS TYPE NAME" */
		if (sscanf(line, "%llx %c %255s", &addr, &type, name) == 3) {
			ctx->symbols[idx].address = (addr_t)addr;
			ctx->symbols[idx].type = type;
			ctx->symbols[idx].name = strdup(name);
			idx++;
		}
	}

	ctx->num_symbols = idx;
	fclose(f);

	printf("Loaded %d symbols from %s\n", ctx->num_symbols, filename);
	return 0;
}

/* ========== Memory Compare ========== */

void
idbg_compare_memory(idbg_enhanced_t *ctx, addr_t addr1, addr_t addr2, size_t length)
{
	const uint8_t *mem1 = ctx->cpu->RAM + addr1;
	const uint8_t *mem2 = ctx->cpu->RAM + addr2;

	int diff_count = 0;
	for (size_t i = 0; i < length; i++) {
		if (mem1[i] != mem2[i]) {
			if (diff_count < 100) { /* Limit output */
				printf("Offset 0x%zx: 0x%02x != 0x%02x\n",
				       i, mem1[i], mem2[i]);
			}
			diff_count++;
		}
	}

	if (diff_count == 0) {
		printf("Memory regions are identical\n");
	} else {
		printf("Total differences: %d\n", diff_count);
	}
}

/* ========== Register Functions ========== */

int
idbg_set_register(idbg_enhanced_t *ctx, int reg_num, uint64_t value)
{
	cpu_t *cpu = ctx->cpu;

	if (reg_num < 0 || reg_num >= cpu->info.register_count) {
		fprintf(stderr, "Error: Invalid register number\n");
		return -1;
	}

	/* Set register based on size */
	void *reg_ptr = (uint8_t *)cpu->rf.grf + cpu->info.register_size * reg_num;

	switch (cpu->info.register_size) {
		case 1: *(uint8_t *)reg_ptr = (uint8_t)value; break;
		case 2: *(uint16_t *)reg_ptr = (uint16_t)value; break;
		case 4: *(uint32_t *)reg_ptr = (uint32_t)value; break;
		case 8: *(uint64_t *)reg_ptr = value; break;
		default:
			fprintf(stderr, "Error: Unsupported register size\n");
			return -1;
	}

	printf("Register %d set to 0x%llx\n", reg_num, (unsigned long long)value);
	return 0;
}

int
idbg_set_pc(idbg_enhanced_t *ctx, addr_t pc)
{
	ctx->cpu->f.set_pc(ctx->cpu, pc);
	printf("PC set to 0x%llx\n", (unsigned long long)pc);
	return 0;
}

void
idbg_print_registers(idbg_enhanced_t *ctx)
{
	cpu_t *cpu = ctx->cpu;

	printf("Registers:\n");

	for (int i = 0; i < cpu->info.register_count; i++) {
		void *reg_ptr = (uint8_t *)cpu->rf.grf + cpu->info.register_size * i;
		uint64_t value = 0;

		switch (cpu->info.register_size) {
			case 1: value = *(uint8_t *)reg_ptr; break;
			case 2: value = *(uint16_t *)reg_ptr; break;
			case 4: value = *(uint32_t *)reg_ptr; break;
			case 8: value = *(uint64_t *)reg_ptr; break;
		}

		const char *name = cpu->info.register_name[i];
		printf("  %-8s = 0x%0*llx", name ? name : "??",
		       cpu->info.register_size * 2,
		       (unsigned long long)value);

		if ((i + 1) % 4 == 0)
			printf("\n");
	}
	printf("\n");

	printf("  PC       = 0x%llx\n", (unsigned long long)cpu->f.get_pc(cpu, 0));
}

void
idbg_print_flags(idbg_enhanced_t *ctx)
{
	cpu_t *cpu = ctx->cpu;

	/* Architecture-specific flag printing */
	/* This is a simplified generic version */
	printf("Flags: (architecture-specific implementation needed)\n");

	/* Try to print flags if available */
	if (cpu->info.psr_size > 0) {
		uint64_t flags = 0;
		if (cpu->info.psr_size <= 8) {
			memcpy(&flags, cpu->rf.psr, cpu->info.psr_size);
			printf("  PSR = 0x%llx\n", (unsigned long long)flags);
		}
	}
}

/* ========== Tracing ========== */

void
idbg_enable_trace(idbg_enhanced_t *ctx, bool enable)
{
	ctx->trace_enabled = enable;
	printf("Instruction tracing %s\n", enable ? "enabled" : "disabled");
}

/* ========== Script Execution ========== */

int
idbg_run_script(idbg_enhanced_t *ctx, const char *filename)
{
	FILE *f = fopen(filename, "r");
	if (!f) {
		fprintf(stderr, "Error: Cannot open script file: %s\n", filename);
		return -1;
	}

	ctx->script_file = f;
	ctx->scripting = true;

	printf("Executing script: %s\n", filename);

	/* Script commands will be read by the main loop */
	/* (Implementation depends on main loop structure) */

	return 0;
}

/* ========== Logging ========== */

static FILE *log_file = NULL;

void
idbg_set_logging(idbg_enhanced_t *ctx, const char *filename)
{
	if (log_file) {
		fclose(log_file);
		log_file = NULL;
	}

	if (filename) {
		log_file = fopen(filename, "w");
		if (!log_file) {
			fprintf(stderr, "Error: Cannot open log file: %s\n", filename);
		} else {
			printf("Logging to: %s\n", filename);
		}
	} else {
		printf("Logging disabled\n");
	}
}

/* ========== Register Name Parsing ========== */

static int
parse_register_name(idbg_enhanced_t *ctx, const char *name)
{
	cpu_t *cpu = ctx->cpu;

	/* Handle special register names */
	if (strcmp(name, "pc") == 0 || strcmp(name, "PC") == 0) {
		/* PC is not in the register array, handle specially */
		return -2; /* Special code for PC */
	}

	/* Try to find by name */
	for (int i = 0; i < cpu->info.register_count; i++) {
		const char *reg_name = cpu->info.register_name[i];
		if (reg_name && (strcmp(reg_name, name) == 0 ||
		    strcasecmp(reg_name, name) == 0)) {
			return i;
		}
	}

	/* Try to parse as register number (e.g., "r0", "r1") */
	if (name[0] == 'r' || name[0] == 'R') {
		int reg_num = atoi(name + 1);
		if (reg_num >= 0 && reg_num < cpu->info.register_count) {
			return reg_num;
		}
	}

	return -1; /* Not found */
}

static uint64_t
get_register_value(idbg_enhanced_t *ctx, int reg_num)
{
	cpu_t *cpu = ctx->cpu;

	if (reg_num == -2) {
		/* PC */
		return cpu->f.get_pc(cpu, 0);
	}

	if (reg_num < 0 || reg_num >= cpu->info.register_count) {
		return 0;
	}

	void *reg_ptr = (uint8_t *)cpu->rf.grf + cpu->info.register_size * reg_num;
	uint64_t value = 0;

	switch (cpu->info.register_size) {
		case 1: value = *(uint8_t *)reg_ptr; break;
		case 2: value = *(uint16_t *)reg_ptr; break;
		case 4: value = *(uint32_t *)reg_ptr; break;
		case 8: value = *(uint64_t *)reg_ptr; break;
	}

	return value;
}

/* ========== Main Debugger Loop ========== */

int
idbg_enhanced_run(idbg_enhanced_t *ctx, debug_function_t debug_func)
{
	cpu_t *cpu = ctx->cpu;
	bool running = true;
	bool stepping = true;
	char cmd_buffer[512];

	printf("Enhanced Interactive Debugger\n");
	printf("Type 'help' for available commands\n\n");

	while (running) {
		/* Check breakpoints */
		addr_t pc = cpu->f.get_pc(cpu, 0);
		if (idbg_check_breakpoints(ctx, pc)) {
			stepping = true;
		}

		/* Check watchpoints */
		if (idbg_check_watchpoints(ctx)) {
			stepping = true;
		}

		/* Print current state if stepping */
		if (stepping) {
			printf("\nPC = 0x%llx\n", (unsigned long long)pc);

			/* Disassemble current instruction */
			char disasm_buf[256];
			cpu->f.disasm_instr(cpu, pc, disasm_buf, sizeof(disasm_buf));
			printf("=> %s\n", disasm_buf);
		}

		/* Get command if stepping or script */
		if (stepping || ctx->scripting) {
			/* Read command */
			if (ctx->scripting && ctx->script_file) {
				if (!fgets(cmd_buffer, sizeof(cmd_buffer), ctx->script_file)) {
					fclose(ctx->script_file);
					ctx->script_file = NULL;
					ctx->scripting = false;
					continue;
				}
			} else {
				printf("(idbg) ");
				fflush(stdout);
				if (!fgets(cmd_buffer, sizeof(cmd_buffer), stdin)) {
					break;
				}
			}

			/* Remove newline */
			cmd_buffer[strcspn(cmd_buffer, "\n")] = 0;

			/* Skip empty commands */
			if (cmd_buffer[0] == '\0')
				continue;

			/* Parse command */
			char *cmd = strtok(cmd_buffer, " \t");
			if (!cmd)
				continue;

			/* Handle commands */
			if (strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0) {
				char *arg = strtok(NULL, " \t");
				idbg_print_help_enhanced(arg);
			}
			else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "q") == 0) {
				running = false;
			}
			else if (strcmp(cmd, "step") == 0 || strcmp(cmd, "s") == 0) {
				cpu->f.step(cpu, debug_func);
				ctx->step_count++;
				ctx->instr_count++;
			}
			else if (strcmp(cmd, "next") == 0 || strcmp(cmd, "n") == 0) {
				/* TODO: Proper step-over implementation needs to:
				 * 1. Disassemble current instruction
				 * 2. Check if it's a call instruction
				 * 3. If yes, set temporary breakpoint at next instruction
				 * 4. Continue until that breakpoint
				 * For now, just step (same as 's')
				 */
				cpu->f.step(cpu, debug_func);
				ctx->step_count++;
				ctx->instr_count++;
			}
			else if (strcmp(cmd, "continue") == 0 || strcmp(cmd, "c") == 0) {
				stepping = false;
			}
			else if (strcmp(cmd, "finish") == 0) {
				/* TODO: Proper step-out implementation needs to:
				 * 1. Get current stack pointer
				 * 2. Set temporary breakpoint at return address
				 * 3. Continue until that breakpoint
				 * For now, print a message
				 */
				fprintf(stderr, "Command 'finish' not yet fully implemented\n");
				fprintf(stderr, "Use 'continue' to run until next breakpoint\n");
			}
			else if (strcmp(cmd, "break") == 0 || strcmp(cmd, "b") == 0) {
				char *addr_str = strtok(NULL, " \t");
				if (addr_str) {
					addr_t addr = strtoull(addr_str, NULL, 0);
					idbg_add_breakpoint(ctx, BP_TYPE_EXEC, addr);
				} else {
					fprintf(stderr, "Usage: break ADDRESS\n");
				}
			}
			else if (strcmp(cmd, "tbreak") == 0) {
				char *addr_str = strtok(NULL, " \t");
				if (addr_str) {
					addr_t addr = strtoull(addr_str, NULL, 0);
					int id = idbg_add_breakpoint(ctx, BP_TYPE_EXEC, addr);
					if (id > 0) {
						/* Find and mark as temporary */
						for (int i = 0; i < ctx->num_breakpoints; i++) {
							if (ctx->breakpoints[i].id == id) {
								ctx->breakpoints[i].temporary = true;
								printf("Temporary breakpoint %d at 0x%llx\n",
								       id, (unsigned long long)addr);
								break;
							}
						}
					}
				} else {
					fprintf(stderr, "Usage: tbreak ADDRESS\n");
				}
			}
			else if (strcmp(cmd, "delete") == 0 || strcmp(cmd, "d") == 0) {
				char *id_str = strtok(NULL, " \t");
				if (id_str) {
					int id = atoi(id_str);
					idbg_remove_breakpoint(ctx, id);
				}
			}
			else if (strcmp(cmd, "watch") == 0) {
				char *addr_str = strtok(NULL, " \t");
				char *size_str = strtok(NULL, " \t");
				if (addr_str) {
					addr_t addr = strtoull(addr_str, NULL, 0);
					size_t size = size_str ? atoi(size_str) : 4;
					idbg_add_watchpoint(ctx, addr, size, NULL);
				} else {
					fprintf(stderr, "Usage: watch ADDRESS [SIZE]\n");
				}
			}
			else if (strcmp(cmd, "info") == 0) {
				char *what = strtok(NULL, " \t");
				if (!what) {
					fprintf(stderr, "Usage: info {breakpoints|watchpoints|registers|symbols|stats}\n");
				} else if (strcmp(what, "breakpoints") == 0 || strcmp(what, "b") == 0) {
					idbg_list_breakpoints(ctx);
				} else if (strcmp(what, "watchpoints") == 0 || strcmp(what, "w") == 0) {
					idbg_list_watchpoints(ctx);
				} else if (strcmp(what, "registers") == 0 || strcmp(what, "r") == 0) {
					idbg_print_registers(ctx);
				} else if (strcmp(what, "symbols") == 0) {
					char *pattern = strtok(NULL, " \t");
					idbg_list_symbols(ctx, pattern);
				} else if (strcmp(what, "stats") == 0) {
					idbg_print_statistics(ctx);
				}
			}
			else if (strcmp(cmd, "dump") == 0) {
				char *addr_str = strtok(NULL, " \t");
				char *len_str = strtok(NULL, " \t");
				if (addr_str && len_str) {
					addr_t addr = strtoull(addr_str, NULL, 0);
					size_t len = strtoull(len_str, NULL, 0);
					idbg_hexdump(ctx, addr, len);
				} else {
					fprintf(stderr, "Usage: dump ADDRESS LENGTH\n");
				}
			}
			else if (strcmp(cmd, "disasm") == 0) {
				char *addr_str = strtok(NULL, " \t");
				char *count_str = strtok(NULL, " \t");
				if (addr_str) {
					addr_t addr = strtoull(addr_str, NULL, 0);
					int count = count_str ? atoi(count_str) : 10;
					idbg_smart_disasm(ctx, addr, count);
				} else {
					fprintf(stderr, "Usage: disasm ADDRESS [COUNT]\n");
				}
			}
			else if (strcmp(cmd, "symbol-file") == 0) {
				char *filename = strtok(NULL, " \t");
				if (filename) {
					idbg_load_symbols(ctx, filename);
				} else {
					fprintf(stderr, "Usage: symbol-file FILENAME\n");
				}
			}
			else if (strcmp(cmd, "bt") == 0 || strcmp(cmd, "backtrace") == 0) {
				idbg_print_backtrace(ctx);
			}
			else if (strcmp(cmd, "set") == 0) {
				char *what = strtok(NULL, " \t");
				if (!what) {
					fprintf(stderr, "Usage: set trace {on|off} | set $REG = VALUE\n");
				} else if (strcmp(what, "trace") == 0) {
					char *value = strtok(NULL, " \t");
					if (value) {
						idbg_enable_trace(ctx, strcmp(value, "on") == 0);
					} else {
						fprintf(stderr, "Usage: set trace {on|off}\n");
					}
				} else if (what[0] == '$') {
					/* set $REG = VALUE */
					char *reg_name = what + 1;
					char *eq = strtok(NULL, " \t");
					char *val_str = strtok(NULL, " \t");

					if (!eq || !val_str) {
						fprintf(stderr, "Usage: set $REG = VALUE\n");
					} else {
						int reg_num = parse_register_name(ctx, reg_name);
						uint64_t value = strtoull(val_str, NULL, 0);

						if (reg_num == -2) {
							/* PC */
							idbg_set_pc(ctx, value);
						} else if (reg_num >= 0) {
							idbg_set_register(ctx, reg_num, value);
						} else {
							fprintf(stderr, "Error: Unknown register: %s\n", reg_name);
						}
					}
				} else {
					fprintf(stderr, "Usage: set trace {on|off} | set $REG = VALUE\n");
				}
			}
			else if (strcmp(cmd, "source") == 0) {
				char *filename = strtok(NULL, " \t");
				if (filename) {
					idbg_run_script(ctx, filename);
				} else {
					fprintf(stderr, "Usage: source FILENAME\n");
				}
			}
			else if (strcmp(cmd, "enable") == 0) {
				char *id_str = strtok(NULL, " \t");
				if (id_str) {
					int id = atoi(id_str);
					idbg_enable_breakpoint(ctx, id);
				} else {
					fprintf(stderr, "Usage: enable ID\n");
				}
			}
			else if (strcmp(cmd, "disable") == 0) {
				char *id_str = strtok(NULL, " \t");
				if (id_str) {
					int id = atoi(id_str);
					idbg_disable_breakpoint(ctx, id);
				} else {
					fprintf(stderr, "Usage: disable ID\n");
				}
			}
			else if (strcmp(cmd, "print") == 0 || strcmp(cmd, "p") == 0) {
				char *reg_str = strtok(NULL, " \t");
				if (reg_str) {
					/* Remove leading $ if present */
					if (reg_str[0] == '$') {
						reg_str++;
					}
					int reg_num = parse_register_name(ctx, reg_str);
					if (reg_num >= -2) {
						uint64_t value = get_register_value(ctx, reg_num);
						if (reg_num == -2) {
							printf("$pc = 0x%llx\n", (unsigned long long)value);
						} else {
							const char *name = cpu->info.register_name[reg_num];
							printf("$%s = 0x%llx (%llu)\n",
							       name ? name : "??",
							       (unsigned long long)value,
							       (unsigned long long)value);
						}
					} else {
						fprintf(stderr, "Error: Unknown register: %s\n", reg_str);
					}
				} else {
					fprintf(stderr, "Usage: print $REGISTER\n");
				}
			}
			else if (strcmp(cmd, "frame") == 0) {
				char *num_str = strtok(NULL, " \t");
				if (num_str) {
					int frame_num = atoi(num_str);
					idbg_print_frame(ctx, frame_num);
				} else {
					fprintf(stderr, "Usage: frame N\n");
				}
			}
			else {
				fprintf(stderr, "Unknown command: %s\n", cmd);
				printf("Type 'help' for available commands\n");
			}
		} else {
			/* Running without stopping */
			cpu->f.step(cpu, debug_func);
			ctx->step_count++;
			ctx->instr_count++;

			/* Trace if enabled */
			if (ctx->trace_enabled) {
				addr_t pc = cpu->f.get_pc(cpu, 0);
				char disasm_buf[256];
				cpu->f.disasm_instr(cpu, pc, disasm_buf, sizeof(disasm_buf));
				printf("0x%llx: %s\n", (unsigned long long)pc, disasm_buf);
			}
		}
	}

	printf("Debugger exited\n");
	return 0;
}

