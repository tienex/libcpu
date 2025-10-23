/*
 * libcpu Backend Abstraction Layer
 * COM-style interfaces for multiple JIT backends
 */

#ifndef __LIBCPU_BACKEND_H__
#define __LIBCPU_BACKEND_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Backend types */
typedef enum {
	BACKEND_LLVM = 0,
	BACKEND_QBE,
	BACKEND_GCCJIT,
	BACKEND_TCG,
	BACKEND_ASMJIT,
	BACKEND_DYNASM,
	BACKEND_SLJIT,
	BACKEND_NANOJIT,
	BACKEND_MIR,
	BACKEND_CRANELIFT,
	BACKEND_LIBJIT,
	BACKEND_NJ,
	BACKEND_MAX
} backend_type_t;

/* Forward declarations */
typedef struct IBackend IBackend;
typedef struct IModule IModule;
typedef struct IFunction IFunction;
typedef struct IBasicBlock IBasicBlock;
typedef struct IBuilder IBuilder;
typedef struct IValue IValue;
typedef struct IType IType;

/* COM-style interface base */
typedef struct IUnknown {
	uint32_t (*AddRef)(void *self);
	uint32_t (*Release)(void *self);
	int (*QueryInterface)(void *self, const char *iid, void **out);
} IUnknown;

/* Value type enumeration */
typedef enum {
	VALUE_TYPE_VOID,
	VALUE_TYPE_INT1,
	VALUE_TYPE_INT8,
	VALUE_TYPE_INT16,
	VALUE_TYPE_INT32,
	VALUE_TYPE_INT64,
	VALUE_TYPE_INT128,
	VALUE_TYPE_FLOAT,
	VALUE_TYPE_DOUBLE,
	VALUE_TYPE_FP80,
	VALUE_TYPE_FP128,
	VALUE_TYPE_POINTER,
	VALUE_TYPE_FUNCTION,
	VALUE_TYPE_STRUCT
} value_type_t;

/* Comparison predicates */
typedef enum {
	ICMP_EQ = 0,  /* Equal */
	ICMP_NE,      /* Not equal */
	ICMP_UGT,     /* Unsigned greater than */
	ICMP_UGE,     /* Unsigned greater or equal */
	ICMP_ULT,     /* Unsigned less than */
	ICMP_ULE,     /* Unsigned less or equal */
	ICMP_SGT,     /* Signed greater than */
	ICMP_SGE,     /* Signed greater or equal */
	ICMP_SLT,     /* Signed less than */
	ICMP_SLE      /* Signed less or equal */
} icmp_predicate_t;

typedef enum {
	FCMP_FALSE = 0, /* Always false */
	FCMP_OEQ,       /* Ordered equal */
	FCMP_OGT,       /* Ordered greater than */
	FCMP_OGE,       /* Ordered greater or equal */
	FCMP_OLT,       /* Ordered less than */
	FCMP_OLE,       /* Ordered less or equal */
	FCMP_ONE,       /* Ordered not equal */
	FCMP_ORD,       /* Ordered (no NaNs) */
	FCMP_UNO,       /* Unordered (has NaNs) */
	FCMP_UEQ,       /* Unordered equal */
	FCMP_UGT,       /* Unordered greater than */
	FCMP_UGE,       /* Unordered greater or equal */
	FCMP_ULT,       /* Unordered less than */
	FCMP_ULE,       /* Unordered less or equal */
	FCMP_UNE,       /* Unordered not equal */
	FCMP_TRUE       /* Always true */
} fcmp_predicate_t;

/***************************************************************************
 * IType Interface - Represents types in the IR
 ***************************************************************************/
typedef struct IType {
	IUnknown base;

	/* Get type kind */
	value_type_t (*GetKind)(IType *self);

	/* Type properties */
	uint32_t (*GetBitWidth)(IType *self);
	int (*IsIntegerTy)(IType *self);
	int (*IsFloatingPointTy)(IType *self);
	int (*IsPointerTy)(IType *self);
	int (*IsVoidTy)(IType *self);

	/* Get element type for pointer types */
	IType* (*GetElementType)(IType *self);
} IType;

/***************************************************************************
 * IValue Interface - Represents values/instructions in the IR
 ***************************************************************************/
typedef struct IValue {
	IUnknown base;

	/* Get value type */
	IType* (*GetType)(IValue *self);

	/* Get value name */
	const char* (*GetName)(IValue *self);
	void (*SetName)(IValue *self, const char *name);

	/* Check if constant */
	int (*IsConstant)(IValue *self);
} IValue;

/***************************************************************************
 * IBasicBlock Interface - Represents a basic block in a function
 ***************************************************************************/
typedef struct IBasicBlock {
	IUnknown base;

	/* Get parent function */
	IFunction* (*GetParent)(IBasicBlock *self);

	/* Get name */
	const char* (*GetName)(IBasicBlock *self);

	/* Terminator operations */
	int (*HasTerminator)(IBasicBlock *self);
} IBasicBlock;

/***************************************************************************
 * IBuilder Interface - IR builder for emitting instructions
 ***************************************************************************/
