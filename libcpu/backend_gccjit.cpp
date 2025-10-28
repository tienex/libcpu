/*
 * libcpu GCCJIT Backend Implementation (Stub)
 *
 * GNU GCC Just-In-Time compilation library
 * https://gcc.gnu.org/onlinedocs/jit/
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
 * GCCJIT Backend Implementation (Stub)
 ***************************************************************************/
typedef struct GCCJITBackend {
	IBackend interface;
	uint32_t refcount;
	uint32_t opt_level;
	int initialized;
} GCCJITBackend;

static const char* gccjit_backend_get_name(IBackend *self)
{
	return "GCCJIT";
}

static const char* gccjit_backend_get_version(IBackend *self)
{
	return "1.0";
}

static backend_type_t gccjit_backend_get_type(IBackend *self)
{
	return BACKEND_GCCJIT;
}

/* Forward declaration from full implementation */
struct GCCJITModule;
extern "C" GCCJITModule* gccjit_module_create_internal(const char *name, uint32_t opt_level);

static int gccjit_backend_initialize(IBackend *self)
{
	GCCJITBackend *backend = (GCCJITBackend*)self;
	if (backend->initialized)
		return 0;

	/* GCCJIT backend initialization - context creation handled per-module */
	backend->initialized = 1;
	return 0;
}

static void gccjit_backend_shutdown(IBackend *self)
{
	GCCJITBackend *backend = (GCCJITBackend*)self;
	backend->initialized = 0;
}

static IModule* gccjit_backend_create_module(IBackend *self, const char *name)
{
	GCCJITBackend *backend = (GCCJITBackend*)self;
	if (!backend->initialized)
		gccjit_backend_initialize(self);

	/* Delegate to full implementation */
	return (IModule*)gccjit_module_create_internal(name, backend->opt_level);
}

static void gccjit_backend_set_opt_level(IBackend *self, uint32_t level)
{
	GCCJITBackend *backend = (GCCJITBackend*)self;
	backend->opt_level = (level > 3) ? 3 : level;
}

static uint32_t gccjit_backend_get_opt_level(IBackend *self)
{
	GCCJITBackend *backend = (GCCJITBackend*)self;
	return backend->opt_level;
}

static int gccjit_backend_supports_feature(IBackend *self, const char *feature)
{
	/* GCCJIT supports most features GCC supports */
	return 1;
}

static const char* gccjit_backend_get_target_triple(IBackend *self)
{
#if defined(__x86_64__) || defined(_M_X64)
	return "x86_64-unknown-linux-gnu";
#elif defined(__i386__) || defined(_M_IX86)
	return "i686-unknown-linux-gnu";
#elif defined(__aarch64__)
	return "aarch64-unknown-linux-gnu";
#elif defined(__arm__)
	return "arm-unknown-linux-gnueabihf";
#else
	return "unknown-unknown-unknown";
#endif
}

static const char* gccjit_backend_get_data_layout(IBackend *self)
{
#if defined(__x86_64__) || defined(_M_X64)
	return "e-m:e-i64:64-f80:128-n8:16:32:64-S128";
#else
	return "e-m:e-p:32:32-i64:64-n32-S128";
#endif
}

static int gccjit_backend_supports_float80(IBackend *self)
{
#if defined(__x86_64__) || defined(_M_X64)
	return 1; /* x86-64 supports FP80 via long double */
#else
	return 0;
#endif
}

static int gccjit_backend_supports_float128(IBackend *self)
{
#if defined(__x86_64__) || defined(_M_X64)
	return 1; /* GCC supports __float128 on x86-64 */
#else
	return 0;
#endif
}

extern "C" IBackend* backend_create_gccjit(void)
{
	GCCJITBackend *backend = new GCCJITBackend();
	backend->refcount = 1;
	backend->opt_level = 2;
	backend->initialized = 0;

	/* Setup interface */
	backend->interface.base.AddRef = backend_addref;
	backend->interface.base.Release = backend_release;
	backend->interface.base.QueryInterface = backend_query_interface;
	backend->interface.GetName = gccjit_backend_get_name;
	backend->interface.GetVersion = gccjit_backend_get_version;
	backend->interface.GetType = gccjit_backend_get_type;
	backend->interface.Initialize = gccjit_backend_initialize;
	backend->interface.Shutdown = gccjit_backend_shutdown;
	backend->interface.CreateModule = gccjit_backend_create_module;
	backend->interface.SetOptimizationLevel = gccjit_backend_set_opt_level;
	backend->interface.GetOptimizationLevel = gccjit_backend_get_opt_level;
	backend->interface.SupportsFeature = gccjit_backend_supports_feature;
	backend->interface.GetTargetTriple = gccjit_backend_get_target_triple;
	backend->interface.GetDataLayout = gccjit_backend_get_data_layout;
	backend->interface.SupportsFloat80 = gccjit_backend_supports_float80;
	backend->interface.SupportsFloat128 = gccjit_backend_supports_float128;

	return (IBackend*)backend;
}
