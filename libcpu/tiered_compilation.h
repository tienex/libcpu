/*
 * libcpu Tiered Compilation System
 *
 * Implements hotspot-like functionality with multiple compilation tiers:
 * - Tier 0: Interpreter (fastest startup, slowest execution)
 * - Tier 1: TCG (fast compilation, basic optimization)
 * - Tier 2: QBE (moderate compilation, good optimization)
 * - Tier 3: GCCJIT (slower compilation, better optimization)
 * - Tier 4: LLVM (slowest compilation, best optimization)
 */

#ifndef __LIBCPU_TIERED_COMPILATION_H__
#define __LIBCPU_TIERED_COMPILATION_H__

#include <stdint.h>
#include <pthread.h>
#include "backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Compilation tiers */
typedef enum {
	TIER_INTERPRETER = 0,  /* Pure interpretation, no compilation */
	TIER_TCG = 1,          /* Tiny Code Generator (QEMU-style) */
	TIER_QBE = 2,          /* Quick Backend */
	TIER_GCCJIT = 3,       /* GCC JIT */
	TIER_LLVM_O0 = 4,      /* LLVM -O0 */
	TIER_LLVM_O1 = 5,      /* LLVM -O1 */
	TIER_LLVM_O2 = 6,      /* LLVM -O2 */
	TIER_LLVM_O3 = 7,      /* LLVM -O3 */
	TIER_MAX
} compilation_tier_t;

/* Thresholds for tier transitions */
#define TIER_THRESHOLD_INTERPRETER_TO_TCG     10      /* After 10 invocations */
#define TIER_THRESHOLD_TCG_TO_QBE            100      /* After 100 invocations */
#define TIER_THRESHOLD_QBE_TO_GCCJIT         1000     /* After 1000 invocations */
#define TIER_THRESHOLD_GCCJIT_TO_LLVM_O0     10000    /* After 10k invocations */
#define TIER_THRESHOLD_LLVM_O0_TO_O1         50000    /* After 50k invocations */
#define TIER_THRESHOLD_LLVM_O1_TO_O2         100000   /* After 100k invocations */
#define TIER_THRESHOLD_LLVM_O2_TO_O3         500000   /* After 500k invocations */

/* Function profiling information */
typedef struct function_profile {
	addr_t address;                /* Function start address */
	uint64_t invocation_count;     /* Number of times called */
	uint64_t total_cycles;         /* Total CPU cycles spent */
	uint64_t compiled_at_count;    /* Invocation count when last compiled */
	compilation_tier_t current_tier; /* Current compilation tier */
	compilation_tier_t target_tier;  /* Target tier for next recompilation */
	void *compiled_code[TIER_MAX]; /* Compiled code for each tier */
	int is_hot;                    /* Hot function flag */
	int recompilation_pending;     /* Background compilation in progress */
	int recompilation_failed;      /* Last recompilation failed */
	pthread_mutex_t lock;          /* Lock for concurrent access */
} function_profile_t;

/* Compilation request for background thread */
typedef struct compilation_request {
	struct cpu *cpu;
	addr_t address;
	compilation_tier_t tier;
	function_profile_t *profile;
	struct compilation_request *next;
} compilation_request_t;

/* Background compilation queue */
typedef struct compilation_queue {
	compilation_request_t *head;
	compilation_request_t *tail;
	uint32_t size;
	uint32_t max_size;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	int shutdown;
} compilation_queue_t;

/* Tiered compilation manager */
typedef struct tier_manager {
	struct cpu *cpu;

	/* Profiling data */
	function_profile_t **profiles;  /* Hash table of function profiles */
	uint32_t profile_count;
	uint32_t profile_capacity;
	pthread_rwlock_t profile_lock;

	/* Backend instances for each tier */
	IBackend *backends[TIER_MAX];
	IModule *modules[TIER_MAX];

	/* Background compilation */
	compilation_queue_t *queue;
	pthread_t *worker_threads;
	uint32_t num_workers;
	int initialized;

	/* Statistics */
	uint64_t total_compilations;
	uint64_t total_recompilations;
	uint64_t tier_compilations[TIER_MAX];
	uint64_t tier_transitions[TIER_MAX][TIER_MAX];

	/* Configuration */
	int enable_background_compilation;
	int enable_profiling;
	int max_tier;  /* Maximum tier to use */
} tier_manager_t;

/***************************************************************************
 * Tier Manager API
 ***************************************************************************/

/* Create and initialize tier manager */
tier_manager_t* tier_manager_create(struct cpu *cpu);

/* Destroy tier manager */
void tier_manager_destroy(tier_manager_t *mgr);

/* Get or create function profile */
function_profile_t* tier_manager_get_profile(tier_manager_t *mgr, addr_t address);

/* Record function invocation */
void tier_manager_record_invocation(tier_manager_t *mgr, addr_t address, uint64_t cycles);

/* Get compiled code for function at current tier */
void* tier_manager_get_code(tier_manager_t *mgr, addr_t address);

/* Check if function should be recompiled */
int tier_manager_should_recompile(tier_manager_t *mgr, function_profile_t *profile);

/* Request background recompilation */
int tier_manager_request_recompilation(tier_manager_t *mgr, addr_t address, compilation_tier_t tier);

/* Compile function at specific tier (synchronous) */
void* tier_manager_compile(tier_manager_t *mgr, addr_t address, compilation_tier_t tier);

/* Replace function code atomically */
int tier_manager_replace_code(tier_manager_t *mgr, addr_t address, compilation_tier_t tier, void *new_code);

/* Print statistics */
void tier_manager_print_stats(tier_manager_t *mgr);

/***************************************************************************
 * Compilation Queue API
 ***************************************************************************/

/* Create compilation queue */
compilation_queue_t* compilation_queue_create(uint32_t max_size);

/* Destroy compilation queue */
void compilation_queue_destroy(compilation_queue_t *queue);

/* Enqueue compilation request */
int compilation_queue_enqueue(compilation_queue_t *queue, compilation_request_t *req);

/* Dequeue compilation request (blocking) */
compilation_request_t* compilation_queue_dequeue(compilation_queue_t *queue);

/* Background worker thread function */
void* compilation_worker_thread(void *arg);

/***************************************************************************
 * Tier Selection Heuristics
 ***************************************************************************/

/* Determine next tier for function based on profiling data */
compilation_tier_t tier_select_next(function_profile_t *profile);

/* Get backend type for compilation tier */
backend_type_t tier_get_backend_type(compilation_tier_t tier);

/* Get optimization level for tier */
uint32_t tier_get_opt_level(compilation_tier_t tier);

/* Get tier name */
const char* tier_get_name(compilation_tier_t tier);

/* Estimate compilation time for tier (in microseconds) */
uint64_t tier_estimate_compile_time(compilation_tier_t tier);

/* Estimate code quality (higher is better) */
uint32_t tier_estimate_code_quality(compilation_tier_t tier);

/***************************************************************************
 * Integration with cpu_t
 ***************************************************************************/

/* Initialize tiered compilation for CPU */
int cpu_init_tiered_compilation(struct cpu *cpu);

/* Shutdown tiered compilation for CPU */
void cpu_shutdown_tiered_compilation(struct cpu *cpu);

/* Configure tiered compilation */
void cpu_set_max_tier(struct cpu *cpu, compilation_tier_t tier);
void cpu_set_background_compilation(struct cpu *cpu, int enable);
void cpu_set_num_compilation_workers(struct cpu *cpu, uint32_t num_workers);

#ifdef __cplusplus
}
#endif

#endif /* __LIBCPU_TIERED_COMPILATION_H__ */
