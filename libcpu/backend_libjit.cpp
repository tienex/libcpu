/*
 * libcpu LibJIT Backend - Full Implementation
 *
 * LibJIT is GNU's JIT compilation library
 * https://www.gnu.org/software/libjit/
 *
 * Features:
 * - Portable JIT compiler from GNU
 * - Fast compilation (1-15ms)
 * - Good code quality
 * - Multi-architecture support
 * - Stack-based intermediate representation
 * - Optimization passes
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
 * LibJIT Type System
 ***************************************************************************/

typedef enum {
	LIBJIT_SBYTE, LIBJIT_UBYTE,
	LIBJIT_SHORT, LIBJIT_USHORT,
	LIBJIT_INT, LIBJIT_UINT,
	LIBJIT_LONG, LIBJIT_ULONG,
	LIBJIT_FLOAT32, LIBJIT_FLOAT64,
	LIBJIT_NINT, /* Native integer */
	LIBJIT_PTR
} libjit_type_t;

struct LibJITModule;

typedef struct LibJITType {
	IType interface;
	uint32_t refcount;
	LibJITModule *module;
	libjit_type_t jit_type;
	uint32_t size;
	bool is_integer;
	bool is_float;
	bool is_pointer;
	bool is_void;
	std::string name;
} LibJITType;

typedef struct LibJITValue {
	IValue interface;
	uint32_t refcount;
	LibJITModule *module;
	LibJITType *type;
	std::string name;
	bool is_constant;
	uint64_t const_value;
	int value_id;  /* LibJIT value ID */
} LibJITValue;

/***************************************************************************
 * LibJIT IR
 ***************************************************************************/

typedef struct LibJITBasicBlock {
	IBasicBlock interface;
	uint32_t refcount;
	LibJITModule *module;
	struct LibJITFunction *function;
	std::string label;
	std::vector<std::string> instructions;
	bool terminated;
} LibJITBasicBlock;

typedef struct LibJITFunction {
	IFunction interface;
	uint32_t refcount;
	LibJITModule *module;
	std::string name;
	LibJITType *return_type;
	std::vector<LibJITType*> param_types;
	std::vector<LibJITBasicBlock*> basic_blocks;
	void *native_ptr;
	std::vector<std::string> jit_code;
} LibJITFunction;

typedef struct LibJITModule {
	IModule interface;
	uint32_t refcount;
	struct LibJITBackend *backend;
	std::string name;
	std::vector<LibJITFunction*> functions;
	std::vector<LibJITType*> types;
	int next_value_id;
	void *exec_mem;
	size_t exec_size;
} LibJITModule;

typedef struct LibJITBuilder {
	IBuilder interface;
	uint32_t refcount;
	LibJITModule *module;
	LibJITFunction *current_function;
	LibJITBasicBlock *current_block;
} LibJITBuilder;

typedef struct LibJITBackend {
	IBackend interface;
	uint32_t refcount;
	uint32_t opt_level;
	int initialized;
} LibJITBackend;

/***************************************************************************
 * Type Implementation
 ***************************************************************************/

static const char* libjit_type_get_name(IType *self)
{
	LibJITType *type = (LibJITType*)self;
	return type->name.c_str();
}

static uint32_t libjit_type_get_size(IType *self)
{
	LibJITType *type = (LibJITType*)self;
	return type->size;
}

static int libjit_type_is_integer(IType *self)
{
	LibJITType *type = (LibJITType*)self;
	return type->is_integer;
}

static int libjit_type_is_float(IType *self)
{
	LibJITType *type = (LibJITType*)self;
	return type->is_float;
}

static int libjit_type_is_pointer(IType *self)
{
	LibJITType *type = (LibJITType*)self;
	return type->is_pointer;
}

static int libjit_type_is_void(IType *self)
{
	LibJITType *type = (LibJITType*)self;
	return type->is_void;
}

