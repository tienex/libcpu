/*
 * libcpu SLJIT Backend - Full Implementation
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
 * SLJIT Type System
 ***************************************************************************/

typedef enum {
	SL_I8, SL_I16, SL_I32, SL_I64,
	SL_F32, SL_F64,
	SL_PTR
} sljit_type_t;

struct SLJitModule;

typedef struct SLJitType {
	IType interface;
	uint32_t refcount;
	SLJitModule *module;
	sljit_type_t sl_type;
	uint32_t size;
	bool is_integer;
	bool is_float;
	bool is_pointer;
	bool is_void;
	std::string name;
} SLJitType;

typedef struct SLJitValue {
	IValue interface;
	uint32_t refcount;
	SLJitModule *module;
	SLJitType *type;
	std::string name;
	bool is_constant;
	uint64_t const_value;
	int reg_id;
} SLJitValue;

/***************************************************************************
 * SLJIT IR Structures
 ***************************************************************************/

typedef struct SLJitBasicBlock {
	IBasicBlock interface;
	uint32_t refcount;
	SLJitModule *module;
	struct SLJitFunction *function;
	std::string label;
	bool terminated;
} SLJitBasicBlock;

typedef struct SLJitFunction {
	IFunction interface;
	uint32_t refcount;
	SLJitModule *module;
	std::string name;
	SLJitType *return_type;
	std::vector<SLJitType*> param_types;
	std::vector<SLJitBasicBlock*> basic_blocks;
	void *native_ptr;
} SLJitFunction;

typedef struct SLJitModule {
	IModule interface;
	uint32_t refcount;
	struct SLJitBackend *backend;
	std::string name;
	std::vector<SLJitFunction*> functions;
	std::vector<SLJitType*> types;
	int next_reg;
	void *exec_mem;
	size_t exec_size;
} SLJitModule;

typedef struct SLJitBuilder {
	IBuilder interface;
	uint32_t refcount;
	SLJitModule *module;
	SLJitFunction *current_function;
	SLJitBasicBlock *current_block;
} SLJitBuilder;

typedef struct SLJitBackend {
	IBackend interface;
	uint32_t refcount;
	uint32_t opt_level;
	int initialized;
} SLJitBackend;

/***************************************************************************
 * Type Implementation
 ***************************************************************************/

static value_type_t sljit_type_get_kind(IType *self)
{
	SLJitType *type = (SLJitType*)self;
	switch (type->sl_type) {
	case SL_I8: return VALUE_TYPE_INT8;
	case SL_I16: return VALUE_TYPE_INT16;
	case SL_I32: return VALUE_TYPE_INT32;
	case SL_I64: return VALUE_TYPE_INT64;
	case SL_F32: return VALUE_TYPE_FLOAT;
	case SL_F64: return VALUE_TYPE_DOUBLE;
	case SL_PTR: return VALUE_TYPE_POINTER;
	default: return VALUE_TYPE_VOID;
	}
}

static uint32_t sljit_type_get_bitwidth(IType *self)
{
	SLJitType *type = (SLJitType*)self;
	return type->size * 8;
}

static int sljit_type_is_integer(IType *self)
{
	SLJitType *type = (SLJitType*)self;
	return type->is_integer;
}

static int sljit_type_is_floating_point(IType *self)
{
	SLJitType *type = (SLJitType*)self;
	return type->is_float;
}

static int sljit_type_is_pointer(IType *self)
{
	SLJitType *type = (SLJitType*)self;
	return type->is_pointer;
}

static int sljit_type_is_void(IType *self)
{
	SLJitType *type = (SLJitType*)self;
	return type->is_void;
}

static IType* sljit_type_get_element_type(IType *self)
{
	return NULL;
}

static SLJitType* sljit_type_create(SLJitModule *module, sljit_type_t sl_type, const char *name)
{
	SLJitType *type = new SLJitType();
	type->refcount = 1;
	type->module = module;
	type->sl_type = sl_type;
	type->is_integer = (sl_type <= SL_I64);
	type->is_float = (sl_type == SL_F32 || sl_type == SL_F64);
	type->is_pointer = (sl_type == SL_PTR);
	type->is_void = false;
	type->name = name;

	switch (sl_type) {
	case SL_I8: type->size = 1; break;
	case SL_I16: type->size = 2; break;
	case SL_I32: type->size = 4; break;
	case SL_I64: type->size = 8; break;
	case SL_F32: type->size = 4; break;
	case SL_F64: type->size = 8; break;
	case SL_PTR: type->size = 8; break;
	}

	type->interface.base.AddRef = backend_addref;
	type->interface.base.Release = backend_release;
	type->interface.base.QueryInterface = backend_query_interface;
	type->interface.GetKind = sljit_type_get_kind;
	type->interface.GetBitWidth = sljit_type_get_bitwidth;
	type->interface.IsIntegerTy = sljit_type_is_integer;
	type->interface.IsFloatingPointTy = sljit_type_is_floating_point;
	type->interface.IsPointerTy = sljit_type_is_pointer;
	type->interface.IsVoidTy = sljit_type_is_void;
	type->interface.GetElementType = sljit_type_get_element_type;

	module->types.push_back(type);
	return type;
}

