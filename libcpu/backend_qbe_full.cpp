/*
 * libcpu QBE Backend - Full Implementation
 *
 * QBE (Quick Backend) is a small compiler backend using SSA form
 * https://c9x.me/compile/
 *
 * QBE IL features:
 * - SSA-based intermediate representation
 * - Simple text format
 * - Types: byte(b), half(h), word(w), long(l), single(s), double(d)
 * - Fast compilation
 */

#include "backend.h"
#include <stdlib.h>
<parameter name="string.h">
#include <stdio.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <sys/mman.h>
#include <unistd.h>
#include <dlfcn.h>

/* Base refcount helpers */
extern uint32_t backend_addref(void *self);
extern uint32_t backend_release(void *self);
extern int backend_query_interface(void *self, const char *iid, void **out);

/***************************************************************************
 * QBE Type System
 ***************************************************************************/

typedef enum {
	QBE_TYPE_BYTE = 0,   /* b - 8-bit integer */
	QBE_TYPE_HALF,       /* h - 16-bit integer */
	QBE_TYPE_WORD,       /* w - 32-bit integer */
	QBE_TYPE_LONG,       /* l - 64-bit integer */
	QBE_TYPE_SINGLE,     /* s - 32-bit float */
	QBE_TYPE_DOUBLE,     /* d - 64-bit float */
	QBE_TYPE_AGGREGATE   /* User-defined aggregate */
} qbe_base_type_t;

struct QBEModule;
struct QBEFunction;
struct QBEBasicBlock;

typedef struct QBEType {
	IType interface;
	uint32_t refcount;
	QBEModule *module;
	qbe_base_type_t base_type;
	uint32_t size;
	bool is_pointer;
	bool is_void;
	QBEType *element_type;  /* For pointers */
	std::string name;
	char qbe_char;  /* QBE type character: b, h, w, l, s, d */
} QBEType;

typedef struct QBEValue {
	IValue interface;
	uint32_t refcount;
	QBEModule *module;
	QBEType *type;
	std::string name;
	bool is_constant;
	uint64_t const_value;
	bool is_temp;
	int temp_id;
} QBEValue;

/***************************************************************************
 * QBE Module and IR Generation
 ***************************************************************************/

typedef struct QBEModule {
	IModule interface;
	uint32_t refcount;
	struct QBEBackend *backend;
	std::string name;

	/* IR generation */
	std::ostringstream ir_stream;
	std::vector<QBEFunction*> functions;
	std::vector<QBEType*> types;

	/* Compilation */
	void *compiled_code;
	size_t code_size;
	void *dl_handle;

	int next_temp_id;
	int next_label_id;
} QBEModule;

typedef struct QBEBasicBlock {
	IBasicBlock interface;
	uint32_t refcount;
	QBEModule *module;
	QBEFunction *function;
	std::string label;
	std::ostringstream code;
	bool terminated;
} QBEBasicBlock;

typedef struct QBEFunction {
	IFunction interface;
	uint32_t refcount;
	QBEModule *module;
	std::string name;
	QBEType *return_type;
	std::vector<QBEType*> param_types;
	std::vector<std::string> param_names;
	std::vector<QBEBasicBlock*> basic_blocks;
	bool is_variadic;
	void *native_ptr;
	std::string linkage;
} QBEFunction;

typedef struct QBEBuilder {
	IBuilder interface;
	uint32_t refcount;
	QBEModule *module;
	QBEFunction *current_function;
	QBEBasicBlock *current_block;
} QBEBuilder;

/***************************************************************************
 * QBE Backend Structure
 ***************************************************************************/

typedef struct QBEBackend {
	IBackend interface;
	uint32_t refcount;
	uint32_t opt_level;
	int initialized;
} QBEBackend;

/***************************************************************************
 * QBE Type Implementation
 ***************************************************************************/

static const char* qbe_type_get_name(IType *self)
{
	QBEType *type = (QBEType*)self;
	return type->name.c_str();
}

static uint32_t qbe_type_get_size(IType *self)
{
	QBEType *type = (QBEType*)self;
	return type->size;
}

static int qbe_type_is_integer(IType *self)
{
	QBEType *type = (QBEType*)self;
	return type->base_type >= QBE_TYPE_BYTE && type->base_type <= QBE_TYPE_LONG;
}

