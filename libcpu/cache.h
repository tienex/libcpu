/*
 * libcpu: cache.h
 *
 * On-disk caching of translated code
 */

#ifndef _CACHE_H_
#define _CACHE_H_

#include "libcpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize cache directory */
int cache_init(cpu_t *cpu);

/* Try to load cached translation from disk */
int cache_load(cpu_t *cpu, const char *cache_key);

/* Save current translation to disk cache */
int cache_save(cpu_t *cpu, const char *cache_key);

/* Compute cache key based on code region and architecture */
void cache_compute_key(cpu_t *cpu, char *key_out, size_t key_size);

/* Clear all cache entries */
void cache_clear(cpu_t *cpu);

#ifdef __cplusplus
}
#endif

#endif /* _CACHE_H_ */