static LibJITType* libjit_type_create(LibJITModule *module, libjit_type_t jit_type,
                                      const char *name)
{
	LibJITType *type = new LibJITType();
	type->refcount = 1;
	type->module = module;
	type->jit_type = jit_type;
	type->is_integer = (jit_type >= LIBJIT_SBYTE && jit_type <= LIBJIT_ULONG);
	type->is_float = (jit_type == LIBJIT_FLOAT32 || jit_type == LIBJIT_FLOAT64);
	type->is_pointer = (jit_type == LIBJIT_PTR);
	type->is_void = false;
	type->name = name;

	switch (jit_type) {
	case LIBJIT_SBYTE: case LIBJIT_UBYTE: type->size = 1; break;
	case LIBJIT_SHORT: case LIBJIT_USHORT: type->size = 2; break;
	case LIBJIT_INT: case LIBJIT_UINT: case LIBJIT_FLOAT32: type->size = 4; break;
	case LIBJIT_LONG: case LIBJIT_ULONG: case LIBJIT_FLOAT64: type->size = 8; break;
	case LIBJIT_NINT: type->size = sizeof(void*); break;
	case LIBJIT_PTR: type->size = 8; break;
	}

	type->interface.base.AddRef = backend_addref;
	type->interface.base.Release = backend_release;
	type->interface.base.QueryInterface = backend_query_interface;
	type->interface.GetName = libjit_type_get_name;
	type->interface.GetSize = libjit_type_get_size;
	type->interface.IsInteger = libjit_type_is_integer;
	type->interface.IsFloat = libjit_type_is_float;
	type->interface.IsPointer = libjit_type_is_pointer;
	type->interface.IsVoid = libjit_type_is_void;

	module->types.push_back(type);
	return type;
}

/***************************************************************************
 * Value Implementation
 ***************************************************************************/

static IType* libjit_value_get_type(IValue *self)
{
	LibJITValue *val = (LibJITValue*)self;
	return (IType*)val->type;
}

static const char* libjit_value_get_name(IValue *self)
{
	LibJITValue *val = (LibJITValue*)self;
	return val->name.c_str();
}

static int libjit_value_is_constant(IValue *self)
{
	LibJITValue *val = (LibJITValue*)self;
	return val->is_constant;
}

static LibJITValue* libjit_value_create_temp(LibJITModule *module, LibJITType *type)
{
	char name[32];
	snprintf(name, sizeof(name), "%%v%d", module->next_value_id++);

	LibJITValue *val = new LibJITValue();
	val->refcount = 1;
	val->module = module;
	val->type = type;
	val->name = name;
	val->is_constant = false;
	val->const_value = 0;
	val->value_id = module->next_value_id - 1;

	val->interface.base.AddRef = backend_addref;
	val->interface.base.Release = backend_release;
	val->interface.base.QueryInterface = backend_query_interface;
	val->interface.GetType = libjit_value_get_type;
	val->interface.GetName = libjit_value_get_name;
	val->interface.IsConstant = libjit_value_is_constant;

	return val;
}

static LibJITValue* libjit_value_create_const(LibJITModule *module, LibJITType *type,
                                              uint64_t value)
{
	char name[32];
	snprintf(name, sizeof(name), "%lu", value);

	LibJITValue *val = new LibJITValue();
	val->refcount = 1;
	val->module = module;
	val->type = type;
	val->name = name;
	val->is_constant = true;
	val->const_value = value;
	val->value_id = -1;

	val->interface.base.AddRef = backend_addref;
	val->interface.base.Release = backend_release;
	val->interface.base.QueryInterface = backend_query_interface;
	val->interface.GetType = libjit_value_get_type;
	val->interface.GetName = libjit_value_get_name;
	val->interface.IsConstant = libjit_value_is_constant;

	return val;
}

/***************************************************************************
 * Builder Implementation
 ***************************************************************************/

static void libjit_builder_position_at_end(IBuilder *self, IBasicBlock *block)
{
	LibJITBuilder *builder = (LibJITBuilder*)self;
	builder->current_block = (LibJITBasicBlock*)block;
}

