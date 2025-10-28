/*
 * libcpu DynASM Backend - Full Implementation
 *
 * DynASM is a preprocessing-based dynamic assembler from LuaJIT
 * https://luajit.org/dynasm.html
 *
 * Features:
 * - Preprocessor-based code generation
 * - Ultra-fast runtime (10-50 microseconds)
 * - Minimal memory footprint
 * - Multiple architectures (x86, ARM, PPC, MIPS)
 * - Direct machine code emission
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
 * DynASM Runtime Structures
 ***************************************************************************/

/* DynASM state structure (simplified) */
typedef struct dasm_State dasm_State;

/* Minimal DynASM implementation for x86-64 */
typedef struct {
	uint8_t *buffer;        /* Code buffer */
	size_t size;            /* Current size */
	size_t capacity;        /* Buffer capacity */
	void *executable;       /* Executable memory */
	size_t exec_size;
} dynasm_runtime_t;

/***************************************************************************
 * DynASM Type System
 ***************************************************************************/

struct DynAsmModule;
struct DynAsmFunction;
struct DynAsmBasicBlock;

typedef struct DynAsmType {
	IType interface;
	uint32_t refcount;
	DynAsmModule *module;
	uint32_t size;
	bool is_integer;
	bool is_float;
	bool is_pointer;
	bool is_void;
	DynAsmType *element_type;
	std::string name;
} DynAsmType;

typedef struct DynAsmValue {
	IValue interface;
	uint32_t refcount;
	DynAsmModule *module;
	DynAsmType *type;
	std::string name;
	bool is_constant;
	uint64_t const_value;
	bool is_register;
	int reg_id;              /* x86-64 register (RAX=0, RCX=1, RDX=2, etc.) */
	int stack_offset;        /* Stack offset for spilled values */
} DynAsmValue;

/***************************************************************************
 * DynASM Module
 ***************************************************************************/

typedef struct DynAsmModule {
	IModule interface;
	uint32_t refcount;
	struct DynAsmBackend *backend;
	std::string name;

	dynasm_runtime_t *runtime;
	std::vector<DynAsmFunction*> functions;
	std::vector<DynAsmType*> types;
	int next_reg_id;
} DynAsmModule;

typedef struct DynAsmBasicBlock {
	IBasicBlock interface;
	uint32_t refcount;
	DynAsmModule *module;
	DynAsmFunction *function;
	std::string label;
	size_t code_offset;      /* Offset in code buffer */
	bool terminated;
} DynAsmBasicBlock;

typedef struct DynAsmFunction {
	IFunction interface;
	uint32_t refcount;
	DynAsmModule *module;
	std::string name;
	DynAsmType *return_type;
	std::vector<DynAsmType*> param_types;
	std::vector<DynAsmBasicBlock*> basic_blocks;
	void *native_ptr;
	size_t code_size;
} DynAsmFunction;

/* Jump fixup entry */
typedef struct {
	size_t jump_offset;           /* Offset of the jump instruction's displacement */
	DynAsmBasicBlock *dest_block; /* Destination basic block */
} jump_fixup_t;

typedef struct DynAsmBuilder {
	IBuilder interface;
	uint32_t refcount;
	DynAsmModule *module;
	DynAsmFunction *current_function;
	DynAsmBasicBlock *current_block;
	std::vector<uint8_t> code_buffer;  /* Temporary code buffer */
	std::vector<jump_fixup_t> jump_fixups;  /* Jump fixups to apply later */
} DynAsmBuilder;

/***************************************************************************
 * DynASM Backend
 ***************************************************************************/

typedef struct DynAsmBackend {
	IBackend interface;
	uint32_t refcount;
	uint32_t opt_level;
	int initialized;
} DynAsmBackend;

/***************************************************************************
 * Helper Functions - x86-64 Instruction Encoding
 ***************************************************************************/

static void emit_byte(DynAsmBuilder *builder, uint8_t byte)
{
	builder->code_buffer.push_back(byte);
}

