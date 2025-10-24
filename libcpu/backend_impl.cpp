/*
 * libcpu Backend Factory Implementation
 */

#include "backend.h"
#include <stdlib.h>
#include <string.h>

/* Backend creation prototypes */
extern "C" {
	IBackend* backend_create_llvm(void);
	IBackend* backend_create_qbe(void);
	IBackend* backend_create_gccjit(void);
	IBackend* backend_create_tcg(void);
	IBackend* backend_create_asmjit(void);
	IBackend* backend_create_dynasm(void);
	IBackend* backend_create_sljit(void);
	IBackend* backend_create_nanojit(void);
	IBackend* backend_create_mir(void);
	IBackend* backend_create_cranelift(void);
	IBackend* backend_create_libjit(void);
	IBackend* backend_create_nj(void);
}

static const char* backend_names[] = {
	"LLVM",
	"QBE",
	"GCCJIT",
	"TCG",
	"AsmJit",
	"DynASM",
	"SLJIT",
	"NanoJIT",
	"MIR",
	"Cranelift",
	"LibJIT",
	"nj"
};

/*
 * Create backend by type
 */
IBackend* backend_create(backend_type_t type)
{
	switch (type) {
	case BACKEND_LLVM:
		return backend_create_llvm();
	case BACKEND_QBE:
		return backend_create_qbe();
	case BACKEND_GCCJIT:
		return backend_create_gccjit();
	case BACKEND_TCG:
		return backend_create_tcg();
	case BACKEND_ASMJIT:
		return backend_create_asmjit();
	case BACKEND_DYNASM:
		return backend_create_dynasm();
	case BACKEND_SLJIT:
		return backend_create_sljit();
	case BACKEND_NANOJIT:
		return backend_create_nanojit();
	case BACKEND_MIR:
		return backend_create_mir();
	case BACKEND_CRANELIFT:
		return backend_create_cranelift();
	case BACKEND_LIBJIT:
		return backend_create_libjit();
	case BACKEND_NJ:
		return backend_create_nj();
	default:
		return NULL;
	}
}

/*
 * Get backend name from type
 */
const char* backend_get_name(backend_type_t type)
{
	if (type >= BACKEND_MAX)
		return NULL;
	return backend_names[type];
}

/*
 * List available backends
 */
uint32_t backend_get_available(backend_type_t *types, uint32_t max_count)
{
	uint32_t count = 0;

	/* Check each backend */
	for (uint32_t i = 0; i < BACKEND_MAX && count < max_count; i++) {
		IBackend *backend = backend_create((backend_type_t)i);
		if (backend != NULL) {
			types[count++] = (backend_type_t)i;
			backend->base.Release(backend);
		}
	}

	return count;
}

/*
 * Base IUnknown implementation helpers
 */
typedef struct BackendObject {
	uint32_t refcount;
} BackendObject;

uint32_t backend_addref(void *self)
{
	BackendObject *obj = (BackendObject*)self;
	return ++obj->refcount;
}

uint32_t backend_release(void *self)
{
	BackendObject *obj = (BackendObject*)self;
	uint32_t count = --obj->refcount;
	if (count == 0) {
		free(obj);
	}
	return count;
}

int backend_query_interface(void *self, const char *iid, void **out)
{
	if (out == NULL || iid == NULL)
		return -1;

	/* Check for IUnknown interface - all objects support this */
	if (strcmp(iid, "IUnknown") == 0 || strcmp(iid, "IBackend") == 0 ||
	    strcmp(iid, "IModule") == 0 || strcmp(iid, "IFunction") == 0 ||
	    strcmp(iid, "IBasicBlock") == 0 || strcmp(iid, "IBuilder") == 0 ||
	    strcmp(iid, "IValue") == 0 || strcmp(iid, "IType") == 0) {
		*out = self;
		((IUnknown*)self)->AddRef(self);
		return 0;
	}

	/* Interface not supported */
	*out = NULL;
	return -1;
}
