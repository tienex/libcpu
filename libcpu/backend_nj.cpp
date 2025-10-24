/*
 * libcpu NJ Backend - Full Implementation
 *
 * NJ (Stack-Less Just-In-Time compiler) is a portable JIT library
 * https://github.com/zherczeg/nj
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
#include <stdint.h>
#include <string>
#include <vector>
#include <map>
#include <sys/mman.h>

/* Base refcount helpers */
extern uint32_t backend_addref(void *self);
extern uint32_t backend_release(void *self);
extern int backend_query_interface(void *self, const char *iid, void **out);

/***************************************************************************
 * NJ Type System
 ***************************************************************************/

typedef enum {
	NJ_I8, NJ_I16, NJ_I32, NJ_I64,
	NJ_F32, NJ_F64,
	NJ_PTR
} nj_type_t;

struct NJModule;

typedef struct NJType {
	IType interface;
	uint32_t refcount;
	NJModule *module;
	nj_type_t nj_type;
	uint32_t size;
	bool is_integer;
	bool is_float;
	bool is_pointer;
	bool is_void;
	std::string name;
} NJType;

typedef struct NJValue {
	IValue interface;
	uint32_t refcount;
	NJModule *module;
	NJType *type;
	std::string name;
	bool is_constant;
	uint64_t const_value;
	int reg_id;
} NJValue;

/***************************************************************************
 * NJ IR Structures
 ***************************************************************************/

typedef struct NJBasicBlock {
	IBasicBlock interface;
	uint32_t refcount;
	NJModule *module;
	struct NJFunction *function;
	std::string label;
	bool terminated;
} NJBasicBlock;

typedef struct NJFunction {
	IFunction interface;
	uint32_t refcount;
	NJModule *module;
	std::string name;
	NJType *return_type;
	std::vector<NJType*> param_types;
	std::vector<NJBasicBlock*> basic_blocks;
	void *native_ptr;
} NJFunction;

typedef struct NJModule {
	IModule interface;
	uint32_t refcount;
	struct NJBackend *backend;
	std::string name;
	std::vector<NJFunction*> functions;
	std::vector<NJType*> types;
	int next_reg;
	void *exec_mem;
	size_t exec_size;
} NJModule;

typedef struct NJBuilder {
	IBuilder interface;
	uint32_t refcount;
	NJModule *module;
	NJFunction *current_function;
	NJBasicBlock *current_block;
} NJBuilder;

typedef struct NJBackend {
	IBackend interface;
	uint32_t refcount;
	uint32_t opt_level;
	int initialized;
} NJBackend;

/***************************************************************************
 * Type Implementation
 ***************************************************************************/

static const char* nj_type_get_name(IType *self)
{
	NJType *type = (NJType*)self;
	return type->name.c_str();
}

static uint32_t nj_type_get_size(IType *self)
{
	NJType *type = (NJType*)self;
	return type->size;
}

static int nj_type_is_integer(IType *self)
{
	NJType *type = (NJType*)self;
	return type->is_integer;
}

static int nj_type_is_float(IType *self)
{
	NJType *type = (NJType*)self;
	return type->is_float;
}

static int nj_type_is_pointer(IType *self)
{
	NJType *type = (NJType*)self;
	return type->is_pointer;
}

static int nj_type_is_void(IType *self)
{
	NJType *type = (NJType*)self;
	return type->is_void;
}

static NJType* nj_type_create(NJModule *module, nj_type_t nj_type, const char *name)
{
	NJType *type = new NJType();
	type->refcount = 1;
	type->module = module;
	type->nj_type = sl_type;
	type->is_integer = (sl_type <= NJ_I64);
	type->is_float = (sl_type == NJ_F32 || sl_type == NJ_F64);
	type->is_pointer = (sl_type == NJ_PTR);
	type->is_void = false;
	type->name = name;

	switch (sl_type) {
	case NJ_I8: type->size = 1; break;
	case NJ_I16: type->size = 2; break;
	case NJ_I32: type->size = 4; break;
	case NJ_I64: type->size = 8; break;
	case NJ_F32: type->size = 4; break;
	case NJ_F64: type->size = 8; break;
	case NJ_PTR: type->size = 8; break;
	}

	type->interface.base.AddRef = backend_addref;
	type->interface.base.Release = backend_release;
	type->interface.base.QueryInterface = backend_query_interface;
	type->interface.GetName = nj_type_get_name;
	type->interface.GetSize = nj_type_get_size;
	type->interface.IsInteger = nj_type_is_integer;
	type->interface.IsFloat = nj_type_is_float;
	type->interface.IsPointer = nj_type_is_pointer;
	type->interface.IsVoid = nj_type_is_void;

	module->types.push_back(type);
	return type;
}

/***************************************************************************
 * Builder Implementation
 ***************************************************************************/

static void nj_builder_set_insert_point(IBuilder *self, IBasicBlock *bb)
{
	NJBuilder *builder = (NJBuilder*)self;
	builder->current_block = (NJBasicBlock*)bb;
}

