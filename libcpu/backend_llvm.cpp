/*
 * libcpu LLVM Backend Implementation
 */

#include "backend.h"
#include "libcpu_llvm.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Verifier.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/DataLayout.h>

#include <string>
#include <vector>
#include <map>

using namespace llvm;

/***************************************************************************
 * LLVM Type Wrapper
 ***************************************************************************/
typedef struct LLVMType {
	IType interface;
	uint32_t refcount;
	Type *llvm_type;
	struct LLVMModule *module;
} LLVMType;

static value_type_t llvm_type_get_kind(IType *self);
static uint32_t llvm_type_get_bitwidth(IType *self);
static int llvm_type_is_integer(IType *self);
static int llvm_type_is_floating_point(IType *self);
static int llvm_type_is_pointer(IType *self);
static int llvm_type_is_void(IType *self);
static IType* llvm_type_get_element_type(IType *self);

static uint32_t llvm_type_addref(void *self)
{
	LLVMType *type = (LLVMType*)self;
	return ++type->refcount;
}

static uint32_t llvm_type_release(void *self)
{
	LLVMType *type = (LLVMType*)self;
	uint32_t count = --type->refcount;
	if (count == 0) {
		delete type;
	}
	return count;
}

static int llvm_type_query_interface(void *self, const char *iid, void **out)
{
	if (out == NULL)
		return -1;
	*out = self;
	llvm_type_addref(self);
	return 0;
}

static LLVMType* llvm_type_create(Type *llvm_type, struct LLVMModule *module)
{
	LLVMType *type = new LLVMType();
	type->refcount = 1;
	type->llvm_type = llvm_type;
	type->module = module;

	/* Setup interface */
	type->interface.base.AddRef = llvm_type_addref;
	type->interface.base.Release = llvm_type_release;
	type->interface.base.QueryInterface = llvm_type_query_interface;
	type->interface.GetKind = llvm_type_get_kind;
	type->interface.GetBitWidth = llvm_type_get_bitwidth;
	type->interface.IsIntegerTy = llvm_type_is_integer;
	type->interface.IsFloatingPointTy = llvm_type_is_floating_point;
	type->interface.IsPointerTy = llvm_type_is_pointer;
	type->interface.IsVoidTy = llvm_type_is_void;
	type->interface.GetElementType = llvm_type_get_element_type;

	return type;
}

static value_type_t llvm_type_get_kind(IType *self)
{
	LLVMType *type = (LLVMType*)self;
	Type *t = type->llvm_type;

	if (t->isVoidTy())
		return VALUE_TYPE_VOID;
	else if (t->isIntegerTy(1))
		return VALUE_TYPE_INT1;
	else if (t->isIntegerTy(8))
		return VALUE_TYPE_INT8;
	else if (t->isIntegerTy(16))
		return VALUE_TYPE_INT16;
	else if (t->isIntegerTy(32))
		return VALUE_TYPE_INT32;
	else if (t->isIntegerTy(64))
		return VALUE_TYPE_INT64;
	else if (t->isIntegerTy(128))
		return VALUE_TYPE_INT128;
	else if (t->isFloatTy())
		return VALUE_TYPE_FLOAT;
	else if (t->isDoubleTy())
		return VALUE_TYPE_DOUBLE;
	else if (t->isX86_FP80Ty())
		return VALUE_TYPE_FP80;
	else if (t->isFP128Ty())
		return VALUE_TYPE_FP128;
	else if (t->isPointerTy())
		return VALUE_TYPE_POINTER;
	else if (t->isFunctionTy())
		return VALUE_TYPE_FUNCTION;
	else if (t->isStructTy())
		return VALUE_TYPE_STRUCT;

	return VALUE_TYPE_VOID;
}

static uint32_t llvm_type_get_bitwidth(IType *self)
{
	LLVMType *type = (LLVMType*)self;
	if (type->llvm_type->isIntegerTy())
		return type->llvm_type->getIntegerBitWidth();
	return 0;
}

static int llvm_type_is_integer(IType *self)
{
	LLVMType *type = (LLVMType*)self;
	return type->llvm_type->isIntegerTy() ? 1 : 0;
}

static int llvm_type_is_floating_point(IType *self)
{
	LLVMType *type = (LLVMType*)self;
	return type->llvm_type->isFloatingPointTy() ? 1 : 0;
}

static int llvm_type_is_pointer(IType *self)
{
	LLVMType *type = (LLVMType*)self;
	return type->llvm_type->isPointerTy() ? 1 : 0;
}

static int llvm_type_is_void(IType *self)
{
	LLVMType *type = (LLVMType*)self;
	return type->llvm_type->isVoidTy() ? 1 : 0;
}

static IType* llvm_type_get_element_type(IType *self)
{
	LLVMType *type = (LLVMType*)self;
	if (type->llvm_type->isPointerTy()) {
		PointerType *ptr = cast<PointerType>(type->llvm_type);
		return (IType*)llvm_type_create(ptr->getElementType(), type->module);
	}
	return NULL;
}

/***************************************************************************
 * LLVM Value Wrapper
 ***************************************************************************/
typedef struct LLVMValue {
	IValue interface;
	uint32_t refcount;
	Value *llvm_value;
	struct LLVMModule *module;
} LLVMValue;

static IType* llvm_value_get_type(IValue *self);
static const char* llvm_value_get_name(IValue *self);
static void llvm_value_set_name(IValue *self, const char *name);
static int llvm_value_is_constant(IValue *self);

static uint32_t llvm_value_addref(void *self)
{
	LLVMValue *val = (LLVMValue*)self;
	return ++val->refcount;
}

static uint32_t llvm_value_release(void *self)
{
	LLVMValue *val = (LLVMValue*)self;
	uint32_t count = --val->refcount;
	if (count == 0) {
		delete val;
	}
	return count;
}

static int llvm_value_query_interface(void *self, const char *iid, void **out)
{
	if (out == NULL)
		return -1;
	*out = self;
	llvm_value_addref(self);
	return 0;
}

static LLVMValue* llvm_value_create(Value *llvm_value, struct LLVMModule *module)
{
	if (llvm_value == NULL)
		return NULL;

	LLVMValue *val = new LLVMValue();
	val->refcount = 1;
	val->llvm_value = llvm_value;
	val->module = module;

	/* Setup interface */
	val->interface.base.AddRef = llvm_value_addref;
	val->interface.base.Release = llvm_value_release;
	val->interface.base.QueryInterface = llvm_value_query_interface;
	val->interface.GetType = llvm_value_get_type;
	val->interface.GetName = llvm_value_get_name;
	val->interface.SetName = llvm_value_set_name;
	val->interface.IsConstant = llvm_value_is_constant;

	return val;
}

static IType* llvm_value_get_type(IValue *self)
{
	LLVMValue *val = (LLVMValue*)self;
	return (IType*)llvm_type_create(val->llvm_value->getType(), val->module);
}

static const char* llvm_value_get_name(IValue *self)
{
	LLVMValue *val = (LLVMValue*)self;
	static std::string name_storage;
	name_storage = val->llvm_value->getName().str();
	return name_storage.c_str();
}

static void llvm_value_set_name(IValue *self, const char *name)
{
	LLVMValue *val = (LLVMValue*)self;
	val->llvm_value->setName(name ? name : "");
}