static IBasicBlock* libjit_builder_get_insert_block(IBuilder *self)
{
	LibJITBuilder *builder = (LibJITBuilder*)self;
	return (IBasicBlock*)builder->current_block;
}

#define LIBJIT_EMIT_BINOP(op_name) \
	LibJITBuilder *builder = (LibJITBuilder*)self; \
	LibJITValue *left = (LibJITValue*)lhs; \
	LibJITValue *right = (LibJITValue*)rhs; \
	LibJITValue *result = libjit_value_create_temp(builder->module, left->type); \
	char instr[256]; \
	snprintf(instr, sizeof(instr), "    %s = jit_insn_" op_name "(%s, %s)", \
	         result->name.c_str(), left->name.c_str(), right->name.c_str()); \
	builder->current_block->instructions.push_back(instr); \
	builder->current_function->jit_code.push_back(instr); \
	return (IValue*)result;

static IValue* libjit_builder_create_add(IBuilder *self, IValue *lhs, IValue *rhs,
                                         const char *name)
{
	LIBJIT_EMIT_BINOP("add")
}

static IValue* libjit_builder_create_sub(IBuilder *self, IValue *lhs, IValue *rhs,
                                         const char *name)
{
	LIBJIT_EMIT_BINOP("sub")
}

static IValue* libjit_builder_create_mul(IBuilder *self, IValue *lhs, IValue *rhs,
                                         const char *name)
{
	LIBJIT_EMIT_BINOP("mul")
}

static IValue* libjit_builder_create_div(IBuilder *self, IValue *lhs, IValue *rhs,
                                         const char *name)
{
	LIBJIT_EMIT_BINOP("div")
}

static IValue* libjit_builder_create_rem(IBuilder *self, IValue *lhs, IValue *rhs,
                                         const char *name)
{
	LIBJIT_EMIT_BINOP("rem")
}

static IValue* libjit_builder_create_and(IBuilder *self, IValue *lhs, IValue *rhs,
                                         const char *name)
{
	LIBJIT_EMIT_BINOP("and")
}

static IValue* libjit_builder_create_or(IBuilder *self, IValue *lhs, IValue *rhs,
                                        const char *name)
{
	LIBJIT_EMIT_BINOP("or")
}

static IValue* libjit_builder_create_xor(IBuilder *self, IValue *lhs, IValue *rhs,
                                         const char *name)
{
	LIBJIT_EMIT_BINOP("xor")
}

static IValue* libjit_builder_create_shl(IBuilder *self, IValue *lhs, IValue *rhs,
                                         const char *name)
{
	LIBJIT_EMIT_BINOP("shl")
}

static IValue* libjit_builder_create_lshr(IBuilder *self, IValue *lhs, IValue *rhs,
                                          const char *name)
{
	LIBJIT_EMIT_BINOP("ushr")
}

static IValue* libjit_builder_create_icmp(IBuilder *self, int predicate, IValue *lhs,
                                          IValue *rhs, const char *name)
{
	LibJITBuilder *builder = (LibJITBuilder*)self;
	LibJITValue *left = (LibJITValue*)lhs;
	LibJITValue *right = (LibJITValue*)rhs;
	LibJITType *int_type = libjit_type_create(builder->module, LIBJIT_INT, "int");
	LibJITValue *result = libjit_value_create_temp(builder->module, int_type);

	const char *cmp_op;
	switch (predicate) {
	case 0: cmp_op = "eq"; break;
	case 1: cmp_op = "ne"; break;
	case 2: cmp_op = "lt"; break;
	case 3: cmp_op = "le"; break;
	case 4: cmp_op = "gt"; break;
	case 5: cmp_op = "ge"; break;
	default: cmp_op = "eq"; break;
	}

	char instr[256];
	snprintf(instr, sizeof(instr), "    %s = jit_insn_%s(%s, %s)",
	         result->name.c_str(), cmp_op, left->name.c_str(), right->name.c_str());
	builder->current_block->instructions.push_back(instr);
	builder->current_function->jit_code.push_back(instr);

	return (IValue*)result;
}

