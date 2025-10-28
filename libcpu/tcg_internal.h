/*
 * libcpu TCG (Tiny Code Generator) Backend - Internal Structures
 *
 * Based on QEMU's TCG but simplified for libcpu
 */

#ifndef __TCG_INTERNAL_H__
#define __TCG_INTERNAL_H__

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TCG operations */
typedef enum {
	/* Basic operations */
	TCG_OP_MOV,      /* Move */
	TCG_OP_MOVI,     /* Move immediate */

	/* Arithmetic */
	TCG_OP_ADD,
	TCG_OP_SUB,
	TCG_OP_MUL,
	TCG_OP_DIV,
	TCG_OP_DIVU,
	TCG_OP_REM,
	TCG_OP_REMU,
	TCG_OP_NEG,

	/* Logical */
	TCG_OP_AND,
	TCG_OP_OR,
	TCG_OP_XOR,
	TCG_OP_NOT,
	TCG_OP_SHL,
	TCG_OP_SHR,
	TCG_OP_SAR,

	/* Comparisons */
	TCG_OP_EQ,
	TCG_OP_NE,
	TCG_OP_LT,
	TCG_OP_LE,
	TCG_OP_GT,
	TCG_OP_GE,
	TCG_OP_LTU,
	TCG_OP_LEU,
	TCG_OP_GTU,
	TCG_OP_GEU,

	/* Memory */
	TCG_OP_LD8U,     /* Load 8-bit unsigned */
	TCG_OP_LD8S,     /* Load 8-bit signed */
	TCG_OP_LD16U,
	TCG_OP_LD16S,
	TCG_OP_LD32U,
	TCG_OP_LD32S,
	TCG_OP_LD64,
	TCG_OP_ST8,      /* Store 8-bit */
	TCG_OP_ST16,
	TCG_OP_ST32,
	TCG_OP_ST64,

	/* Conversions */
	TCG_OP_EXT8S,    /* Sign extend 8-bit */
	TCG_OP_EXT8U,    /* Zero extend 8-bit */
	TCG_OP_EXT16S,
	TCG_OP_EXT16U,
	TCG_OP_EXT32S,
	TCG_OP_EXT32U,
	TCG_OP_TRUNC,

	/* Control flow */
	TCG_OP_BR,       /* Unconditional branch */
	TCG_OP_BRCOND,   /* Conditional branch */
	TCG_OP_CALL,     /* Function call */
	TCG_OP_RET,      /* Return */

	/* Special */
	TCG_OP_NOP,
	TCG_OP_EXIT,     /* Exit translation block */

	TCG_OP_MAX
} tcg_opcode_t;

/* TCG register types */
typedef enum {
	TCG_TYPE_I32,
	TCG_TYPE_I64,
	TCG_TYPE_PTR
} tcg_type_t;

/* TCG temporary register */
typedef struct {
	uint32_t id;
	tcg_type_t type;
	int is_const;
	uint64_t const_val;
	int reg;         /* Physical register allocation (-1 = not allocated) */
	int spilled;     /* Spilled to memory */
	int32_t spill_offset;
} tcg_temp_t;

/* TCG label */
typedef struct {
	uint32_t id;
	uint32_t offset; /* Offset in code buffer (set during emission) */
	int bound;       /* Label has been bound to location */
} tcg_label_t;

/* TCG instruction */
typedef struct tcg_insn {
	tcg_opcode_t op;
	tcg_temp_t *dest;
	tcg_temp_t *src1;
	tcg_temp_t *src2;
	tcg_label_t *label;
	uint64_t imm;
	struct tcg_insn *next;
} tcg_insn_t;

/* TCG context */
typedef struct {
	/* Instruction list */
	tcg_insn_t *insn_head;
	tcg_insn_t *insn_tail;
	uint32_t insn_count;

	/* Temporaries */
	tcg_temp_t **temps;
	uint32_t temp_count;
	uint32_t temp_capacity;

	/* Labels */
	tcg_label_t **labels;
	uint32_t label_count;
	uint32_t label_capacity;

	/* Code buffer */
	uint8_t *code_buffer;
	uint32_t code_size;
	uint32_t code_capacity;

	/* Register allocation */
	int reg_alloc[16];  /* Physical register usage */
	int stack_offset;   /* Current stack frame offset */

	/* Configuration */
	int optimize;
} tcg_context_t;

/***************************************************************************
 * TCG Context API
 ***************************************************************************/

tcg_context_t* tcg_context_create(void);
void tcg_context_destroy(tcg_context_t *ctx);
void tcg_context_reset(tcg_context_t *ctx);

/***************************************************************************
 * TCG Temporary Management
 ***************************************************************************/

tcg_temp_t* tcg_temp_new(tcg_context_t *ctx, tcg_type_t type);
tcg_temp_t* tcg_const_i32(tcg_context_t *ctx, uint32_t val);
tcg_temp_t* tcg_const_i64(tcg_context_t *ctx, uint64_t val);
void tcg_temp_free(tcg_context_t *ctx, tcg_temp_t *temp);

/***************************************************************************
 * TCG Label Management
 ***************************************************************************/

tcg_label_t* tcg_label_new(tcg_context_t *ctx);
void tcg_label_bind(tcg_context_t *ctx, tcg_label_t *label);

/***************************************************************************
 * TCG Instruction Emission
 ***************************************************************************/

void tcg_emit(tcg_context_t *ctx, tcg_opcode_t op, tcg_temp_t *dest,
              tcg_temp_t *src1, tcg_temp_t *src2);
void tcg_emit_imm(tcg_context_t *ctx, tcg_opcode_t op, tcg_temp_t *dest,
                  tcg_temp_t *src1, uint64_t imm);
void tcg_emit_branch(tcg_context_t *ctx, tcg_label_t *label);
void tcg_emit_brcond(tcg_context_t *ctx, tcg_opcode_t cond, tcg_temp_t *src1,
                     tcg_temp_t *src2, tcg_label_t *label);
void tcg_emit_call(tcg_context_t *ctx, void *func, tcg_temp_t *ret,
                   tcg_temp_t **args, int num_args);
void tcg_emit_ret(tcg_context_t *ctx, tcg_temp_t *val);

/***************************************************************************
 * TCG Optimization
 ***************************************************************************/

void tcg_optimize(tcg_context_t *ctx);

/***************************************************************************
 * TCG Register Allocation
 ***************************************************************************/

void tcg_regalloc(tcg_context_t *ctx);

/***************************************************************************
 * TCG Code Generation (x86-64)
 ***************************************************************************/

void* tcg_generate_code(tcg_context_t *ctx);

/***************************************************************************
 * TCG Utilities
 ***************************************************************************/

const char* tcg_op_name(tcg_opcode_t op);
void tcg_dump(tcg_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* __TCG_INTERNAL_H__ */
