/*
 * libcpu: idbg_enhanced.h
 *
 * Enhanced interactive debugger with breakpoints, watchpoints,
 * and advanced debugging features
 */

#ifndef _IDBG_ENHANCED_H_
#define _IDBG_ENHANCED_H_

#include "libcpu.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of breakpoints/watchpoints */
#define MAX_BREAKPOINTS 256
#define MAX_WATCHPOINTS 256
#define MAX_CALL_STACK_DEPTH 1024

/* Breakpoint types */
typedef enum {
	BP_TYPE_EXEC,          /* Execution breakpoint */
	BP_TYPE_READ,          /* Memory read breakpoint */
	BP_TYPE_WRITE,         /* Memory write breakpoint */
	BP_TYPE_ACCESS         /* Memory access breakpoint */
} breakpoint_type_t;

/* Breakpoint structure */
typedef struct breakpoint {
	int id;                /* Breakpoint ID */
	breakpoint_type_t type; /* Type */
	addr_t address;        /* Address */
	bool enabled;          /* Enabled flag */
	bool temporary;        /* Temporary (one-shot) */
	uint64_t hit_count;    /* Number of times hit */
	const char *condition; /* Conditional expression (NULL if none) */
	const char *commands;  /* Commands to execute on hit */
} breakpoint_t;

/* Watchpoint structure */
typedef struct watchpoint {
	int id;                /* Watchpoint ID */
	addr_t address;        /* Address to watch */
	size_t size;           /* Size in bytes */
	bool enabled;          /* Enabled flag */
	uint64_t old_value;    /* Last known value */
	uint64_t hit_count;    /* Number of times triggered */
	const char *expression; /* Expression being watched */
} watchpoint_t;

/* Call stack frame */
typedef struct call_frame {
	addr_t pc;             /* Program counter */
	addr_t sp;             /* Stack pointer */
	const char *function;  /* Function name (if known) */
} call_frame_t;

/* Symbol table entry */
typedef struct symbol {
	addr_t address;        /* Symbol address */
	const char *name;      /* Symbol name */
	char type;             /* Symbol type (F=func, D=data, etc.) */
} symbol_t;

/* Enhanced debugger context */
typedef struct idbg_enhanced {
	cpu_t *cpu;

	/* Breakpoints */
	breakpoint_t breakpoints[MAX_BREAKPOINTS];
	int num_breakpoints;
	int next_bp_id;

	/* Watchpoints */
	watchpoint_t watchpoints[MAX_WATCHPOINTS];
	int num_watchpoints;
	int next_wp_id;

	/* Call stack */
	call_frame_t call_stack[MAX_CALL_STACK_DEPTH];
	int call_stack_depth;

	/* Symbol table */
	symbol_t *symbols;
	int num_symbols;

	/* Debug flags */
	bool trace_enabled;    /* Instruction tracing */
	bool verbose;          /* Verbose output */

	/* Statistics */
	uint64_t step_count;   /* Total steps executed */
	uint64_t instr_count;  /* Total instructions */

	/* Command history */
	char **history;
	int history_count;
	int history_size;

	/* Script support */
	FILE *script_file;     /* Current script file */
	bool scripting;        /* In scripting mode */

} idbg_enhanced_t;

/* Initialize enhanced debugger */
void idbg_enhanced_init(idbg_enhanced_t *ctx, cpu_t *cpu);

/* Cleanup enhanced debugger */
void idbg_enhanced_free(idbg_enhanced_t *ctx);

/* Main debugger loop */
int idbg_enhanced_run(idbg_enhanced_t *ctx, debug_function_t debug_func);

/* Breakpoint functions */
int idbg_add_breakpoint(idbg_enhanced_t *ctx, breakpoint_type_t type, addr_t address);
int idbg_remove_breakpoint(idbg_enhanced_t *ctx, int id);
int idbg_enable_breakpoint(idbg_enhanced_t *ctx, int id);
int idbg_disable_breakpoint(idbg_enhanced_t *ctx, int id);
void idbg_list_breakpoints(idbg_enhanced_t *ctx);
bool idbg_check_breakpoints(idbg_enhanced_t *ctx, addr_t pc);

/* Watchpoint functions */
int idbg_add_watchpoint(idbg_enhanced_t *ctx, addr_t address, size_t size, const char *expr);
int idbg_remove_watchpoint(idbg_enhanced_t *ctx, int id);
int idbg_enable_watchpoint(idbg_enhanced_t *ctx, int id);
int idbg_disable_watchpoint(idbg_enhanced_t *ctx, int id);
void idbg_list_watchpoints(idbg_enhanced_t *ctx);
bool idbg_check_watchpoints(idbg_enhanced_t *ctx);

/* Stack trace functions */
void idbg_update_call_stack(idbg_enhanced_t *ctx, addr_t pc, addr_t sp);
void idbg_print_backtrace(idbg_enhanced_t *ctx);
void idbg_print_frame(idbg_enhanced_t *ctx, int frame_num);

/* Symbol table functions */
int idbg_load_symbols(idbg_enhanced_t *ctx, const char *filename);
const char *idbg_lookup_symbol(idbg_enhanced_t *ctx, addr_t address);
addr_t idbg_lookup_address(idbg_enhanced_t *ctx, const char *symbol);
void idbg_list_symbols(idbg_enhanced_t *ctx, const char *pattern);

/* Memory inspection */
void idbg_hexdump(idbg_enhanced_t *ctx, addr_t address, size_t length);
void idbg_smart_disasm(idbg_enhanced_t *ctx, addr_t address, int count);
void idbg_compare_memory(idbg_enhanced_t *ctx, addr_t addr1, addr_t addr2, size_t length);
void idbg_search_memory(idbg_enhanced_t *ctx, addr_t start, addr_t end,
                        const uint8_t *pattern, size_t pattern_len);

/* Register modification */
int idbg_set_register(idbg_enhanced_t *ctx, int reg_num, uint64_t value);
int idbg_set_pc(idbg_enhanced_t *ctx, addr_t pc);

/* Advanced features */
void idbg_enable_trace(idbg_enhanced_t *ctx, bool enable);
void idbg_print_statistics(idbg_enhanced_t *ctx);
int idbg_run_script(idbg_enhanced_t *ctx, const char *filename);
void idbg_set_logging(idbg_enhanced_t *ctx, const char *filename);

/* Utility functions */
void idbg_print_help_enhanced(const char *command);
void idbg_print_registers(idbg_enhanced_t *ctx);
void idbg_print_flags(idbg_enhanced_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* _IDBG_ENHANCED_H_ */