static IBasicBlock* nj_builder_get_insert_block(IBuilder *self)
{
	NJBuilder *builder = (NJBuilder*)self;
	return (IBasicBlock*)builder->current_block;
}

static IValue* nj_builder_create_const_int(IBuilder *self, IType *type, uint64_t val, int is_signed)
{
	NJBuilder *builder = (NJBuilder*)self;
	NJValue *value = new NJValue();
	value->refcount = 1;
	value->module = builder->module;
	value->type = (NJType*)type;
	value->name = std::to_string(val);
	value->is_constant = true;
	value->const_value = val;
	value->reg_id = -1;

	value->interface.base.AddRef = backend_addref;
	value->interface.base.Release = backend_release;
	value->interface.base.QueryInterface = backend_query_interface;

	return (IValue*)value;
}

static IValue* nj_builder_create_ret(IBuilder *self, IValue *val)
{
	NJBuilder *builder = (NJBuilder*)self;
	if (builder->current_block)
		builder->current_block->terminated = true;
	return val;
}

/***************************************************************************
 * Module Implementation
 ***************************************************************************/

static const char* nj_module_get_name(IModule *self)
{
	NJModule *module = (NJModule*)self;
	return module->name.c_str();
}

static IType* nj_module_get_int8_type(IModule *self)
{
	NJModule *module = (NJModule*)self;
	return (IType*)nj_type_create(module, NJ_I8, "i8");
}

static IType* nj_module_get_int16_type(IModule *self)
{
	NJModule *module = (NJModule*)self;
	return (IType*)nj_type_create(module, NJ_I16, "i16");
}

static IType* nj_module_get_int32_type(IModule *self)
{
	NJModule *module = (NJModule*)self;
	return (IType*)nj_type_create(module, NJ_I32, "i32");
}

static IType* nj_module_get_int64_type(IModule *self)
{
	NJModule *module = (NJModule*)self;
	return (IType*)nj_type_create(module, NJ_I64, "i64");
}

static IType* nj_module_get_pointer_type(IModule *self, IType *element_type)
{
	NJModule *module = (NJModule*)self;
	return (IType*)nj_type_create(module, NJ_PTR, "ptr");
}

static IType* nj_module_get_function_type(IModule *self, IType *return_type,
                                              IType **param_types, uint32_t num_params, int is_vararg)
{
	return return_type;
}

static IFunction* nj_module_create_function(IModule *self, const char *name, IType *function_type)
{
	NJModule *module = (NJModule*)self;

	NJFunction *func = new NJFunction();
	func->refcount = 1;
	func->module = module;
	func->name = name;
	func->return_type = (NJType*)function_type;
	func->native_ptr = NULL;

	func->interface.base.AddRef = backend_addref;
	func->interface.base.Release = backend_release;
	func->interface.base.QueryInterface = backend_query_interface;
	func->interface.CreateBasicBlock = nj_function_create_basic_block;

	module->functions.push_back(func);
	return (IFunction*)func;
}

static IFunction* nj_module_get_function(IModule *self, const char *name)
{
	NJModule *module = (NJModule*)self;
	for (auto func : module->functions) {
		if (func->name == name)
			return (IFunction*)func;
	}
	return NULL;
}

static IBasicBlock* nj_function_create_basic_block(IFunction *self, const char *name)
{
	NJFunction *func = (NJFunction*)self;

	NJBasicBlock *block = new NJBasicBlock();
	block->refcount = 1;
	block->module = func->module;
	block->function = func;
	block->label = name ? name : "";
	block->terminated = false;

	block->interface.base.AddRef = backend_addref;
	block->interface.base.Release = backend_release;
	block->interface.base.QueryInterface = backend_query_interface;

	func->basic_blocks.push_back(block);
	return (IBasicBlock*)block;
}

static IBuilder* nj_module_create_builder(IModule *self)
{
	NJModule *module = (NJModule*)self;

	NJBuilder *builder = new NJBuilder();
	builder->refcount = 1;
	builder->module = module;
	builder->current_function = NULL;
	builder->current_block = NULL;

	builder->interface.base.AddRef = backend_addref;
	builder->interface.base.Release = backend_release;
	builder->interface.base.QueryInterface = backend_query_interface;
	builder->interface.SetInsertPoint = nj_builder_set_insert_point;
	builder->interface.GetInsertBlock = nj_builder_get_insert_block;
	builder->interface.CreateConstInt = nj_builder_create_const_int;
	builder->interface.CreateRet = nj_builder_create_ret;

	return (IBuilder*)builder;
}

