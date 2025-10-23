/*
 * libcpu Tiered Compilation System Implementation
 */

#include "tiered_compilation.h"
#include "libcpu.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

/***************************************************************************
 * Tier Selection and Metadata
 ***************************************************************************/

const char* tier_get_name(compilation_tier_t tier)
{
	static const char *names[] = {
		"Interpreter",
		"TCG",
		"QBE",
		"GCCJIT",
		"LLVM-O0",
		"LLVM-O1",
		"LLVM-O2",
		"LLVM-O3"
	};
	if (tier >= TIER_MAX)
		return "Unknown";
	return names[tier];
}

backend_type_t tier_get_backend_type(compilation_tier_t tier)
{
	switch (tier) {
	case TIER_INTERPRETER:
		return BACKEND_LLVM; /* Fallback to LLVM for now */
	case TIER_TCG:
		return BACKEND_TCG;
	case TIER_QBE:
		return BACKEND_QBE;
	case TIER_GCCJIT:
		return BACKEND_GCCJIT;
	case TIER_LLVM_O0:
	case TIER_LLVM_O1:
	case TIER_LLVM_O2:
	case TIER_LLVM_O3:
		return BACKEND_LLVM;
	default:
		return BACKEND_LLVM;
	}
}

uint32_t tier_get_opt_level(compilation_tier_t tier)
{
	switch (tier) {
	case TIER_INTERPRETER:
	case TIER_TCG:
	case TIER_QBE:
	case TIER_LLVM_O0:
		return 0;
	case TIER_LLVM_O1:
		return 1;
	case TIER_GCCJIT:
	case TIER_LLVM_O2:
		return 2;
	case TIER_LLVM_O3:
		return 3;
	default:
		return 0;
	}
}

uint64_t tier_estimate_compile_time(compilation_tier_t tier)
{
	/* Estimated compilation time in microseconds */
	static const uint64_t times[] = {
		0,       /* Interpreter: no compilation */
		100,     /* TCG: ~100 μs */
		500,     /* QBE: ~500 μs */
		2000,    /* GCCJIT: ~2 ms */
		5000,    /* LLVM-O0: ~5 ms */
		10000,   /* LLVM-O1: ~10 ms */
		50000,   /* LLVM-O2: ~50 ms */
		200000   /* LLVM-O3: ~200 ms */
	};
	if (tier >= TIER_MAX)
		return 1000000;
	return times[tier];
}

uint32_t tier_estimate_code_quality(compilation_tier_t tier)
{
	/* Code quality score (higher is better) */
	static const uint32_t quality[] = {
		10,   /* Interpreter */
		30,   /* TCG */
		50,   /* QBE */
		70,   /* GCCJIT */
		75,   /* LLVM-O0 */
		85,   /* LLVM-O1 */
		95,   /* LLVM-O2 */
		100   /* LLVM-O3 */
	};
	if (tier >= TIER_MAX)
		return 0;
	return quality[tier];
}

compilation_tier_t tier_select_next(function_profile_t *profile)
{
	compilation_tier_t current = profile->current_tier;
	uint64_t count = profile->invocation_count;

	/* Check thresholds for tier transitions */
	if (current == TIER_INTERPRETER && count >= TIER_THRESHOLD_INTERPRETER_TO_TCG)
		return TIER_TCG;
	if (current == TIER_TCG && count >= TIER_THRESHOLD_TCG_TO_QBE)
		return TIER_QBE;
	if (current == TIER_QBE && count >= TIER_THRESHOLD_QBE_TO_GCCJIT)
		return TIER_GCCJIT;
	if (current == TIER_GCCJIT && count >= TIER_THRESHOLD_GCCJIT_TO_LLVM_O0)
		return TIER_LLVM_O0;
	if (current == TIER_LLVM_O0 && count >= TIER_THRESHOLD_LLVM_O0_TO_O1)
		return TIER_LLVM_O1;
	if (current == TIER_LLVM_O1 && count >= TIER_THRESHOLD_LLVM_O1_TO_O2)
		return TIER_LLVM_O2;
	if (current == TIER_LLVM_O2 && count >= TIER_THRESHOLD_LLVM_O2_TO_O3)
		return TIER_LLVM_O3;

	return current; /* Stay at current tier */
}

/***************************************************************************
 * Compilation Queue Implementation
 ***************************************************************************/

compilation_queue_t* compilation_queue_create(uint32_t max_size)
{
	compilation_queue_t *queue = (compilation_queue_t*)calloc(1, sizeof(compilation_queue_t));
	if (!queue)
		return NULL;

	queue->head = NULL;
	queue->tail = NULL;
	queue->size = 0;
	queue->max_size = max_size;
	queue->shutdown = 0;

	pthread_mutex_init(&queue->lock, NULL);
	pthread_cond_init(&queue->cond, NULL);

	return queue;
}