typedef struct IBuilder {
	IUnknown base;

	/* Position management */
	void (*SetInsertPoint)(IBuilder *self, IBasicBlock *bb);
	IBasicBlock* (*GetInsertBlock)(IBuilder *self);

	/* Integer constants */
	IValue* (*CreateConstInt)(IBuilder *self, IType *type, uint64_t val, int is_signed);
	IValue* (*CreateConstInt1)(IBuilder *self, int val);
	IValue* (*CreateConstInt8)(IBuilder *self, uint8_t val);
	IValue* (*CreateConstInt16)(IBuilder *self, uint16_t val);
	IValue* (*CreateConstInt32)(IBuilder *self, uint32_t val);
	IValue* (*CreateConstInt64)(IBuilder *self, uint64_t val);

	/* Float constants */
	IValue* (*CreateConstFloat)(IBuilder *self, float val);
	IValue* (*CreateConstDouble)(IBuilder *self, double val);

	/* Arithmetic operations */
	IValue* (*CreateAdd)(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
	IValue* (*CreateSub)(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
	IValue* (*CreateMul)(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
	IValue* (*CreateUDiv)(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
	IValue* (*CreateSDiv)(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
	IValue* (*CreateURem)(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
	IValue* (*CreateSRem)(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
	IValue* (*CreateNeg)(IBuilder *self, IValue *val, const char *name);

	/* Bitwise operations */
	IValue* (*CreateAnd)(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
	IValue* (*CreateOr)(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
	IValue* (*CreateXor)(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
	IValue* (*CreateNot)(IBuilder *self, IValue *val, const char *name);
	IValue* (*CreateShl)(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
	IValue* (*CreateLShr)(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
	IValue* (*CreateAShr)(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);

	/* Floating point operations */
	IValue* (*CreateFAdd)(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
	IValue* (*CreateFSub)(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
	IValue* (*CreateFMul)(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
	IValue* (*CreateFDiv)(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
	IValue* (*CreateFNeg)(IBuilder *self, IValue *val, const char *name);

	/* Comparison operations */
	IValue* (*CreateICmp)(IBuilder *self, icmp_predicate_t pred, IValue *lhs, IValue *rhs, const char *name);
	IValue* (*CreateFCmp)(IBuilder *self, fcmp_predicate_t pred, IValue *lhs, IValue *rhs, const char *name);

	/* Memory operations */
	IValue* (*CreateLoad)(IBuilder *self, IType *type, IValue *ptr, const char *name);
	IValue* (*CreateStore)(IBuilder *self, IValue *val, IValue *ptr);
	IValue* (*CreateGEP)(IBuilder *self, IType *type, IValue *ptr, IValue **indices, size_t num_indices, const char *name);
	IValue* (*CreateInBoundsGEP)(IBuilder *self, IType *type, IValue *ptr, IValue **indices, size_t num_indices, const char *name);

	/* Cast operations */
	IValue* (*CreateTrunc)(IBuilder *self, IValue *val, IType *dest_type, const char *name);
	IValue* (*CreateZExt)(IBuilder *self, IValue *val, IType *dest_type, const char *name);
	IValue* (*CreateSExt)(IBuilder *self, IValue *val, IType *dest_type, const char *name);
	IValue* (*CreateFPTrunc)(IBuilder *self, IValue *val, IType *dest_type, const char *name);
	IValue* (*CreateFPExt)(IBuilder *self, IValue *val, IType *dest_type, const char *name);
	IValue* (*CreateFPToUI)(IBuilder *self, IValue *val, IType *dest_type, const char *name);
	IValue* (*CreateFPToSI)(IBuilder *self, IValue *val, IType *dest_type, const char *name);
	IValue* (*CreateUIToFP)(IBuilder *self, IValue *val, IType *dest_type, const char *name);
	IValue* (*CreateSIToFP)(IBuilder *self, IValue *val, IType *dest_type, const char *name);
	IValue* (*CreatePtrToInt)(IBuilder *self, IValue *val, IType *dest_type, const char *name);
	IValue* (*CreateIntToPtr)(IBuilder *self, IValue *val, IType *dest_type, const char *name);
	IValue* (*CreateBitCast)(IBuilder *self, IValue *val, IType *dest_type, const char *name);

	/* Control flow */
	IValue* (*CreateBr)(IBuilder *self, IBasicBlock *dest);
	IValue* (*CreateCondBr)(IBuilder *self, IValue *cond, IBasicBlock *true_bb, IBasicBlock *false_bb);
	IValue* (*CreateSwitch)(IBuilder *self, IValue *val, IBasicBlock *default_bb, uint32_t num_cases);
	void (*AddSwitchCase)(IBuilder *self, IValue *switch_inst, uint64_t case_val, IBasicBlock *dest);
	IValue* (*CreateRet)(IBuilder *self, IValue *val);
	IValue* (*CreateRetVoid)(IBuilder *self);

	/* Function calls */
	IValue* (*CreateCall)(IBuilder *self, IFunction *func, IValue **args, size_t num_args, const char *name);

	/* PHI nodes */
	IValue* (*CreatePHI)(IBuilder *self, IType *type, uint32_t num_reserved, const char *name);
	void (*AddPHIIncoming)(IBuilder *self, IValue *phi, IValue *val, IBasicBlock *bb);

	/* Select */
	IValue* (*CreateSelect)(IBuilder *self, IValue *cond, IValue *true_val, IValue *false_val, const char *name);
} IBuilder;

/***************************************************************************
 * IFunction Interface - Represents a function in the module
 ***************************************************************************/
typedef struct IFunction {
	IUnknown base;

	/* Function properties */
	const char* (*GetName)(IFunction *self);
	IType* (*GetReturnType)(IFunction *self);

	/* Arguments */
	uint32_t (*GetNumArgs)(IFunction *self);
	IValue* (*GetArg)(IFunction *self, uint32_t index);

	/* Basic blocks */
	IBasicBlock* (*CreateBasicBlock)(IFunction *self, const char *name);
	IBasicBlock* (*GetEntryBlock)(IFunction *self);

	/* Verification */
	int (*Verify)(IFunction *self, char **error_msg);

	/* Optimization */
	void (*Optimize)(IFunction *self);

	/* Debug output */
	void (*Dump)(IFunction *self);
	char* (*ToString)(IFunction *self);
} IFunction;

/***************************************************************************
 * IModule Interface - Represents a compilation unit
 ***************************************************************************/
typedef struct IModule {
	IUnknown base;

	/* Module properties */
	const char* (*GetName)(IModule *self);
	void (*SetDataLayout)(IModule *self, const char *layout);
	const char* (*GetDataLayout)(IModule *self);

	/* Type creation */
	IType* (*GetVoidType)(IModule *self);
	IType* (*GetInt1Type)(IModule *self);
	IType* (*GetInt8Type)(IModule *self);
	IType* (*GetInt16Type)(IModule *self);
	IType* (*GetInt32Type)(IModule *self);
	IType* (*GetInt64Type)(IModule *self);
	IType* (*GetInt128Type)(IModule *self);
	IType* (*GetIntType)(IModule *self, uint32_t num_bits);
	IType* (*GetFloatType)(IModule *self);
	IType* (*GetDoubleType)(IModule *self);
	IType* (*GetFP80Type)(IModule *self);
	IType* (*GetFP128Type)(IModule *self);
	IType* (*GetPointerType)(IModule *self, IType *element_type);
	IType* (*GetStructType)(IModule *self, IType **element_types, uint32_t num_elements, const char *name);
	IType* (*GetArrayType)(IModule *self, IType *element_type, uint32_t num_elements);
	IType* (*GetFunctionType)(IModule *self, IType *return_type, IType **param_types, uint32_t num_params, int is_vararg);

	/* Function creation */
	IFunction* (*CreateFunction)(IModule *self, const char *name, IType *function_type);
	IFunction* (*GetFunction)(IModule *self, const char *name);

	/* Global variables */
	IValue* (*CreateGlobalVariable)(IModule *self, IType *type, const char *name, int is_constant);
	IValue* (*GetGlobalVariable)(IModule *self, const char *name);

	/* Builder creation */
	IBuilder* (*CreateBuilder)(IModule *self);

	/* Compilation and execution */
	int (*Compile)(IModule *self);
	void* (*GetFunctionAddress)(IModule *self, const char *name);

	/* Debug output */
	void (*Dump)(IModule *self);
	char* (*ToString)(IModule *self);
} IModule;

/***************************************************************************
 * IBackend Interface - Main backend factory and manager
 ***************************************************************************/
typedef struct IBackend {
	IUnknown base;

	/* Backend information */
	const char* (*GetName)(IBackend *self);
	const char* (*GetVersion)(IBackend *self);
	backend_type_t (*GetType)(IBackend *self);

	/* Initialization */
	int (*Initialize)(IBackend *self);
	void (*Shutdown)(IBackend *self);

	/* Module creation */
	IModule* (*CreateModule)(IBackend *self, const char *name);

	/* Optimization levels */
	void (*SetOptimizationLevel)(IBackend *self, uint32_t level); /* 0-3 */
	uint32_t (*GetOptimizationLevel)(IBackend *self);

	/* Feature detection */
	int (*SupportsFeature)(IBackend *self, const char *feature);

	/* Target information */
	const char* (*GetTargetTriple)(IBackend *self);
	const char* (*GetDataLayout)(IBackend *self);
	int (*SupportsFloat80)(IBackend *self);
	int (*SupportsFloat128)(IBackend *self);
} IBackend;

/***************************************************************************
 * Backend Factory Functions
 ***************************************************************************/

/* Create backend by type */
IBackend* backend_create(backend_type_t type);

/* Get backend name from type */
const char* backend_get_name(backend_type_t type);

/* List available backends */
uint32_t backend_get_available(backend_type_t *types, uint32_t max_count);

#ifdef __cplusplus
}
#endif

#endif /* __LIBCPU_BACKEND_H__ */