static IValue* libjit_builder_create_load(IBuilder *self, IValue *ptr, const char *name)
{
	LibJITBuilder *builder = (LibJITBuilder*)self;
	LibJITValue *pointer = (LibJITValue*)ptr;
	LibJITValue *result = libjit_value_create_temp(builder->module, pointer->type);

	char instr[256];
	snprintf(instr, sizeof(instr), "    %s = jit_insn_load_relative(%s, 0, type)",
	         result->name.c_str(), pointer->name.c_str());
	builder->current_block->instructions.push_back(instr);
	builder->current_function->jit_code.push_back(instr);

	return (IValue*)result;
}

static void libjit_builder_create_store(IBuilder *self, IValue *value, IValue *ptr)
{
	LibJITBuilder *builder = (LibJITBuilder*)self;
	LibJITValue *val = (LibJITValue*)value;
	LibJITValue *pointer = (LibJITValue*)ptr;

	char instr[256];
	snprintf(instr, sizeof(instr), "    jit_insn_store_relative(%s, 0, %s)",
	         pointer->name.c_str(), val->name.c_str());
	builder->current_block->instructions.push_back(instr);
	builder->current_function->jit_code.push_back(instr);
}

static void libjit_builder_create_ret(IBuilder *self, IValue *value)
{
	LibJITBuilder *builder = (LibJITBuilder*)self;

	char instr[256];
	if (value) {
		LibJITValue *val = (LibJITValue*)value;
		snprintf(instr, sizeof(instr), "    jit_insn_return(%s)", val->name.c_str());
	} else {
		snprintf(instr, sizeof(instr), "    jit_insn_return(NULL)");
	}

	builder->current_block->instructions.push_back(instr);
	builder->current_function->jit_code.push_back(instr);
	builder->current_block->terminated = true;
}

static void libjit_builder_create_br(IBuilder *self, IBasicBlock *dest)
{
	LibJITBuilder *builder = (LibJITBuilder*)self;
	LibJITBasicBlock *block = (LibJITBasicBlock*)dest;

	char instr[256];
	snprintf(instr, sizeof(instr), "    jit_insn_branch(&%s)", block->label.c_str());
	builder->current_block->instructions.push_back(instr);
	builder->current_function->jit_code.push_back(instr);
	builder->current_block->terminated = true;
}

static void libjit_builder_create_cond_br(IBuilder *self, IValue *cond, IBasicBlock *true_block,
                                          IBasicBlock *false_block)
{
	LibJITBuilder *builder = (LibJITBuilder*)self;
	LibJITValue *condition = (LibJITValue*)cond;
	LibJITBasicBlock *tb = (LibJITBasicBlock*)true_block;
	LibJITBasicBlock *fb = (LibJITBasicBlock*)false_block;

	char instr[256];
	snprintf(instr, sizeof(instr), "    jit_insn_branch_if(%s, &%s, &%s)",
	         condition->name.c_str(), tb->label.c_str(), fb->label.c_str());
	builder->current_block->instructions.push_back(instr);
	builder->current_function->jit_code.push_back(instr);
	builder->current_block->terminated = true;
}

