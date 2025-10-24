/*
 * libcpu GCCJIT Backend Implementation (Full)
 *
 * Uses libgccjit for JIT compilation with GCC optimization
 */

#include "backend.h"
#include <libgccjit.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <map>
#include <string>
#include <vector>

/* GCCJIT Module Implementation */
typedef struct GCCJITModule {
	IModule interface;
	uint32_t refcount;
	gcc_jit_context *ctx;
	std::string name;

	/* Type cache */
	std::map<value_type_t, gcc_jit_type*> *type_cache;

	/* Functions */
	std::map<std::string, gcc_jit_function*> *functions;

	/* Compiled result */
	gcc_jit_result *result;

	/* Globals */
	std::map<std::string, gcc_jit_lvalue*> *globals;

	/* Current function being built */
	gcc_jit_function *current_function;
} GCCJITModule;

/* GCCJIT Function Implementation */
typedef struct GCCJITFunction {
	IFunction interface;
	uint32_t refcount;
	std::string name;
	GCCJITModule *module;
	gcc_jit_function *gcc_func;
	gcc_jit_type *return_type;
	std::vector<gcc_jit_param*> *params;
	std::vector<gcc_jit_block*> *blocks;
} GCCJITFunction;

/* GCCJIT BasicBlock Implementation */
typedef struct GCCJITBasicBlock {
	IBasicBlock interface;
	uint32_t refcount;
	std::string name;
	GCCJITFunction *function;
	gcc_jit_block *gcc_block;
	int has_terminator;
} GCCJITBasicBlock;

/* GCCJIT Value Implementation */
typedef struct GCCJITValue {
	IValue interface;
	uint32_t refcount;
	GCCJITModule *module;
	IType *type;
	std::string name;
	gcc_jit_rvalue *gcc_rvalue;
	gcc_jit_lvalue *gcc_lvalue; /* For assignable values */
	int is_constant;
	uint64_t const_val;
} GCCJITValue;

/* GCCJIT Type Implementation */
typedef struct GCCJITType {
	IType interface;
	uint32_t refcount;
	GCCJITModule *module;
	value_type_t kind;
	gcc_jit_type *gcc_type;
	uint32_t bit_width;
} GCCJITType;

/* GCCJIT Builder Implementation */
typedef struct GCCJITBuilder {
	IBuilder interface;
	uint32_t refcount;
	GCCJITModule *module;
	GCCJITBasicBlock *current_bb;
} GCCJITBuilder;

/* Forward declarations */
static uint32_t gccjit_addref(void *self);
static uint32_t gccjit_release(void *self);
static int gccjit_query_interface(void *self, const char *iid, void **out);

/***************************************************************************
 * GCCJIT Type Implementation
 ***************************************************************************/

static value_type_t gccjit_type_get_kind(IType *self)
{
	GCCJITType *type = (GCCJITType*)self;
	return type->kind;
}

static uint32_t gccjit_type_get_bitwidth(IType *self)
{
	GCCJITType *type = (GCCJITType*)self;
	return type->bit_width;
}

static int gccjit_type_is_integer(IType *self)
{
	GCCJITType *type = (GCCJITType*)self;
	return (type->kind >= VALUE_TYPE_INT1 && type->kind <= VALUE_TYPE_INT128);
}

static int gccjit_type_is_floating_point(IType *self)
{
	GCCJITType *type = (GCCJITType*)self;
	return (type->kind >= VALUE_TYPE_FLOAT && type->kind <= VALUE_TYPE_FP128);
}

static int gccjit_type_is_pointer(IType *self)
{
	GCCJITType *type = (GCCJITType*)self;
	return type->kind == VALUE_TYPE_POINTER;
}

static int gccjit_type_is_void(IType *self)
{
	GCCJITType *type = (GCCJITType*)self;
	return type->kind == VALUE_TYPE_VOID;
}

static IType* gccjit_type_get_element_type(IType *self)
{
	/* TODO: Implement pointer element type tracking */
	return NULL;
}