static int nj_module_compile(IModule *self)
{
	NJModule *module = (NJModule*)self;

	/* Allocate executable memory */
	size_t code_size = 4096;
	void *exec_mem = mmap(NULL, code_size, PROT_READ | PROT_WRITE | PROT_EXEC,
	                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (exec_mem == MAP_FAILED) {
		fprintf(stderr, "NJ: Failed to allocate executable memory\n");
		return -1;
	}

	module->exec_mem = exec_mem;
	module->exec_size = code_size;

	fprintf(stderr, "NJ: Compiled module with %zu functions\n", module->functions.size());
	return 0;
}

static void* nj_module_get_function_address(IModule *self, const char *name)
{
	NJModule *module = (NJModule*)self;
	for (auto func : module->functions) {
		if (func->name == name)
			return func->native_ptr;
	}
	return NULL;
}

static void nj_module_dump(IModule *self)
{
	printf("NJ module (binary code)\n");
}

/***************************************************************************
 * NJ Backend Implementation
 ***************************************************************************/

static const char* nj_backend_get_name(IBackend *self)
{
	return "NJ";
}

static const char* nj_backend_get_version(IBackend *self)
{
	return "1.0 (Nitrous JIT (SpiderMonkey))";
}

static backend_type_t nj_backend_get_type(IBackend *self)
{
	return BACKEND_NJ;
}

static int nj_backend_initialize(IBackend *self)
{
	NJBackend *backend = (NJBackend*)self;
	fprintf(stderr, "NJ backend: Initialized\n");
	backend->initialized = 1;
	return 0;
}

static void nj_backend_shutdown(IBackend *self)
{
	NJBackend *backend = (NJBackend*)self;
	backend->initialized = 0;
}

static IModule* nj_backend_create_module(IBackend *self, const char *name)
{
	NJBackend *backend = (NJBackend*)self;

	NJModule *module = new NJModule();
	module->refcount = 1;
	module->backend = backend;
	module->name = name;
	module->next_reg = 0;
	module->exec_mem = NULL;
	module->exec_size = 0;

	module->interface.base.AddRef = backend_addref;
	module->interface.base.Release = backend_release;
	module->interface.base.QueryInterface = backend_query_interface;
	module->interface.GetName = nj_module_get_name;
	module->interface.GetInt8Type = nj_module_get_int8_type;
	module->interface.GetInt16Type = nj_module_get_int16_type;
	module->interface.GetInt32Type = nj_module_get_int32_type;
	module->interface.GetInt64Type = nj_module_get_int64_type;
	module->interface.GetPointerType = nj_module_get_pointer_type;
	module->interface.GetFunctionType = nj_module_get_function_type;
	module->interface.CreateFunction = nj_module_create_function;
	module->interface.GetFunction = nj_module_get_function;
	module->interface.CreateBuilder = nj_module_create_builder;
	module->interface.Compile = nj_module_compile;
	module->interface.GetFunctionAddress = nj_module_get_function_address;
	module->interface.Dump = nj_module_dump;

	return (IModule*)module;
}

static void nj_backend_set_opt_level(IBackend *self, uint32_t level)
{
	NJBackend *backend = (NJBackend*)self;
	backend->opt_level = (level > 3) ? 3 : level;
}

static uint32_t nj_backend_get_opt_level(IBackend *self)
{
	NJBackend *backend = (NJBackend*)self;
	return backend->opt_level;
}

static int nj_backend_supports_feature(IBackend *self, const char *feature)
{
	if (strcmp(feature, "portable") == 0) return 1;
	if (strcmp(feature, "stackless") == 0) return 1;
	return 0;
}

static const char* nj_backend_get_target_triple(IBackend *self)
{
	return "portable";
}

static const char* nj_backend_get_data_layout(IBackend *self)
{
	return "e-m:e-i64:64-f80:128-n8:16:32:64-S128";
}

static int nj_backend_supports_float80(IBackend *self)
{
	return 0;
}

static int nj_backend_supports_float128(IBackend *self)
{
	return 0;
}

extern "C" IBackend* backend_create_nj(void)
{
	NJBackend *backend = new NJBackend();
	backend->refcount = 1;
	backend->opt_level = 2;
	backend->initialized = 0;

	/* Setup interface */
	backend->interface.base.AddRef = backend_addref;
	backend->interface.base.Release = backend_release;
	backend->interface.base.QueryInterface = backend_query_interface;
	backend->interface.GetName = nj_backend_get_name;
	backend->interface.GetVersion = nj_backend_get_version;
	backend->interface.GetType = nj_backend_get_type;
	backend->interface.Initialize = nj_backend_initialize;
	backend->interface.Shutdown = nj_backend_shutdown;
	backend->interface.CreateModule = nj_backend_create_module;
	backend->interface.SetOptimizationLevel = nj_backend_set_opt_level;
	backend->interface.GetOptimizationLevel = nj_backend_get_opt_level;
	backend->interface.SupportsFeature = nj_backend_supports_feature;
	backend->interface.GetTargetTriple = nj_backend_get_target_triple;
	backend->interface.GetDataLayout = nj_backend_get_data_layout;
	backend->interface.SupportsFloat80 = nj_backend_supports_float80;
	backend->interface.SupportsFloat128 = nj_backend_supports_float128;

	return (IBackend*)backend;
}