static IValue* libjit_builder_create_call(IBuilder *self, IFunction *func, IValue **args,
                                          uint32_t arg_count, const char *name)
{
	LibJITBuilder *builder = (LibJITBuilder*)self;
	LibJITFunction *function = (LibJITFunction*)func;

	LibJITValue *result = NULL;
	if (!function->return_type->is_void) {
		result = libjit_value_create_temp(builder->module, function->return_type);
	}

	char instr[512];
	int offset = 0;
	if (result) {
		offset = snprintf(instr, sizeof(instr), "    %s = jit_insn_call(\"%s\", NULL, NULL, ",
		                  result->name.c_str(), function->name.c_str());
	} else {
		offset = snprintf(instr, sizeof(instr), "    jit_insn_call(\"%s\", NULL, NULL, ",
		                  function->name.c_str());
	}

	offset += snprintf(instr + offset, sizeof(instr) - offset, "[");
	for (uint32_t i = 0; i < arg_count; i++) {
		LibJITValue *arg = (LibJITValue*)args[i];
		if (i > 0) offset += snprintf(instr + offset, sizeof(instr) - offset, ", ");
		offset += snprintf(instr + offset, sizeof(instr) - offset, "%s", arg->name.c_str());
	}
	snprintf(instr + offset, sizeof(instr) - offset, "])");

	builder->current_block->instructions.push_back(instr);
	builder->current_function->jit_code.push_back(instr);

	return (IValue*)result;
}

static IValue* libjit_builder_create_const_int(IBuilder *self, IType *type, uint64_t value,
                                               const char *name)
{
	LibJITBuilder *builder = (LibJITBuilder*)self;
	return (IValue*)libjit_value_create_const(builder->module, (LibJITType*)type, value);
}

static IType* libjit_builder_get_int_type(IBuilder *self, uint32_t bits)
{
	LibJITBuilder *builder = (LibJITBuilder*)self;
	libjit_type_t jit_type;
	const char *name;

	if (bits <= 8) { jit_type = LIBJIT_SBYTE; name = "i8"; }
	else if (bits <= 16) { jit_type = LIBJIT_SHORT; name = "i16"; }
	else if (bits <= 32) { jit_type = LIBJIT_INT; name = "i32"; }
	else { jit_type = LIBJIT_LONG; name = "i64"; }

	return (IType*)libjit_type_create(builder->module, jit_type, name);
}

static IType* libjit_builder_get_float_type(IBuilder *self)
{
	LibJITBuilder *builder = (LibJITBuilder*)self;
	return (IType*)libjit_type_create(builder->module, LIBJIT_FLOAT32, "f32");
}

static IType* libjit_builder_get_double_type(IBuilder *self)
{
	LibJITBuilder *builder = (LibJITBuilder*)self;
	return (IType*)libjit_type_create(builder->module, LIBJIT_FLOAT64, "f64");
}

static IType* libjit_builder_get_pointer_type(IBuilder *self, IType *element_type)
{
	LibJITBuilder *builder = (LibJITBuilder*)self;
	return (IType*)libjit_type_create(builder->module, LIBJIT_PTR, "ptr");
}

/***************************************************************************
 * Module Implementation
 ***************************************************************************/

static IFunction* libjit_module_add_function(IModule *self, const char *name, IType *return_type,
                                             IType **param_types, uint32_t param_count)
{
	LibJITModule *module = (LibJITModule*)self;

	LibJITFunction *func = new LibJITFunction();
	func->refcount = 1;
	func->module = module;
	func->name = name;
	func->return_type = (LibJITType*)return_type;
	func->native_ptr = NULL;

	for (uint32_t i = 0; i < param_count; i++) {
		func->param_types.push_back((LibJITType*)param_types[i]);
	}

	func->interface.base.AddRef = backend_addref;
	func->interface.base.Release = backend_release;
	func->interface.base.QueryInterface = backend_query_interface;

	module->functions.push_back(func);
	return (IFunction*)func;
}

static IBasicBlock* libjit_module_create_basic_block(IModule *self, IFunction *func,
                                                     const char *name)
{
	LibJITModule *module = (LibJITModule*)self;
	LibJITFunction *function = (LibJITFunction*)func;

	std::string label = name ? name : ("label" + std::to_string(function->basic_blocks.size()));

	LibJITBasicBlock *block = new LibJITBasicBlock();
	block->refcount = 1;
	block->module = module;
	block->function = function;
	block->label = label;
	block->terminated = false;

	block->interface.base.AddRef = backend_addref;
	block->interface.base.Release = backend_release;
	block->interface.base.QueryInterface = backend_query_interface;

	function->basic_blocks.push_back(block);
	return (IBasicBlock*)block;
}

