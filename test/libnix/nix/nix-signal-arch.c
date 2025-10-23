/*
 * nix-signal-arch.c
 *
 * Architecture-specific signal handling implementation
 */

#include "nix-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "nix.h"
#include "nix-signal-arch.h"
#include "xec-mem.h"
#include "xec-debug.h"

extern void *g_nix_log;

/* Current architecture (set at runtime) */
static nix_arch_t g_current_arch = NIX_ARCH_UNKNOWN;

void
nix_signal_set_architecture(nix_arch_t arch)
{
	g_current_arch = arch;
	XEC_LOG(g_nix_log, XEC_LOG_INFO, 0, "Signal architecture set to %d", arch);
}

nix_arch_t
nix_signal_get_architecture(void)
{
	return g_current_arch;
}

/*
 * ARM32 Signal Frame Setup
 */
static int
nix_signal_setup_frame_arm32(
	struct nix_signal_frame *frame,
	int signo,
	struct nix_sigaction const *sa,
	nix_sigset_t *oldmask,
	void *guest_context,
	nix_env_t *env)
{
	/* ARM32 uses reg_arm_t structure - cast from void* */
	uint32_t *regs = (uint32_t *)guest_context;
	struct nix_sigcontext_arm32 *sc = &frame->sf_sc.arm32;

	/* Save all general purpose registers */
	for (int i = 0; i < 16; i++) {
		*(&sc->r0 + i) = regs[i];
	}
	sc->cpsr = regs[16];	/* Status register */

	/* Save signal mask */
	sc->oldmask = *oldmask;
	sc->trap_no = 0;
	sc->error_code = 0;
	sc->fault_address = 0;

	/* Set up frame header */
	frame->sf_signum = signo;
	frame->sf_code = 0;
	frame->sf_handler = sa->__sa_handler;

	return 0;
}

static int
nix_signal_restore_frame_arm32(
	struct nix_signal_frame *frame,
	void *guest_context,
	nix_sigset_t *restored_mask,
	nix_env_t *env)
{
	uint32_t *regs = (uint32_t *)guest_context;
	struct nix_sigcontext_arm32 *sc = &frame->sf_sc.arm32;

	/* Restore all general purpose registers */
	for (int i = 0; i < 16; i++) {
		regs[i] = *(&sc->r0 + i);
	}
	regs[16] = sc->cpsr;

	/* Restore signal mask */
	*restored_mask = sc->oldmask;

	return 0;
}

/*
 * ARM64 Signal Frame Setup
 */
static int
nix_signal_setup_frame_arm64(
	struct nix_signal_frame *frame,
	int signo,
	struct nix_sigaction const *sa,
	nix_sigset_t *oldmask,
	void *guest_context,
	nix_env_t *env)
{
	uint64_t *regs = (uint64_t *)guest_context;
	struct nix_sigcontext_arm64 *sc = &frame->sf_sc.arm64;

	/* Save x0-x30 */
	for (int i = 0; i < 31; i++) {
		sc->regs[i] = regs[i];
	}
	sc->sp = regs[31];		/* Stack pointer */
	sc->pc = regs[32];		/* Program counter */
	sc->pstate = regs[33];	/* Processor state */
	sc->fault_address = 0;

	memset(sc->__reserved, 0, sizeof(sc->__reserved));

	frame->sf_signum = signo;
	frame->sf_code = 0;
	frame->sf_handler = sa->__sa_handler;

	return 0;
}

static int
nix_signal_restore_frame_arm64(
	struct nix_signal_frame *frame,
	void *guest_context,
	nix_sigset_t *restored_mask,
	nix_env_t *env)
{
	uint64_t *regs = (uint64_t *)guest_context;
	struct nix_sigcontext_arm64 *sc = &frame->sf_sc.arm64;

	/* Restore x0-x30 */
	for (int i = 0; i < 31; i++) {
		regs[i] = sc->regs[i];
	}
	regs[31] = sc->sp;
	regs[32] = sc->pc;
	regs[33] = sc->pstate;

	/* ARM64 stores mask elsewhere - set via parameter */
	*restored_mask = 0;  /* Would be extracted from frame */

	return 0;
}

/*
 * Alpha Signal Frame Setup
 */