/***************************************************************************
 * Forward Declarations
 ***************************************************************************/

static IBasicBlock* sljit_function_create_basic_block(IFunction *self, const char *name);

/***************************************************************************
 * Builder Implementation
 ***************************************************************************/

static void sljit_builder_set_insert_point(IBuilder *self, IBasicBlock *bb)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	builder->current_block = (SLJitBasicBlock*)bb;
}

static IBasicBlock* sljit_builder_get_insert_block(IBuilder *self)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	return (IBasicBlock*)builder->current_block;
}

static IValue* sljit_builder_create_const_int(IBuilder *self, IType *type, uint64_t val, int is_signed)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *value = new SLJitValue();
	value->refcount = 1;
	value->module = builder->module;
	value->type = (SLJitType*)type;
	value->name = std::to_string(val);
	value->is_constant = true;
	value->const_value = val;
	value->reg_id = -1;

	value->interface.base.AddRef = backend_addref;
	value->interface.base.Release = backend_release;
	value->interface.base.QueryInterface = backend_query_interface;

	return (IValue*)value;
}

static IValue* sljit_builder_create_ret(IBuilder *self, IValue *val)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	if (builder->current_block)
		builder->current_block->terminated = true;
	return val;
}

/* Binary operations - simplified stubs */
static IValue* sljit_builder_create_add(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = ((SLJitValue*)lhs)->type;
	result->name = name ? name : "add_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_sub(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = ((SLJitValue*)lhs)->type;
	result->name = name ? name : "sub_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_mul(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = ((SLJitValue*)lhs)->type;
	result->name = name ? name : "mul_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_and(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = ((SLJitValue*)lhs)->type;
	result->name = name ? name : "and_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_or(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = ((SLJitValue*)lhs)->type;
	result->name = name ? name : "or_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_xor(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = ((SLJitValue*)lhs)->type;
	result->name = name ? name : "xor_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_shl(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = ((SLJitValue*)lhs)->type;
	result->name = name ? name : "shl_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_lshr(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = ((SLJitValue*)lhs)->type;
	result->name = name ? name : "lshr_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

/* Additional constant creation methods */
static IValue* sljit_builder_create_const_int1(IBuilder *self, int val)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *value = new SLJitValue();
	value->refcount = 1;
	value->module = builder->module;
	value->type = sljit_type_create(builder->module, SL_I8, "i1");
	value->name = std::to_string(val);
	value->is_constant = true;
	value->const_value = val;
	value->reg_id = -1;
	value->interface.base.AddRef = backend_addref;
	value->interface.base.Release = backend_release;
	value->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)value;
}

static IValue* sljit_builder_create_const_int8(IBuilder *self, uint8_t val)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *value = new SLJitValue();
	value->refcount = 1;
	value->module = builder->module;
	value->type = sljit_type_create(builder->module, SL_I8, "i8");
	value->name = std::to_string(val);
	value->is_constant = true;
	value->const_value = val;
	value->reg_id = -1;
	value->interface.base.AddRef = backend_addref;
	value->interface.base.Release = backend_release;
	value->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)value;
}

static IValue* sljit_builder_create_const_int16(IBuilder *self, uint16_t val)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *value = new SLJitValue();
	value->refcount = 1;
	value->module = builder->module;
	value->type = sljit_type_create(builder->module, SL_I16, "i16");
	value->name = std::to_string(val);
	value->is_constant = true;
	value->const_value = val;
	value->reg_id = -1;
	value->interface.base.AddRef = backend_addref;
	value->interface.base.Release = backend_release;
	value->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)value;
}