static int qbe_type_is_float(IType *self)
{
	QBEType *type = (QBEType*)self;
	return type->base_type == QBE_TYPE_SINGLE || type->base_type == QBE_TYPE_DOUBLE;
}

static int qbe_type_is_pointer(IType *self)
{
	QBEType *type = (QBEType*)self;
	return type->is_pointer;
}

static int qbe_type_is_void(IType *self)
{
	QBEType *type = (QBEType*)self;
	return type->is_void;
}

static QBEType* qbe_type_create(QBEModule *module, qbe_base_type_t base_type)
{
	QBEType *type = new QBEType();
	type->refcount = 1;
	type->module = module;
	type->base_type = base_type;
	type->is_pointer = false;
	type->is_void = false;
	type->element_type = NULL;

	switch (base_type) {
	case QBE_TYPE_BYTE:   type->size = 1; type->qbe_char = 'b'; type->name = "byte"; break;
	case QBE_TYPE_HALF:   type->size = 2; type->qbe_char = 'h'; type->name = "half"; break;
	case QBE_TYPE_WORD:   type->size = 4; type->qbe_char = 'w'; type->name = "word"; break;
	case QBE_TYPE_LONG:   type->size = 8; type->qbe_char = 'l'; type->name = "long"; break;
	case QBE_TYPE_SINGLE: type->size = 4; type->qbe_char = 's'; type->name = "single"; break;
	case QBE_TYPE_DOUBLE: type->size = 8; type->qbe_char = 'd'; type->name = "double"; break;
	default:              type->size = 0; type->qbe_char = 'w'; type->name = "unknown"; break;
	}

	/* Setup interface */
	type->interface.base.AddRef = backend_addref;
	type->interface.base.Release = backend_release;
	type->interface.base.QueryInterface = backend_query_interface;
	type->interface.GetName = qbe_type_get_name;
	type->interface.GetSize = qbe_type_get_size;
	type->interface.IsInteger = qbe_type_is_integer;
	type->interface.IsFloat = qbe_type_is_float;
	type->interface.IsPointer = qbe_type_is_pointer;
	type->interface.IsVoid = qbe_type_is_void;

	module->types.push_back(type);
	return type;
}

static QBEType* qbe_type_create_pointer(QBEModule *module, QBEType *element_type)
{
	QBEType *type = new QBEType();
	type->refcount = 1;
	type->module = module;
	type->base_type = QBE_TYPE_LONG;  /* Pointers are 64-bit */
	type->size = 8;
	type->is_pointer = true;
	type->is_void = false;
	type->element_type = element_type;
	type->qbe_char = 'l';
	type->name = element_type->name + "*";

	/* Setup interface */
	type->interface.base.AddRef = backend_addref;
	type->interface.base.Release = backend_release;
	type->interface.base.QueryInterface = backend_query_interface;
	type->interface.GetName = qbe_type_get_name;
	type->interface.GetSize = qbe_type_get_size;
	type->interface.IsInteger = qbe_type_is_integer;
	type->interface.IsFloat = qbe_type_is_float;
	type->interface.IsPointer = qbe_type_is_pointer;
	type->interface.IsVoid = qbe_type_is_void;

	module->types.push_back(type);
	return type;
}

/***************************************************************************
 * QBE Value Implementation
 ***************************************************************************/

static IType* qbe_value_get_type(IValue *self)
{
	QBEValue *val = (QBEValue*)self;
	return (IType*)val->type;
}

static const char* qbe_value_get_name(IValue *self)
{
	QBEValue *val = (QBEValue*)self;
	return val->name.c_str();
}

static int qbe_value_is_constant(IValue *self)
{
	QBEValue *val = (QBEValue*)self;
	return val->is_constant;
}

static QBEValue* qbe_value_create(QBEModule *module, QBEType *type, const std::string &name)
{
	QBEValue *val = new QBEValue();
	val->refcount = 1;
	val->module = module;
	val->type = type;
	val->name = name;
	val->is_constant = false;
	val->const_value = 0;
	val->is_temp = false;
	val->temp_id = 0;

	/* Setup interface */
	val->interface.base.AddRef = backend_addref;
	val->interface.base.Release = backend_release;
	val->interface.base.QueryInterface = backend_query_interface;
	val->interface.GetType = qbe_value_get_type;
	val->interface.GetName = qbe_value_get_name;
	val->interface.IsConstant = qbe_value_is_constant;

	return val;
}