static int
nix_signal_setup_frame_alpha(
	struct nix_signal_frame *frame,
	int signo,
	struct nix_sigaction const *sa,
	nix_sigset_t *oldmask,
	void *guest_context,
	nix_env_t *env)
{
	int64_t *regs = (int64_t *)guest_context;
	struct nix_sigcontext_alpha *sc = &frame->sf_sc.alpha;

	/* Save integer registers $0-$31 */
	for (int i = 0; i < 32; i++) {
		sc->sc_regs[i] = regs[i];
	}
	sc->sc_pc = regs[32];
	sc->sc_ps = regs[33];  /* Processor status */

	sc->sc_mask = *oldmask;
	sc->sc_onstack = 0;
	sc->sc_ownedfp = 0;

	/* Clear FP registers */
	memset(sc->sc_fpregs, 0, sizeof(sc->sc_fpregs));
	sc->sc_fpcr = 0;

	frame->sf_signum = signo;
	frame->sf_code = 0;
	frame->sf_handler = sa->__sa_handler;

	return 0;
}

static int
nix_signal_restore_frame_alpha(
	struct nix_signal_frame *frame,
	void *guest_context,
	nix_sigset_t *restored_mask,
	nix_env_t *env)
{
	int64_t *regs = (int64_t *)guest_context;
	struct nix_sigcontext_alpha *sc = &frame->sf_sc.alpha;

	/* Restore integer registers */
	for (int i = 0; i < 32; i++) {
		regs[i] = sc->sc_regs[i];
	}
	regs[32] = sc->sc_pc;
	regs[33] = sc->sc_ps;

	*restored_mask = sc->sc_mask;

	return 0;
}

/*
 * MIPS32 Signal Frame Setup
 */
static int
nix_signal_setup_frame_mips32(
	struct nix_signal_frame *frame,
	int signo,
	struct nix_sigaction const *sa,
	nix_sigset_t *oldmask,
	void *guest_context,
	nix_env_t *env)
{
	uint32_t *regs = (uint32_t *)guest_context;
	struct nix_sigcontext_mips32 *sc = &frame->sf_sc.mips32;

	/* Save $0-$31 */
	for (int i = 0; i < 32; i++) {
		sc->sc_regs[i] = regs[i];
	}
	sc->sc_pc = regs[32];  /* PC */

	sc->sc_status = 0;
	sc->sc_regmask = 0xffffffff;
	sc->sc_used_math = 0;
	sc->sc_dsp = 0;

	/* Clear FP registers */
	memset(sc->sc_fpregs, 0, sizeof(sc->sc_fpregs));
	sc->sc_fpc_csr = 0;
	sc->sc_fpc_eir = 0;

	/* HI/LO registers */
	sc->sc_mdhi = 0;
	sc->sc_mdlo = 0;

	frame->sf_signum = signo;
	frame->sf_code = 0;
	frame->sf_handler = sa->__sa_handler;

	return 0;
}

static int
nix_signal_restore_frame_mips32(
	struct nix_signal_frame *frame,
	void *guest_context,
	nix_sigset_t *restored_mask,
	nix_env_t *env)
{
	uint32_t *regs = (uint32_t *)guest_context;
	struct nix_sigcontext_mips32 *sc = &frame->sf_sc.mips32;

	/* Restore $0-$31 */
	for (int i = 0; i < 32; i++) {
		regs[i] = (uint32_t)sc->sc_regs[i];
	}
	regs[32] = (uint32_t)sc->sc_pc;

	*restored_mask = 0;  /* Would be in frame somewhere */

	return 0;
}

/*
 * MIPS64 Signal Frame Setup
 */
static int
nix_signal_setup_frame_mips64(
	struct nix_signal_frame *frame,
	int signo,
	struct nix_sigaction const *sa,
	nix_sigset_t *oldmask,
	void *guest_context,
	nix_env_t *env)
{
	uint64_t *regs = (uint64_t *)guest_context;
	struct nix_sigcontext_mips64 *sc = &frame->sf_sc.mips64;

	/* Save $0-$31 */
	for (int i = 0; i < 32; i++) {
		sc->sc_regs[i] = regs[i];
	}
	sc->sc_pc = regs[32];

	sc->sc_used_math = 0;
	sc->sc_dsp = 0;

	/* Clear FP registers */
	memset(sc->sc_fpregs, 0, sizeof(sc->sc_fpregs));
	sc->sc_fpc_csr = 0;

	/* HI/LO registers */
	sc->sc_mdhi = 0;
	sc->sc_mdlo = 0;

	frame->sf_signum = signo;
	frame->sf_code = 0;
	frame->sf_handler = sa->__sa_handler;

	return 0;
}