void compilation_queue_destroy(compilation_queue_t *queue)
{
	if (!queue)
		return;

	pthread_mutex_lock(&queue->lock);
	queue->shutdown = 1;
	pthread_cond_broadcast(&queue->cond);
	pthread_mutex_unlock(&queue->lock);

	/* Free all pending requests */
	compilation_request_t *req = queue->head;
	while (req) {
		compilation_request_t *next = req->next;
		free(req);
		req = next;
	}

	pthread_mutex_destroy(&queue->lock);
	pthread_cond_destroy(&queue->cond);
	free(queue);
}

int compilation_queue_enqueue(compilation_queue_t *queue, compilation_request_t *req)
{
	pthread_mutex_lock(&queue->lock);

	if (queue->size >= queue->max_size) {
		pthread_mutex_unlock(&queue->lock);
		return -1; /* Queue full */
	}

	req->next = NULL;
	if (queue->tail) {
		queue->tail->next = req;
		queue->tail = req;
	} else {
		queue->head = queue->tail = req;
	}
	queue->size++;

	pthread_cond_signal(&queue->cond);
	pthread_mutex_unlock(&queue->lock);

	return 0;
}

compilation_request_t* compilation_queue_dequeue(compilation_queue_t *queue)
{
	pthread_mutex_lock(&queue->lock);

	while (!queue->head && !queue->shutdown) {
		pthread_cond_wait(&queue->cond, &queue->lock);
	}

	if (queue->shutdown) {
		pthread_mutex_unlock(&queue->lock);
		return NULL;
	}

	compilation_request_t *req = queue->head;
	queue->head = req->next;
	if (!queue->head)
		queue->tail = NULL;
	queue->size--;

	pthread_mutex_unlock(&queue->lock);

	return req;
}

/***************************************************************************
 * Background Compilation Worker
 ***************************************************************************/

void* compilation_worker_thread(void *arg)
{
	tier_manager_t *mgr = (tier_manager_t*)arg;

	while (1) {
		compilation_request_t *req = compilation_queue_dequeue(mgr->queue);
		if (!req)
			break; /* Shutdown */

		/* Compile the function */
		void *code = tier_manager_compile(mgr, req->address, req->tier);

		if (code) {
			/* Replace the code atomically */
			tier_manager_replace_code(mgr, req->address, req->tier, code);
		} else {
			/* Mark compilation as failed */
			pthread_mutex_lock(&req->profile->lock);
			req->profile->recompilation_failed = 1;
			pthread_mutex_unlock(&req->profile->lock);
		}

		/* Clear pending flag */
		pthread_mutex_lock(&req->profile->lock);
		req->profile->recompilation_pending = 0;
		pthread_mutex_unlock(&req->profile->lock);

		free(req);
	}

	return NULL;
}

/***************************************************************************
 * Tier Manager Implementation
 ***************************************************************************/

tier_manager_t* tier_manager_create(struct cpu *cpu)
{
	tier_manager_t *mgr = (tier_manager_t*)calloc(1, sizeof(tier_manager_t));
	if (!mgr)
		return NULL;

	mgr->cpu = cpu;
	mgr->profile_capacity = 1024;
	mgr->profile_count = 0;
	mgr->profiles = (function_profile_t**)calloc(mgr->profile_capacity, sizeof(function_profile_t*));

	pthread_rwlock_init(&mgr->profile_lock, NULL);

	/* Initialize backends for each tier */
	for (int i = 0; i < TIER_MAX; i++) {
		backend_type_t backend_type = tier_get_backend_type((compilation_tier_t)i);
		mgr->backends[i] = backend_create(backend_type);
		if (mgr->backends[i]) {
			mgr->backends[i]->Initialize(mgr->backends[i]);
			mgr->backends[i]->SetOptimizationLevel(mgr->backends[i], tier_get_opt_level((compilation_tier_t)i));
		}
		mgr->modules[i] = NULL; /* Created on demand */
	}

	/* Create compilation queue */
	mgr->queue = compilation_queue_create(1000);

	/* Start worker threads */
	mgr->num_workers = 4; /* Default 4 workers */
	mgr->worker_threads = (pthread_t*)calloc(mgr->num_workers, sizeof(pthread_t));
	for (uint32_t i = 0; i < mgr->num_workers; i++) {
		pthread_create(&mgr->worker_threads[i], NULL, compilation_worker_thread, mgr);
	}

	/* Configuration */
	mgr->enable_background_compilation = 1;
	mgr->enable_profiling = 1;
	mgr->max_tier = TIER_LLVM_O3;
	mgr->initialized = 1;

	return mgr;
}