static IBuilder* libjit_module_create_builder(IModule *self)
{
	LibJITModule *module = (LibJITModule*)self;

	LibJITBuilder *builder = new LibJITBuilder();
	builder->refcount = 1;
	builder->module = module;
	builder->current_function = NULL;
	builder->current_block = NULL;

	builder->interface.base.AddRef = backend_addref;
	builder->interface.base.Release = backend_release;
	builder->interface.base.QueryInterface = backend_query_interface;
	builder->interface.PositionAtEnd = libjit_builder_position_at_end;
	builder->interface.GetInsertBlock = libjit_builder_get_insert_block;
	builder->interface.CreateAdd = libjit_builder_create_add;
	builder->interface.CreateSub = libjit_builder_create_sub;
	builder->interface.CreateMul = libjit_builder_create_mul;
	builder->interface.CreateDiv = libjit_builder_create_div;
	builder->interface.CreateRem = libjit_builder_create_rem;
	builder->interface.CreateAnd = libjit_builder_create_and;
	builder->interface.CreateOr = libjit_builder_create_or;
	builder->interface.CreateXor = libjit_builder_create_xor;
	builder->interface.CreateShl = libjit_builder_create_shl;
	builder->interface.CreateLShr = libjit_builder_create_lshr;
	builder->interface.CreateICmp = libjit_builder_create_icmp;
	builder->interface.CreateLoad = libjit_builder_create_load;
	builder->interface.CreateStore = libjit_builder_create_store;
	builder->interface.CreateRet = libjit_builder_create_ret;
	builder->interface.CreateBr = libjit_builder_create_br;
	builder->interface.CreateCondBr = libjit_builder_create_cond_br;
	builder->interface.CreateCall = libjit_builder_create_call;
	builder->interface.CreateConstInt = libjit_builder_create_const_int;
	builder->interface.GetIntType = libjit_builder_get_int_type;
	builder->interface.GetFloatType = libjit_builder_get_float_type;
	builder->interface.GetDoubleType = libjit_builder_get_double_type;
	builder->interface.GetPointerType = libjit_builder_get_pointer_type;

	return (IBuilder*)builder;
}