static int
nix_signal_restore_frame_mips64(
	struct nix_signal_frame *frame,
	void *guest_context,
	nix_sigset_t *restored_mask,
	nix_env_t *env)
{
	uint64_t *regs = (uint64_t *)guest_context;
	struct nix_sigcontext_mips64 *sc = &frame->sf_sc.mips64;

	/* Restore $0-$31 */
	for (int i = 0; i < 32; i++) {
		regs[i] = sc->sc_regs[i];
	}
	regs[32] = sc->sc_pc;

	*restored_mask = 0;

	return 0;
}

/*
 * PowerPC 32-bit Signal Frame Setup
 */
static int
nix_signal_setup_frame_ppc32(
	struct nix_signal_frame *frame,
	int signo,
	struct nix_sigaction const *sa,
	nix_sigset_t *oldmask,
	void *guest_context,
	nix_env_t *env)
{
	uint32_t *regs = (uint32_t *)guest_context;
	struct nix_sigcontext_ppc32 *sc = &frame->sf_sc.ppc32;

	/* Save r0-r31 */
	for (int i = 0; i < 32; i++) {
		sc->sc_gpr[i] = regs[i];
	}

	/* Special registers (offsets depend on layout) */
	sc->sc_pc = regs[32];   /* SRR0 - PC */
	sc->sc_msr = regs[33];  /* SRR1 - MSR */
	sc->sc_lr = regs[34];   /* Link register */
	sc->sc_ctr = regs[35];  /* Count register */
	sc->sc_cr = regs[36];   /* Condition register */
	sc->sc_xer = regs[37];  /* XER */

	sc->sc_mask = *oldmask;
	sc->sc_onstack = 0;

	frame->sf_signum = signo;
	frame->sf_code = 0;
	frame->sf_handler = sa->__sa_handler;

	return 0;
}

static int
nix_signal_restore_frame_ppc32(
	struct nix_signal_frame *frame,
	void *guest_context,
	nix_sigset_t *restored_mask,
	nix_env_t *env)
{
	uint32_t *regs = (uint32_t *)guest_context;
	struct nix_sigcontext_ppc32 *sc = &frame->sf_sc.ppc32;

	/* Restore r0-r31 */
	for (int i = 0; i < 32; i++) {
		regs[i] = sc->sc_gpr[i];
	}

	/* Restore special registers */
	regs[32] = sc->sc_pc;
	regs[33] = sc->sc_msr;
	regs[34] = sc->sc_lr;
	regs[35] = sc->sc_ctr;
	regs[36] = sc->sc_cr;
	regs[37] = sc->sc_xer;

	*restored_mask = sc->sc_mask;

	return 0;
}

/*
 * PowerPC 64-bit Signal Frame Setup
 */
static int
nix_signal_setup_frame_ppc64(
	struct nix_signal_frame *frame,
	int signo,
	struct nix_sigaction const *sa,
	nix_sigset_t *oldmask,
	void *guest_context,
	nix_env_t *env)
{
	uint64_t *regs = (uint64_t *)guest_context;
	struct nix_sigcontext_ppc64 *sc = &frame->sf_sc.ppc64;

	/* Save gp_regs (includes r0-r31 + special regs) */
	for (int i = 0; i < 48 && i < 40; i++) {
		sc->gp_regs[i] = regs[i];
	}

	/* Clear FP and vector registers */
	memset(sc->fp_regs, 0, sizeof(sc->fp_regs));
	memset(sc->v_regs, 0, sizeof(sc->v_regs));

	frame->sf_signum = signo;
	frame->sf_code = 0;
	frame->sf_handler = sa->__sa_handler;

	return 0;
}

static int
nix_signal_restore_frame_ppc64(
	struct nix_signal_frame *frame,
	void *guest_context,
	nix_sigset_t *restored_mask,
	nix_env_t *env)
{
	uint64_t *regs = (uint64_t *)guest_context;
	struct nix_sigcontext_ppc64 *sc = &frame->sf_sc.ppc64;

	/* Restore gp_regs */
	for (int i = 0; i < 48 && i < 40; i++) {
		regs[i] = sc->gp_regs[i];
	}

	*restored_mask = 0;

	return 0;
}