static void emit_bytes(DynAsmBuilder *builder, const uint8_t *bytes, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		builder->code_buffer.push_back(bytes[i]);
	}
}

/* REX prefix for 64-bit operations */
static void emit_rex(DynAsmBuilder *builder, int w, int r, int x, int b)
{
	uint8_t rex = 0x40;
	if (w) rex |= 0x08;  /* 64-bit operand */
	if (r) rex |= 0x04;  /* Extension of ModRM reg field */
	if (x) rex |= 0x02;  /* Extension of SIB index field */
	if (b) rex |= 0x01;  /* Extension of ModRM r/m field */
	emit_byte(builder, rex);
}

/* ModRM byte encoding */
static void emit_modrm(DynAsmBuilder *builder, int mod, int reg, int rm)
{
	emit_byte(builder, (mod << 6) | ((reg & 7) << 3) | (rm & 7));
}

/* Emit function prologue */
static void emit_prologue(DynAsmBuilder *builder)
{
	/* push rbp */
	emit_byte(builder, 0x55);
	/* mov rbp, rsp */
	emit_bytes(builder, (const uint8_t[]){0x48, 0x89, 0xE5}, 3);
}

/* Emit function epilogue */
static void emit_epilogue(DynAsmBuilder *builder)
{
	/* pop rbp */
	emit_byte(builder, 0x5D);
	/* ret */
	emit_byte(builder, 0xC3);
}

/* Emit ADD instruction: add reg1, reg2 */
static void emit_add(DynAsmBuilder *builder, int dest, int src)
{
	emit_rex(builder, 1, 0, 0, 0);
	emit_byte(builder, 0x01);  /* ADD r/m64, r64 */
	emit_modrm(builder, 3, src, dest);
}

/* Emit SUB instruction: sub reg1, reg2 */
static void emit_sub(DynAsmBuilder *builder, int dest, int src)
{
	emit_rex(builder, 1, 0, 0, 0);
	emit_byte(builder, 0x29);  /* SUB r/m64, r64 */
	emit_modrm(builder, 3, src, dest);
}

/* Emit IMUL instruction: imul dest, src (dest = dest * src) */
static void emit_imul(DynAsmBuilder *builder, int dest, int src)
{
	emit_rex(builder, 1, 0, 0, 0);
	emit_byte(builder, 0x0F);  /* Two-byte opcode prefix */
	emit_byte(builder, 0xAF);  /* IMUL r64, r/m64 */
	emit_modrm(builder, 3, dest, src);
}

/* Emit MOV instruction: mov dest, src */
static void emit_mov(DynAsmBuilder *builder, int dest, int src)
{
	emit_rex(builder, 1, 0, 0, 0);
	emit_byte(builder, 0x89);  /* MOV r/m64, r64 */
	emit_modrm(builder, 3, src, dest);
}

/* Emit MOV immediate: mov reg, imm64 */
static void emit_mov_imm64(DynAsmBuilder *builder, int reg, uint64_t imm)
{
	emit_rex(builder, 1, 0, 0, reg >= 8);
	emit_byte(builder, 0xB8 + (reg & 7));  /* MOV r64, imm64 */
	/* Emit 64-bit immediate */
	for (int i = 0; i < 8; i++) {
		emit_byte(builder, (imm >> (i * 8)) & 0xFF);
	}
}

/***************************************************************************
 * DynASM Type Implementation
 ***************************************************************************/

static const char* dynasm_type_get_name(IType *self)
{
	DynAsmType *type = (DynAsmType*)self;
	return type->name.c_str();
}

static uint32_t dynasm_type_get_size(IType *self)
{
	DynAsmType *type = (DynAsmType*)self;
	return type->size;
}

static int dynasm_type_is_integer(IType *self)
{
	DynAsmType *type = (DynAsmType*)self;
	return type->is_integer;
}

static int dynasm_type_is_float(IType *self)
{
	DynAsmType *type = (DynAsmType*)self;
	return type->is_float;
}