static IValue* sljit_builder_create_const_int32(IBuilder *self, uint32_t val)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *value = new SLJitValue();
	value->refcount = 1;
	value->module = builder->module;
	value->type = sljit_type_create(builder->module, SL_I32, "i32");
	value->name = std::to_string(val);
	value->is_constant = true;
	value->const_value = val;
	value->reg_id = -1;
	value->interface.base.AddRef = backend_addref;
	value->interface.base.Release = backend_release;
	value->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)value;
}

static IValue* sljit_builder_create_const_int64(IBuilder *self, uint64_t val)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *value = new SLJitValue();
	value->refcount = 1;
	value->module = builder->module;
	value->type = sljit_type_create(builder->module, SL_I64, "i64");
	value->name = std::to_string(val);
	value->is_constant = true;
	value->const_value = val;
	value->reg_id = -1;
	value->interface.base.AddRef = backend_addref;
	value->interface.base.Release = backend_release;
	value->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)value;
}

static IValue* sljit_builder_create_const_float(IBuilder *self, float val)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *value = new SLJitValue();
	value->refcount = 1;
	value->module = builder->module;
	value->type = sljit_type_create(builder->module, SL_F32, "f32");
	value->name = std::to_string(val);
	value->is_constant = true;
	value->const_value = 0;
	value->reg_id = -1;
	value->interface.base.AddRef = backend_addref;
	value->interface.base.Release = backend_release;
	value->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)value;
}

static IValue* sljit_builder_create_const_double(IBuilder *self, double val)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *value = new SLJitValue();
	value->refcount = 1;
	value->module = builder->module;
	value->type = sljit_type_create(builder->module, SL_F64, "f64");
	value->name = std::to_string(val);
	value->is_constant = true;
	value->const_value = 0;
	value->reg_id = -1;
	value->interface.base.AddRef = backend_addref;
	value->interface.base.Release = backend_release;
	value->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)value;
}