/*
 * IA-64 Signal Frame Setup
 */
static int
nix_signal_setup_frame_ia64(
	struct nix_signal_frame *frame,
	int signo,
	struct nix_sigaction const *sa,
	nix_sigset_t *oldmask,
	void *guest_context,
	nix_env_t *env)
{
	uint64_t *regs = (uint64_t *)guest_context;
	struct nix_sigcontext_ia64 *sc = &frame->sf_sc.ia64;

	/* Save general registers r1-r31 (r0 is always 0) */
	for (int i = 0; i < 32 && i < 31; i++) {
		sc->sc_gr[i] = regs[i];
	}

	/* Save special registers */
	sc->sc_ip = regs[32];	/* Instruction pointer */
	sc->sc_cfm = regs[33];	/* Current frame marker */

	/* Clear other state */
	sc->sc_flags = 0;
	sc->sc_nat = 0;
	memset(sc->sc_br, 0, sizeof(sc->sc_br));
	memset(sc->sc_fr, 0, sizeof(sc->sc_fr));

	frame->sf_signum = signo;
	frame->sf_code = 0;
	frame->sf_handler = sa->__sa_handler;

	return 0;
}

static int
nix_signal_restore_frame_ia64(
	struct nix_signal_frame *frame,
	void *guest_context,
	nix_sigset_t *restored_mask,
	nix_env_t *env)
{
	uint64_t *regs = (uint64_t *)guest_context;
	struct nix_sigcontext_ia64 *sc = &frame->sf_sc.ia64;

	/* Restore general registers */
	for (int i = 0; i < 32 && i < 31; i++) {
		regs[i] = sc->sc_gr[i];
	}

	regs[32] = sc->sc_ip;
	regs[33] = sc->sc_cfm;

	*restored_mask = 0;

	return 0;
}

/*
 * i386 Signal Frame Setup
 */
static int
nix_signal_setup_frame_i386(
	struct nix_signal_frame *frame,
	int signo,
	struct nix_sigaction const *sa,
	nix_sigset_t *oldmask,
	void *guest_context,
	nix_env_t *env)
{
	uint32_t *regs = (uint32_t *)guest_context;
	struct nix_sigcontext_i386 *sc = &frame->sf_sc.i386;

	/* Save general purpose registers */
	sc->eax = regs[0];
	sc->ecx = regs[1];
	sc->edx = regs[2];
	sc->ebx = regs[3];
	sc->esp = regs[4];
	sc->ebp = regs[5];
	sc->esi = regs[6];
	sc->edi = regs[7];
	sc->eip = regs[8];
	sc->eflags = regs[9];

	/* Segment registers */
	sc->cs = 0x23;  /* User code segment */
	sc->ds = 0x2b;  /* User data segment */
	sc->es = 0x2b;
	sc->fs = 0x2b;
	sc->gs = 0x2b;
	sc->ss = 0x2b;

	sc->oldmask = *oldmask;
	sc->trapno = 0;
	sc->err = 0;
	sc->cr2 = 0;
	sc->fpstate = 0;

	frame->sf_signum = signo;
	frame->sf_code = 0;
	frame->sf_handler = sa->__sa_handler;

	return 0;
}

static int
nix_signal_restore_frame_i386(
	struct nix_signal_frame *frame,
	void *guest_context,
	nix_sigset_t *restored_mask,
	nix_env_t *env)
{
	uint32_t *regs = (uint32_t *)guest_context;
	struct nix_sigcontext_i386 *sc = &frame->sf_sc.i386;

	/* Restore general purpose registers */
	regs[0] = sc->eax;
	regs[1] = sc->ecx;
	regs[2] = sc->edx;
	regs[3] = sc->ebx;
	regs[4] = sc->esp;
	regs[5] = sc->ebp;
	regs[6] = sc->esi;
	regs[7] = sc->edi;
	regs[8] = sc->eip;
	regs[9] = sc->eflags;

	*restored_mask = sc->oldmask;

	return 0;
}

/*
 * x86-64 Signal Frame Setup
 */