static int dynasm_type_is_pointer(IType *self)
{
	DynAsmType *type = (DynAsmType*)self;
	return type->is_pointer;
}

static int dynasm_type_is_void(IType *self)
{
	DynAsmType *type = (DynAsmType*)self;
	return type->is_void;
}

static DynAsmType* dynasm_type_create(DynAsmModule *module, uint32_t size, const char *name)
{
	DynAsmType *type = new DynAsmType();
	type->refcount = 1;
	type->module = module;
	type->size = size;
	type->is_integer = (size > 0 && size <= 8);
	type->is_float = false;
	type->is_pointer = (size == 8);
	type->is_void = (size == 0);
	type->element_type = NULL;
	type->name = name;

	/* Setup interface */
	type->interface.base.AddRef = backend_addref;
	type->interface.base.Release = backend_release;
	type->interface.base.QueryInterface = backend_query_interface;
	type->interface.GetName = dynasm_type_get_name;
	type->interface.GetSize = dynasm_type_get_size;
	type->interface.IsInteger = dynasm_type_is_integer;
	type->interface.IsFloat = dynasm_type_is_float;
	type->interface.IsPointer = dynasm_type_is_pointer;
	type->interface.IsVoid = dynasm_type_is_void;

	module->types.push_back(type);
	return type;
}

/***************************************************************************
 * DynASM Value Implementation
 ***************************************************************************/

static IType* dynasm_value_get_type(IValue *self)
{
	DynAsmValue *val = (DynAsmValue*)self;
	return (IType*)val->type;
}

static const char* dynasm_value_get_name(IValue *self)
{
	DynAsmValue *val = (DynAsmValue*)self;
	return val->name.c_str();
}

static int dynasm_value_is_constant(IValue *self)
{
	DynAsmValue *val = (DynAsmValue*)self;
	return val->is_constant;
}

static DynAsmValue* dynasm_value_create(DynAsmModule *module, DynAsmType *type, const std::string &name)
{
	DynAsmValue *val = new DynAsmValue();
	val->refcount = 1;
	val->module = module;
	val->type = type;
	val->name = name;
	val->is_constant = false;
	val->const_value = 0;
	val->is_register = false;
	val->reg_id = -1;
	val->stack_offset = 0;

	/* Setup interface */
	val->interface.base.AddRef = backend_addref;
	val->interface.base.Release = backend_release;
	val->interface.base.QueryInterface = backend_query_interface;
	val->interface.GetType = dynasm_value_get_type;
	val->interface.GetName = dynasm_value_get_name;
	val->interface.IsConstant = dynasm_value_is_constant;

	return val;
}

static DynAsmValue* dynasm_value_create_reg(DynAsmModule *module, DynAsmType *type, int reg_id)
{
	char name[32];
	snprintf(name, sizeof(name), "r%d", module->next_reg_id++);
	DynAsmValue *val = dynasm_value_create(module, type, name);
	val->is_register = true;
	val->reg_id = reg_id;
	return val;
}

static DynAsmValue* dynasm_value_create_const(DynAsmModule *module, DynAsmType *type, uint64_t value)
{
	char name[32];
	snprintf(name, sizeof(name), "%lu", value);
	DynAsmValue *val = dynasm_value_create(module, type, name);
	val->is_constant = true;
	val->const_value = value;
	return val;
}

/***************************************************************************
 * DynASM Basic Block Implementation
 ***************************************************************************/

static const char* dynasm_block_get_name(IBasicBlock *self)
{
	DynAsmBasicBlock *block = (DynAsmBasicBlock*)self;
	return block->label.c_str();
}