void tier_manager_destroy(tier_manager_t *mgr)
{
	if (!mgr)
		return;

	/* Shutdown worker threads */
	compilation_queue_destroy(mgr->queue);
	for (uint32_t i = 0; i < mgr->num_workers; i++) {
		pthread_join(mgr->worker_threads[i], NULL);
	}
	free(mgr->worker_threads);

	/* Destroy backends */
	for (int i = 0; i < TIER_MAX; i++) {
		if (mgr->modules[i])
			mgr->modules[i]->base.Release(mgr->modules[i]);
		if (mgr->backends[i])
			mgr->backends[i]->base.Release(mgr->backends[i]);
	}

	/* Free profiles */
	for (uint32_t i = 0; i < mgr->profile_capacity; i++) {
		if (mgr->profiles[i]) {
			pthread_mutex_destroy(&mgr->profiles[i]->lock);
			free(mgr->profiles[i]);
		}
	}
	free(mgr->profiles);

	pthread_rwlock_destroy(&mgr->profile_lock);
	free(mgr);
}

function_profile_t* tier_manager_get_profile(tier_manager_t *mgr, addr_t address)
{
	/* Simple hash table lookup */
	uint32_t hash = (uint32_t)(address % mgr->profile_capacity);

	pthread_rwlock_rdlock(&mgr->profile_lock);

	/* Linear probing */
	for (uint32_t i = 0; i < mgr->profile_capacity; i++) {
		uint32_t idx = (hash + i) % mgr->profile_capacity;
		if (mgr->profiles[idx] && mgr->profiles[idx]->address == address) {
			function_profile_t *profile = mgr->profiles[idx];
			pthread_rwlock_unlock(&mgr->profile_lock);
			return profile;
		}
		if (!mgr->profiles[idx])
			break;
	}

	pthread_rwlock_unlock(&mgr->profile_lock);

	/* Create new profile */
	pthread_rwlock_wrlock(&mgr->profile_lock);

	/* Check again in case another thread created it */
	for (uint32_t i = 0; i < mgr->profile_capacity; i++) {
		uint32_t idx = (hash + i) % mgr->profile_capacity;
		if (mgr->profiles[idx] && mgr->profiles[idx]->address == address) {
			function_profile_t *profile = mgr->profiles[idx];
			pthread_rwlock_unlock(&mgr->profile_lock);
			return profile;
		}
		if (!mgr->profiles[idx]) {
			/* Create here */
			function_profile_t *profile = (function_profile_t*)calloc(1, sizeof(function_profile_t));
			profile->address = address;
			profile->current_tier = TIER_INTERPRETER;
			profile->target_tier = TIER_INTERPRETER;
			pthread_mutex_init(&profile->lock, NULL);
			mgr->profiles[idx] = profile;
			mgr->profile_count++;
			pthread_rwlock_unlock(&mgr->profile_lock);
			return profile;
		}
	}

	pthread_rwlock_unlock(&mgr->profile_lock);

	/* Table full - should resize, but for now return NULL */
	return NULL;
}

void tier_manager_record_invocation(tier_manager_t *mgr, addr_t address, uint64_t cycles)
{
	if (!mgr->enable_profiling)
		return;

	function_profile_t *profile = tier_manager_get_profile(mgr, address);
	if (!profile)
		return;

	pthread_mutex_lock(&profile->lock);
	profile->invocation_count++;
	profile->total_cycles += cycles;

	/* Check if function is hot */
	if (profile->invocation_count >= TIER_THRESHOLD_TCG_TO_QBE)
		profile->is_hot = 1;

	pthread_mutex_unlock(&profile->lock);

	/* Check if recompilation is needed */
	if (tier_manager_should_recompile(mgr, profile)) {
		compilation_tier_t next_tier = tier_select_next(profile);
		if (next_tier != profile->current_tier && next_tier <= mgr->max_tier) {
			tier_manager_request_recompilation(mgr, address, next_tier);
		}
	}
}

void* tier_manager_get_code(tier_manager_t *mgr, addr_t address)
{
	function_profile_t *profile = tier_manager_get_profile(mgr, address);
	if (!profile)
		return NULL;

	pthread_mutex_lock(&profile->lock);
	void *code = profile->compiled_code[profile->current_tier];
	pthread_mutex_unlock(&profile->lock);

	return code;
}

int tier_manager_should_recompile(tier_manager_t *mgr, function_profile_t *profile)
{
	if (!mgr->enable_background_compilation)
		return 0;

	pthread_mutex_lock(&profile->lock);

	/* Don't recompile if already pending or failed */
	if (profile->recompilation_pending || profile->recompilation_failed) {
		pthread_mutex_unlock(&profile->lock);
		return 0;
	}

	/* Check if we've passed a threshold */
	compilation_tier_t next_tier = tier_select_next(profile);
	int should = (next_tier != profile->current_tier && next_tier <= mgr->max_tier);

	pthread_mutex_unlock(&profile->lock);

	return should;
}