static int llvm_value_is_constant(IValue *self)
{
	LLVMValue *val = (LLVMValue*)self;
	return isa<Constant>(val->llvm_value) ? 1 : 0;
}

/***************************************************************************
 * LLVM BasicBlock Wrapper
 ***************************************************************************/
typedef struct LLVMBasicBlock {
	IBasicBlock interface;
	uint32_t refcount;
	BasicBlock *llvm_bb;
	struct LLVMModule *module;
} LLVMBasicBlock;

static IFunction* llvm_bb_get_parent(IBasicBlock *self);
static const char* llvm_bb_get_name(IBasicBlock *self);
static int llvm_bb_has_terminator(IBasicBlock *self);

static uint32_t llvm_bb_addref(void *self)
{
	LLVMBasicBlock *bb = (LLVMBasicBlock*)self;
	return ++bb->refcount;
}

static uint32_t llvm_bb_release(void *self)
{
	LLVMBasicBlock *bb = (LLVMBasicBlock*)self;
	uint32_t count = --bb->refcount;
	if (count == 0) {
		delete bb;
	}
	return count;
}

static int llvm_bb_query_interface(void *self, const char *iid, void **out)
{
	if (out == NULL)
		return -1;
	*out = self;
	llvm_bb_addref(self);
	return 0;
}

static LLVMBasicBlock* llvm_bb_create(BasicBlock *llvm_bb, struct LLVMModule *module);

static IFunction* llvm_bb_get_parent(IBasicBlock *self)
{
	/* Forward declaration issue - implement later */
	return NULL;
}

static const char* llvm_bb_get_name(IBasicBlock *self)
{
	LLVMBasicBlock *bb = (LLVMBasicBlock*)self;
	static std::string name_storage;
	name_storage = bb->llvm_bb->getName().str();
	return name_storage.c_str();
}

static int llvm_bb_has_terminator(IBasicBlock *self)
{
	LLVMBasicBlock *bb = (LLVMBasicBlock*)self;
	return bb->llvm_bb->getTerminator() != NULL ? 1 : 0;
}

static LLVMBasicBlock* llvm_bb_create(BasicBlock *llvm_bb, struct LLVMModule *module)
{
	if (llvm_bb == NULL)
		return NULL;

	LLVMBasicBlock *bb = new LLVMBasicBlock();
	bb->refcount = 1;
	bb->llvm_bb = llvm_bb;
	bb->module = module;

	/* Setup interface */
	bb->interface.base.AddRef = llvm_bb_addref;
	bb->interface.base.Release = llvm_bb_release;
	bb->interface.base.QueryInterface = llvm_bb_query_interface;
	bb->interface.GetParent = llvm_bb_get_parent;
	bb->interface.GetName = llvm_bb_get_name;
	bb->interface.HasTerminator = llvm_bb_has_terminator;

	return bb;
}

/***************************************************************************
 * LLVM Builder Wrapper
 ***************************************************************************/
typedef struct LLVMBuilder {
	IBuilder interface;
	uint32_t refcount;
	IRBuilder<> *llvm_builder;
	struct LLVMModule *module;
} LLVMBuilder;