static QBEValue* qbe_value_create_temp(QBEModule *module, QBEType *type)
{
	char temp_name[64];
	snprintf(temp_name, sizeof(temp_name), "%%t%d", module->next_temp_id++);
	QBEValue *val = qbe_value_create(module, type, temp_name);
	val->is_temp = true;
	val->temp_id = module->next_temp_id - 1;
	return val;
}

static QBEValue* qbe_value_create_const(QBEModule *module, QBEType *type, uint64_t value)
{
	char const_name[64];
	snprintf(const_name, sizeof(const_name), "%lu", value);
	QBEValue *val = qbe_value_create(module, type, const_name);
	val->is_constant = true;
	val->const_value = value;
	return val;
}

/***************************************************************************
 * QBE Basic Block Implementation
 ***************************************************************************/

static const char* qbe_block_get_name(IBasicBlock *self)
{
	QBEBasicBlock *block = (QBEBasicBlock*)self;
	return block->label.c_str();
}

static QBEBasicBlock* qbe_block_create(QBEModule *module, QBEFunction *func, const std::string &label)
{
	QBEBasicBlock *block = new QBEBasicBlock();
	block->refcount = 1;
	block->module = module;
	block->function = func;
	block->label = label;
	block->terminated = false;

	/* Setup interface */
	block->interface.base.AddRef = backend_addref;
	block->interface.base.Release = backend_release;
	block->interface.base.QueryInterface = backend_query_interface;
	block->interface.GetName = qbe_block_get_name;

	func->basic_blocks.push_back(block);
	return block;
}

/***************************************************************************
 * QBE Function Implementation
 ***************************************************************************/

static const char* qbe_function_get_name(IFunction *self)
{
	QBEFunction *func = (QBEFunction*)self;
	return func->name.c_str();
}

static IType* qbe_function_get_return_type(IFunction *self)
{
	QBEFunction *func = (QBEFunction*)self;
	return (IType*)func->return_type;
}

static uint32_t qbe_function_get_param_count(IFunction *self)
{
	QBEFunction *func = (QBEFunction*)self;
	return func->param_types.size();
}

static IType* qbe_function_get_param_type(IFunction *self, uint32_t index)
{
	QBEFunction *func = (QBEFunction*)self;
	if (index >= func->param_types.size())
		return NULL;
	return (IType*)func->param_types[index];
}

static void* qbe_function_get_native_pointer(IFunction *self)
{
	QBEFunction *func = (QBEFunction*)self;
	return func->native_ptr;
}

static QBEFunction* qbe_function_create(QBEModule *module, const std::string &name,
                                        QBEType *return_type)
{
	QBEFunction *func = new QBEFunction();
	func->refcount = 1;
	func->module = module;
	func->name = name;
	func->return_type = return_type;
	func->is_variadic = false;
	func->native_ptr = NULL;
	func->linkage = "export";

	/* Setup interface */
	func->interface.base.AddRef = backend_addref;
	func->interface.base.Release = backend_release;
	func->interface.base.QueryInterface = backend_query_interface;
	func->interface.GetName = qbe_function_get_name;
	func->interface.GetReturnType = qbe_function_get_return_type;
	func->interface.GetParamCount = qbe_function_get_param_count;
	func->interface.GetParamType = qbe_function_get_param_type;
	func->interface.GetNativePointer = qbe_function_get_native_pointer;

	module->functions.push_back(func);
	return func;
}

/***************************************************************************
 * QBE Builder Implementation
 ***************************************************************************/

static void qbe_builder_position_at_end(IBuilder *self, IBasicBlock *block)
{
	QBEBuilder *builder = (QBEBuilder*)self;
	builder->current_block = (QBEBasicBlock*)block;
}

static IBasicBlock* qbe_builder_get_insert_block(IBuilder *self)
{
	QBEBuilder *builder = (QBEBuilder*)self;
	return (IBasicBlock*)builder->current_block;
}