static int
nix_signal_setup_frame_x64(
	struct nix_signal_frame *frame,
	int signo,
	struct nix_sigaction const *sa,
	nix_sigset_t *oldmask,
	void *guest_context,
	nix_env_t *env)
{
	uint64_t *regs = (uint64_t *)guest_context;
	struct nix_sigcontext_x64 *sc = &frame->sf_sc.x64;

	/* Save general purpose registers */
	sc->rax = regs[0];
	sc->rcx = regs[1];
	sc->rdx = regs[2];
	sc->rbx = regs[3];
	sc->rsp = regs[4];
	sc->rbp = regs[5];
	sc->rsi = regs[6];
	sc->rdi = regs[7];
	sc->r8  = regs[8];
	sc->r9  = regs[9];
	sc->r10 = regs[10];
	sc->r11 = regs[11];
	sc->r12 = regs[12];
	sc->r13 = regs[13];
	sc->r14 = regs[14];
	sc->r15 = regs[15];
	sc->rip = regs[16];
	sc->eflags = regs[17];

	/* Segment registers */
	sc->cs = 0x33;  /* User code segment (64-bit) */
	sc->fs = 0;
	sc->gs = 0;

	sc->oldmask = *oldmask;
	sc->trapno = 0;
	sc->err = 0;
	sc->cr2 = 0;
	sc->fpstate = 0;

	frame->sf_signum = signo;
	frame->sf_code = 0;
	frame->sf_handler = sa->__sa_handler;

	return 0;
}

static int
nix_signal_restore_frame_x64(
	struct nix_signal_frame *frame,
	void *guest_context,
	nix_sigset_t *restored_mask,
	nix_env_t *env)
{
	uint64_t *regs = (uint64_t *)guest_context;
	struct nix_sigcontext_x64 *sc = &frame->sf_sc.x64;

	/* Restore general purpose registers */
	regs[0]  = sc->rax;
	regs[1]  = sc->rcx;
	regs[2]  = sc->rdx;
	regs[3]  = sc->rbx;
	regs[4]  = sc->rsp;
	regs[5]  = sc->rbp;
	regs[6]  = sc->rsi;
	regs[7]  = sc->rdi;
	regs[8]  = sc->r8;
	regs[9]  = sc->r9;
	regs[10] = sc->r10;
	regs[11] = sc->r11;
	regs[12] = sc->r12;
	regs[13] = sc->r13;
	regs[14] = sc->r14;
	regs[15] = sc->r15;
	regs[16] = sc->rip;
	regs[17] = sc->eflags;

	*restored_mask = sc->oldmask;

	return 0;
}

/*
 * Generic signal frame setup dispatcher
 */
int
nix_signal_setup_frame(
	nix_arch_t arch,
	void *stack_ptr,
	size_t *frame_size,
	int signo,
	struct nix_sigaction const *sa,
	nix_sigset_t *oldmask,
	void *guest_context,
	nix_env_t *env)
{
	struct nix_signal_frame *frame = (struct nix_signal_frame *)stack_ptr;
	int rc;

	if (frame == NULL || sa == NULL || guest_context == NULL) {
		nix_env_set_errno(env, EFAULT);
		return -1;
	}

	memset(frame, 0, sizeof(*frame));

	switch (arch) {
	case NIX_ARCH_ARM32:
		rc = nix_signal_setup_frame_arm32(frame, signo, sa, oldmask, guest_context, env);
		break;
	case NIX_ARCH_ARM64:
		rc = nix_signal_setup_frame_arm64(frame, signo, sa, oldmask, guest_context, env);
		break;
	case NIX_ARCH_ALPHA:
		rc = nix_signal_setup_frame_alpha(frame, signo, sa, oldmask, guest_context, env);
		break;
	case NIX_ARCH_MIPS32:
		rc = nix_signal_setup_frame_mips32(frame, signo, sa, oldmask, guest_context, env);
		break;
	case NIX_ARCH_MIPS64:
		rc = nix_signal_setup_frame_mips64(frame, signo, sa, oldmask, guest_context, env);
		break;
	case NIX_ARCH_PPC32:
		rc = nix_signal_setup_frame_ppc32(frame, signo, sa, oldmask, guest_context, env);
		break;
	case NIX_ARCH_PPC64:
		rc = nix_signal_setup_frame_ppc64(frame, signo, sa, oldmask, guest_context, env);
		break;
	case NIX_ARCH_IA64:
		rc = nix_signal_setup_frame_ia64(frame, signo, sa, oldmask, guest_context, env);
		break;
	case NIX_ARCH_I386:
		rc = nix_signal_setup_frame_i386(frame, signo, sa, oldmask, guest_context, env);
		break;
	case NIX_ARCH_X64:
		rc = nix_signal_setup_frame_x64(frame, signo, sa, oldmask, guest_context, env);
		break;
	default:
		XEC_LOG(g_nix_log, XEC_LOG_ERROR, 0, "Unsupported architecture: %d", arch);
		nix_env_set_errno(env, ENOSYS);
		return -1;
	}

	if (rc == 0 && frame_size != NULL) {
		*frame_size = sizeof(struct nix_signal_frame);
	}

	return rc;
}

