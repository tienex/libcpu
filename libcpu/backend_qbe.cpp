/*
 * libcpu QBE Backend Implementation (Stub)
 *
 * QBE (Quick Backend) is a small C compiler backend
 * https://c9x.me/compile/
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
 * QBE Backend Implementation (Stub)
 ***************************************************************************/
typedef struct QBEBackend {
	IBackend interface;
	uint32_t refcount;
	uint32_t opt_level;
	int initialized;
} QBEBackend;

static const char* qbe_backend_get_name(IBackend *self)
{
	return "QBE";
}

static const char* qbe_backend_get_version(IBackend *self)
{
	return "1.0";
}

static backend_type_t qbe_backend_get_type(IBackend *self)
{
	return BACKEND_QBE;
}

/* Forward declaration from full implementation */
struct QBEModule;
extern "C" QBEModule* qbe_module_create(QBEBackend *backend, const char *name);

static int qbe_backend_initialize(IBackend *self)
{
	QBEBackend *backend = (QBEBackend*)self;
	if (backend->initialized)
		return 0;

	/* QBE backend initialization - context creation handled per-module */
	backend->initialized = 1;
	return 0;
}

static void qbe_backend_shutdown(IBackend *self)
{
	QBEBackend *backend = (QBEBackend*)self;
	backend->initialized = 0;
}

static IModule* qbe_backend_create_module(IBackend *self, const char *name)
{
	QBEBackend *backend = (QBEBackend*)self;
	if (!backend->initialized)
		qbe_backend_initialize(self);

	/* Delegate to full implementation */
	return (IModule*)qbe_module_create(backend, name);
}

static void qbe_backend_set_opt_level(IBackend *self, uint32_t level)
{
	QBEBackend *backend = (QBEBackend*)self;
	backend->opt_level = (level > 3) ? 3 : level;
}

static uint32_t qbe_backend_get_opt_level(IBackend *self)
{
	QBEBackend *backend = (QBEBackend*)self;
	return backend->opt_level;
}

static int qbe_backend_supports_feature(IBackend *self, const char *feature)
{
	/* QBE has limited feature support */
	if (strcmp(feature, "float") == 0)
		return 1;
	if (strcmp(feature, "double") == 0)
		return 1;
	return 0;
}

static const char* qbe_backend_get_target_triple(IBackend *self)
{
	return "x86_64-unknown-linux";
}

static const char* qbe_backend_get_data_layout(IBackend *self)
{
	return "e-m:e-i64:64-f80:128-n8:16:32:64-S128";
}

static int qbe_backend_supports_float80(IBackend *self)
{
	return 0; /* QBE doesn't support FP80 */
}

static int qbe_backend_supports_float128(IBackend *self)
{
	return 0; /* QBE doesn't support FP128 */
}

extern "C" IBackend* backend_create_qbe(void)
{
	QBEBackend *backend = new QBEBackend();
	backend->refcount = 1;
	backend->opt_level = 2;
	backend->initialized = 0;

	/* Setup interface */
	backend->interface.base.AddRef = backend_addref;
	backend->interface.base.Release = backend_release;
	backend->interface.base.QueryInterface = backend_query_interface;
	backend->interface.GetName = qbe_backend_get_name;
	backend->interface.GetVersion = qbe_backend_get_version;
	backend->interface.GetType = qbe_backend_get_type;
	backend->interface.Initialize = qbe_backend_initialize;
	backend->interface.Shutdown = qbe_backend_shutdown;
	backend->interface.CreateModule = qbe_backend_create_module;
	backend->interface.SetOptimizationLevel = qbe_backend_set_opt_level;
	backend->interface.GetOptimizationLevel = qbe_backend_get_opt_level;
	backend->interface.SupportsFeature = qbe_backend_supports_feature;
	backend->interface.GetTargetTriple = qbe_backend_get_target_triple;
	backend->interface.GetDataLayout = qbe_backend_get_data_layout;
	backend->interface.SupportsFloat80 = qbe_backend_supports_float80;
	backend->interface.SupportsFloat128 = qbe_backend_supports_float128;

	return (IBackend*)backend;
}