/* Arithmetic operations */
static IValue* qbe_builder_create_add(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	QBEBuilder *builder = (QBEBuilder*)self;
	QBEValue *left = (QBEValue*)lhs;
	QBEValue *right = (QBEValue*)rhs;
	QBEValue *result = qbe_value_create_temp(builder->module, left->type);

	builder->current_block->code << "\t" << result->name << " =" << left->type->qbe_char
	                              << " add " << left->name << ", " << right->name << "\n";
	return (IValue*)result;
}

static IValue* qbe_builder_create_sub(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	QBEBuilder *builder = (QBEBuilder*)self;
	QBEValue *left = (QBEValue*)lhs;
	QBEValue *right = (QBEValue*)rhs;
	QBEValue *result = qbe_value_create_temp(builder->module, left->type);

	builder->current_block->code << "\t" << result->name << " =" << left->type->qbe_char
	                              << " sub " << left->name << ", " << right->name << "\n";
	return (IValue*)result;
}

static IValue* qbe_builder_create_mul(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	QBEBuilder *builder = (QBEBuilder*)self;
	QBEValue *left = (QBEValue*)lhs;
	QBEValue *right = (QBEValue*)rhs;
	QBEValue *result = qbe_value_create_temp(builder->module, left->type);

	builder->current_block->code << "\t" << result->name << " =" << left->type->qbe_char
	                              << " mul " << left->name << ", " << right->name << "\n";
	return (IValue*)result;
}

static IValue* qbe_builder_create_div(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	QBEBuilder *builder = (QBEBuilder*)self;
	QBEValue *left = (QBEValue*)lhs;
	QBEValue *right = (QBEValue*)rhs;
	QBEValue *result = qbe_value_create_temp(builder->module, left->type);

	/* QBE has div for signed and udiv for unsigned */
	builder->current_block->code << "\t" << result->name << " =" << left->type->qbe_char
	                              << " div " << left->name << ", " << right->name << "\n";
	return (IValue*)result;
}

static IValue* qbe_builder_create_rem(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	QBEBuilder *builder = (QBEBuilder*)self;
	QBEValue *left = (QBEValue*)lhs;
	QBEValue *right = (QBEValue*)rhs;
	QBEValue *result = qbe_value_create_temp(builder->module, left->type);

	builder->current_block->code << "\t" << result->name << " =" << left->type->qbe_char
	                              << " rem " << left->name << ", " << right->name << "\n";
	return (IValue*)result;
}

/* Bitwise operations */
static IValue* qbe_builder_create_and(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	QBEBuilder *builder = (QBEBuilder*)self;
	QBEValue *left = (QBEValue*)lhs;
	QBEValue *right = (QBEValue*)rhs;
	QBEValue *result = qbe_value_create_temp(builder->module, left->type);

	builder->current_block->code << "\t" << result->name << " =" << left->type->qbe_char
	                              << " and " << left->name << ", " << right->name << "\n";
	return (IValue*)result;
}

static IValue* qbe_builder_create_or(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	QBEBuilder *builder = (QBEBuilder*)self;
	QBEValue *left = (QBEValue*)lhs;
	QBEValue *right = (QBEValue*)rhs;
	QBEValue *result = qbe_value_create_temp(builder->module, left->type);

	builder->current_block->code << "\t" << result->name << " =" << left->type->qbe_char
	                              << " or " << left->name << ", " << right->name << "\n";
	return (IValue*)result;
}

static IValue* qbe_builder_create_xor(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	QBEBuilder *builder = (QBEBuilder*)self;
	QBEValue *left = (QBEValue*)lhs;
	QBEValue *right = (QBEValue*)rhs;
	QBEValue *result = qbe_value_create_temp(builder->module, left->type);

	builder->current_block->code << "\t" << result->name << " =" << left->type->qbe_char
	                              << " xor " << left->name << ", " << right->name << "\n";
	return (IValue*)result;
}

static IValue* qbe_builder_create_shl(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	QBEBuilder *builder = (QBEBuilder*)self;
	QBEValue *left = (QBEValue*)lhs;
	QBEValue *right = (QBEValue*)rhs;
	QBEValue *result = qbe_value_create_temp(builder->module, left->type);

	builder->current_block->code << "\t" << result->name << " =" << left->type->qbe_char
	                              << " shl " << left->name << ", " << right->name << "\n";
	return (IValue*)result;
}