/*
 * Generic signal frame restore dispatcher
 */
int
nix_signal_restore_frame(
	nix_arch_t arch,
	void *stack_ptr,
	void *guest_context,
	nix_sigset_t *restored_mask,
	nix_env_t *env)
{
	struct nix_signal_frame *frame = (struct nix_signal_frame *)stack_ptr;

	if (frame == NULL || guest_context == NULL) {
		nix_env_set_errno(env, EFAULT);
		return -1;
	}

	switch (arch) {
	case NIX_ARCH_ARM32:
		return nix_signal_restore_frame_arm32(frame, guest_context, restored_mask, env);
	case NIX_ARCH_ARM64:
		return nix_signal_restore_frame_arm64(frame, guest_context, restored_mask, env);
	case NIX_ARCH_ALPHA:
		return nix_signal_restore_frame_alpha(frame, guest_context, restored_mask, env);
	case NIX_ARCH_MIPS32:
		return nix_signal_restore_frame_mips32(frame, guest_context, restored_mask, env);
	case NIX_ARCH_MIPS64:
		return nix_signal_restore_frame_mips64(frame, guest_context, restored_mask, env);
	case NIX_ARCH_PPC32:
		return nix_signal_restore_frame_ppc32(frame, guest_context, restored_mask, env);
	case NIX_ARCH_PPC64:
		return nix_signal_restore_frame_ppc64(frame, guest_context, restored_mask, env);
	case NIX_ARCH_IA64:
		return nix_signal_restore_frame_ia64(frame, guest_context, restored_mask, env);
	case NIX_ARCH_I386:
		return nix_signal_restore_frame_i386(frame, guest_context, restored_mask, env);
	case NIX_ARCH_X64:
		return nix_signal_restore_frame_x64(frame, guest_context, restored_mask, env);
	default:
		nix_env_set_errno(env, ENOSYS);
		return -1;
	}
}

/*
 * Generate architecture-specific sigreturn trampoline code
 */
