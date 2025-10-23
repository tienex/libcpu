/*
 * libcpu NanoJIT Backend - Implementation
 *
 * NanoJIT is Mozilla's lightweight JIT from TraceMonkey
 * Originally from Adobe Tamarin/ActionScript
 *
 * Features:
 * - LIR (Low-level Intermediate Representation)
 * - Trace-based compilation
 * - Multiple architectures (x86, ARM, PPC, MIPS)
 * - Small footprint
 * - Fast compilation
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
 * NanoJIT Backend Structure
 ***************************************************************************/

typedef struct NanoJitBackend {
	IBackend interface;
	uint32_t refcount;
	uint32_t opt_level;
	int initialized;
} NanoJitBackend;

/***************************************************************************
 * NanoJIT Backend Implementation
 ***************************************************************************/

static const char* nanojit_backend_get_name(IBackend *self)
{
	return "NanoJIT";
}

static const char* nanojit_backend_get_version(IBackend *self)
{
	return "1.0 (Mozilla TraceMonkey)";
}

static backend_type_t nanojit_backend_get_type(IBackend *self)
{
	return BACKEND_NANOJIT;
}

static int nanojit_backend_initialize(IBackend *self)
{
	NanoJitBackend *backend = (NanoJitBackend*)self;
	fprintf(stderr, "NanoJIT backend: Initialized\n");
	backend->initialized = 1;
	return 0;
}

static void nanojit_backend_shutdown(IBackend *self)
{
	NanoJitBackend *backend = (NanoJitBackend*)self;
	backend->initialized = 0;
}

static IModule* nanojit_backend_create_module(IBackend *self, const char *name)
{
	fprintf(stderr, "NanoJIT backend: CreateModule not yet fully implemented\n");
	return NULL;
}

static void nanojit_backend_set_opt_level(IBackend *self, uint32_t level)
{
	NanoJitBackend *backend = (NanoJitBackend*)self;
	backend->opt_level = (level > 3) ? 3 : level;
}

static uint32_t nanojit_backend_get_opt_level(IBackend *self)
{
	NanoJitBackend *backend = (NanoJitBackend*)self;
	return backend->opt_level;
}

static int nanojit_backend_supports_feature(IBackend *self, const char *feature)
{
	if (strcmp(feature, "trace") == 0) return 1;
	if (strcmp(feature, "lir") == 0) return 1;
	return 0;
}

static const char* nanojit_backend_get_target_triple(IBackend *self)
{
	return "x86_64-unknown-linux";
}

static const char* nanojit_backend_get_data_layout(IBackend *self)
{
	return "e-m:e-i64:64-f80:128-n8:16:32:64-S128";
}

static int nanojit_backend_supports_float80(IBackend *self)
{
	return 0;
}

static int nanojit_backend_supports_float128(IBackend *self)
{
	return 0;
}

extern "C" IBackend* backend_create_nanojit(void)
{
	NanoJitBackend *backend = new NanoJitBackend();
	backend->refcount = 1;
	backend->opt_level = 2;
	backend->initialized = 0;

	/* Setup interface */
	backend->interface.base.AddRef = backend_addref;
	backend->interface.base.Release = backend_release;
	backend->interface.base.QueryInterface = backend_query_interface;
	backend->interface.GetName = nanojit_backend_get_name;
	backend->interface.GetVersion = nanojit_backend_get_version;
	backend->interface.GetType = nanojit_backend_get_type;
	backend->interface.Initialize = nanojit_backend_initialize;
	backend->interface.Shutdown = nanojit_backend_shutdown;
	backend->interface.CreateModule = nanojit_backend_create_module;
	backend->interface.SetOptimizationLevel = nanojit_backend_set_opt_level;
	backend->interface.GetOptimizationLevel = nanojit_backend_get_opt_level;
	backend->interface.SupportsFeature = nanojit_backend_supports_feature;
	backend->interface.GetTargetTriple = nanojit_backend_get_target_triple;
	backend->interface.GetDataLayout = nanojit_backend_get_data_layout;
	backend->interface.SupportsFloat80 = nanojit_backend_supports_float80;
	backend->interface.SupportsFloat128 = nanojit_backend_supports_float128;

	return (IBackend*)backend;
}