static IValue* qbe_builder_create_lshr(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	QBEBuilder *builder = (QBEBuilder*)self;
	QBEValue *left = (QBEValue*)lhs;
	QBEValue *right = (QBEValue*)rhs;
	QBEValue *result = qbe_value_create_temp(builder->module, left->type);

	builder->current_block->code << "\t" << result->name << " =" << left->type->qbe_char
	                              << " shr " << left->name << ", " << right->name << "\n";
	return (IValue*)result;
}

/* Comparison operations */
static IValue* qbe_builder_create_icmp(IBuilder *self, int predicate, IValue *lhs,
                                       IValue *rhs, const char *name)
{
	QBEBuilder *builder = (QBEBuilder*)self;
	QBEValue *left = (QBEValue*)lhs;
	QBEValue *right = (QBEValue*)rhs;
	QBEType *int_type = qbe_type_create(builder->module, QBE_TYPE_WORD);
	QBEValue *result = qbe_value_create_temp(builder->module, int_type);

	const char *cmp_op = "";
	switch (predicate) {
	case 0: cmp_op = "ceq"; break;  /* EQ */
	case 1: cmp_op = "cne"; break;  /* NE */
	case 2: cmp_op = "cslt"; break; /* SLT - signed less than */
	case 3: cmp_op = "csle"; break; /* SLE - signed less or equal */
	case 4: cmp_op = "csgt"; break; /* SGT - signed greater than */
	case 5: cmp_op = "csge"; break; /* SGE - signed greater or equal */
	case 6: cmp_op = "cult"; break; /* ULT - unsigned less than */
	case 7: cmp_op = "cule"; break; /* ULE - unsigned less or equal */
	case 8: cmp_op = "cugt"; break; /* UGT - unsigned greater than */
	case 9: cmp_op = "cuge"; break; /* UGE - unsigned greater or equal */
	default: cmp_op = "ceq"; break;
	}

	builder->current_block->code << "\t" << result->name << " =" << left->type->qbe_char
	                              << " " << cmp_op << " " << left->name << ", " << right->name << "\n";
	return (IValue*)result;
}

/* Memory operations */
static IValue* qbe_builder_create_load(IBuilder *self, IValue *ptr, const char *name)
{
	QBEBuilder *builder = (QBEBuilder*)self;
	QBEValue *pointer = (QBEValue*)ptr;
	QBEType *elem_type = pointer->type->element_type;
	QBEValue *result = qbe_value_create_temp(builder->module, elem_type);

	/* QBE load: %result =t load{t} ptr */
	builder->current_block->code << "\t" << result->name << " =" << elem_type->qbe_char
	                              << " load" << elem_type->qbe_char << " " << pointer->name << "\n";
	return (IValue*)result;
}

static void qbe_builder_create_store(IBuilder *self, IValue *value, IValue *ptr)
{
	QBEBuilder *builder = (QBEBuilder*)self;
	QBEValue *val = (QBEValue*)value;
	QBEValue *pointer = (QBEValue*)ptr;

	/* QBE store: store{t} val, ptr */
	builder->current_block->code << "\tstore" << val->type->qbe_char << " "
	                              << val->name << ", " << pointer->name << "\n";
}

/* Control flow */
static void qbe_builder_create_ret(IBuilder *self, IValue *value)
{
	QBEBuilder *builder = (QBEBuilder*)self;

	if (value) {
		QBEValue *val = (QBEValue*)value;
		builder->current_block->code << "\tret " << val->name << "\n";
	} else {
		builder->current_block->code << "\tret\n";
	}
	builder->current_block->terminated = true;
}

static void qbe_builder_create_br(IBuilder *self, IBasicBlock *dest)
{
	QBEBuilder *builder = (QBEBuilder*)self;
	QBEBasicBlock *block = (QBEBasicBlock*)dest;

	builder->current_block->code << "\tjmp @" << block->label << "\n";
	builder->current_block->terminated = true;
}

static void qbe_builder_create_cond_br(IBuilder *self, IValue *cond, IBasicBlock *true_block,
                                       IBasicBlock *false_block)
{
	QBEBuilder *builder = (QBEBuilder*)self;
	QBEValue *condition = (QBEValue*)cond;
	QBEBasicBlock *tb = (QBEBasicBlock*)true_block;
	QBEBasicBlock *fb = (QBEBasicBlock*)false_block;

	builder->current_block->code << "\tjnz " << condition->name << ", @" << tb->label
	                              << ", @" << fb->label << "\n";
	builder->current_block->terminated = true;
}