int
nix_signal_gen_trampoline(
	nix_arch_t arch,
	struct nix_signal_trampoline *tramp,
	int sigreturn_syscall_nr)
{
	if (tramp == NULL)
		return -1;

	memset(tramp, 0, sizeof(*tramp));
	tramp->sigreturn_nr = sigreturn_syscall_nr;

	switch (arch) {
	case NIX_ARCH_ARM32:
		/* ARM32: mov r7, #sigreturn_nr; swi 0 */
		tramp->code[0] = 0x07;  /* mov r7, #sigreturn_nr (partial) */
		tramp->code[1] = 0x70;
		tramp->code[2] = 0xa0 + (sigreturn_syscall_nr & 0xf);
		tramp->code[3] = 0xe3;
		tramp->code[4] = 0x00;  /* swi 0 */
		tramp->code[5] = 0x00;
		tramp->code[6] = 0x00;
		tramp->code[7] = 0xef;
		break;

	case NIX_ARCH_ARM64:
		/* ARM64: mov x8, #sigreturn_nr; svc #0 */
		tramp->code[0] = (sigreturn_syscall_nr & 0xff);
		tramp->code[1] = ((sigreturn_syscall_nr >> 8) & 0xff);
		tramp->code[2] = 0x80;
		tramp->code[3] = 0xd2;  /* mov x8, #imm */
		tramp->code[4] = 0x01;
		tramp->code[5] = 0x00;
		tramp->code[6] = 0x00;
		tramp->code[7] = 0xd4;  /* svc #0 */
		break;

	case NIX_ARCH_MIPS32:
	case NIX_ARCH_MIPS64:
		/* MIPS: li $v0, sigreturn_nr; syscall */
		tramp->code[0] = 0x02;  /* addiu/li $v0, $zero, sigreturn_nr */
		tramp->code[1] = 0x10;
		tramp->code[2] = (sigreturn_syscall_nr & 0xff);
		tramp->code[3] = ((sigreturn_syscall_nr >> 8) & 0xff);
		tramp->code[4] = 0x0c;  /* syscall */
		tramp->code[5] = 0x00;
		tramp->code[6] = 0x00;
		tramp->code[7] = 0x00;
		break;

	case NIX_ARCH_I386:
		/* i386: mov eax, sigreturn_nr; int 0x80 */
		tramp->code[0] = 0xb8;  /* mov eax, imm32 */
		tramp->code[1] = (sigreturn_syscall_nr & 0xff);
		tramp->code[2] = ((sigreturn_syscall_nr >> 8) & 0xff);
		tramp->code[3] = ((sigreturn_syscall_nr >> 16) & 0xff);
		tramp->code[4] = ((sigreturn_syscall_nr >> 24) & 0xff);
		tramp->code[5] = 0xcd;  /* int 0x80 */
		tramp->code[6] = 0x80;
		break;

	case NIX_ARCH_X64:
		/* x86-64: mov rax, sigreturn_nr; syscall */
		tramp->code[0] = 0x48;  /* mov rax, imm32 (sign-extended) */
		tramp->code[1] = 0xc7;
		tramp->code[2] = 0xc0;
		tramp->code[3] = (sigreturn_syscall_nr & 0xff);
		tramp->code[4] = ((sigreturn_syscall_nr >> 8) & 0xff);
		tramp->code[5] = ((sigreturn_syscall_nr >> 16) & 0xff);
		tramp->code[6] = ((sigreturn_syscall_nr >> 24) & 0xff);
		tramp->code[7] = 0x0f;  /* syscall */
		tramp->code[8] = 0x05;
		break;

	case NIX_ARCH_PPC32:
	case NIX_ARCH_PPC64:
		/* PowerPC: li r0, sigreturn_nr; sc */
		tramp->code[0] = 0x38;  /* li r0, sigreturn_nr */
		tramp->code[1] = 0x00;
		tramp->code[2] = (sigreturn_syscall_nr >> 8) & 0xff;
		tramp->code[3] = sigreturn_syscall_nr & 0xff;
		tramp->code[4] = 0x44;  /* sc */
		tramp->code[5] = 0x00;
		tramp->code[6] = 0x00;
		tramp->code[7] = 0x02;
		break;

	case NIX_ARCH_ALPHA:
		/* Alpha: lda $0, sigreturn_nr; callsys */
		tramp->code[0] = sigreturn_syscall_nr & 0xff;
		tramp->code[1] = (sigreturn_syscall_nr >> 8) & 0xff;
		tramp->code[2] = 0x1f;
		tramp->code[3] = 0x20;  /* lda $0, imm($31) */
		tramp->code[4] = 0x83;  /* callsys */
		tramp->code[5] = 0x00;
		tramp->code[6] = 0x00;
		tramp->code[7] = 0x00;
		break;

	case NIX_ARCH_IA64:
		/* IA-64: Complex bundle-based encoding - simplified */
		memset(tramp->code, 0, 16);
		tramp->code[0] = 0x00;  /* Placeholder for break 0 */
		tramp->code[1] = 0x00;
		tramp->code[2] = 0x00;
		tramp->code[3] = 0x00;
		tramp->code[4] = 0x1d;  /* break instruction */
		break;

	default:
		return -1;
	}

	return 0;
}

/*
 * Signal delivery - sets up signal frame and redirects execution
 */
int
nix_signal_deliver(
	nix_arch_t arch,
	int signo,
	void *guest_context,
	nix_env_t *env)
{
	/* This would integrate with the emulator's execution loop */
	/* For now, just a placeholder */
	XEC_LOG(g_nix_log, XEC_LOG_DEBUG, 0, "Signal %d delivered (arch=%d)", signo, arch);
	return 0;
}

/*
 * Check for pending signals and deliver if appropriate
 */
int
nix_signal_check_pending(
	nix_arch_t arch,
	void *guest_context,
	nix_env_t *env)
{
	/* This would check g_sigpending and deliver any unmasked signals */
	/* Placeholder for now */
	return 0;
}