static GCCJITType* gccjit_type_create(GCCJITModule *module, value_type_t kind, gcc_jit_type *gcc_type, uint32_t bit_width)
{
	GCCJITType *type = new GCCJITType();
	type->refcount = 1;
	type->module = module;
	type->kind = kind;
	type->gcc_type = gcc_type;
	type->bit_width = bit_width;

	type->interface.base.AddRef = gccjit_addref;
	type->interface.base.Release = gccjit_release;
	type->interface.base.QueryInterface = gccjit_query_interface;
	type->interface.GetKind = gccjit_type_get_kind;
	type->interface.GetBitWidth = gccjit_type_get_bitwidth;
	type->interface.IsIntegerTy = gccjit_type_is_integer;
	type->interface.IsFloatingPointTy = gccjit_type_is_floating_point;
	type->interface.IsPointerTy = gccjit_type_is_pointer;
	type->interface.IsVoidTy = gccjit_type_is_void;
	type->interface.GetElementType = gccjit_type_get_element_type;

	return type;
}

#define GET_GCC_TYPE(t) (((GCCJITType*)(t))->gcc_type)

/***************************************************************************
 * GCCJIT Value Implementation
 ***************************************************************************/

static IType* gccjit_value_get_type(IValue *self)
{
	GCCJITValue *val = (GCCJITValue*)self;
	if (val->type) {
		val->type->base.AddRef(val->type);
	}
	return val->type;
}

static const char* gccjit_value_get_name(IValue *self)
{
	GCCJITValue *val = (GCCJITValue*)self;
	return val->name.c_str();
}

static void gccjit_value_set_name(IValue *self, const char *name)
{
	GCCJITValue *val = (GCCJITValue*)self;
	if (name)
		val->name = name;
}

static int gccjit_value_is_constant(IValue *self)
{
	GCCJITValue *val = (GCCJITValue*)self;
	return val->is_constant;
}

static GCCJITValue* gccjit_value_create(GCCJITModule *module, IType *type, gcc_jit_rvalue *rvalue)
{
	GCCJITValue *val = new GCCJITValue();
	val->refcount = 1;
	val->module = module;
	val->type = type;
	if (type) {
		type->base.AddRef(type);
	}
	val->gcc_rvalue = rvalue;
	val->gcc_lvalue = nullptr;
	val->is_constant = 0;

	val->interface.base.AddRef = gccjit_addref;
	val->interface.base.Release = gccjit_release;
	val->interface.base.QueryInterface = gccjit_query_interface;
	val->interface.GetType = gccjit_value_get_type;
	val->interface.GetName = gccjit_value_get_name;
	val->interface.SetName = gccjit_value_set_name;
	val->interface.IsConstant = gccjit_value_is_constant;

	return val;
}

#define GET_GCC_RVALUE(v) (((GCCJITValue*)(v))->gcc_rvalue)
#define GET_GCC_LVALUE(v) (((GCCJITValue*)(v))->gcc_lvalue)

/***************************************************************************
 * GCCJIT BasicBlock Implementation
 ***************************************************************************/

static IFunction* gccjit_bb_get_parent(IBasicBlock *self)
{
	GCCJITBasicBlock *bb = (GCCJITBasicBlock*)self;
	return (IFunction*)bb->function;
}

static const char* gccjit_bb_get_name(IBasicBlock *self)
{
	GCCJITBasicBlock *bb = (GCCJITBasicBlock*)self;
	return bb->name.c_str();
}

static int gccjit_bb_has_terminator(IBasicBlock *self)
{
	GCCJITBasicBlock *bb = (GCCJITBasicBlock*)self;
	return bb->has_terminator;
}

static GCCJITBasicBlock* gccjit_bb_create(GCCJITModule *module, GCCJITFunction *function, const char *name)
{
	GCCJITBasicBlock *bb = new GCCJITBasicBlock();
	bb->refcount = 1;
	bb->name = name ? name : "";
	bb->function = function;
	bb->gcc_block = gcc_jit_function_new_block(function->gcc_func, name);
	bb->has_terminator = 0;

	bb->interface.base.AddRef = gccjit_addref;
	bb->interface.base.Release = gccjit_release;
	bb->interface.base.QueryInterface = gccjit_query_interface;
	bb->interface.GetParent = gccjit_bb_get_parent;
	bb->interface.GetName = gccjit_bb_get_name;
	bb->interface.HasTerminator = gccjit_bb_has_terminator;

	return bb;
}