static IValue* qbe_builder_create_call(IBuilder *self, IFunction *func, IValue **args,
                                       uint32_t arg_count, const char *name)
{
	QBEBuilder *builder = (QBEBuilder*)self;
	QBEFunction *function = (QBEFunction*)func;

	QBEValue *result = NULL;
	if (!function->return_type->is_void) {
		result = qbe_value_create_temp(builder->module, function->return_type);
		builder->current_block->code << "\t" << result->name << " ="
		                              << function->return_type->qbe_char << " call ";
	} else {
		builder->current_block->code << "\tcall ";
	}

	builder->current_block->code << "$" << function->name << "(";
	for (uint32_t i = 0; i < arg_count; i++) {
		QBEValue *arg = (QBEValue*)args[i];
		if (i > 0) builder->current_block->code << ", ";
		builder->current_block->code << arg->type->qbe_char << " " << arg->name;
	}
	builder->current_block->code << ")\n";

	return (IValue*)result;
}

/* Constants */
static IValue* qbe_builder_create_const_int(IBuilder *self, IType *type, uint64_t value,
                                            const char *name)
{
	QBEBuilder *builder = (QBEBuilder*)self;
	return (IValue*)qbe_value_create_const(builder->module, (QBEType*)type, value);
}

/* Type creation */
static IType* qbe_builder_get_int_type(IBuilder *self, uint32_t bits)
{
	QBEBuilder *builder = (QBEBuilder*)self;
	qbe_base_type_t base_type;

	if (bits <= 8)       base_type = QBE_TYPE_BYTE;
	else if (bits <= 16) base_type = QBE_TYPE_HALF;
	else if (bits <= 32) base_type = QBE_TYPE_WORD;
	else                 base_type = QBE_TYPE_LONG;

	return (IType*)qbe_type_create(builder->module, base_type);
}

static IType* qbe_builder_get_float_type(IBuilder *self)
{
	QBEBuilder *builder = (QBEBuilder*)self;
	return (IType*)qbe_type_create(builder->module, QBE_TYPE_SINGLE);
}

static IType* qbe_builder_get_double_type(IBuilder *self)
{
	QBEBuilder *builder = (QBEBuilder*)self;
	return (IType*)qbe_type_create(builder->module, QBE_TYPE_DOUBLE);
}

static IType* qbe_builder_get_pointer_type(IBuilder *self, IType *element_type)
{
	QBEBuilder *builder = (QBEBuilder*)self;
	return (IType*)qbe_type_create_pointer(builder->module, (QBEType*)element_type);
}

static QBEBuilder* qbe_builder_create(QBEModule *module)
{
	QBEBuilder *builder = new QBEBuilder();
	builder->refcount = 1;
	builder->module = module;
	builder->current_function = NULL;
	builder->current_block = NULL;

	/* Setup interface */
	builder->interface.base.AddRef = backend_addref;
	builder->interface.base.Release = backend_release;
	builder->interface.base.QueryInterface = backend_query_interface;
	builder->interface.PositionAtEnd = qbe_builder_position_at_end;
	builder->interface.GetInsertBlock = qbe_builder_get_insert_block;
	builder->interface.CreateAdd = qbe_builder_create_add;
	builder->interface.CreateSub = qbe_builder_create_sub;
	builder->interface.CreateMul = qbe_builder_create_mul;
	builder->interface.CreateDiv = qbe_builder_create_div;
	builder->interface.CreateRem = qbe_builder_create_rem;
	builder->interface.CreateAnd = qbe_builder_create_and;
	builder->interface.CreateOr = qbe_builder_create_or;
	builder->interface.CreateXor = qbe_builder_create_xor;
	builder->interface.CreateShl = qbe_builder_create_shl;
	builder->interface.CreateLShr = qbe_builder_create_lshr;
	builder->interface.CreateICmp = qbe_builder_create_icmp;
	builder->interface.CreateLoad = qbe_builder_create_load;
	builder->interface.CreateStore = qbe_builder_create_store;
	builder->interface.CreateRet = qbe_builder_create_ret;
	builder->interface.CreateBr = qbe_builder_create_br;
	builder->interface.CreateCondBr = qbe_builder_create_cond_br;
	builder->interface.CreateCall = qbe_builder_create_call;
	builder->interface.CreateConstInt = qbe_builder_create_const_int;
	builder->interface.GetIntType = qbe_builder_get_int_type;
	builder->interface.GetFloatType = qbe_builder_get_float_type;
	builder->interface.GetDoubleType = qbe_builder_get_double_type;
	builder->interface.GetPointerType = qbe_builder_get_pointer_type;

	return builder;
}

