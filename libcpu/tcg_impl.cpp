/*
 * libcpu TCG (Tiny Code Generator) Implementation
 */

#include "tcg_internal.h"
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>

/***************************************************************************
 * TCG Context Management
 ***************************************************************************/

tcg_context_t* tcg_context_create(void)
{
	tcg_context_t *ctx = (tcg_context_t*)calloc(1, sizeof(tcg_context_t));
	if (!ctx)
		return NULL;

	ctx->temp_capacity = 256;
	ctx->temps = (tcg_temp_t**)calloc(ctx->temp_capacity, sizeof(tcg_temp_t*));

	ctx->label_capacity = 64;
	ctx->labels = (tcg_label_t**)calloc(ctx->label_capacity, sizeof(tcg_label_t*));

	ctx->code_capacity = 65536;  /* 64KB initial */
	ctx->code_buffer = (uint8_t*)mmap(NULL, ctx->code_capacity,
	                                   PROT_READ | PROT_WRITE | PROT_EXEC,
	                                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (ctx->code_buffer == MAP_FAILED) {
		free(ctx->temps);
		free(ctx->labels);
		free(ctx);
		return NULL;
	}

	ctx->optimize = 1;

	return ctx;
}

void tcg_context_destroy(tcg_context_t *ctx)
{
	if (!ctx)
		return;

	/* Free instructions */
	tcg_insn_t *insn = ctx->insn_head;
	while (insn) {
		tcg_insn_t *next = insn->next;
		free(insn);
		insn = next;
	}

	/* Free temps */
	for (uint32_t i = 0; i < ctx->temp_count; i++) {
		free(ctx->temps[i]);
	}
	free(ctx->temps);

	/* Free labels */
	for (uint32_t i = 0; i < ctx->label_count; i++) {
		free(ctx->labels[i]);
	}
	free(ctx->labels);

	/* Free code buffer */
	if (ctx->code_buffer != MAP_FAILED)
		munmap(ctx->code_buffer, ctx->code_capacity);

	free(ctx);
}

void tcg_context_reset(tcg_context_t *ctx)
{
	/* Free instructions */
	tcg_insn_t *insn = ctx->insn_head;
	while (insn) {
		tcg_insn_t *next = insn->next;
		free(insn);
		insn = next;
	}
	ctx->insn_head = ctx->insn_tail = NULL;
	ctx->insn_count = 0;

	/* Reset temps */
	for (uint32_t i = 0; i < ctx->temp_count; i++) {
		free(ctx->temps[i]);
	}
	ctx->temp_count = 0;

	/* Reset labels */
	for (uint32_t i = 0; i < ctx->label_count; i++) {
		free(ctx->labels[i]);
	}
	ctx->label_count = 0;

	ctx->code_size = 0;
	ctx->stack_offset = 0;
	memset(ctx->reg_alloc, 0, sizeof(ctx->reg_alloc));
}

/***************************************************************************
 * TCG Temporary Management
 ***************************************************************************/

tcg_temp_t* tcg_temp_new(tcg_context_t *ctx, tcg_type_t type)
{
	if (ctx->temp_count >= ctx->temp_capacity) {
		ctx->temp_capacity *= 2;
		ctx->temps = (tcg_temp_t**)realloc(ctx->temps,
		                                    ctx->temp_capacity * sizeof(tcg_temp_t*));
	}

	tcg_temp_t *temp = (tcg_temp_t*)calloc(1, sizeof(tcg_temp_t));
	temp->id = ctx->temp_count;
	temp->type = type;
	temp->reg = -1;
	temp->spilled = 0;
	temp->is_const = 0;

	ctx->temps[ctx->temp_count++] = temp;
	return temp;
}

tcg_temp_t* tcg_const_i32(tcg_context_t *ctx, uint32_t val)
{
	tcg_temp_t *temp = tcg_temp_new(ctx, TCG_TYPE_I32);
	temp->is_const = 1;
	temp->const_val = val;
	return temp;
}

tcg_temp_t* tcg_const_i64(tcg_context_t *ctx, uint64_t val)
{
	tcg_temp_t *temp = tcg_temp_new(ctx, TCG_TYPE_I64);
	temp->is_const = 1;
	temp->const_val = val;
	return temp;
}

void tcg_temp_free(tcg_context_t *ctx, tcg_temp_t *temp)
{
	/* Temps are managed by context, don't actually free here */
}

/***************************************************************************
 * TCG Label Management
 ***************************************************************************/

tcg_label_t* tcg_label_new(tcg_context_t *ctx)
{
	if (ctx->label_count >= ctx->label_capacity) {
		ctx->label_capacity *= 2;
		ctx->labels = (tcg_label_t**)realloc(ctx->labels,
		                                      ctx->label_capacity * sizeof(tcg_label_t*));
	}

	tcg_label_t *label = (tcg_label_t*)calloc(1, sizeof(tcg_label_t));
	label->id = ctx->label_count;
	label->bound = 0;

	ctx->labels[ctx->label_count++] = label;
	return label;
}

void tcg_label_bind(tcg_context_t *ctx, tcg_label_t *label)
{
	label->offset = ctx->code_size;
	label->bound = 1;
}

/***************************************************************************
 * TCG Instruction Emission
 ***************************************************************************/

static void tcg_append_insn(tcg_context_t *ctx, tcg_insn_t *insn)
{
	if (ctx->insn_tail) {
		ctx->insn_tail->next = insn;
		ctx->insn_tail = insn;
	} else {
		ctx->insn_head = ctx->insn_tail = insn;
	}
	insn->next = NULL;
	ctx->insn_count++;
}

void tcg_emit(tcg_context_t *ctx, tcg_opcode_t op, tcg_temp_t *dest,
              tcg_temp_t *src1, tcg_temp_t *src2)
{
	tcg_insn_t *insn = (tcg_insn_t*)calloc(1, sizeof(tcg_insn_t));
	insn->op = op;
	insn->dest = dest;
	insn->src1 = src1;
	insn->src2 = src2;
	tcg_append_insn(ctx, insn);
}

void tcg_emit_imm(tcg_context_t *ctx, tcg_opcode_t op, tcg_temp_t *dest,
                  tcg_temp_t *src1, uint64_t imm)
{
	tcg_insn_t *insn = (tcg_insn_t*)calloc(1, sizeof(tcg_insn_t));
	insn->op = op;
	insn->dest = dest;
	insn->src1 = src1;
	insn->imm = imm;
	tcg_append_insn(ctx, insn);
}

void tcg_emit_branch(tcg_context_t *ctx, tcg_label_t *label)
{
	tcg_insn_t *insn = (tcg_insn_t*)calloc(1, sizeof(tcg_insn_t));
	insn->op = TCG_OP_BR;
	insn->label = label;
	tcg_append_insn(ctx, insn);
}

void tcg_emit_brcond(tcg_context_t *ctx, tcg_opcode_t cond, tcg_temp_t *src1,
                     tcg_temp_t *src2, tcg_label_t *label)
{
	tcg_insn_t *insn = (tcg_insn_t*)calloc(1, sizeof(tcg_insn_t));
	insn->op = cond; /* Condition as opcode */
	insn->src1 = src1;
	insn->src2 = src2;
	insn->label = label;
	tcg_append_insn(ctx, insn);
}

void tcg_emit_call(tcg_context_t *ctx, void *func, tcg_temp_t *ret,
                   tcg_temp_t **args, int num_args)
{
	tcg_insn_t *insn = (tcg_insn_t*)calloc(1, sizeof(tcg_insn_t));
	insn->op = TCG_OP_CALL;
	insn->dest = ret;
	insn->imm = (uint64_t)func;
	/* Store args in instruction - simplified */
	tcg_append_insn(ctx, insn);
}

void tcg_emit_ret(tcg_context_t *ctx, tcg_temp_t *val)
{
	tcg_insn_t *insn = (tcg_insn_t*)calloc(1, sizeof(tcg_insn_t));
	insn->op = TCG_OP_RET;
	insn->src1 = val;
	tcg_append_insn(ctx, insn);
}

/***************************************************************************
 * TCG Optimization (Simplified)
 ***************************************************************************/

void tcg_optimize(tcg_context_t *ctx)
{
	if (!ctx->optimize)
		return;

	/* Simple optimizations:
	 * 1. Constant folding
	 * 2. Dead code elimination
	 * 3. Copy propagation
	 */

	tcg_insn_t *insn = ctx->insn_head;
	while (insn) {
		/* Constant folding */
		if (insn->src1 && insn->src1->is_const &&
		    insn->src2 && insn->src2->is_const) {
			uint64_t result = 0;
			int can_fold = 1;

			switch (insn->op) {
			case TCG_OP_ADD:
				result = insn->src1->const_val + insn->src2->const_val;
				break;
			case TCG_OP_SUB:
				result = insn->src1->const_val - insn->src2->const_val;
				break;
			case TCG_OP_AND:
				result = insn->src1->const_val & insn->src2->const_val;
				break;
			case TCG_OP_OR:
				result = insn->src1->const_val | insn->src2->const_val;
				break;
			case TCG_OP_XOR:
				result = insn->src1->const_val ^ insn->src2->const_val;
				break;
			default:
				can_fold = 0;
			}

			if (can_fold && insn->dest) {
				insn->dest->is_const = 1;
				insn->dest->const_val = result;
			}
		}

		insn = insn->next;
	}
}

/***************************************************************************
 * TCG Register Allocation (Simplified)
 ***************************************************************************/

/* x86-64 register mapping */
#define REG_RAX 0
#define REG_RCX 1
#define REG_RDX 2
#define REG_RBX 3
#define REG_RSI 4
#define REG_RDI 5
#define REG_R8  6
#define REG_R9  7
#define REG_R10 8
#define REG_R11 9

void tcg_regalloc(tcg_context_t *ctx)
{
	/* Simple linear scan register allocation */
	int next_reg = REG_RAX;

	tcg_insn_t *insn = ctx->insn_head;
	while (insn) {
		/* Allocate registers for dest/src */
		if (insn->dest && insn->dest->reg == -1 && !insn->dest->is_const) {
			if (next_reg < REG_R11) {
				insn->dest->reg = next_reg++;
				ctx->reg_alloc[insn->dest->reg] = 1;
			} else {
				/* Spill to stack */
				insn->dest->spilled = 1;
				insn->dest->spill_offset = ctx->stack_offset;
				ctx->stack_offset += 8;
			}
		}

		insn = insn->next;
	}
}

/***************************************************************************
 * x86-64 Code Emission Helpers
 ***************************************************************************/

static void emit_byte(tcg_context_t *ctx, uint8_t byte)
{
	if (ctx->code_size >= ctx->code_capacity)
		return; /* Buffer full */
	ctx->code_buffer[ctx->code_size++] = byte;
}

static void emit_word(tcg_context_t *ctx, uint16_t word)
{
	emit_byte(ctx, word & 0xFF);
	emit_byte(ctx, (word >> 8) & 0xFF);
}

static void emit_dword(tcg_context_t *ctx, uint32_t dword)
{
	emit_word(ctx, dword & 0xFFFF);
	emit_word(ctx, (dword >> 16) & 0xFFFF);
}

static void emit_qword(tcg_context_t *ctx, uint64_t qword)
{
	emit_dword(ctx, qword & 0xFFFFFFFF);
	emit_dword(ctx, (qword >> 32) & 0xFFFFFFFF);
}

/* REX prefix for 64-bit operations */
static void emit_rex(tcg_context_t *ctx, int w, int r, int x, int b)
{
	emit_byte(ctx, 0x40 | (w << 3) | (r << 2) | (x << 1) | b);
}

/* ModR/M byte */
static void emit_modrm(tcg_context_t *ctx, int mod, int reg, int rm)
{
	emit_byte(ctx, (mod << 6) | (reg << 3) | rm);
}

/***************************************************************************
 * TCG to x86-64 Code Generation
 ***************************************************************************/

static void tcg_gen_x64_mov(tcg_context_t *ctx, int dest_reg, int src_reg)
{
	/* MOV dest, src */
	emit_rex(ctx, 1, 0, 0, 0);
	emit_byte(ctx, 0x89); /* MOV r/m64, r64 */
	emit_modrm(ctx, 3, src_reg, dest_reg);
}

static void tcg_gen_x64_movi(tcg_context_t *ctx, int dest_reg, uint64_t imm)
{
	/* MOV dest, imm */
	if (imm <= 0xFFFFFFFF) {
		emit_byte(ctx, 0xB8 + dest_reg); /* MOV r32, imm32 */
		emit_dword(ctx, (uint32_t)imm);
	} else {
		emit_rex(ctx, 1, 0, 0, dest_reg >= 8);
		emit_byte(ctx, 0xB8 + (dest_reg & 7)); /* MOV r64, imm64 */
		emit_qword(ctx, imm);
	}
}

static void tcg_gen_x64_add(tcg_context_t *ctx, int dest, int src1, int src2)
{
	/* ADD dest, src */
	if (dest != src1)
		tcg_gen_x64_mov(ctx, dest, src1);
	emit_rex(ctx, 1, 0, 0, 0);
	emit_byte(ctx, 0x01); /* ADD r/m64, r64 */
	emit_modrm(ctx, 3, src2, dest);
}

static void tcg_gen_x64_sub(tcg_context_t *ctx, int dest, int src1, int src2)
{
	/* SUB dest, src */
	if (dest != src1)
		tcg_gen_x64_mov(ctx, dest, src1);
	emit_rex(ctx, 1, 0, 0, 0);
	emit_byte(ctx, 0x29); /* SUB r/m64, r64 */
	emit_modrm(ctx, 3, src2, dest);
}

static void tcg_gen_x64_and(tcg_context_t *ctx, int dest, int src1, int src2)
{
	if (dest != src1)
		tcg_gen_x64_mov(ctx, dest, src1);
	emit_rex(ctx, 1, 0, 0, 0);
	emit_byte(ctx, 0x21); /* AND r/m64, r64 */
	emit_modrm(ctx, 3, src2, dest);
}

static void tcg_gen_x64_or(tcg_context_t *ctx, int dest, int src1, int src2)
{
	if (dest != src1)
		tcg_gen_x64_mov(ctx, dest, src1);
	emit_rex(ctx, 1, 0, 0, 0);
	emit_byte(ctx, 0x09); /* OR r/m64, r64 */
	emit_modrm(ctx, 3, src2, dest);
}

static void tcg_gen_x64_xor(tcg_context_t *ctx, int dest, int src1, int src2)
{
	if (dest != src1)
		tcg_gen_x64_mov(ctx, dest, src1);
	emit_rex(ctx, 1, 0, 0, 0);
	emit_byte(ctx, 0x31); /* XOR r/m64, r64 */
	emit_modrm(ctx, 3, src2, dest);
}

static void tcg_gen_x64_ret(tcg_context_t *ctx)
{
	emit_byte(ctx, 0xC3); /* RET */
}

void* tcg_generate_code(tcg_context_t *ctx)
{
	/* Run optimization and register allocation */
	tcg_optimize(ctx);
	tcg_regalloc(ctx);

	/* Function prologue */
	emit_byte(ctx, 0x55); /* PUSH RBP */
	emit_rex(ctx, 1, 0, 0, 0);
	emit_byte(ctx, 0x89); /* MOV RBP, RSP */
	emit_modrm(ctx, 3, 4, 5);

	/* Generate code for each instruction */
	tcg_insn_t *insn = ctx->insn_head;
	while (insn) {
		int dest_reg = insn->dest ? insn->dest->reg : -1;
		int src1_reg = insn->src1 ? insn->src1->reg : -1;
		int src2_reg = insn->src2 ? insn->src2->reg : -1;

		/* Handle constant src1 */
		if (insn->src1 && insn->src1->is_const && src1_reg != -1) {
			tcg_gen_x64_movi(ctx, src1_reg, insn->src1->const_val);
		}

		/* Handle constant src2 */
		if (insn->src2 && insn->src2->is_const && src2_reg != -1) {
			tcg_gen_x64_movi(ctx, src2_reg, insn->src2->const_val);
		}

		switch (insn->op) {
		case TCG_OP_MOV:
			if (dest_reg >= 0 && src1_reg >= 0)
				tcg_gen_x64_mov(ctx, dest_reg, src1_reg);
			break;

		case TCG_OP_MOVI:
			if (dest_reg >= 0)
				tcg_gen_x64_movi(ctx, dest_reg, insn->imm);
			break;

		case TCG_OP_ADD:
			if (dest_reg >= 0 && src1_reg >= 0 && src2_reg >= 0)
				tcg_gen_x64_add(ctx, dest_reg, src1_reg, src2_reg);
			break;

		case TCG_OP_SUB:
			if (dest_reg >= 0 && src1_reg >= 0 && src2_reg >= 0)
				tcg_gen_x64_sub(ctx, dest_reg, src1_reg, src2_reg);
			break;

		case TCG_OP_AND:
			if (dest_reg >= 0 && src1_reg >= 0 && src2_reg >= 0)
				tcg_gen_x64_and(ctx, dest_reg, src1_reg, src2_reg);
			break;

		case TCG_OP_OR:
			if (dest_reg >= 0 && src1_reg >= 0 && src2_reg >= 0)
				tcg_gen_x64_or(ctx, dest_reg, src1_reg, src2_reg);
			break;

		case TCG_OP_XOR:
			if (dest_reg >= 0 && src1_reg >= 0 && src2_reg >= 0)
				tcg_gen_x64_xor(ctx, dest_reg, src1_reg, src2_reg);
			break;

		case TCG_OP_RET:
			tcg_gen_x64_ret(ctx);
			break;

		default:
			/* Unimplemented operation */
			break;
		}

		insn = insn->next;
	}

	/* Function epilogue */
	emit_byte(ctx, 0x5D); /* POP RBP */
	tcg_gen_x64_ret(ctx);

	return ctx->code_buffer;
}

/***************************************************************************
 * TCG Utilities
 ***************************************************************************/

const char* tcg_op_name(tcg_opcode_t op)
{
	static const char *names[] = {
		"mov", "movi", "add", "sub", "mul", "div", "divu", "rem", "remu", "neg",
		"and", "or", "xor", "not", "shl", "shr", "sar",
		"eq", "ne", "lt", "le", "gt", "ge", "ltu", "leu", "gtu", "geu",
		"ld8u", "ld8s", "ld16u", "ld16s", "ld32u", "ld32s", "ld64",
		"st8", "st16", "st32", "st64",
		"ext8s", "ext8u", "ext16s", "ext16u", "ext32s", "ext32u", "trunc",
		"br", "brcond", "call", "ret",
		"nop", "exit"
	};
	if (op >= TCG_OP_MAX)
		return "unknown";
	return names[op];
}

void tcg_dump(tcg_context_t *ctx)
{
	printf("TCG IR dump (%u instructions):\n", ctx->insn_count);
	tcg_insn_t *insn = ctx->insn_head;
	int i = 0;
	while (insn) {
		printf("  %3d: %s", i++, tcg_op_name(insn->op));
		if (insn->dest)
			printf(" t%u", insn->dest->id);
		if (insn->src1)
			printf(" t%u", insn->src1->id);
		if (insn->src2)
			printf(" t%u", insn->src2->id);
		if (insn->label)
			printf(" L%u", insn->label->id);
		printf("\n");
		insn = insn->next;
	}
}