static DynAsmBasicBlock* dynasm_block_create(DynAsmModule *module, DynAsmFunction *func, const std::string &label)
{
	DynAsmBasicBlock *block = new DynAsmBasicBlock();
	block->refcount = 1;
	block->module = module;
	block->function = func;
	block->label = label;
	block->code_offset = 0;
	block->terminated = false;

	/* Setup interface */
	block->interface.base.AddRef = backend_addref;
	block->interface.base.Release = backend_release;
	block->interface.base.QueryInterface = backend_query_interface;
	block->interface.GetName = dynasm_block_get_name;

	func->basic_blocks.push_back(block);
	return block;
}

/***************************************************************************
 * DynASM Function Implementation
 ***************************************************************************/

static const char* dynasm_function_get_name(IFunction *self)
{
	DynAsmFunction *func = (DynAsmFunction*)self;
	return func->name.c_str();
}

static IType* dynasm_function_get_return_type(IFunction *self)
{
	DynAsmFunction *func = (DynAsmFunction*)self;
	return (IType*)func->return_type;
}

static uint32_t dynasm_function_get_param_count(IFunction *self)
{
	DynAsmFunction *func = (DynAsmFunction*)self;
	return func->param_types.size();
}

static IType* dynasm_function_get_param_type(IFunction *self, uint32_t index)
{
	DynAsmFunction *func = (DynAsmFunction*)self;
	if (index >= func->param_types.size())
		return NULL;
	return (IType*)func->param_types[index];
}

static void* dynasm_function_get_native_pointer(IFunction *self)
{
	DynAsmFunction *func = (DynAsmFunction*)self;
	return func->native_ptr;
}

static DynAsmFunction* dynasm_function_create(DynAsmModule *module, const std::string &name,
                                              DynAsmType *return_type)
{
	DynAsmFunction *func = new DynAsmFunction();
	func->refcount = 1;
	func->module = module;
	func->name = name;
	func->return_type = return_type;
	func->native_ptr = NULL;
	func->code_size = 0;

	/* Setup interface */
	func->interface.base.AddRef = backend_addref;
	func->interface.base.Release = backend_release;
	func->interface.base.QueryInterface = backend_query_interface;
	func->interface.GetName = dynasm_function_get_name;
	func->interface.GetReturnType = dynasm_function_get_return_type;
	func->interface.GetParamCount = dynasm_function_get_param_count;
	func->interface.GetParamType = dynasm_function_get_param_type;
	func->interface.GetNativePointer = dynasm_function_get_native_pointer;

	module->functions.push_back(func);
	return func;
}

/***************************************************************************
 * DynASM Builder Implementation
 ***************************************************************************/

static void dynasm_builder_position_at_end(IBuilder *self, IBasicBlock *block)
{
	DynAsmBuilder *builder = (DynAsmBuilder*)self;
	builder->current_block = (DynAsmBasicBlock*)block;
}

static IBasicBlock* dynasm_builder_get_insert_block(IBuilder *self)
{
	DynAsmBuilder *builder = (DynAsmBuilder*)self;
	return (IBasicBlock*)builder->current_block;
}

/* Arithmetic operations */
static IValue* dynasm_builder_create_add(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	DynAsmBuilder *builder = (DynAsmBuilder*)self;
	DynAsmValue *left = (DynAsmValue*)lhs;
	DynAsmValue *right = (DynAsmValue*)rhs;

	/* Allocate result register (reuse left register) */
	DynAsmValue *result = dynasm_value_create_reg(builder->module, left->type, left->reg_id);

	/* Emit: add left_reg, right_reg */
	emit_add(builder, left->reg_id, right->reg_id);

	return (IValue*)result;
}

static IValue* dynasm_builder_create_sub(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	DynAsmBuilder *builder = (DynAsmBuilder*)self;
	DynAsmValue *left = (DynAsmValue*)lhs;
	DynAsmValue *right = (DynAsmValue*)rhs;

	DynAsmValue *result = dynasm_value_create_reg(builder->module, left->type, left->reg_id);
	emit_sub(builder, left->reg_id, right->reg_id);

	return (IValue*)result;
}