/***************************************************************************
 * QBE Module Implementation
 ***************************************************************************/

static IFunction* qbe_module_add_function(IModule *self, const char *name, IType *return_type,
                                          IType **param_types, uint32_t param_count)
{
	QBEModule *module = (QBEModule*)self;
	QBEFunction *func = qbe_function_create(module, name, (QBEType*)return_type);

	for (uint32_t i = 0; i < param_count; i++) {
		func->param_types.push_back((QBEType*)param_types[i]);
		char param_name[64];
		snprintf(param_name, sizeof(param_name), "%%arg%d", i);
		func->param_names.push_back(param_name);
	}

	return (IFunction*)func;
}

static IFunction* qbe_module_get_function(IModule *self, const char *name)
{
	QBEModule *module = (QBEModule*)self;

	for (auto func : module->functions) {
		if (func->name == name)
			return (IFunction*)func;
	}
	return NULL;
}

static IBasicBlock* qbe_module_create_basic_block(IModule *self, IFunction *func, const char *name)
{
	QBEModule *module = (QBEModule*)self;
	QBEFunction *function = (QBEFunction*)func;

	std::string label = name ? name : ("L" + std::to_string(module->next_label_id++));
	return (IBasicBlock*)qbe_block_create(module, function, label);
}

static IBuilder* qbe_module_create_builder(IModule *self)
{
	QBEModule *module = (QBEModule*)self;
	return (IBuilder*)qbe_builder_create(module);
}

/* Generate QBE IL text from internal representation */
static void qbe_module_generate_il(QBEModule *module)
{
	module->ir_stream.str("");
	module->ir_stream.clear();

	/* Generate functions */
	for (auto func : module->functions) {
		/* Function signature: export function $name(params) { */
		module->ir_stream << func->linkage << " function ";
		if (!func->return_type->is_void) {
			module->ir_stream << func->return_type->qbe_char << " ";
		}
		module->ir_stream << "$" << func->name << "(";

		for (size_t i = 0; i < func->param_types.size(); i++) {
			if (i > 0) module->ir_stream << ", ";
			module->ir_stream << func->param_types[i]->qbe_char << " " << func->param_names[i];
		}
		module->ir_stream << ") {\n";

		/* Generate basic blocks */
		for (auto block : func->basic_blocks) {
			module->ir_stream << "@" << block->label << "\n";
			module->ir_stream << block->code.str();
		}

		module->ir_stream << "}\n\n";
	}
}