/* Forward declarations for all builder methods */
static void llvm_builder_set_insert_point(IBuilder *self, IBasicBlock *bb);
static IBasicBlock* llvm_builder_get_insert_block(IBuilder *self);
static IValue* llvm_builder_create_const_int(IBuilder *self, IType *type, uint64_t val, int is_signed);
static IValue* llvm_builder_create_const_int1(IBuilder *self, int val);
static IValue* llvm_builder_create_const_int8(IBuilder *self, uint8_t val);
static IValue* llvm_builder_create_const_int16(IBuilder *self, uint16_t val);
static IValue* llvm_builder_create_const_int32(IBuilder *self, uint32_t val);
static IValue* llvm_builder_create_const_int64(IBuilder *self, uint64_t val);
static IValue* llvm_builder_create_const_float(IBuilder *self, float val);
static IValue* llvm_builder_create_const_double(IBuilder *self, double val);
static IValue* llvm_builder_create_add(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
static IValue* llvm_builder_create_sub(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
static IValue* llvm_builder_create_mul(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
static IValue* llvm_builder_create_udiv(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
static IValue* llvm_builder_create_sdiv(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
static IValue* llvm_builder_create_urem(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
static IValue* llvm_builder_create_srem(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
static IValue* llvm_builder_create_neg(IBuilder *self, IValue *val, const char *name);
static IValue* llvm_builder_create_and(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
static IValue* llvm_builder_create_or(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
static IValue* llvm_builder_create_xor(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
static IValue* llvm_builder_create_not(IBuilder *self, IValue *val, const char *name);
static IValue* llvm_builder_create_shl(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
static IValue* llvm_builder_create_lshr(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
static IValue* llvm_builder_create_ashr(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
static IValue* llvm_builder_create_fadd(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
static IValue* llvm_builder_create_fsub(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
static IValue* llvm_builder_create_fmul(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
static IValue* llvm_builder_create_fdiv(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
static IValue* llvm_builder_create_fneg(IBuilder *self, IValue *val, const char *name);
static IValue* llvm_builder_create_icmp(IBuilder *self, icmp_predicate_t pred, IValue *lhs, IValue *rhs, const char *name);
static IValue* llvm_builder_create_fcmp(IBuilder *self, fcmp_predicate_t pred, IValue *lhs, IValue *rhs, const char *name);
static IValue* llvm_builder_create_load(IBuilder *self, IType *type, IValue *ptr, const char *name);
static IValue* llvm_builder_create_store(IBuilder *self, IValue *val, IValue *ptr);
static IValue* llvm_builder_create_gep(IBuilder *self, IType *type, IValue *ptr, IValue **indices, size_t num_indices, const char *name);
static IValue* llvm_builder_create_inbounds_gep(IBuilder *self, IType *type, IValue *ptr, IValue **indices, size_t num_indices, const char *name);
static IValue* llvm_builder_create_trunc(IBuilder *self, IValue *val, IType *dest_type, const char *name);
static IValue* llvm_builder_create_zext(IBuilder *self, IValue *val, IType *dest_type, const char *name);
static IValue* llvm_builder_create_sext(IBuilder *self, IValue *val, IType *dest_type, const char *name);
static IValue* llvm_builder_create_fptrunc(IBuilder *self, IValue *val, IType *dest_type, const char *name);
static IValue* llvm_builder_create_fpext(IBuilder *self, IValue *val, IType *dest_type, const char *name);
static IValue* llvm_builder_create_fptoui(IBuilder *self, IValue *val, IType *dest_type, const char *name);
static IValue* llvm_builder_create_fptosi(IBuilder *self, IValue *val, IType *dest_type, const char *name);
static IValue* llvm_builder_create_uitofp(IBuilder *self, IValue *val, IType *dest_type, const char *name);
static IValue* llvm_builder_create_sitofp(IBuilder *self, IValue *val, IType *dest_type, const char *name);
static IValue* llvm_builder_create_ptrtoint(IBuilder *self, IValue *val, IType *dest_type, const char *name);
static IValue* llvm_builder_create_inttoptr(IBuilder *self, IValue *val, IType *dest_type, const char *name);
static IValue* llvm_builder_create_bitcast(IBuilder *self, IValue *val, IType *dest_type, const char *name);
static IValue* llvm_builder_create_br(IBuilder *self, IBasicBlock *dest);
static IValue* llvm_builder_create_condbr(IBuilder *self, IValue *cond, IBasicBlock *true_bb, IBasicBlock *false_bb);
static IValue* llvm_builder_create_switch(IBuilder *self, IValue *val, IBasicBlock *default_bb, uint32_t num_cases);
static void llvm_builder_add_switch_case(IBuilder *self, IValue *switch_inst, uint64_t case_val, IBasicBlock *dest);
static IValue* llvm_builder_create_ret(IBuilder *self, IValue *val);
static IValue* llvm_builder_create_ret_void(IBuilder *self);
static IValue* llvm_builder_create_call(IBuilder *self, IFunction *func, IValue **args, size_t num_args, const char *name);
static IValue* llvm_builder_create_phi(IBuilder *self, IType *type, uint32_t num_reserved, const char *name);
static void llvm_builder_add_phi_incoming(IBuilder *self, IValue *phi, IValue *val, IBasicBlock *bb);
static IValue* llvm_builder_create_select(IBuilder *self, IValue *cond, IValue *true_val, IValue *false_val, const char *name);

static uint32_t llvm_builder_addref(void *self)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	return ++builder->refcount;
}

static uint32_t llvm_builder_release(void *self)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	uint32_t count = --builder->refcount;
	if (count == 0) {
		delete builder->llvm_builder;
		delete builder;
	}
	return count;
}

static int llvm_builder_query_interface(void *self, const char *iid, void **out)
{
	if (out == NULL)
		return -1;
	*out = self;
	llvm_builder_addref(self);
	return 0;
}

static LLVMBuilder* llvm_builder_create(LLVMContext &context, struct LLVMModule *module)
{
	LLVMBuilder *builder = new LLVMBuilder();
	builder->refcount = 1;
	builder->llvm_builder = new IRBuilder<>(context);
	builder->module = module;

	/* Setup interface */
	builder->interface.base.AddRef = llvm_builder_addref;
	builder->interface.base.Release = llvm_builder_release;
	builder->interface.base.QueryInterface = llvm_builder_query_interface;
	builder->interface.SetInsertPoint = llvm_builder_set_insert_point;
	builder->interface.GetInsertBlock = llvm_builder_get_insert_block;
	builder->interface.CreateConstInt = llvm_builder_create_const_int;
	builder->interface.CreateConstInt1 = llvm_builder_create_const_int1;
	builder->interface.CreateConstInt8 = llvm_builder_create_const_int8;
	builder->interface.CreateConstInt16 = llvm_builder_create_const_int16;
	builder->interface.CreateConstInt32 = llvm_builder_create_const_int32;
	builder->interface.CreateConstInt64 = llvm_builder_create_const_int64;
	builder->interface.CreateConstFloat = llvm_builder_create_const_float;
	builder->interface.CreateConstDouble = llvm_builder_create_const_double;
	builder->interface.CreateAdd = llvm_builder_create_add;
	builder->interface.CreateSub = llvm_builder_create_sub;
	builder->interface.CreateMul = llvm_builder_create_mul;
	builder->interface.CreateUDiv = llvm_builder_create_udiv;
	builder->interface.CreateSDiv = llvm_builder_create_sdiv;
	builder->interface.CreateURem = llvm_builder_create_urem;
	builder->interface.CreateSRem = llvm_builder_create_srem;
	builder->interface.CreateNeg = llvm_builder_create_neg;
	builder->interface.CreateAnd = llvm_builder_create_and;
	builder->interface.CreateOr = llvm_builder_create_or;
	builder->interface.CreateXor = llvm_builder_create_xor;
	builder->interface.CreateNot = llvm_builder_create_not;
	builder->interface.CreateShl = llvm_builder_create_shl;
	builder->interface.CreateLShr = llvm_builder_create_lshr;
	builder->interface.CreateAShr = llvm_builder_create_ashr;
	builder->interface.CreateFAdd = llvm_builder_create_fadd;
	builder->interface.CreateFSub = llvm_builder_create_fsub;
	builder->interface.CreateFMul = llvm_builder_create_fmul;
	builder->interface.CreateFDiv = llvm_builder_create_fdiv;
	builder->interface.CreateFNeg = llvm_builder_create_fneg;
	builder->interface.CreateICmp = llvm_builder_create_icmp;
	builder->interface.CreateFCmp = llvm_builder_create_fcmp;
	builder->interface.CreateLoad = llvm_builder_create_load;
	builder->interface.CreateStore = llvm_builder_create_store;
	builder->interface.CreateGEP = llvm_builder_create_gep;
	builder->interface.CreateInBoundsGEP = llvm_builder_create_inbounds_gep;
	builder->interface.CreateTrunc = llvm_builder_create_trunc;
	builder->interface.CreateZExt = llvm_builder_create_zext;
	builder->interface.CreateSExt = llvm_builder_create_sext;
	builder->interface.CreateFPTrunc = llvm_builder_create_fptrunc;
	builder->interface.CreateFPExt = llvm_builder_create_fpext;
	builder->interface.CreateFPToUI = llvm_builder_create_fptoui;
	builder->interface.CreateFPToSI = llvm_builder_create_fptosi;
	builder->interface.CreateUIToFP = llvm_builder_create_uitofp;
	builder->interface.CreateSIToFP = llvm_builder_create_sitofp;
	builder->interface.CreatePtrToInt = llvm_builder_create_ptrtoint;
	builder->interface.CreateIntToPtr = llvm_builder_create_inttoptr;
	builder->interface.CreateBitCast = llvm_builder_create_bitcast;
	builder->interface.CreateBr = llvm_builder_create_br;
	builder->interface.CreateCondBr = llvm_builder_create_condbr;
	builder->interface.CreateSwitch = llvm_builder_create_switch;
	builder->interface.AddSwitchCase = llvm_builder_add_switch_case;
	builder->interface.CreateRet = llvm_builder_create_ret;
	builder->interface.CreateRetVoid = llvm_builder_create_ret_void;
	builder->interface.CreateCall = llvm_builder_create_call;
	builder->interface.CreatePHI = llvm_builder_create_phi;
	builder->interface.AddPHIIncoming = llvm_builder_add_phi_incoming;
	builder->interface.CreateSelect = llvm_builder_create_select;

	return builder;
}

/* Builder method implementations */
#define GET_LLVM_VALUE(v) (((LLVMValue*)(v))->llvm_value)
#define GET_LLVM_TYPE(t) (((LLVMType*)(t))->llvm_type)
#define GET_LLVM_BB(bb) (((LLVMBasicBlock*)(bb))->llvm_bb)

static void llvm_builder_set_insert_point(IBuilder *self, IBasicBlock *bb)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	builder->llvm_builder->SetInsertPoint(GET_LLVM_BB(bb));
}

static IBasicBlock* llvm_builder_get_insert_block(IBuilder *self)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	return (IBasicBlock*)llvm_bb_create(builder->llvm_builder->GetInsertBlock(), builder->module);
}

static IValue* llvm_builder_create_const_int(IBuilder *self, IType *type, uint64_t val, int is_signed)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = ConstantInt::get(GET_LLVM_TYPE(type), val, is_signed != 0);
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_const_int1(IBuilder *self, int val)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->getInt1(val != 0);
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_const_int8(IBuilder *self, uint8_t val)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->getInt8(val);
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_const_int16(IBuilder *self, uint16_t val)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->getInt16(val);
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_const_int32(IBuilder *self, uint32_t val)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->getInt32(val);
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_const_int64(IBuilder *self, uint64_t val)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->getInt64(val);
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_const_float(IBuilder *self, float val)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = ConstantFP::get(builder->llvm_builder->getFloatTy(), val);
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_const_double(IBuilder *self, double val)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = ConstantFP::get(builder->llvm_builder->getDoubleTy(), val);
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_add(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateAdd(GET_LLVM_VALUE(lhs), GET_LLVM_VALUE(rhs), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_sub(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateSub(GET_LLVM_VALUE(lhs), GET_LLVM_VALUE(rhs), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_mul(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateMul(GET_LLVM_VALUE(lhs), GET_LLVM_VALUE(rhs), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_udiv(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateUDiv(GET_LLVM_VALUE(lhs), GET_LLVM_VALUE(rhs), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_sdiv(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateSDiv(GET_LLVM_VALUE(lhs), GET_LLVM_VALUE(rhs), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_urem(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateURem(GET_LLVM_VALUE(lhs), GET_LLVM_VALUE(rhs), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_srem(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateSRem(GET_LLVM_VALUE(lhs), GET_LLVM_VALUE(rhs), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_neg(IBuilder *self, IValue *val, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateNeg(GET_LLVM_VALUE(val), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_and(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateAnd(GET_LLVM_VALUE(lhs), GET_LLVM_VALUE(rhs), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_or(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateOr(GET_LLVM_VALUE(lhs), GET_LLVM_VALUE(rhs), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_xor(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateXor(GET_LLVM_VALUE(lhs), GET_LLVM_VALUE(rhs), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_not(IBuilder *self, IValue *val, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateNot(GET_LLVM_VALUE(val), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_shl(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateShl(GET_LLVM_VALUE(lhs), GET_LLVM_VALUE(rhs), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_lshr(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateLShr(GET_LLVM_VALUE(lhs), GET_LLVM_VALUE(rhs), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_ashr(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateAShr(GET_LLVM_VALUE(lhs), GET_LLVM_VALUE(rhs), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_fadd(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateFAdd(GET_LLVM_VALUE(lhs), GET_LLVM_VALUE(rhs), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_fsub(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateFSub(GET_LLVM_VALUE(lhs), GET_LLVM_VALUE(rhs), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_fmul(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateFMul(GET_LLVM_VALUE(lhs), GET_LLVM_VALUE(rhs), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_fdiv(IBuilder *self, IValue *lhs, IValue *rhs, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateFDiv(GET_LLVM_VALUE(lhs), GET_LLVM_VALUE(rhs), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_fneg(IBuilder *self, IValue *val, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateFNeg(GET_LLVM_VALUE(val), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static CmpInst::Predicate convert_icmp_predicate(icmp_predicate_t pred)
{
	switch (pred) {
	case ICMP_EQ: return CmpInst::ICMP_EQ;
	case ICMP_NE: return CmpInst::ICMP_NE;
	case ICMP_UGT: return CmpInst::ICMP_UGT;
	case ICMP_UGE: return CmpInst::ICMP_UGE;
	case ICMP_ULT: return CmpInst::ICMP_ULT;
	case ICMP_ULE: return CmpInst::ICMP_ULE;
	case ICMP_SGT: return CmpInst::ICMP_SGT;
	case ICMP_SGE: return CmpInst::ICMP_SGE;
	case ICMP_SLT: return CmpInst::ICMP_SLT;
	case ICMP_SLE: return CmpInst::ICMP_SLE;
	default: return CmpInst::ICMP_EQ;
	}
}

static CmpInst::Predicate convert_fcmp_predicate(fcmp_predicate_t pred)
{
	switch (pred) {
	case FCMP_FALSE: return CmpInst::FCMP_FALSE;
	case FCMP_OEQ: return CmpInst::FCMP_OEQ;
	case FCMP_OGT: return CmpInst::FCMP_OGT;
	case FCMP_OGE: return CmpInst::FCMP_OGE;
	case FCMP_OLT: return CmpInst::FCMP_OLT;
	case FCMP_OLE: return CmpInst::FCMP_OLE;
	case FCMP_ONE: return CmpInst::FCMP_ONE;
	case FCMP_ORD: return CmpInst::FCMP_ORD;
	case FCMP_UNO: return CmpInst::FCMP_UNO;
	case FCMP_UEQ: return CmpInst::FCMP_UEQ;
	case FCMP_UGT: return CmpInst::FCMP_UGT;
	case FCMP_UGE: return CmpInst::FCMP_UGE;
	case FCMP_ULT: return CmpInst::FCMP_ULT;
	case FCMP_ULE: return CmpInst::FCMP_ULE;
	case FCMP_UNE: return CmpInst::FCMP_UNE;
	case FCMP_TRUE: return CmpInst::FCMP_TRUE;
	default: return CmpInst::FCMP_FALSE;
	}
}

static IValue* llvm_builder_create_icmp(IBuilder *self, icmp_predicate_t pred, IValue *lhs, IValue *rhs, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateICmp(convert_icmp_predicate(pred), GET_LLVM_VALUE(lhs), GET_LLVM_VALUE(rhs), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_fcmp(IBuilder *self, fcmp_predicate_t pred, IValue *lhs, IValue *rhs, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateFCmp(convert_fcmp_predicate(pred), GET_LLVM_VALUE(lhs), GET_LLVM_VALUE(rhs), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_load(IBuilder *self, IType *type, IValue *ptr, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateLoad(GET_LLVM_TYPE(type), GET_LLVM_VALUE(ptr), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_store(IBuilder *self, IValue *val, IValue *ptr)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateStore(GET_LLVM_VALUE(val), GET_LLVM_VALUE(ptr));
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_gep(IBuilder *self, IType *type, IValue *ptr, IValue **indices, size_t num_indices, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	std::vector<Value*> idx_vec;
	for (size_t i = 0; i < num_indices; i++)
		idx_vec.push_back(GET_LLVM_VALUE(indices[i]));
	Value *v = builder->llvm_builder->CreateGEP(GET_LLVM_TYPE(type), GET_LLVM_VALUE(ptr), idx_vec, name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_inbounds_gep(IBuilder *self, IType *type, IValue *ptr, IValue **indices, size_t num_indices, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	std::vector<Value*> idx_vec;
	for (size_t i = 0; i < num_indices; i++)
		idx_vec.push_back(GET_LLVM_VALUE(indices[i]));
	Value *v = builder->llvm_builder->CreateInBoundsGEP(GET_LLVM_TYPE(type), GET_LLVM_VALUE(ptr), idx_vec, name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_trunc(IBuilder *self, IValue *val, IType *dest_type, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateTrunc(GET_LLVM_VALUE(val), GET_LLVM_TYPE(dest_type), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_zext(IBuilder *self, IValue *val, IType *dest_type, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateZExt(GET_LLVM_VALUE(val), GET_LLVM_TYPE(dest_type), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_sext(IBuilder *self, IValue *val, IType *dest_type, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateSExt(GET_LLVM_VALUE(val), GET_LLVM_TYPE(dest_type), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_fptrunc(IBuilder *self, IValue *val, IType *dest_type, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateFPTrunc(GET_LLVM_VALUE(val), GET_LLVM_TYPE(dest_type), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_fpext(IBuilder *self, IValue *val, IType *dest_type, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateFPExt(GET_LLVM_VALUE(val), GET_LLVM_TYPE(dest_type), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_fptoui(IBuilder *self, IValue *val, IType *dest_type, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateFPToUI(GET_LLVM_VALUE(val), GET_LLVM_TYPE(dest_type), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_fptosi(IBuilder *self, IValue *val, IType *dest_type, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateFPToSI(GET_LLVM_VALUE(val), GET_LLVM_TYPE(dest_type), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_uitofp(IBuilder *self, IValue *val, IType *dest_type, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateUIToFP(GET_LLVM_VALUE(val), GET_LLVM_TYPE(dest_type), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_sitofp(IBuilder *self, IValue *val, IType *dest_type, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateSIToFP(GET_LLVM_VALUE(val), GET_LLVM_TYPE(dest_type), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_ptrtoint(IBuilder *self, IValue *val, IType *dest_type, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreatePtrToInt(GET_LLVM_VALUE(val), GET_LLVM_TYPE(dest_type), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_inttoptr(IBuilder *self, IValue *val, IType *dest_type, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateIntToPtr(GET_LLVM_VALUE(val), GET_LLVM_TYPE(dest_type), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_bitcast(IBuilder *self, IValue *val, IType *dest_type, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateBitCast(GET_LLVM_VALUE(val), GET_LLVM_TYPE(dest_type), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_br(IBuilder *self, IBasicBlock *dest)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateBr(GET_LLVM_BB(dest));
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_condbr(IBuilder *self, IValue *cond, IBasicBlock *true_bb, IBasicBlock *false_bb)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateCondBr(GET_LLVM_VALUE(cond), GET_LLVM_BB(true_bb), GET_LLVM_BB(false_bb));
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_switch(IBuilder *self, IValue *val, IBasicBlock *default_bb, uint32_t num_cases)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateSwitch(GET_LLVM_VALUE(val), GET_LLVM_BB(default_bb), num_cases);
	return (IValue*)llvm_value_create(v, builder->module);
}

static void llvm_builder_add_switch_case(IBuilder *self, IValue *switch_inst, uint64_t case_val, IBasicBlock *dest)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	SwitchInst *si = cast<SwitchInst>(GET_LLVM_VALUE(switch_inst));
	si->addCase(builder->llvm_builder->getInt32(case_val), GET_LLVM_BB(dest));
}

static IValue* llvm_builder_create_ret(IBuilder *self, IValue *val)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateRet(GET_LLVM_VALUE(val));
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_ret_void(IBuilder *self)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateRetVoid();
	return (IValue*)llvm_value_create(v, builder->module);
}

static IValue* llvm_builder_create_call(IBuilder *self, IFunction *func, IValue **args, size_t num_args, const char *name);

static IValue* llvm_builder_create_phi(IBuilder *self, IType *type, uint32_t num_reserved, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreatePHI(GET_LLVM_TYPE(type), num_reserved, name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

static void llvm_builder_add_phi_incoming(IBuilder *self, IValue *phi, IValue *val, IBasicBlock *bb)
{
	PHINode *phi_node = cast<PHINode>(GET_LLVM_VALUE(phi));
	phi_node->addIncoming(GET_LLVM_VALUE(val), GET_LLVM_BB(bb));
}

static IValue* llvm_builder_create_select(IBuilder *self, IValue *cond, IValue *true_val, IValue *false_val, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	Value *v = builder->llvm_builder->CreateSelect(GET_LLVM_VALUE(cond), GET_LLVM_VALUE(true_val), GET_LLVM_VALUE(false_val), name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

/***************************************************************************
 * LLVM Function Wrapper
 ***************************************************************************/
typedef struct LLVMFunction {
	IFunction interface;
	uint32_t refcount;
	Function *llvm_function;
	struct LLVMModule *module;
} LLVMFunction;

static const char* llvm_function_get_name(IFunction *self);
static IType* llvm_function_get_return_type(IFunction *self);
static uint32_t llvm_function_get_num_args(IFunction *self);
static IValue* llvm_function_get_arg(IFunction *self, uint32_t index);
static IBasicBlock* llvm_function_create_basic_block(IFunction *self, const char *name);
static IBasicBlock* llvm_function_get_entry_block(IFunction *self);
static int llvm_function_verify(IFunction *self, char **error_msg);
static void llvm_function_optimize(IFunction *self);
static void llvm_function_dump(IFunction *self);
static char* llvm_function_to_string(IFunction *self);

static uint32_t llvm_function_addref(void *self)
{
	LLVMFunction *func = (LLVMFunction*)self;
	return ++func->refcount;
}

static uint32_t llvm_function_release(void *self)
{
	LLVMFunction *func = (LLVMFunction*)self;
	uint32_t count = --func->refcount;
	if (count == 0) {
		delete func;
	}
	return count;
}

static int llvm_function_query_interface(void *self, const char *iid, void **out)
{
	if (out == NULL)
		return -1;
	*out = self;
	llvm_function_addref(self);
	return 0;
}

static LLVMFunction* llvm_function_create(Function *llvm_function, struct LLVMModule *module)
{
	if (llvm_function == NULL)
		return NULL;

	LLVMFunction *func = new LLVMFunction();
	func->refcount = 1;
	func->llvm_function = llvm_function;
	func->module = module;

	/* Setup interface */
	func->interface.base.AddRef = llvm_function_addref;
	func->interface.base.Release = llvm_function_release;
	func->interface.base.QueryInterface = llvm_function_query_interface;
	func->interface.GetName = llvm_function_get_name;
	func->interface.GetReturnType = llvm_function_get_return_type;
	func->interface.GetNumArgs = llvm_function_get_num_args;
	func->interface.GetArg = llvm_function_get_arg;
	func->interface.CreateBasicBlock = llvm_function_create_basic_block;
	func->interface.GetEntryBlock = llvm_function_get_entry_block;
	func->interface.Verify = llvm_function_verify;
	func->interface.Optimize = llvm_function_optimize;
	func->interface.Dump = llvm_function_dump;
	func->interface.ToString = llvm_function_to_string;

	return func;
}

#define GET_LLVM_FUNCTION(f) (((LLVMFunction*)(f))->llvm_function)

static const char* llvm_function_get_name(IFunction *self)
{
	LLVMFunction *func = (LLVMFunction*)self;
	static std::string name_storage;
	name_storage = func->llvm_function->getName().str();
	return name_storage.c_str();
}

static IType* llvm_function_get_return_type(IFunction *self)
{
	LLVMFunction *func = (LLVMFunction*)self;
	return (IType*)llvm_type_create(func->llvm_function->getReturnType(), func->module);
}

static uint32_t llvm_function_get_num_args(IFunction *self)
{
	LLVMFunction *func = (LLVMFunction*)self;
	return func->llvm_function->arg_size();
}

static IValue* llvm_function_get_arg(IFunction *self, uint32_t index)
{
	LLVMFunction *func = (LLVMFunction*)self;
	return (IValue*)llvm_value_create(func->llvm_function->getArg(index), func->module);
}

static IBasicBlock* llvm_function_create_basic_block(IFunction *self, const char *name)
{
	LLVMFunction *func = (LLVMFunction*)self;
	LLVMModule *mod = func->module;
	BasicBlock *bb = BasicBlock::Create(*(LLVMContext*)mod->llvm_context, name ? name : "", func->llvm_function);
	return (IBasicBlock*)llvm_bb_create(bb, mod);
}

static IBasicBlock* llvm_function_get_entry_block(IFunction *self)
{
	LLVMFunction *func = (LLVMFunction*)self;
	return (IBasicBlock*)llvm_bb_create(&func->llvm_function->getEntryBlock(), func->module);
}

static int llvm_function_verify(IFunction *self, char **error_msg)
{
	LLVMFunction *func = (LLVMFunction*)self;
	std::string error_str;
	raw_string_ostream error_stream(error_str);
	bool failed = verifyFunction(*func->llvm_function, &error_stream);
	if (failed && error_msg) {
		error_stream.flush();
		*error_msg = strdup(error_str.c_str());
	}
	return failed ? -1 : 0;
}

static void llvm_function_optimize(IFunction *self)
{
	/* Optimization is done at module level in LLVM backend */
}

static void llvm_function_dump(IFunction *self)
{
	LLVMFunction *func = (LLVMFunction*)self;
	func->llvm_function->print(errs());
}

static char* llvm_function_to_string(IFunction *self)
{
	LLVMFunction *func = (LLVMFunction*)self;
	std::string str;
	raw_string_ostream stream(str);
	func->llvm_function->print(stream);
	stream.flush();
	return strdup(str.c_str());
}

/* Now implement builder call */
static IValue* llvm_builder_create_call(IBuilder *self, IFunction *func, IValue **args, size_t num_args, const char *name)
{
	LLVMBuilder *builder = (LLVMBuilder*)self;
	std::vector<Value*> arg_vec;
	for (size_t i = 0; i < num_args; i++)
		arg_vec.push_back(GET_LLVM_VALUE(args[i]));
	Value *v = builder->llvm_builder->CreateCall(GET_LLVM_FUNCTION(func), arg_vec, name ? name : "");
	return (IValue*)llvm_value_create(v, builder->module);
}

/***************************************************************************
 * LLVM Module Wrapper
 ***************************************************************************/
typedef struct LLVMModule {
	IModule interface;
	uint32_t refcount;
	LLVMContext *llvm_context;
	Module *llvm_module;
	ExecutionEngine *exec_engine;
	uint32_t opt_level;
	int initialized;
} LLVMModule;

static const char* llvm_module_get_name(IModule *self);
static void llvm_module_set_data_layout(IModule *self, const char *layout);
static const char* llvm_module_get_data_layout(IModule *self);
static IType* llvm_module_get_void_type(IModule *self);
static IType* llvm_module_get_int1_type(IModule *self);
static IType* llvm_module_get_int8_type(IModule *self);
static IType* llvm_module_get_int16_type(IModule *self);
static IType* llvm_module_get_int32_type(IModule *self);
static IType* llvm_module_get_int64_type(IModule *self);
static IType* llvm_module_get_int128_type(IModule *self);
static IType* llvm_module_get_int_type(IModule *self, uint32_t num_bits);
static IType* llvm_module_get_float_type(IModule *self);
static IType* llvm_module_get_double_type(IModule *self);
static IType* llvm_module_get_fp80_type(IModule *self);
static IType* llvm_module_get_fp128_type(IModule *self);
static IType* llvm_module_get_pointer_type(IModule *self, IType *element_type);
static IType* llvm_module_get_struct_type(IModule *self, IType **element_types, uint32_t num_elements, const char *name);
static IType* llvm_module_get_array_type(IModule *self, IType *element_type, uint32_t num_elements);
static IType* llvm_module_get_function_type(IModule *self, IType *return_type, IType **param_types, uint32_t num_params, int is_vararg);
static IFunction* llvm_module_create_function(IModule *self, const char *name, IType *function_type);
static IFunction* llvm_module_get_function(IModule *self, const char *name);
static IValue* llvm_module_create_global_variable(IModule *self, IType *type, const char *name, int is_constant);
static IValue* llvm_module_get_global_variable(IModule *self, const char *name);
static IBuilder* llvm_module_create_builder(IModule *self);
static int llvm_module_compile(IModule *self);
static void* llvm_module_get_function_address(IModule *self, const char *name);
static void llvm_module_dump(IModule *self);
static char* llvm_module_to_string(IModule *self);

static uint32_t llvm_module_addref(void *self)
{
	LLVMModule *mod = (LLVMModule*)self;
	return ++mod->refcount;
}

static uint32_t llvm_module_release(void *self)
{
	LLVMModule *mod = (LLVMModule*)self;
	uint32_t count = --mod->refcount;
	if (count == 0) {
		if (mod->exec_engine)
			delete mod->exec_engine;
		/* Module is owned by execution engine, don't delete */
		delete mod->llvm_context;
		delete mod;
	}
	return count;
}

static int llvm_module_query_interface(void *self, const char *iid, void **out)
{
	if (out == NULL)
		return -1;
	*out = self;
	llvm_module_addref(self);
	return 0;
}

static LLVMModule* llvm_module_create_internal(const char *name)
{
	LLVMModule *mod = new LLVMModule();
	mod->refcount = 1;
	mod->llvm_context = new LLVMContext();
	mod->llvm_module = new Module(name, *mod->llvm_context);
	mod->exec_engine = NULL;
	mod->opt_level = 2;
	mod->initialized = 0;

	/* Setup interface */
	mod->interface.base.AddRef = llvm_module_addref;
	mod->interface.base.Release = llvm_module_release;
	mod->interface.base.QueryInterface = llvm_module_query_interface;
	mod->interface.GetName = llvm_module_get_name;
	mod->interface.SetDataLayout = llvm_module_set_data_layout;
	mod->interface.GetDataLayout = llvm_module_get_data_layout;
	mod->interface.GetVoidType = llvm_module_get_void_type;
	mod->interface.GetInt1Type = llvm_module_get_int1_type;
	mod->interface.GetInt8Type = llvm_module_get_int8_type;
	mod->interface.GetInt16Type = llvm_module_get_int16_type;
	mod->interface.GetInt32Type = llvm_module_get_int32_type;
	mod->interface.GetInt64Type = llvm_module_get_int64_type;
	mod->interface.GetInt128Type = llvm_module_get_int128_type;
	mod->interface.GetIntType = llvm_module_get_int_type;
	mod->interface.GetFloatType = llvm_module_get_float_type;
	mod->interface.GetDoubleType = llvm_module_get_double_type;
	mod->interface.GetFP80Type = llvm_module_get_fp80_type;
	mod->interface.GetFP128Type = llvm_module_get_fp128_type;
	mod->interface.GetPointerType = llvm_module_get_pointer_type;
	mod->interface.GetStructType = llvm_module_get_struct_type;
	mod->interface.GetArrayType = llvm_module_get_array_type;
	mod->interface.GetFunctionType = llvm_module_get_function_type;
	mod->interface.CreateFunction = llvm_module_create_function;
	mod->interface.GetFunction = llvm_module_get_function;
	mod->interface.CreateGlobalVariable = llvm_module_create_global_variable;
	mod->interface.GetGlobalVariable = llvm_module_get_global_variable;
	mod->interface.CreateBuilder = llvm_module_create_builder;
	mod->interface.Compile = llvm_module_compile;
	mod->interface.GetFunctionAddress = llvm_module_get_function_address;
	mod->interface.Dump = llvm_module_dump;
	mod->interface.ToString = llvm_module_to_string;

	return mod;
}

static const char* llvm_module_get_name(IModule *self)
{
	LLVMModule *mod = (LLVMModule*)self;
	static std::string name_storage;
	name_storage = mod->llvm_module->getName().str();
	return name_storage.c_str();
}

static void llvm_module_set_data_layout(IModule *self, const char *layout)
{
	LLVMModule *mod = (LLVMModule*)self;
	mod->llvm_module->setDataLayout(layout);
}

static const char* llvm_module_get_data_layout(IModule *self)
{
	LLVMModule *mod = (LLVMModule*)self;
	static std::string layout_storage;
	layout_storage = mod->llvm_module->getDataLayoutStr();
	return layout_storage.c_str();
}

static IType* llvm_module_get_void_type(IModule *self)
{
	LLVMModule *mod = (LLVMModule*)self;
	return (IType*)llvm_type_create(Type::getVoidTy(*mod->llvm_context), mod);
}

static IType* llvm_module_get_int1_type(IModule *self)
{
	LLVMModule *mod = (LLVMModule*)self;
	return (IType*)llvm_type_create(Type::getInt1Ty(*mod->llvm_context), mod);
}

static IType* llvm_module_get_int8_type(IModule *self)
{
	LLVMModule *mod = (LLVMModule*)self;
	return (IType*)llvm_type_create(Type::getInt8Ty(*mod->llvm_context), mod);
}

static IType* llvm_module_get_int16_type(IModule *self)
{
	LLVMModule *mod = (LLVMModule*)self;
	return (IType*)llvm_type_create(Type::getInt16Ty(*mod->llvm_context), mod);
}

static IType* llvm_module_get_int32_type(IModule *self)
{
	LLVMModule *mod = (LLVMModule*)self;
	return (IType*)llvm_type_create(Type::getInt32Ty(*mod->llvm_context), mod);
}

static IType* llvm_module_get_int64_type(IModule *self)
{
	LLVMModule *mod = (LLVMModule*)self;
	return (IType*)llvm_type_create(Type::getInt64Ty(*mod->llvm_context), mod);
}

static IType* llvm_module_get_int128_type(IModule *self)
{
	LLVMModule *mod = (LLVMModule*)self;
	return (IType*)llvm_type_create(Type::getInt128Ty(*mod->llvm_context), mod);
}

static IType* llvm_module_get_int_type(IModule *self, uint32_t num_bits)
{
	LLVMModule *mod = (LLVMModule*)self;
	return (IType*)llvm_type_create(Type::getIntNTy(*mod->llvm_context, num_bits), mod);
}

static IType* llvm_module_get_float_type(IModule *self)
{
	LLVMModule *mod = (LLVMModule*)self;
	return (IType*)llvm_type_create(Type::getFloatTy(*mod->llvm_context), mod);
}

static IType* llvm_module_get_double_type(IModule *self)
{
	LLVMModule *mod = (LLVMModule*)self;
	return (IType*)llvm_type_create(Type::getDoubleTy(*mod->llvm_context), mod);
}

static IType* llvm_module_get_fp80_type(IModule *self)
{
	LLVMModule *mod = (LLVMModule*)self;
	return (IType*)llvm_type_create(Type::getX86_FP80Ty(*mod->llvm_context), mod);
}

static IType* llvm_module_get_fp128_type(IModule *self)
{
	LLVMModule *mod = (LLVMModule*)self;
	return (IType*)llvm_type_create(Type::getFP128Ty(*mod->llvm_context), mod);
}

static IType* llvm_module_get_pointer_type(IModule *self, IType *element_type)
{
	LLVMModule *mod = (LLVMModule*)self;
	return (IType*)llvm_type_create(PointerType::get(GET_LLVM_TYPE(element_type), 0), mod);
}

static IType* llvm_module_get_struct_type(IModule *self, IType **element_types, uint32_t num_elements, const char *name)
{
	LLVMModule *mod = (LLVMModule*)self;
	std::vector<Type*> types;
	for (uint32_t i = 0; i < num_elements; i++)
		types.push_back(GET_LLVM_TYPE(element_types[i]));
	Type *struct_type = StructType::create(*mod->llvm_context, types, name ? name : "");
	return (IType*)llvm_type_create(struct_type, mod);
}

static IType* llvm_module_get_array_type(IModule *self, IType *element_type, uint32_t num_elements)
{
	LLVMModule *mod = (LLVMModule*)self;
	return (IType*)llvm_type_create(ArrayType::get(GET_LLVM_TYPE(element_type), num_elements), mod);
}

static IType* llvm_module_get_function_type(IModule *self, IType *return_type, IType **param_types, uint32_t num_params, int is_vararg)
{
	LLVMModule *mod = (LLVMModule*)self;
	std::vector<Type*> params;
	for (uint32_t i = 0; i < num_params; i++)
		params.push_back(GET_LLVM_TYPE(param_types[i]));
	Type *func_type = FunctionType::get(GET_LLVM_TYPE(return_type), params, is_vararg != 0);
	return (IType*)llvm_type_create(func_type, mod);
}

static IFunction* llvm_module_create_function(IModule *self, const char *name, IType *function_type)
{
	LLVMModule *mod = (LLVMModule*)self;
	Function *func = Function::Create(cast<FunctionType>(GET_LLVM_TYPE(function_type)),
	                                   Function::ExternalLinkage, name, mod->llvm_module);
	return (IFunction*)llvm_function_create(func, mod);
}

static IFunction* llvm_module_get_function(IModule *self, const char *name)
{
	LLVMModule *mod = (LLVMModule*)self;
	Function *func = mod->llvm_module->getFunction(name);
	return (IFunction*)llvm_function_create(func, mod);
}

static IValue* llvm_module_create_global_variable(IModule *self, IType *type, const char *name, int is_constant)
{
	LLVMModule *mod = (LLVMModule*)self;
	GlobalVariable *gv = new GlobalVariable(*mod->llvm_module, GET_LLVM_TYPE(type), is_constant != 0,
	                                         GlobalValue::ExternalLinkage, NULL, name);
	return (IValue*)llvm_value_create(gv, mod);
}

static IValue* llvm_module_get_global_variable(IModule *self, const char *name)
{
	LLVMModule *mod = (LLVMModule*)self;
	GlobalVariable *gv = mod->llvm_module->getGlobalVariable(name);
	return (IValue*)llvm_value_create(gv, mod);
}

static IBuilder* llvm_module_create_builder(IModule *self)
{
	LLVMModule *mod = (LLVMModule*)self;
	return (IBuilder*)llvm_builder_create(*mod->llvm_context, mod);
}

static int llvm_module_compile(IModule *self)
{
	LLVMModule *mod = (LLVMModule*)self;

	if (!mod->initialized) {
		/* Create execution engine */
		std::string error_str;
		EngineBuilder builder(std::unique_ptr<Module>(mod->llvm_module));
		builder.setErrorStr(&error_str);
		builder.setEngineKind(EngineKind::JIT);
		mod->exec_engine = builder.create();

		if (!mod->exec_engine) {
			return -1;
		}

		mod->initialized = 1;
	}

	/* Run optimization passes if requested */
	if (mod->opt_level > 0) {
		legacy::FunctionPassManager fpm(mod->llvm_module);
		fpm.add(createPromoteMemoryToRegisterPass());
		fpm.add(createInstructionCombiningPass());
		fpm.add(createCFGSimplificationPass());
		fpm.add(createDeadCodeEliminationPass());

		fpm.doInitialization();
		for (Function &func : *mod->llvm_module) {
			if (!func.isDeclaration())
				fpm.run(func);
		}
		fpm.doFinalization();
	}

	return 0;
}

static void* llvm_module_get_function_address(IModule *self, const char *name)
{
	LLVMModule *mod = (LLVMModule*)self;
	if (!mod->exec_engine)
		return NULL;

	uint64_t addr = mod->exec_engine->getFunctionAddress(name);
	return (void*)addr;
}

static void llvm_module_dump(IModule *self)
{
	LLVMModule *mod = (LLVMModule*)self;
	mod->llvm_module->print(errs(), nullptr);
}

static char* llvm_module_to_string(IModule *self)
{
	LLVMModule *mod = (LLVMModule*)self;
	std::string str;
	raw_string_ostream stream(str);
	mod->llvm_module->print(stream, nullptr);
	stream.flush();
	return strdup(str.c_str());
}

/***************************************************************************
 * LLVM Backend Interface
 ***************************************************************************/
typedef struct LLVMBackend {
	IBackend interface;
	uint32_t refcount;
	uint32_t opt_level;
	int initialized;
} LLVMBackend;

static const char* llvm_backend_get_name(IBackend *self);
static const char* llvm_backend_get_version(IBackend *self);
static backend_type_t llvm_backend_get_type(IBackend *self);
static int llvm_backend_initialize(IBackend *self);
static void llvm_backend_shutdown(IBackend *self);
static IModule* llvm_backend_create_module(IBackend *self, const char *name);
static void llvm_backend_set_opt_level(IBackend *self, uint32_t level);
static uint32_t llvm_backend_get_opt_level(IBackend *self);
static int llvm_backend_supports_feature(IBackend *self, const char *feature);
static const char* llvm_backend_get_target_triple(IBackend *self);
static const char* llvm_backend_get_data_layout(IBackend *self);
static int llvm_backend_supports_float80(IBackend *self);
static int llvm_backend_supports_float128(IBackend *self);

static uint32_t llvm_backend_addref(void *self)
{
	LLVMBackend *backend = (LLVMBackend*)self;
	return ++backend->refcount;
}

static uint32_t llvm_backend_release(void *self)
{
	LLVMBackend *backend = (LLVMBackend*)self;
	uint32_t count = --backend->refcount;
	if (count == 0) {
		llvm_backend_shutdown((IBackend*)backend);
		delete backend;
	}
	return count;
}

static int llvm_backend_query_interface(void *self, const char *iid, void **out)
{
	if (out == NULL)
		return -1;
	*out = self;
	llvm_backend_addref(self);
	return 0;
}

extern "C" IBackend* backend_create_llvm(void)
{
	LLVMBackend *backend = new LLVMBackend();
	backend->refcount = 1;
	backend->opt_level = 2;
	backend->initialized = 0;

	/* Setup interface */
	backend->interface.base.AddRef = llvm_backend_addref;
	backend->interface.base.Release = llvm_backend_release;
	backend->interface.base.QueryInterface = llvm_backend_query_interface;
	backend->interface.GetName = llvm_backend_get_name;
	backend->interface.GetVersion = llvm_backend_get_version;
	backend->interface.GetType = llvm_backend_get_type;
	backend->interface.Initialize = llvm_backend_initialize;
	backend->interface.Shutdown = llvm_backend_shutdown;
	backend->interface.CreateModule = llvm_backend_create_module;
	backend->interface.SetOptimizationLevel = llvm_backend_set_opt_level;
	backend->interface.GetOptimizationLevel = llvm_backend_get_opt_level;
	backend->interface.SupportsFeature = llvm_backend_supports_feature;
	backend->interface.GetTargetTriple = llvm_backend_get_target_triple;
	backend->interface.GetDataLayout = llvm_backend_get_data_layout;
	backend->interface.SupportsFloat80 = llvm_backend_supports_float80;
	backend->interface.SupportsFloat128 = llvm_backend_supports_float128;

	return (IBackend*)backend;
}

static const char* llvm_backend_get_name(IBackend *self)
{
	return "LLVM";
}

static const char* llvm_backend_get_version(IBackend *self)
{
	return LLVM_VERSION_STRING;
}

static backend_type_t llvm_backend_get_type(IBackend *self)
{
	return BACKEND_LLVM;
}

static int llvm_backend_initialize(IBackend *self)
{
	LLVMBackend *backend = (LLVMBackend*)self;
	if (backend->initialized)
		return 0;

	InitializeNativeTarget();
	InitializeNativeTargetAsmPrinter();
	InitializeNativeTargetAsmParser();

	backend->initialized = 1;
	return 0;
}

static void llvm_backend_shutdown(IBackend *self)
{
	LLVMBackend *backend = (LLVMBackend*)self;
	backend->initialized = 0;
}

static IModule* llvm_backend_create_module(IBackend *self, const char *name)
{
	LLVMBackend *backend = (LLVMBackend*)self;
	if (!backend->initialized)
		llvm_backend_initialize(self);

	LLVMModule *mod = llvm_module_create_internal(name);
	mod->opt_level = backend->opt_level;
	return (IModule*)mod;
}

static void llvm_backend_set_opt_level(IBackend *self, uint32_t level)
{
	LLVMBackend *backend = (LLVMBackend*)self;
	backend->opt_level = (level > 3) ? 3 : level;
}

static uint32_t llvm_backend_get_opt_level(IBackend *self)
{
	LLVMBackend *backend = (LLVMBackend*)self;
	return backend->opt_level;
}

static int llvm_backend_supports_feature(IBackend *self, const char *feature)
{
	/* LLVM supports most features */
	return 1;
}

static const char* llvm_backend_get_target_triple(IBackend *self)
{
	static std::string triple;
	triple = llvm::sys::getDefaultTargetTriple();
	return triple.c_str();
}

static const char* llvm_backend_get_data_layout(IBackend *self)
{
	/* Create a temporary module to get data layout */
	LLVMContext context;
	Module mod("temp", context);
	static std::string layout;
	layout = mod.getDataLayoutStr();
	return layout.c_str();
}

static int llvm_backend_supports_float80(IBackend *self)
{
	/* x86-64 supports FP80 */
#if defined(__x86_64__) || defined(_M_X64)
	return 1;
#else
	return 0;
#endif
}

static int llvm_backend_supports_float128(IBackend *self)
{
	/* Most platforms don't have native FP128 */
	return 0;
}
