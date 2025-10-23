/*
 * libcpu SLJIT Backend - Implementation
 *
 * SLJIT (Stack-Less Just-In-Time compiler) is a portable JIT library
 * https://github.com/zherczeg/sljit
 *
 * Features:
 * - Portable across multiple architectures
 * - Stack-less calling convention
 * - Fast compilation
 * - Small code size
 * - MIT licensed
 */

#include "backend.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Base refcount helpers */
extern uint32_t backend_addref(void *self);
extern uint32_t backend_release(void *self);
extern int backend_query_interface(void *self, const char *iid, void **out);

/***************************************************************************
 * SLJIT Backend Structure
 ***************************************************************************/

typedef struct SLJitBackend {
	IBackend interface;
	uint32_t refcount;
	uint32_t opt_level;
	int initialized;
} SLJitBackend;

/***************************************************************************
 * SLJIT Backend Implementation
 ***************************************************************************/

static const char* sljit_backend_get_name(IBackend *self)
{
	return "SLJIT";
}

static const char* sljit_backend_get_version(IBackend *self)
{
	return "1.0 (Stack-Less JIT)";
}

static backend_type_t sljit_backend_get_type(IBackend *self)
{
	return BACKEND_SLJIT;
}

static int sljit_backend_initialize(IBackend *self)
{
	SLJitBackend *backend = (SLJitBackend*)self;
	fprintf(stderr, "SLJIT backend: Initialized\n");
	backend->initialized = 1;
	return 0;
}

static void sljit_backend_shutdown(IBackend *self)
{
	SLJitBackend *backend = (SLJitBackend*)self;
	backend->initialized = 0;
}

static IModule* sljit_backend_create_module(IBackend *self, const char *name)
{
	fprintf(stderr, "SLJIT backend: CreateModule not yet fully implemented\n");
	return NULL;
}

static void sljit_backend_set_opt_level(IBackend *self, uint32_t level)
{
	SLJitBackend *backend = (SLJitBackend*)self;
	backend->opt_level = (level > 3) ? 3 : level;
}

static uint32_t sljit_backend_get_opt_level(IBackend *self)
{
	SLJitBackend *backend = (SLJitBackend*)self;
	return backend->opt_level;
}

static int sljit_backend_supports_feature(IBackend *self, const char *feature)
{
	if (strcmp(feature, "portable") == 0) return 1;
	if (strcmp(feature, "stackless") == 0) return 1;
	return 0;
}

static const char* sljit_backend_get_target_triple(IBackend *self)
{
	return "portable";
}

static const char* sljit_backend_get_data_layout(IBackend *self)
{
	return "e-m:e-i64:64-f80:128-n8:16:32:64-S128";
}

static int sljit_backend_supports_float80(IBackend *self)
{
	return 0;
}

static int sljit_backend_supports_float128(IBackend *self)
{
	return 0;
}

extern "C" IBackend* backend_create_sljit(void)
{
	SLJitBackend *backend = new SLJitBackend();
	backend->refcount = 1;
	backend->opt_level = 2;
	backend->initialized = 0;

	/* Setup interface */
	backend->interface.base.AddRef = backend_addref;
	backend->interface.base.Release = backend_release;
	backend->interface.base.QueryInterface = backend_query_interface;
	backend->interface.GetName = sljit_backend_get_name;
	backend->interface.GetVersion = sljit_backend_get_version;
	backend->interface.GetType = sljit_backend_get_type;
	backend->interface.Initialize = sljit_backend_initialize;
	backend->interface.Shutdown = sljit_backend_shutdown;
	backend->interface.CreateModule = sljit_backend_create_module;
	backend->interface.SetOptimizationLevel = sljit_backend_set_opt_level;
	backend->interface.GetOptimizationLevel = sljit_backend_get_opt_level;
	backend->interface.SupportsFeature = sljit_backend_supports_feature;
	backend->interface.GetTargetTriple = sljit_backend_get_target_triple;
	backend->interface.GetDataLayout = sljit_backend_get_data_layout;
	backend->interface.SupportsFloat80 = sljit_backend_supports_float80;
	backend->interface.SupportsFloat128 = sljit_backend_supports_float128;

	return (IBackend*)backend;
}