#define GET_GCC_BLOCK(bb) (((GCCJITBasicBlock*)(bb))->gcc_block)

/***************************************************************************
 * GCCJIT Builder Implementation - Instruction Emission
 ***************************************************************************/

static void gccjit_builder_set_insert_point(IBuilder *self, IBasicBlock *bb)
{
	GCCJITBuilder *builder = (GCCJITBuilder*)self;
	builder->current_bb = (GCCJITBasicBlock*)bb;
}

static IBasicBlock* gccjit_builder_get_insert_block(IBuilder *self)
{
	GCCJITBuilder *builder = (GCCJITBuilder*)self;
	return (IBasicBlock*)builder->current_bb;
}

/* Create constants */
static IValue* gccjit_builder_create_const_int32(IBuilder *self, uint32_t val)
{
	GCCJITBuilder *builder = (GCCJITBuilder*)self;
	gcc_jit_type *type = gcc_jit_context_get_type(builder->module->ctx, GCC_JIT_TYPE_INT);
	gcc_jit_rvalue *rvalue = gcc_jit_context_new_rvalue_from_int(builder->module->ctx, type, val);

	IType *itype = (IType*)gccjit_type_create(builder->module, VALUE_TYPE_INT32, type, 32);
	GCCJITValue *value = gccjit_value_create(builder->module, itype, rvalue);
	value->is_constant = 1;
	value->const_val = val;

	return (IValue*)value;
}

static IValue* gccjit_builder_create_const_int64(IBuilder *self, uint64_t val)
{
	GCCJITBuilder *builder = (GCCJITBuilder*)self;
	gcc_jit_type *type = gcc_jit_context_get_type(builder->module->ctx, GCC_JIT_TYPE_LONG_LONG);
	gcc_jit_rvalue *rvalue = gcc_jit_context_new_rvalue_from_long(builder->module->ctx, type, val);

	IType *itype = (IType*)gccjit_type_create(builder->module, VALUE_TYPE_INT64, type, 64);
	GCCJITValue *value = gccjit_value_create(builder->module, itype, rvalue);
	value->is_constant = 1;
	value->const_val = val;

	return (IValue*)value;
}

/* Arithmetic operations */
static IValue* gccjit_builder_create_add(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	GCCJITBuilder *builder = (GCCJITBuilder*)self;
	gcc_jit_rvalue *result = gcc_jit_context_new_binary_op(
		builder->module->ctx, NULL,
		GCC_JIT_BINARY_OP_PLUS,
		GET_GCC_TYPE(((GCCJITValue*)lhs)->type),
		GET_GCC_RVALUE(lhs),
		GET_GCC_RVALUE(rhs)
	);

	GCCJITValue *value = gccjit_value_create(builder->module, ((GCCJITValue*)lhs)->type, result);
	if (name)
		value->name = name;

	return (IValue*)value;
}

static IValue* gccjit_builder_create_sub(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	GCCJITBuilder *builder = (GCCJITBuilder*)self;
	gcc_jit_rvalue *result = gcc_jit_context_new_binary_op(
		builder->module->ctx, NULL,
		GCC_JIT_BINARY_OP_MINUS,
		GET_GCC_TYPE(((GCCJITValue*)lhs)->type),
		GET_GCC_RVALUE(lhs),
		GET_GCC_RVALUE(rhs)
	);

	GCCJITValue *value = gccjit_value_create(builder->module, ((GCCJITValue*)lhs)->type, result);
	if (name)
		value->name = name;

	return (IValue*)value;
}

static IValue* gccjit_builder_create_mul(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	GCCJITBuilder *builder = (GCCJITBuilder*)self;
	gcc_jit_rvalue *result = gcc_jit_context_new_binary_op(
		builder->module->ctx, NULL,
		GCC_JIT_BINARY_OP_MULT,
		GET_GCC_TYPE(((GCCJITValue*)lhs)->type),
		GET_GCC_RVALUE(lhs),
		GET_GCC_RVALUE(rhs)
	);

	GCCJITValue *value = gccjit_value_create(builder->module, ((GCCJITValue*)lhs)->type, result);
	if (name)
		value->name = name;

	return (IValue*)value;
}

