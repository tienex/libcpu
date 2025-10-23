/*
 * libcpu: jumpcache.cpp
 *
 * Indirect jump cache implementation
 */

#include <stdio.h>
#include <string.h>
#include "jumpcache.h"

/* Simple hash function for addresses */
static inline uint32_t
hash_addr(addr_t addr)
{
	/* Mix the address bits for better distribution */
	uint64_t h = addr;
	h ^= h >> 33;
	h *= 0xff51afd7ed558ccdULL;
	h ^= h >> 33;
	h *= 0xc4ceb9fe1a85ec53ULL;
	h ^= h >> 33;
	return (uint32_t)(h & JUMP_CACHE_MASK);
}

void
jump_cache_init(jump_cache_t *cache)
{
	memset(cache, 0, sizeof(jump_cache_t));
}

void *
jump_cache_lookup(jump_cache_t *cache, addr_t target_pc)
{
	uint32_t index = hash_addr(target_pc);
	jump_cache_entry_t *entry = &cache->entries[index];

	/* Check if entry matches */
	if (entry->target_pc == target_pc && entry->target_code != NULL) {
		entry->hit_count++;
		cache->total_hits++;
		return entry->target_code;
	}

	cache->total_misses++;
	return NULL;
}

void
jump_cache_add(jump_cache_t *cache, addr_t target_pc, void *target_code)
{
	uint32_t index = hash_addr(target_pc);
	jump_cache_entry_t *entry = &cache->entries[index];

	/* Simple replacement: always replace existing entry */
	entry->target_pc = target_pc;
	entry->target_code = target_code;
	entry->hit_count = 0;
}

void
jump_cache_clear(jump_cache_t *cache)
{
	memset(cache->entries, 0, sizeof(cache->entries));
	/* Keep statistics */
}

void
jump_cache_print_stats(jump_cache_t *cache)
{
	uint64_t total = cache->total_hits + cache->total_misses;
	double hit_rate = total > 0 ? (100.0 * cache->total_hits) / total : 0.0;

	printf("Jump Cache Statistics:\n");
	printf("  Total lookups: %llu\n", (unsigned long long)total);
	printf("  Hits:          %llu\n", (unsigned long long)cache->total_hits);
	printf("  Misses:        %llu\n", (unsigned long long)cache->total_misses);
	printf("  Hit rate:      %.2f%%\n", hit_rate);

	/* Find most frequently used entries */
	printf("  Top jump targets:\n");
	for (int i = 0; i < JUMP_CACHE_SIZE; i++) {
		jump_cache_entry_t *entry = &cache->entries[i];
		if (entry->hit_count > 100) {
			printf("    0x%llx: %llu hits\n",
				   (unsigned long long)entry->target_pc,
				   (unsigned long long)entry->hit_count);
		}
	}
}
