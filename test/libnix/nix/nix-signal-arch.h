/*
 * nix-signal-arch.h
 *
 * Architecture-specific signal context definitions for guest OS emulation.
 * Supports: ARM32, ARM64, Alpha, MIPS32, MIPS64, PowerPC, IA-64, i386, x86-64
 *
 * These structures match the signal frame layouts used by various Unix systems
 * (BSD, Linux, etc.) to enable proper signal delivery and sigreturn handling.
 */

#ifndef __nix_signal_arch_h
#define __nix_signal_arch_h

#include "nix-types.h"
#include "nix-signal.h"

/*
 * ARM 32-bit Signal Context
 * Used by ARM Linux and BSD systems
 */
struct nix_sigcontext_arm32 {
	uint32_t	trap_no;
	uint32_t	error_code;
	uint32_t	oldmask;
	uint32_t	r0;
	uint32_t	r1;
	uint32_t	r2;
	uint32_t	r3;
	uint32_t	r4;
	uint32_t	r5;
	uint32_t	r6;
	uint32_t	r7;
	uint32_t	r8;
	uint32_t	r9;
	uint32_t	r10;
	uint32_t	r11;		/* fp */
	uint32_t	r12;		/* ip */
	uint32_t	r13;		/* sp */
	uint32_t	r14;		/* lr */
	uint32_t	r15;		/* pc */
	uint32_t	cpsr;
	uint32_t	fault_address;
};

/*
 * ARM 64-bit Signal Context (AArch64)
 * Used by ARM64 Linux
 */
struct nix_sigcontext_arm64 {
	uint64_t	fault_address;
	uint64_t	regs[31];		/* x0-x30 */
	uint64_t	sp;
	uint64_t	pc;
	uint64_t	pstate;
	uint8_t		__reserved[4096];	/* FP/SIMD state */
};

/*
 * Alpha Signal Context
 * Used by Alpha Linux and OSF/1
 */
struct nix_sigcontext_alpha {
	int64_t		sc_onstack;
	int64_t		sc_mask;
	int64_t		sc_pc;
	int64_t		sc_ps;
	int64_t		sc_regs[32];	/* $0-$31 */
	int64_t		sc_ownedfp;
	int64_t		sc_fpregs[32];	/* $f0-$f31 */
	uint64_t	sc_fpcr;
	uint64_t	sc_fp_control;
	uint64_t	sc_reserved1;
	uint64_t	sc_reserved2;
	uint64_t	sc_ssize;
	char		*sc_sbase;
	uint64_t	sc_traparg_a0;
	uint64_t	sc_traparg_a1;
	uint64_t	sc_traparg_a2;
	uint64_t	sc_fp_trap_pc;
	uint64_t	sc_fp_trigger_sum;
	uint64_t	sc_fp_trigger_inst;
};

/*
 * MIPS 32-bit Signal Context
 * Used by MIPS Linux and BSD systems
 */
struct nix_sigcontext_mips32 {
	uint32_t	sc_regmask;
	uint32_t	sc_status;
	uint64_t	sc_pc;
	uint64_t	sc_regs[32];
	uint64_t	sc_fpregs[32];
	uint32_t	sc_acx;
	uint32_t	sc_fpc_csr;
	uint32_t	sc_fpc_eir;
	uint32_t	sc_used_math;
	uint32_t	sc_dsp;
	uint64_t	sc_mdhi;
	uint64_t	sc_mdlo;
	uint32_t	sc_hi1;
	uint32_t	sc_lo1;
	uint32_t	sc_hi2;
	uint32_t	sc_lo2;
	uint32_t	sc_hi3;
	uint32_t	sc_lo3;
};

/*
 * MIPS 64-bit Signal Context
 * Used by MIPS64 Linux
 */
struct nix_sigcontext_mips64 {
	uint64_t	sc_regs[32];
	uint64_t	sc_fpregs[32];
	uint64_t	sc_mdhi;
	uint64_t	sc_hi1;
	uint64_t	sc_hi2;
	uint64_t	sc_hi3;
	uint64_t	sc_mdlo;
	uint64_t	sc_lo1;
	uint64_t	sc_lo2;
	uint64_t	sc_lo3;
	uint64_t	sc_pc;
	uint32_t	sc_fpc_csr;
	uint32_t	sc_used_math;
	uint32_t	sc_dsp;
	uint32_t	sc_reserved;
};