static IValue* gccjit_builder_create_sdiv(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	GCCJITBuilder *builder = (GCCJITBuilder*)self;
	gcc_jit_rvalue *result = gcc_jit_context_new_binary_op(
		builder->module->ctx, NULL,
		GCC_JIT_BINARY_OP_DIVIDE,
		GET_GCC_TYPE(((GCCJITValue*)lhs)->type),
		GET_GCC_RVALUE(lhs),
		GET_GCC_RVALUE(rhs)
	);

	GCCJITValue *value = gccjit_value_create(builder->module, ((GCCJITValue*)lhs)->type, result);
	if (name)
		value->name = name;

	return (IValue*)value;
}

static IValue* gccjit_builder_create_srem(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	GCCJITBuilder *builder = (GCCJITBuilder*)self;
	gcc_jit_rvalue *result = gcc_jit_context_new_binary_op(
		builder->module->ctx, NULL,
		GCC_JIT_BINARY_OP_MODULO,
		GET_GCC_TYPE(((GCCJITValue*)lhs)->type),
		GET_GCC_RVALUE(lhs),
		GET_GCC_RVALUE(rhs)
	);

	GCCJITValue *value = gccjit_value_create(builder->module, ((GCCJITValue*)lhs)->type, result);
	if (name)
		value->name = name;

	return (IValue*)value;
}

/* Logical operations */
static IValue* gccjit_builder_create_and(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	GCCJITBuilder *builder = (GCCJITBuilder*)self;
	gcc_jit_rvalue *result = gcc_jit_context_new_binary_op(
		builder->module->ctx, NULL,
		GCC_JIT_BINARY_OP_BITWISE_AND,
		GET_GCC_TYPE(((GCCJITValue*)lhs)->type),
		GET_GCC_RVALUE(lhs),
		GET_GCC_RVALUE(rhs)
	);

	GCCJITValue *value = gccjit_value_create(builder->module, ((GCCJITValue*)lhs)->type, result);
	if (name)
		value->name = name;

	return (IValue*)value;
}

static IValue* gccjit_builder_create_or(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	GCCJITBuilder *builder = (GCCJITBuilder*)self;
	gcc_jit_rvalue *result = gcc_jit_context_new_binary_op(
		builder->module->ctx, NULL,
		GCC_JIT_BINARY_OP_BITWISE_OR,
		GET_GCC_TYPE(((GCCJITValue*)lhs)->type),
		GET_GCC_RVALUE(lhs),
		GET_GCC_RVALUE(rhs)
	);

	GCCJITValue *value = gccjit_value_create(builder->module, ((GCCJITValue*)lhs)->type, result);
	if (name)
		value->name = name;

	return (IValue*)value;
}

static IValue* gccjit_builder_create_xor(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	GCCJITBuilder *builder = (GCCJITBuilder*)self;
	gcc_jit_rvalue *result = gcc_jit_context_new_binary_op(
		builder->module->ctx, NULL,
		GCC_JIT_BINARY_OP_BITWISE_XOR,
		GET_GCC_TYPE(((GCCJITValue*)lhs)->type),
		GET_GCC_RVALUE(lhs),
		GET_GCC_RVALUE(rhs)
	);

	GCCJITValue *value = gccjit_value_create(builder->module, ((GCCJITValue*)lhs)->type, result);
	if (name)
		value->name = name;

	return (IValue*)value;
}

static IValue* gccjit_builder_create_shl(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	GCCJITBuilder *builder = (GCCJITBuilder*)self;
	gcc_jit_rvalue *result = gcc_jit_context_new_binary_op(
		builder->module->ctx, NULL,
		GCC_JIT_BINARY_OP_LSHIFT,
		GET_GCC_TYPE(((GCCJITValue*)lhs)->type),
		GET_GCC_RVALUE(lhs),
		GET_GCC_RVALUE(rhs)
	);

	GCCJITValue *value = gccjit_value_create(builder->module, ((GCCJITValue*)lhs)->type, result);
	if (name)
		value->name = name;

	return (IValue*)value;
}