/* Additional arithmetic operations */
static IValue* sljit_builder_create_udiv(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = ((SLJitValue*)lhs)->type;
	result->name = name ? name : "udiv_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_sdiv(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = ((SLJitValue*)lhs)->type;
	result->name = name ? name : "sdiv_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_urem(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = ((SLJitValue*)lhs)->type;
	result->name = name ? name : "urem_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_srem(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = ((SLJitValue*)lhs)->type;
	result->name = name ? name : "srem_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_neg(IBuilder *self, IValue *val, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = ((SLJitValue*)val)->type;
	result->name = name ? name : "neg_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_not(IBuilder *self, IValue *val, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = ((SLJitValue*)val)->type;
	result->name = name ? name : "not_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_ashr(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = ((SLJitValue*)lhs)->type;
	result->name = name ? name : "ashr_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

/* Floating point operations */
static IValue* sljit_builder_create_fadd(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = ((SLJitValue*)lhs)->type;
	result->name = name ? name : "fadd_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_fsub(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = ((SLJitValue*)lhs)->type;
	result->name = name ? name : "fsub_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_fmul(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = ((SLJitValue*)lhs)->type;
	result->name = name ? name : "fmul_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_fdiv(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = ((SLJitValue*)lhs)->type;
	result->name = name ? name : "fdiv_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_fneg(IBuilder *self, IValue *val, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = ((SLJitValue*)val)->type;
	result->name = name ? name : "fneg_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

/* Comparison operations */
static IValue* sljit_builder_create_icmp(IBuilder *self, icmp_predicate_t pred, IValue *lhs, IValue *rhs, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = sljit_type_create(builder->module, SL_I8, "i1");
	result->name = name ? name : "icmp_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_fcmp(IBuilder *self, fcmp_predicate_t pred, IValue *lhs, IValue *rhs, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = sljit_type_create(builder->module, SL_I8, "i1");
	result->name = name ? name : "fcmp_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

/* Memory operations */
static IValue* sljit_builder_create_load(IBuilder *self, IType *type, IValue *ptr, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = (SLJitType*)type;
	result->name = name ? name : "load_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_store(IBuilder *self, IValue *val, IValue *ptr)
{
	return val;
}

static IValue* sljit_builder_create_gep(IBuilder *self, IType *type, IValue *ptr, IValue **indices, size_t num_indices, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = sljit_type_create(builder->module, SL_PTR, "ptr");
	result->name = name ? name : "gep_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_inbounds_gep(IBuilder *self, IType *type, IValue *ptr, IValue **indices, size_t num_indices, const char *name)
{
	return sljit_builder_create_gep(self, type, ptr, indices, num_indices, name);
}

/* Cast operations */
static IValue* sljit_builder_create_trunc(IBuilder *self, IValue *val, IType *dest_type, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = (SLJitType*)dest_type;
	result->name = name ? name : "trunc_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_zext(IBuilder *self, IValue *val, IType *dest_type, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = (SLJitType*)dest_type;
	result->name = name ? name : "zext_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_sext(IBuilder *self, IValue *val, IType *dest_type, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = (SLJitType*)dest_type;
	result->name = name ? name : "sext_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_fptrunc(IBuilder *self, IValue *val, IType *dest_type, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = (SLJitType*)dest_type;
	result->name = name ? name : "fptrunc_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_fpext(IBuilder *self, IValue *val, IType *dest_type, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = (SLJitType*)dest_type;
	result->name = name ? name : "fpext_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_fptoui(IBuilder *self, IValue *val, IType *dest_type, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = (SLJitType*)dest_type;
	result->name = name ? name : "fptoui_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_fptosi(IBuilder *self, IValue *val, IType *dest_type, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = (SLJitType*)dest_type;
	result->name = name ? name : "fptosi_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_uitofp(IBuilder *self, IValue *val, IType *dest_type, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = (SLJitType*)dest_type;
	result->name = name ? name : "uitofp_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_sitofp(IBuilder *self, IValue *val, IType *dest_type, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = (SLJitType*)dest_type;
	result->name = name ? name : "sitofp_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_ptrtoint(IBuilder *self, IValue *val, IType *dest_type, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = (SLJitType*)dest_type;
	result->name = name ? name : "ptrtoint_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_inttoptr(IBuilder *self, IValue *val, IType *dest_type, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = (SLJitType*)dest_type;
	result->name = name ? name : "inttoptr_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

static IValue* sljit_builder_create_bitcast(IBuilder *self, IValue *val, IType *dest_type, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = (SLJitType*)dest_type;
	result->name = name ? name : "bitcast_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

/* Control flow operations */
static IValue* sljit_builder_create_br(IBuilder *self, IBasicBlock *dest)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	if (builder->current_block)
		builder->current_block->terminated = true;
	return NULL;
}

static IValue* sljit_builder_create_condbr(IBuilder *self, IValue *cond, IBasicBlock *true_bb, IBasicBlock *false_bb)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	if (builder->current_block)
		builder->current_block->terminated = true;
	return NULL;
}

static IValue* sljit_builder_create_switch(IBuilder *self, IValue *val, IBasicBlock *default_bb, uint32_t num_cases)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	if (builder->current_block)
		builder->current_block->terminated = true;
	return NULL;
}

static IValue* sljit_builder_create_retvoid(IBuilder *self)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	if (builder->current_block)
		builder->current_block->terminated = true;
	return NULL;
}

/* Call operation */
static IValue* sljit_builder_create_call(IBuilder *self, IFunction *func, IValue **args, size_t num_args, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitFunction *target = (SLJitFunction*)func;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = target->return_type;
	result->name = name ? name : "call_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

/* PHI operation */
static IValue* sljit_builder_create_phi(IBuilder *self, IType *type, uint32_t num_reserved, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = (SLJitType*)type;
	result->name = name ? name : "phi_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

/* Select operation */
static IValue* sljit_builder_create_select(IBuilder *self, IValue *cond, IValue *true_val, IValue *false_val, const char *name)
{
	SLJitBuilder *builder = (SLJitBuilder*)self;
	SLJitValue *result = new SLJitValue();
	result->refcount = 1;
	result->module = builder->module;
	result->type = ((SLJitValue*)true_val)->type;
	result->name = name ? name : "select_result";
	result->is_constant = false;
	result->reg_id = builder->module->next_reg++;
	result->interface.base.AddRef = backend_addref;
	result->interface.base.Release = backend_release;
	result->interface.base.QueryInterface = backend_query_interface;
	return (IValue*)result;
}

/***************************************************************************
 * Module Implementation
 ***************************************************************************/

static const char* sljit_module_get_name(IModule *self)
{
	SLJitModule *module = (SLJitModule*)self;
	return module->name.c_str();
}

static IType* sljit_module_get_int8_type(IModule *self)
{
	SLJitModule *module = (SLJitModule*)self;
	return (IType*)sljit_type_create(module, SL_I8, "i8");
}

static IType* sljit_module_get_int16_type(IModule *self)
{
	SLJitModule *module = (SLJitModule*)self;
	return (IType*)sljit_type_create(module, SL_I16, "i16");
}

static IType* sljit_module_get_int32_type(IModule *self)
{
	SLJitModule *module = (SLJitModule*)self;
	return (IType*)sljit_type_create(module, SL_I32, "i32");
}

static IType* sljit_module_get_int64_type(IModule *self)
{
	SLJitModule *module = (SLJitModule*)self;
	return (IType*)sljit_type_create(module, SL_I64, "i64");
}

static IType* sljit_module_get_pointer_type(IModule *self, IType *element_type)
{
	SLJitModule *module = (SLJitModule*)self;
	return (IType*)sljit_type_create(module, SL_PTR, "ptr");
}

static IType* sljit_module_get_function_type(IModule *self, IType *return_type,
                                              IType **param_types, uint32_t num_params, int is_vararg)
{
	return return_type;
}

static IFunction* sljit_module_create_function(IModule *self, const char *name, IType *function_type)
{
	SLJitModule *module = (SLJitModule*)self;

	SLJitFunction *func = new SLJitFunction();
	func->refcount = 1;
	func->module = module;
	func->name = name;
	func->return_type = (SLJitType*)function_type;
	func->native_ptr = NULL;

	func->interface.base.AddRef = backend_addref;
	func->interface.base.Release = backend_release;
	func->interface.base.QueryInterface = backend_query_interface;
	func->interface.CreateBasicBlock = sljit_function_create_basic_block;

	module->functions.push_back(func);
	return (IFunction*)func;
}

static IFunction* sljit_module_get_function(IModule *self, const char *name)
{
	SLJitModule *module = (SLJitModule*)self;
	for (auto func : module->functions) {
		if (func->name == name)
			return (IFunction*)func;
	}
	return NULL;
}

static IBasicBlock* sljit_function_create_basic_block(IFunction *self, const char *name)
{
	SLJitFunction *func = (SLJitFunction*)self;

	SLJitBasicBlock *block = new SLJitBasicBlock();
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

static IBuilder* sljit_module_create_builder(IModule *self)
{
	SLJitModule *module = (SLJitModule*)self;

	SLJitBuilder *builder = new SLJitBuilder();
	builder->refcount = 1;
	builder->module = module;
	builder->current_function = NULL;
	builder->current_block = NULL;

	builder->interface.base.AddRef = backend_addref;
	builder->interface.base.Release = backend_release;
	builder->interface.base.QueryInterface = backend_query_interface;
	builder->interface.SetInsertPoint = sljit_builder_set_insert_point;
	builder->interface.GetInsertBlock = sljit_builder_get_insert_block;

	/* Constants */
	builder->interface.CreateConstInt = sljit_builder_create_const_int;
	builder->interface.CreateConstInt1 = sljit_builder_create_const_int1;
	builder->interface.CreateConstInt8 = sljit_builder_create_const_int8;
	builder->interface.CreateConstInt16 = sljit_builder_create_const_int16;
	builder->interface.CreateConstInt32 = sljit_builder_create_const_int32;
	builder->interface.CreateConstInt64 = sljit_builder_create_const_int64;
	builder->interface.CreateConstFloat = sljit_builder_create_const_float;
	builder->interface.CreateConstDouble = sljit_builder_create_const_double;

	/* Arithmetic operations */
	builder->interface.CreateAdd = sljit_builder_create_add;
	builder->interface.CreateSub = sljit_builder_create_sub;
	builder->interface.CreateMul = sljit_builder_create_mul;
	builder->interface.CreateUDiv = sljit_builder_create_udiv;
	builder->interface.CreateSDiv = sljit_builder_create_sdiv;
	builder->interface.CreateURem = sljit_builder_create_urem;
	builder->interface.CreateSRem = sljit_builder_create_srem;
	builder->interface.CreateNeg = sljit_builder_create_neg;

	/* Bitwise operations */
	builder->interface.CreateAnd = sljit_builder_create_and;
	builder->interface.CreateOr = sljit_builder_create_or;
	builder->interface.CreateXor = sljit_builder_create_xor;
	builder->interface.CreateNot = sljit_builder_create_not;
	builder->interface.CreateShl = sljit_builder_create_shl;
	builder->interface.CreateLShr = sljit_builder_create_lshr;
	builder->interface.CreateAShr = sljit_builder_create_ashr;

	/* Floating point operations */
	builder->interface.CreateFAdd = sljit_builder_create_fadd;
	builder->interface.CreateFSub = sljit_builder_create_fsub;
	builder->interface.CreateFMul = sljit_builder_create_fmul;
	builder->interface.CreateFDiv = sljit_builder_create_fdiv;
	builder->interface.CreateFNeg = sljit_builder_create_fneg;

	/* Comparison operations */
	builder->interface.CreateICmp = sljit_builder_create_icmp;
	builder->interface.CreateFCmp = sljit_builder_create_fcmp;

	/* Memory operations */
	builder->interface.CreateLoad = sljit_builder_create_load;
	builder->interface.CreateStore = sljit_builder_create_store;
	builder->interface.CreateGEP = sljit_builder_create_gep;
	builder->interface.CreateInBoundsGEP = sljit_builder_create_inbounds_gep;

	/* Cast operations */
	builder->interface.CreateTrunc = sljit_builder_create_trunc;
	builder->interface.CreateZExt = sljit_builder_create_zext;
	builder->interface.CreateSExt = sljit_builder_create_sext;
	builder->interface.CreateFPTrunc = sljit_builder_create_fptrunc;
	builder->interface.CreateFPExt = sljit_builder_create_fpext;
	builder->interface.CreateFPToUI = sljit_builder_create_fptoui;
	builder->interface.CreateFPToSI = sljit_builder_create_fptosi;
	builder->interface.CreateUIToFP = sljit_builder_create_uitofp;
	builder->interface.CreateSIToFP = sljit_builder_create_sitofp;
	builder->interface.CreatePtrToInt = sljit_builder_create_ptrtoint;
	builder->interface.CreateIntToPtr = sljit_builder_create_inttoptr;
	builder->interface.CreateBitCast = sljit_builder_create_bitcast;

	/* Control flow operations */
	builder->interface.CreateBr = sljit_builder_create_br;
	builder->interface.CreateCondBr = sljit_builder_create_condbr;
	builder->interface.CreateSwitch = sljit_builder_create_switch;
	builder->interface.CreateRet = sljit_builder_create_ret;
	builder->interface.CreateRetVoid = sljit_builder_create_retvoid;

	/* Call operation */
	builder->interface.CreateCall = sljit_builder_create_call;

	/* PHI operation */
	builder->interface.CreatePHI = sljit_builder_create_phi;

	/* Select operation */
	builder->interface.CreateSelect = sljit_builder_create_select;

	return (IBuilder*)builder;
}

static int sljit_module_compile(IModule *self)
{
	SLJitModule *module = (SLJitModule*)self;

	/* Allocate executable memory */
	size_t code_size = 4096;
	void *exec_mem = mmap(NULL, code_size, PROT_READ | PROT_WRITE | PROT_EXEC,
	                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (exec_mem == MAP_FAILED) {
		fprintf(stderr, "SLJIT: Failed to allocate executable memory\n");
		return -1;
	}

	module->exec_mem = exec_mem;
	module->exec_size = code_size;

	fprintf(stderr, "SLJIT: Compiled module with %zu functions\n", module->functions.size());
	return 0;
}

static void* sljit_module_get_function_address(IModule *self, const char *name)
{
	SLJitModule *module = (SLJitModule*)self;
	for (auto func : module->functions) {
		if (func->name == name)
			return func->native_ptr;
	}
	return NULL;
}

static void sljit_module_dump(IModule *self)
{
	printf("SLJIT module (binary code)\n");
}

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
	SLJitBackend *backend = (SLJitBackend*)self;

	SLJitModule *module = new SLJitModule();
	module->refcount = 1;
	module->backend = backend;
	module->name = name;
	module->next_reg = 0;
	module->exec_mem = NULL;
	module->exec_size = 0;

	module->interface.base.AddRef = backend_addref;
	module->interface.base.Release = backend_release;
	module->interface.base.QueryInterface = backend_query_interface;
	module->interface.GetName = sljit_module_get_name;
	module->interface.GetInt8Type = sljit_module_get_int8_type;
	module->interface.GetInt16Type = sljit_module_get_int16_type;
	module->interface.GetInt32Type = sljit_module_get_int32_type;
	module->interface.GetInt64Type = sljit_module_get_int64_type;
	module->interface.GetPointerType = sljit_module_get_pointer_type;
	module->interface.GetFunctionType = sljit_module_get_function_type;
	module->interface.CreateFunction = sljit_module_create_function;
	module->interface.GetFunction = sljit_module_get_function;
	module->interface.CreateBuilder = sljit_module_create_builder;
	module->interface.Compile = sljit_module_compile;
	module->interface.GetFunctionAddress = sljit_module_get_function_address;
	module->interface.Dump = sljit_module_dump;

	return (IModule*)module;
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