/*
 * PowerPC 32-bit Signal Context
 * Used by PowerPC Linux and BSD systems
 */
struct nix_sigcontext_ppc32 {
	uint32_t	sc_onstack;
	uint32_t	sc_mask;
	uint32_t	sc_frame[2];
	/* General purpose registers 0-31 */
	uint32_t	sc_gpr[32];
	uint32_t	sc_cr;
	uint32_t	sc_xer;
	uint32_t	sc_lr;
	uint32_t	sc_ctr;
	uint32_t	sc_pc;		/* srr0 */
	uint32_t	sc_msr;		/* srr1 */
	uint32_t	sc_dar;
	uint32_t	sc_dsisr;
};

/*
 * PowerPC 64-bit Signal Context
 * Used by PowerPC64 Linux
 */
struct nix_sigcontext_ppc64 {
	uint64_t	gp_regs[48];	/* Includes r0-r31, nip, msr, etc. */
	uint64_t	fp_regs[33];	/* FPR0-FPR31 + FPSCR */
	/* VMX/Altivec registers would go here */
	uint64_t	v_regs[34][2];	/* VR0-VR31 + VSCR + VRSAVE */
};

/*
 * IA-64 (Itanium) Signal Context
 * Used by IA-64 Linux
 */
struct nix_sigcontext_ia64 {
	uint64_t	sc_flags;
	uint64_t	sc_nat;
	uint64_t	sc_stack[2];	/* sc_stack.ss_sp, sc_stack.ss_size */
	uint64_t	sc_ip;		/* instruction pointer */
	uint64_t	sc_cfm;
	uint64_t	sc_um;
	uint64_t	sc_ar_rsc;
	uint64_t	sc_ar_bsp;
	uint64_t	sc_ar_rnat;
	uint64_t	sc_ar_ccv;
	uint64_t	sc_ar_unat;
	uint64_t	sc_ar_fpsr;
	uint64_t	sc_ar_pfs;
	uint64_t	sc_ar_lc;
	uint64_t	sc_ar_ec;
	uint64_t	sc_br[8];	/* Branch registers */
	uint64_t	sc_gr[32];	/* General registers r1-r31 (r0=0) */
	uint64_t	sc_fr[128][2];	/* FP registers (16 bytes each) */
	uint64_t	sc_rbs_base;
	uint64_t	sc_loadrs;
	uint64_t	sc_ar25;
	uint64_t	sc_ar26;
	uint64_t	sc_rsvd[12];
};

/*
 * i386 (x86 32-bit) Signal Context
 * Used by i386 Linux and BSD systems
 */
struct nix_sigcontext_i386 {
	uint16_t	gs, __gsh;
	uint16_t	fs, __fsh;
	uint16_t	es, __esh;
	uint16_t	ds, __dsh;
	uint32_t	edi;
	uint32_t	esi;
	uint32_t	ebp;
	uint32_t	esp;
	uint32_t	ebx;
	uint32_t	edx;
	uint32_t	ecx;
	uint32_t	eax;
	uint32_t	trapno;
	uint32_t	err;
	uint32_t	eip;
	uint16_t	cs, __csh;
	uint32_t	eflags;
	uint32_t	esp_at_signal;
	uint16_t	ss, __ssh;
	/* FPU state pointer would follow */
	uint32_t	fpstate;
	uint32_t	oldmask;
	uint32_t	cr2;
};

/*
 * x86-64 (AMD64) Signal Context
 * Used by x86-64 Linux and BSD systems
 */
struct nix_sigcontext_x64 {
	uint64_t	r8;
	uint64_t	r9;
	uint64_t	r10;
	uint64_t	r11;
	uint64_t	r12;
	uint64_t	r13;
	uint64_t	r14;
	uint64_t	r15;
	uint64_t	rdi;
	uint64_t	rsi;
	uint64_t	rbp;
	uint64_t	rbx;
	uint64_t	rdx;
	uint64_t	rax;
	uint64_t	rcx;
	uint64_t	rsp;
	uint64_t	rip;
	uint64_t	eflags;
	uint16_t	cs;
	uint16_t	gs;
	uint16_t	fs;
	uint16_t	__pad0;
	uint64_t	err;
	uint64_t	trapno;
	uint64_t	oldmask;
	uint64_t	cr2;
	/* FPU state pointer would follow */
	uint64_t	fpstate;
	uint64_t	reserved1[8];
};