static IValue* gccjit_builder_create_lshr(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	GCCJITBuilder *builder = (GCCJITBuilder*)self;
	gcc_jit_rvalue *result = gcc_jit_context_new_binary_op(
		builder->module->ctx, NULL,
		GCC_JIT_BINARY_OP_RSHIFT,
		GET_GCC_TYPE(((GCCJITValue*)lhs)->type),
		GET_GCC_RVALUE(lhs),
		GET_GCC_RVALUE(rhs)
	);

	GCCJITValue *value = gccjit_value_create(builder->module, ((GCCJITValue*)lhs)->type, result);
	if (name)
		value->name = name;

	return (IValue*)value;
}

/* Comparison */
static IValue* gccjit_builder_create_icmp(IBuilder *self, icmp_predicate_t pred, IValue *lhs, IValue *rhs, const char *name)
{
	GCCJITBuilder *builder = (GCCJITBuilder*)self;

	enum gcc_jit_comparison gcc_cmp;
	switch (pred) {
	case ICMP_EQ: gcc_cmp = GCC_JIT_COMPARISON_EQ; break;
	case ICMP_NE: gcc_cmp = GCC_JIT_COMPARISON_NE; break;
	case ICMP_UGT: gcc_cmp = GCC_JIT_COMPARISON_GT; break;
	case ICMP_UGE: gcc_cmp = GCC_JIT_COMPARISON_GE; break;
	case ICMP_ULT: gcc_cmp = GCC_JIT_COMPARISON_LT; break;
	case ICMP_ULE: gcc_cmp = GCC_JIT_COMPARISON_LE; break;
	case ICMP_SGT: gcc_cmp = GCC_JIT_COMPARISON_GT; break;
	case ICMP_SGE: gcc_cmp = GCC_JIT_COMPARISON_GE; break;
	case ICMP_SLT: gcc_cmp = GCC_JIT_COMPARISON_LT; break;
	case ICMP_SLE: gcc_cmp = GCC_JIT_COMPARISON_LE; break;
	default: gcc_cmp = GCC_JIT_COMPARISON_EQ;
	}

	gcc_jit_rvalue *result = gcc_jit_context_new_comparison(
		builder->module->ctx, NULL, gcc_cmp,
		GET_GCC_RVALUE(lhs),
		GET_GCC_RVALUE(rhs)
	);

	gcc_jit_type *bool_type = gcc_jit_context_get_type(builder->module->ctx, GCC_JIT_TYPE_BOOL);
	IType *itype = (IType*)gccjit_type_create(builder->module, VALUE_TYPE_INT1, bool_type, 1);
	GCCJITValue *value = gccjit_value_create(builder->module, itype, result);
	if (name)
		value->name = name;

	return (IValue*)value;
}

/* Control flow */
static IValue* gccjit_builder_create_ret(IBuilder *self, IValue *val)
{
	GCCJITBuilder *builder = (GCCJITBuilder*)self;

	if (val) {
		gcc_jit_block_end_with_return(
			builder->current_bb->gcc_block, NULL,
			GET_GCC_RVALUE(val)
		);
	} else {
		gcc_jit_block_end_with_void_return(
			builder->current_bb->gcc_block, NULL
		);
	}

	builder->current_bb->has_terminator = 1;
	return NULL;
}

static IValue* gccjit_builder_create_br(IBuilder *self, IBasicBlock *dest)
{
	GCCJITBuilder *builder = (GCCJITBuilder*)self;

	gcc_jit_block_end_with_jump(
		builder->current_bb->gcc_block, NULL,
		GET_GCC_BLOCK(dest)
	);

	builder->current_bb->has_terminator = 1;
	return NULL;
}

static IValue* gccjit_builder_create_condbr(IBuilder *self, IValue *cond, IBasicBlock *true_bb, IBasicBlock *false_bb)
{
	GCCJITBuilder *builder = (GCCJITBuilder*)self;

	gcc_jit_block_end_with_conditional(
		builder->current_bb->gcc_block, NULL,
		GET_GCC_RVALUE(cond),
		GET_GCC_BLOCK(true_bb),
		GET_GCC_BLOCK(false_bb)
	);

	builder->current_bb->has_terminator = 1;
	return NULL;
}