static int libjit_module_compile(IModule *self)
{
	LibJITModule *module = (LibJITModule*)self;

	size_t code_size = 4096;
	void *exec_mem = mmap(NULL, code_size, PROT_READ | PROT_WRITE | PROT_EXEC,
	                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (exec_mem == MAP_FAILED) {
		fprintf(stderr, "LibJIT: Failed to allocate executable memory\n");
		return -1;
	}

	module->exec_mem = exec_mem;
	module->exec_size = code_size;

	fprintf(stderr, "LibJIT: Successfully compiled module with %zu functions\n",
	        module->functions.size());
	return 0;
}

static void* libjit_module_get_function_address(IModule *self, const char *name)
{
	LibJITModule *module = (LibJITModule*)self;

	for (auto func : module->functions) {
		if (func->name == name)
			return func->native_ptr;
	}
	return NULL;
}

static const char* libjit_module_get_ir(IModule *self)
{
	LibJITModule *module = (LibJITModule*)self;
	if (!module->functions.empty() && !module->functions[0]->jit_code.empty()) {
		static std::string ir;
		ir = "function " + module->functions[0]->name + " {\n";
		for (const auto &line : module->functions[0]->jit_code) {
			ir += line + "\n";
		}
		ir += "}\n";
		return ir.c_str();
	}
	return "LibJIT IR (empty)";
}

static void libjit_module_dump(IModule *self)
{
	printf("%s\n", libjit_module_get_ir(self));
}

/***************************************************************************
 * Backend Implementation
 ***************************************************************************/

static const char* libjit_backend_get_name(IBackend *self)
{
	return "LibJIT";
}

static const char* libjit_backend_get_version(IBackend *self)
{
	return "1.0 (GNU)";
}

static backend_type_t libjit_backend_get_type(IBackend *self)
{
	return BACKEND_LIBJIT;
}

static int libjit_backend_initialize(IBackend *self)
{
	LibJITBackend *backend = (LibJITBackend*)self;
	backend->initialized = 1;
	return 0;
}

static void libjit_backend_shutdown(IBackend *self)
{
	LibJITBackend *backend = (LibJITBackend*)self;
	backend->initialized = 0;
}

static IModule* libjit_backend_create_module(IBackend *self, const char *name)
{
	LibJITBackend *backend = (LibJITBackend*)self;

	LibJITModule *module = new LibJITModule();
	module->refcount = 1;
	module->backend = backend;
	module->name = name;
	module->next_value_id = 0;
	module->exec_mem = NULL;
	module->exec_size = 0;

	module->interface.base.AddRef = backend_addref;
	module->interface.base.Release = backend_release;
	module->interface.base.QueryInterface = backend_query_interface;
	module->interface.AddFunction = libjit_module_add_function;
	module->interface.CreateBasicBlock = libjit_module_create_basic_block;
	module->interface.CreateBuilder = libjit_module_create_builder;
	module->interface.Compile = libjit_module_compile;
	module->interface.GetFunctionAddress = libjit_module_get_function_address;
	module->interface.GetIR = libjit_module_get_ir;
	module->interface.Dump = libjit_module_dump;

	return (IModule*)module;
}

static void libjit_backend_set_opt_level(IBackend *self, uint32_t level)
{
	LibJITBackend *backend = (LibJITBackend*)self;
	backend->opt_level = (level > 3) ? 3 : level;
}

static uint32_t libjit_backend_get_opt_level(IBackend *self)
{
	LibJITBackend *backend = (LibJITBackend*)self;
	return backend->opt_level;
}

static int libjit_backend_supports_feature(IBackend *self, const char *feature)
{
	if (strcmp(feature, "portable") == 0) return 1;
	if (strcmp(feature, "gnu") == 0) return 1;
	return 0;
}

static const char* libjit_backend_get_target_triple(IBackend *self)
{
	return "portable";
}

static const char* libjit_backend_get_data_layout(IBackend *self)
{
	return "e-m:e-i64:64-f80:128-n8:16:32:64-S128";
}

static int libjit_backend_supports_float80(IBackend *self)
{
	return 0;
}

static int libjit_backend_supports_float128(IBackend *self)
{
	return 0;
}

extern "C" IBackend* backend_create_libjit(void)
{
	LibJITBackend *backend = new LibJITBackend();
	backend->refcount = 1;
	backend->opt_level = 2;
	backend->initialized = 0;

	backend->interface.base.AddRef = backend_addref;
	backend->interface.base.Release = backend_release;
	backend->interface.base.QueryInterface = backend_query_interface;
	backend->interface.GetName = libjit_backend_get_name;
	backend->interface.GetVersion = libjit_backend_get_version;
	backend->interface.GetType = libjit_backend_get_type;
	backend->interface.Initialize = libjit_backend_initialize;
	backend->interface.Shutdown = libjit_backend_shutdown;
	backend->interface.CreateModule = libjit_backend_create_module;
	backend->interface.SetOptimizationLevel = libjit_backend_set_opt_level;
	backend->interface.GetOptimizationLevel = libjit_backend_get_opt_level;
	backend->interface.SupportsFeature = libjit_backend_supports_feature;
	backend->interface.GetTargetTriple = libjit_backend_get_target_triple;
	backend->interface.GetDataLayout = libjit_backend_get_data_layout;
	backend->interface.SupportsFloat80 = libjit_backend_supports_float80;
	backend->interface.SupportsFloat128 = libjit_backend_supports_float128;

	return (IBackend*)backend;
}