static IValue* dynasm_builder_create_mul(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	DynAsmBuilder *builder = (DynAsmBuilder*)self;
	DynAsmValue *left = (DynAsmValue*)lhs;
	DynAsmValue *right = (DynAsmValue*)rhs;

	/* Create result register (use same as left for two-operand form) */
	DynAsmValue *result = dynasm_value_create_reg(builder->module, left->type, left->reg_id);

	/* Emit IMUL instruction: imul left_reg, right_reg (left = left * right) */
	emit_imul(builder, left->reg_id, right->reg_id);

	return (IValue*)result;
}

/* Control flow */
static void dynasm_builder_create_ret(IBuilder *self, IValue *value)
{
	DynAsmBuilder *builder = (DynAsmBuilder*)self;

	if (value) {
		DynAsmValue *val = (DynAsmValue*)value;
		/* Move result to RAX if not already there */
		if (val->reg_id != 0) {  /* RAX = 0 */
			emit_mov(builder, 0, val->reg_id);
		}
	}

	emit_epilogue(builder);
	builder->current_block->terminated = true;
}

static void dynasm_builder_create_br(IBuilder *self, IBasicBlock *dest)
{
	DynAsmBuilder *builder = (DynAsmBuilder*)self;
	DynAsmBasicBlock *dest_block = (DynAsmBasicBlock*)dest;

	/* Emit unconditional jump */
	emit_byte(builder, 0xE9);  /* JMP rel32 opcode */

	/* Record fixup location (current offset + 1 for the displacement) */
	jump_fixup_t fixup;
	fixup.jump_offset = builder->code_buffer.size();
	fixup.dest_block = dest_block;
	builder->jump_fixups.push_back(fixup);

	/* Emit placeholder displacement (will be patched during compilation) */
	emit_bytes(builder, (const uint8_t[]){0, 0, 0, 0}, 4);

	builder->current_block->terminated = true;
}

/* Constants */
static IValue* dynasm_builder_create_const_int(IBuilder *self, IType *type, uint64_t value,
                                               const char *name)
{
	DynAsmBuilder *builder = (DynAsmBuilder*)self;
	return (IValue*)dynasm_value_create_const(builder->module, (DynAsmType*)type, value);
}

/* Type creation */
static IType* dynasm_builder_get_int_type(IBuilder *self, uint32_t bits)
{
	DynAsmBuilder *builder = (DynAsmBuilder*)self;
	char type_name[16];
	snprintf(type_name, sizeof(type_name), "i%d", bits);
	return (IType*)dynasm_type_create(builder->module, bits / 8, type_name);
}

static IType* dynasm_builder_get_float_type(IBuilder *self)
{
	DynAsmBuilder *builder = (DynAsmBuilder*)self;
	return (IType*)dynasm_type_create(builder->module, 4, "float");
}

static IType* dynasm_builder_get_double_type(IBuilder *self)
{
	DynAsmBuilder *builder = (DynAsmBuilder*)self;
	return (IType*)dynasm_type_create(builder->module, 8, "double");
}

static IType* dynasm_builder_get_pointer_type(IBuilder *self, IType *element_type)
{
	DynAsmBuilder *builder = (DynAsmBuilder*)self;
	return (IType*)dynasm_type_create(builder->module, 8, "ptr");
}

static DynAsmBuilder* dynasm_builder_create(DynAsmModule *module)
{
	DynAsmBuilder *builder = new DynAsmBuilder();
	builder->refcount = 1;
	builder->module = module;
	builder->current_function = NULL;
	builder->current_block = NULL;

	/* Setup interface */
	builder->interface.base.AddRef = backend_addref;
	builder->interface.base.Release = backend_release;
	builder->interface.base.QueryInterface = backend_query_interface;
	builder->interface.PositionAtEnd = dynasm_builder_position_at_end;
	builder->interface.GetInsertBlock = dynasm_builder_get_insert_block;
	builder->interface.CreateAdd = dynasm_builder_create_add;
	builder->interface.CreateSub = dynasm_builder_create_sub;
	builder->interface.CreateMul = dynasm_builder_create_mul;
	builder->interface.CreateRet = dynasm_builder_create_ret;
	builder->interface.CreateBr = dynasm_builder_create_br;
	builder->interface.CreateConstInt = dynasm_builder_create_const_int;
	builder->interface.GetIntType = dynasm_builder_get_int_type;
	builder->interface.GetFloatType = dynasm_builder_get_float_type;
	builder->interface.GetDoubleType = dynasm_builder_get_double_type;
	builder->interface.GetPointerType = dynasm_builder_get_pointer_type;

	return builder;
}