int tier_manager_request_recompilation(tier_manager_t *mgr, addr_t address, compilation_tier_t tier)
{
	function_profile_t *profile = tier_manager_get_profile(mgr, address);
	if (!profile)
		return -1;

	pthread_mutex_lock(&profile->lock);

	/* Check if already pending */
	if (profile->recompilation_pending) {
		pthread_mutex_unlock(&profile->lock);
		return 0;
	}

	profile->recompilation_pending = 1;
	profile->target_tier = tier;

	pthread_mutex_unlock(&profile->lock);

	/* Create compilation request */
	compilation_request_t *req = (compilation_request_t*)calloc(1, sizeof(compilation_request_t));
	req->cpu = mgr->cpu;
	req->address = address;
	req->tier = tier;
	req->profile = profile;

	/* Enqueue */
	if (compilation_queue_enqueue(mgr->queue, req) != 0) {
		pthread_mutex_lock(&profile->lock);
		profile->recompilation_pending = 0;
		pthread_mutex_unlock(&profile->lock);
		free(req);
		return -1;
	}

	return 0;
}

void* tier_manager_compile(tier_manager_t *mgr, addr_t address, compilation_tier_t tier)
{
	/* This is a stub - actual implementation would call cpu_translate_function
	 * with the appropriate backend */
	fprintf(stderr, "tier_manager_compile: addr=0x%llx tier=%s (stub)\n",
	        (unsigned long long)address, tier_get_name(tier));

	/* For now, return NULL to indicate compilation not yet implemented */
	return NULL;
}

int tier_manager_replace_code(tier_manager_t *mgr, addr_t address, compilation_tier_t tier, void *new_code)
{
	function_profile_t *profile = tier_manager_get_profile(mgr, address);
	if (!profile)
		return -1;

	pthread_mutex_lock(&profile->lock);

	/* Store compiled code */
	profile->compiled_code[tier] = new_code;

	/* Update current tier if this is better */
	if (tier > profile->current_tier) {
		profile->current_tier = tier;
		profile->compiled_at_count = profile->invocation_count;
		mgr->total_recompilations++;
		mgr->tier_transitions[profile->current_tier][tier]++;
	}

	mgr->total_compilations++;
	mgr->tier_compilations[tier]++;

	pthread_mutex_unlock(&profile->lock);

	return 0;
}

void tier_manager_print_stats(tier_manager_t *mgr)
{
	printf("\n=== Tiered Compilation Statistics ===\n");
	printf("Total compilations: %llu\n", (unsigned long long)mgr->total_compilations);
	printf("Total recompilations: %llu\n", (unsigned long long)mgr->total_recompilations);
	printf("Total profiles: %u\n", mgr->profile_count);
	printf("\nCompilations by tier:\n");
	for (int i = 0; i < TIER_MAX; i++) {
		if (mgr->tier_compilations[i] > 0) {
			printf("  %-12s: %llu\n", tier_get_name((compilation_tier_t)i),
			       (unsigned long long)mgr->tier_compilations[i]);
		}
	}

	/* Count hot functions */
	uint32_t hot_count = 0;
	pthread_rwlock_rdlock(&mgr->profile_lock);
	for (uint32_t i = 0; i < mgr->profile_capacity; i++) {
		if (mgr->profiles[i] && mgr->profiles[i]->is_hot)
			hot_count++;
	}
	pthread_rwlock_unlock(&mgr->profile_lock);
	printf("\nHot functions: %u (%.1f%%)\n", hot_count,
	       mgr->profile_count > 0 ? (100.0 * hot_count / mgr->profile_count) : 0.0);
}

/***************************************************************************
 * CPU Integration
 ***************************************************************************/

int cpu_init_tiered_compilation(struct cpu *cpu)
{
	if (cpu->tier_mgr)
		return 0; /* Already initialized */

	cpu->tier_mgr = tier_manager_create(cpu);
	return cpu->tier_mgr ? 0 : -1;
}

void cpu_shutdown_tiered_compilation(struct cpu *cpu)
{
	if (cpu->tier_mgr) {
		tier_manager_destroy(cpu->tier_mgr);
		cpu->tier_mgr = NULL;
	}
}

void cpu_set_max_tier(struct cpu *cpu, compilation_tier_t tier)
{
	if (cpu->tier_mgr)
		cpu->tier_mgr->max_tier = tier;
}

void cpu_set_background_compilation(struct cpu *cpu, int enable)
{
	if (cpu->tier_mgr)
		cpu->tier_mgr->enable_background_compilation = enable;
}

void cpu_set_num_compilation_workers(struct cpu *cpu, uint32_t num_workers)
{
	/* Would need to restart worker threads - not implemented yet */
	fprintf(stderr, "cpu_set_num_compilation_workers: not implemented\n");
}
