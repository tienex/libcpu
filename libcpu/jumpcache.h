/*
 * libcpu: jumpcache.h
 *
 * Indirect jump cache for efficient block chaining
 */

#ifndef _JUMPCACHE_H_
#define _JUMPCACHE_H_

#include "libcpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Jump cache entry structure */
typedef struct jump_cache_entry {
	addr_t target_pc;     /* Target address */
	void *target_code;    /* Pointer to compiled code */
	uint64_t hit_count;   /* Number of times this target was hit */
} jump_cache_entry_t;

/* Jump cache structure - hash table for O(1) lookup */
#define JUMP_CACHE_SIZE 4096
#define JUMP_CACHE_MASK (JUMP_CACHE_SIZE - 1)

typedef struct jump_cache {
	jump_cache_entry_t entries[JUMP_CACHE_SIZE];
	uint64_t total_hits;
	uint64_t total_misses;
} jump_cache_t;

/* Initialize jump cache */
void jump_cache_init(jump_cache_t *cache);

/* Lookup target in jump cache */
void *jump_cache_lookup(jump_cache_t *cache, addr_t target_pc);

/* Add entry to jump cache */
void jump_cache_add(jump_cache_t *cache, addr_t target_pc, void *target_code);

/* Clear all cache entries */
void jump_cache_clear(jump_cache_t *cache);

/* Print statistics */
void jump_cache_print_stats(jump_cache_t *cache);

#ifdef __cplusplus
}
#endif

#endif /* _JUMPCACHE_H_ */