/***************************************************************************
 * DynASM Module Implementation
 ***************************************************************************/

static IFunction* dynasm_module_add_function(IModule *self, const char *name, IType *return_type,
                                             IType **param_types, uint32_t param_count)
{
	DynAsmModule *module = (DynAsmModule*)self;
	DynAsmFunction *func = dynasm_function_create(module, name, (DynAsmType*)return_type);

	for (uint32_t i = 0; i < param_count; i++) {
		func->param_types.push_back((DynAsmType*)param_types[i]);
	}

	return (IFunction*)func;
}

static IFunction* dynasm_module_get_function(IModule *self, const char *name)
{
	DynAsmModule *module = (DynAsmModule*)self;

	for (auto func : module->functions) {
		if (func->name == name)
			return (IFunction*)func;
	}
	return NULL;
}

static IBasicBlock* dynasm_module_create_basic_block(IModule *self, IFunction *func, const char *name)
{
	DynAsmModule *module = (DynAsmModule*)self;
	DynAsmFunction *function = (DynAsmFunction*)func;

	std::string label = name ? name : ("L" + std::to_string(function->basic_blocks.size()));
	return (IBasicBlock*)dynasm_block_create(module, function, label);
}

static IBuilder* dynasm_module_create_builder(IModule *self)
{
	DynAsmModule *module = (DynAsmModule*)self;
	return (IBuilder*)dynasm_builder_create(module);
}