/* Memory operations */
static IValue* gccjit_builder_create_load(IBuilder *self, IType *type, IValue *ptr, const char *name)
{
	GCCJITBuilder *builder = (GCCJITBuilder*)self;

	gcc_jit_lvalue *lvalue = gcc_jit_rvalue_dereference(
		GET_GCC_RVALUE(ptr), NULL
	);
	gcc_jit_rvalue *rvalue = gcc_jit_lvalue_as_rvalue(lvalue);

	GCCJITValue *value = gccjit_value_create(builder->module, type, rvalue);
	if (name)
		value->name = name;

	return (IValue*)value;
}

static IValue* gccjit_builder_create_store(IBuilder *self, IValue *val, IValue *ptr)
{
	GCCJITBuilder *builder = (GCCJITBuilder*)self;

	gcc_jit_lvalue *lvalue = gcc_jit_rvalue_dereference(
		GET_GCC_RVALUE(ptr), NULL
	);

	gcc_jit_block_add_assignment(
		builder->current_bb->gcc_block, NULL,
		lvalue,
		GET_GCC_RVALUE(val)
	);

	return NULL;
}

static GCCJITBuilder* gccjit_builder_create(GCCJITModule *module)
{
	GCCJITBuilder *builder = new GCCJITBuilder();
	builder->refcount = 1;
	builder->module = module;
	builder->current_bb = NULL;

	/* Setup interface */
	builder->interface.base.AddRef = gccjit_addref;
	builder->interface.base.Release = gccjit_release;
	builder->interface.base.QueryInterface = gccjit_query_interface;
	builder->interface.SetInsertPoint = gccjit_builder_set_insert_point;
	builder->interface.GetInsertBlock = gccjit_builder_get_insert_block;
	builder->interface.CreateConstInt32 = gccjit_builder_create_const_int32;
	builder->interface.CreateConstInt64 = gccjit_builder_create_const_int64;
	builder->interface.CreateAdd = gccjit_builder_create_add;
	builder->interface.CreateSub = gccjit_builder_create_sub;
	builder->interface.CreateMul = gccjit_builder_create_mul;
	builder->interface.CreateSDiv = gccjit_builder_create_sdiv;
	builder->interface.CreateSRem = gccjit_builder_create_srem;
	builder->interface.CreateAnd = gccjit_builder_create_and;
	builder->interface.CreateOr = gccjit_builder_create_or;
	builder->interface.CreateXor = gccjit_builder_create_xor;
	builder->interface.CreateShl = gccjit_builder_create_shl;
	builder->interface.CreateLShr = gccjit_builder_create_lshr;
	builder->interface.CreateICmp = gccjit_builder_create_icmp;
	builder->interface.CreateLoad = gccjit_builder_create_load;
	builder->interface.CreateStore = gccjit_builder_create_store;
	builder->interface.CreateRet = gccjit_builder_create_ret;
	builder->interface.CreateBr = gccjit_builder_create_br;
	builder->interface.CreateCondBr = gccjit_builder_create_condbr;
	/* ... rest NULL for now */

	return builder;
}

/***************************************************************************
 * Reference Counting
 ***************************************************************************/

static uint32_t gccjit_addref(void *self)
{
	uint32_t *refcount = (uint32_t*)self;
	return ++(*refcount);
}

static uint32_t gccjit_release(void *self)
{
	uint32_t *refcount = (uint32_t*)self;
	uint32_t count = --(*refcount);
	if (count == 0) {
		delete self;
	}
	return count;
}

static int gccjit_query_interface(void *self, const char *iid, void **out)
{
	if (out == NULL)
		return -1;
	*out = self;
	gccjit_addref(self);
	return 0;
}

/***************************************************************************
 * GCCJIT Module Implementation
 ***************************************************************************/