/*
 * Generic BSD sigcontext (used as fallback)
 */
struct nix_bsd_sigcontext {
	int			sc_onstack;
	nix_sigset_t	sc_mask;
	uintmax_t		sc_sp;
	uintmax_t		sc_pc;
	uintmax_t		sc_ps;
	/* Architecture-specific registers follow */
};

/*
 * Generic Linux sigcontext (used as fallback)
 */
struct nix_linux_sigcontext {
	nix_sigset_t	oldmask;
	uintmax_t		pc;
	uintmax_t		sp;
	uintmax_t		pstate;
	/* Architecture-specific registers follow */
};

/*
 * Signal frame structure - what's actually pushed on the stack
 * when a signal is delivered
 */
struct nix_signal_frame {
	uint32_t		sf_signum;		/* Signal number */
	uint32_t		sf_code;		/* Signal code */
	uintptr_t		sf_scp;			/* Pointer to sigcontext */
	uintptr_t		sf_handler;		/* Handler address */
	/* Sigcontext follows here */
	union {
		struct nix_sigcontext_arm32		arm32;
		struct nix_sigcontext_arm64		arm64;
		struct nix_sigcontext_alpha		alpha;
		struct nix_sigcontext_mips32	mips32;
		struct nix_sigcontext_mips64	mips64;
		struct nix_sigcontext_ppc32		ppc32;
		struct nix_sigcontext_ppc64		ppc64;
		struct nix_sigcontext_ia64		ia64;
		struct nix_sigcontext_i386		i386;
		struct nix_sigcontext_x64		x64;
	} sf_sc;
	/* Signal info structure */
	struct nix_siginfo sf_si;
};

/*
 * Architecture IDs for signal handling
 */
typedef enum {
	NIX_ARCH_ARM32 = 0,
	NIX_ARCH_ARM64,
	NIX_ARCH_ALPHA,
	NIX_ARCH_MIPS32,
	NIX_ARCH_MIPS64,
	NIX_ARCH_PPC32,
	NIX_ARCH_PPC64,
	NIX_ARCH_IA64,
	NIX_ARCH_I386,
	NIX_ARCH_X64,
	NIX_ARCH_M68K,
	NIX_ARCH_M88K,
	NIX_ARCH_SPARC32,
	NIX_ARCH_SPARC64,
	NIX_ARCH_UNKNOWN
} nix_arch_t;

/*
 * Signal trampoline code - small piece of code on the stack
 * that calls sigreturn after the signal handler returns
 */
struct nix_signal_trampoline {
	uint8_t		code[64];		/* Machine code for sigreturn */
	uint32_t	sigreturn_nr;	/* Syscall number for sigreturn */
};

/*
 * Function prototypes
 */

/* Setup signal frame on guest stack */
int nix_signal_setup_frame(
	nix_arch_t arch,
	void *stack_ptr,
	size_t *frame_size,
	int signo,
	struct nix_sigaction const *sa,
	nix_sigset_t *oldmask,
	void *guest_context,
	nix_env_t *env);

/* Restore context from signal frame */
int nix_signal_restore_frame(
	nix_arch_t arch,
	void *stack_ptr,
	void *guest_context,
	nix_sigset_t *restored_mask,
	nix_env_t *env);

/* Generate architecture-specific signal trampoline */
int nix_signal_gen_trampoline(
	nix_arch_t arch,
	struct nix_signal_trampoline *tramp,
	int sigreturn_syscall_nr);

/* Deliver a pending signal to guest */
int nix_signal_deliver(
	nix_arch_t arch,
	int signo,
	void *guest_context,
	nix_env_t *env);

/* Check and deliver pending signals */
int nix_signal_check_pending(
	nix_arch_t arch,
	void *guest_context,
	nix_env_t *env);

#endif  /* !__nix_signal_arch_h */