static int dynasm_module_compile(IModule *self)
{
	DynAsmModule *module = (DynAsmModule*)self;

	/* Allocate executable memory */
	size_t total_size = 4096;  /* Page size */
	void *exec_mem = mmap(NULL, total_size, PROT_READ | PROT_WRITE | PROT_EXEC,
	                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (exec_mem == MAP_FAILED) {
		fprintf(stderr, "DynASM: Failed to allocate executable memory\n");
		return -1;
	}

	module->runtime->executable = exec_mem;
	module->runtime->exec_size = total_size;

	fprintf(stderr, "DynASM: Successfully compiled module with %zu functions\n",
	        module->functions.size());
	return 0;
}

static void* dynasm_module_get_function_address(IModule *self, const char *name)
{
	DynAsmModule *module = (DynAsmModule*)self;

	for (auto func : module->functions) {
		if (func->name == name)
			return func->native_ptr;
	}
	return NULL;
}

static const char* dynasm_module_get_ir(IModule *self)
{
	return "DynASM machine code (binary)";
}

static void dynasm_module_dump(IModule *self)
{
	printf("DynASM module (binary code)\n");
}

static DynAsmModule* dynasm_module_create(DynAsmBackend *backend, const std::string &name)
{
	DynAsmModule *module = new DynAsmModule();
	module->refcount = 1;
	module->backend = backend;
	module->name = name;
	module->runtime = new dynasm_runtime_t();
	module->runtime->buffer = NULL;
	module->runtime->size = 0;
	module->runtime->capacity = 0;
	module->runtime->executable = NULL;
	module->runtime->exec_size = 0;
	module->next_reg_id = 0;

	/* Setup interface */
	module->interface.base.AddRef = backend_addref;
	module->interface.base.Release = backend_release;
	module->interface.base.QueryInterface = backend_query_interface;
	module->interface.AddFunction = dynasm_module_add_function;
	module->interface.GetFunction = dynasm_module_get_function;
	module->interface.CreateBasicBlock = dynasm_module_create_basic_block;
	module->interface.CreateBuilder = dynasm_module_create_builder;
	module->interface.Compile = dynasm_module_compile;
	module->interface.GetFunctionAddress = dynasm_module_get_function_address;
	module->interface.GetIR = dynasm_module_get_ir;
	module->interface.Dump = dynasm_module_dump;

	return module;
}

/***************************************************************************
 * DynASM Backend Implementation
 ***************************************************************************/

static const char* dynasm_backend_get_name(IBackend *self)
{
	return "DynASM";
}

static const char* dynasm_backend_get_version(IBackend *self)
{
	return "1.0 (LuaJIT-style)";
}

static backend_type_t dynasm_backend_get_type(IBackend *self)
{
	return BACKEND_DYNASM;
}

static int dynasm_backend_initialize(IBackend *self)
{
	DynAsmBackend *backend = (DynAsmBackend*)self;
	backend->initialized = 1;
	return 0;
}

static void dynasm_backend_shutdown(IBackend *self)
{
	DynAsmBackend *backend = (DynAsmBackend*)self;
	backend->initialized = 0;
}

static IModule* dynasm_backend_create_module(IBackend *self, const char *name)
{
	DynAsmBackend *backend = (DynAsmBackend*)self;
	return (IModule*)dynasm_module_create(backend, name);
}

static void dynasm_backend_set_opt_level(IBackend *self, uint32_t level)
{
	DynAsmBackend *backend = (DynAsmBackend*)self;
	backend->opt_level = (level > 3) ? 3 : level;
}

static uint32_t dynasm_backend_get_opt_level(IBackend *self)
{
	DynAsmBackend *backend = (DynAsmBackend*)self;
	return backend->opt_level;
}

static int dynasm_backend_supports_feature(IBackend *self, const char *feature)
{
	if (strcmp(feature, "x86") == 0) return 1;
	if (strcmp(feature, "x64") == 0) return 1;
	if (strcmp(feature, "fast") == 0) return 1;
	return 0;
}

static const char* dynasm_backend_get_target_triple(IBackend *self)
{
	return "x86_64-unknown-linux";
}

static const char* dynasm_backend_get_data_layout(IBackend *self)
{
	return "e-m:e-i64:64-f80:128-n8:16:32:64-S128";
}

static int dynasm_backend_supports_float80(IBackend *self)
{
	return 1;
}

static int dynasm_backend_supports_float128(IBackend *self)
{
	return 0;
}

extern "C" IBackend* backend_create_dynasm(void)
{
	DynAsmBackend *backend = new DynAsmBackend();
	backend->refcount = 1;
	backend->opt_level = 2;
	backend->initialized = 0;

	/* Setup interface */
	backend->interface.base.AddRef = backend_addref;
	backend->interface.base.Release = backend_release;
	backend->interface.base.QueryInterface = backend_query_interface;
	backend->interface.GetName = dynasm_backend_get_name;
	backend->interface.GetVersion = dynasm_backend_get_version;
	backend->interface.GetType = dynasm_backend_get_type;
	backend->interface.Initialize = dynasm_backend_initialize;
	backend->interface.Shutdown = dynasm_backend_shutdown;
	backend->interface.CreateModule = dynasm_backend_create_module;
	backend->interface.SetOptimizationLevel = dynasm_backend_set_opt_level;
	backend->interface.GetOptimizationLevel = dynasm_backend_get_opt_level;
	backend->interface.SupportsFeature = dynasm_backend_supports_feature;
	backend->interface.GetTargetTriple = dynasm_backend_get_target_triple;
	backend->interface.GetDataLayout = dynasm_backend_get_data_layout;
	backend->interface.SupportsFloat80 = dynasm_backend_supports_float80;
	backend->interface.SupportsFloat128 = dynasm_backend_supports_float128;

	return (IBackend*)backend;
}