/* Export for stub backend */
extern "C" GCCJITModule* gccjit_module_create_internal(const char *name, uint32_t opt_level)
{
	GCCJITModule *mod = new GCCJITModule();
	mod->refcount = 1;
	mod->name = name;
	mod->ctx = gcc_jit_context_acquire();
	mod->type_cache = new std::map<value_type_t, gcc_jit_type*>();
	mod->functions = new std::map<std::string, gcc_jit_function*>();
	mod->globals = new std::map<std::string, gcc_jit_lvalue*>();
	mod->result = NULL;
	mod->current_function = NULL;

	/* Set optimization level */
	gcc_jit_context_set_int_option(mod->ctx, GCC_JIT_INT_OPTION_OPTIMIZATION_LEVEL, opt_level);

	/* Cache common types */
	(*mod->type_cache)[VALUE_TYPE_VOID] = gcc_jit_context_get_type(mod->ctx, GCC_JIT_TYPE_VOID);
	(*mod->type_cache)[VALUE_TYPE_INT8] = gcc_jit_context_get_type(mod->ctx, GCC_JIT_TYPE_CHAR);
	(*mod->type_cache)[VALUE_TYPE_INT16] = gcc_jit_context_get_type(mod->ctx, GCC_JIT_TYPE_SHORT);
	(*mod->type_cache)[VALUE_TYPE_INT32] = gcc_jit_context_get_type(mod->ctx, GCC_JIT_TYPE_INT);
	(*mod->type_cache)[VALUE_TYPE_INT64] = gcc_jit_context_get_type(mod->ctx, GCC_JIT_TYPE_LONG_LONG);
	(*mod->type_cache)[VALUE_TYPE_FLOAT] = gcc_jit_context_get_type(mod->ctx, GCC_JIT_TYPE_FLOAT);
	(*mod->type_cache)[VALUE_TYPE_DOUBLE] = gcc_jit_context_get_type(mod->ctx, GCC_JIT_TYPE_DOUBLE);

	return mod;
}

static int gccjit_module_compile(IModule *self)
{
	GCCJITModule *mod = (GCCJITModule*)self;

	/* Compile the context */
	mod->result = gcc_jit_context_compile(mod->ctx);
	if (!mod->result)
		return -1;

	return 0;
}

static void* gccjit_module_get_function_address(IModule *self, const char *name)
{
	GCCJITModule *mod = (GCCJITModule*)self;

	if (!mod->result)
		return NULL;

	return gcc_jit_result_get_code(mod->result, name);
}

static IBuilder* gccjit_module_create_builder(IModule *self)
{
	GCCJITModule *mod = (GCCJITModule*)self;
	return (IBuilder*)gccjit_builder_create(mod);
}

static IType* gccjit_module_get_int32_type(IModule *self)
{
	GCCJITModule *mod = (GCCJITModule*)self;
	gcc_jit_type *type = (*mod->type_cache)[VALUE_TYPE_INT32];
	return (IType*)gccjit_type_create(mod, VALUE_TYPE_INT32, type, 32);
}

/***************************************************************************
 * GCCJIT Backend Implementation
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

static int gccjit_backend_initialize(IBackend *self)
{
	GCCJITBackend *backend = (GCCJITBackend*)self;
	if (backend->initialized)
		return 0;

	fprintf(stderr, "GCCJIT backend: Initialized\n");
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

extern "C" IBackend* backend_create_gccjit(void)
{
	GCCJITBackend *backend = new GCCJITBackend();
	backend->refcount = 1;
	backend->opt_level = 2;
	backend->initialized = 0;

	/* Setup interface */
	backend->interface.base.AddRef = gccjit_addref;
	backend->interface.base.Release = gccjit_release;
	backend->interface.base.QueryInterface = gccjit_query_interface;
	backend->interface.GetName = gccjit_backend_get_name;
	backend->interface.GetVersion = gccjit_backend_get_version;
	backend->interface.GetType = gccjit_backend_get_type;
	backend->interface.Initialize = gccjit_backend_initialize;
	backend->interface.Shutdown = gccjit_backend_shutdown;
	backend->interface.CreateModule = gccjit_backend_create_module;
	backend->interface.SetOptimizationLevel = gccjit_backend_set_opt_level;
	backend->interface.GetOptimizationLevel = gccjit_backend_get_opt_level;
	/* ... rest of interface setup ... */

	return (IBackend*)backend;
}