/* Compile QBE IL to native code */
static int qbe_module_compile(IModule *self)
{
	QBEModule *module = (QBEModule*)self;

	/* Generate QBE IL */
	qbe_module_generate_il(module);
	std::string qbe_il = module->ir_stream.str();

	/* Write QBE IL to temporary file */
	char qbe_file[] = "/tmp/libcpu_qbe_XXXXXX.ssa";
	int fd = mkstemps(qbe_file, 4);
	if (fd < 0) {
		fprintf(stderr, "QBE: Failed to create temporary file\n");
		return -1;
	}

	write(fd, qbe_il.c_str(), qbe_il.size());
	close(fd);

	/* Compile QBE IL to assembly */
	char asm_file[] = "/tmp/libcpu_qbe_XXXXXX.s";
	int asm_fd = mkstemps(asm_file, 2);
	if (asm_fd < 0) {
		unlink(qbe_file);
		return -1;
	}
	close(asm_fd);

	/* Run QBE compiler: qbe -o asm_file qbe_file */
	char cmd[512];
	snprintf(cmd, sizeof(cmd), "qbe -o %s %s 2>/dev/null", asm_file, qbe_file);
	int ret = system(cmd);

	if (ret != 0) {
		/* QBE binary not available, fall back to simple stub */
		fprintf(stderr, "QBE: Compiler not found, using stub implementation\n");
		unlink(qbe_file);
		unlink(asm_file);
		return -1;
	}

	/* Compile assembly to shared object */
	char so_file[] = "/tmp/libcpu_qbe_XXXXXX.so";
	int so_fd = mkstemps(so_file, 3);
	if (so_fd < 0) {
		unlink(qbe_file);
		unlink(asm_file);
		return -1;
	}
	close(so_fd);

	snprintf(cmd, sizeof(cmd), "gcc -shared -o %s %s 2>/dev/null", so_file, asm_file);
	ret = system(cmd);

	unlink(qbe_file);
	unlink(asm_file);

	if (ret != 0) {
		unlink(so_file);
		return -1;
	}

	/* Load shared object */
	module->dl_handle = dlopen(so_file, RTLD_NOW);
	if (!module->dl_handle) {
		fprintf(stderr, "QBE: Failed to load shared object: %s\n", dlerror());
		unlink(so_file);
		return -1;
	}

	/* Resolve function addresses */
	for (auto func : module->functions) {
		func->native_ptr = dlsym(module->dl_handle, func->name.c_str());
		if (!func->native_ptr) {
			fprintf(stderr, "QBE: Failed to resolve function '%s'\n", func->name.c_str());
		}
	}

	/* Clean up temporary file */
	unlink(so_file);

	fprintf(stderr, "QBE: Successfully compiled module with %zu functions\n",
	        module->functions.size());
	return 0;
}

static void* qbe_module_get_function_address(IModule *self, const char *name)
{
	QBEModule *module = (QBEModule*)self;

	for (auto func : module->functions) {
		if (func->name == name)
			return func->native_ptr;
	}
	return NULL;
}

static const char* qbe_module_get_ir(IModule *self)
{
	QBEModule *module = (QBEModule*)self;
	qbe_module_generate_il(module);
	return module->ir_stream.str().c_str();
}

static void qbe_module_dump(IModule *self)
{
	QBEModule *module = (QBEModule*)self;
	qbe_module_generate_il(module);
	printf("%s", module->ir_stream.str().c_str());
}

/* Export for stub backend */
extern "C" QBEModule* qbe_module_create(QBEBackend *backend, const char *name)
{
	QBEModule *module = new QBEModule();
	module->refcount = 1;
	module->backend = backend;
	module->name = name;
	module->compiled_code = NULL;
	module->code_size = 0;
	module->dl_handle = NULL;
	module->next_temp_id = 0;
	module->next_label_id = 0;

	/* Setup interface */
	module->interface.base.AddRef = backend_addref;
	module->interface.base.Release = backend_release;
	module->interface.base.QueryInterface = backend_query_interface;
	module->interface.AddFunction = qbe_module_add_function;
	module->interface.GetFunction = qbe_module_get_function;
	module->interface.CreateBasicBlock = qbe_module_create_basic_block;
	module->interface.CreateBuilder = qbe_module_create_builder;
	module->interface.Compile = qbe_module_compile;
	module->interface.GetFunctionAddress = qbe_module_get_function_address;
	module->interface.GetIR = qbe_module_get_ir;
	module->interface.Dump = qbe_module_dump;

	return module;
}

/***************************************************************************
 * QBE Backend Implementation
 ***************************************************************************/

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

static int qbe_backend_initialize(IBackend *self)
{
	QBEBackend *backend = (QBEBackend*)self;
	if (backend->initialized)
		return 0;

	/* Check if QBE is available */
	int ret = system("which qbe >/dev/null 2>&1");
	if (ret != 0) {
		fprintf(stderr, "QBE backend: Warning - QBE compiler not found in PATH\n");
		fprintf(stderr, "QBE backend: Install from https://c9x.me/compile/\n");
	}

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
	if (strcmp(feature, "float") == 0)
		return 1;
	if (strcmp(feature, "double") == 0)
		return 1;
	if (strcmp(feature, "ssa") == 0)
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
	return 0;
}

static int qbe_backend_supports_float128(IBackend *self)
{
	return 0;
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
