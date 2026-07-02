/*
 * nbsd101-personality.c -- the NetBSD/m88k personality behind the generic
 * nix_personality_t plugin seam.  This is where ALL the NetBSD/m88k ABI
 * knowledge lives: the entry (uframe) stack layout, the brk(2) heap model,
 * and the mapping of the host register file onto an m88k_context_t that the
 * nix_us_syscall_dispatch machinery drives.  The host (lcx) knows none of it.
 */
#include "nix-personality.h"
#include "nix-syscall.h"
#include "nix.h"
#include "m88k-context.h"
#include "LibCPU/LcLog.h"

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Built in nbsd101-init.c. */
extern nix_us_syscall_if_t *nbsd101_us_syscall_create(nix_mem_if_t *memif);

/* m88k PSR carry (C) bit -- the BSD kernel's system-call error flag. */
#define NBSD101_PSR_CARRY (UINT32_C(1) << 28)

/* m88k register numbers this ABI assigns meaning to. */
enum {
	NBSD101_R_LINK = 1, /* r1  return/link */
	NBSD101_R_RET = 2,  /* r2  first arg / first return word */
	NBSD101_R_RET2 = 3, /* r3  second return word (dword split) */
	NBSD101_R_SP = 31,  /* r31 stack pointer */
	NBSD101_R_SCNO = 13 /* r13 system-call number */
};

/* NetBSD/m88k system-call numbers the adapter must service itself, because
 * the host has no page tables (brk) or should stop the loop (exit). */
enum { NBSD101_SYS_EXIT = 1,
       NBSD101_SYS_OBREAK = 17 };

typedef struct _nbsd101_personality {
	struct _nix_personality_vtbl const *vtbl;
	nix_us_syscall_if_t                *us;
	nix_monitor_t                      *monitor;
	nix_guest_info_t                    guest_info;
	m88k_context_t                      ctx;
	nix_mem_if_t                       *mem;
	uint64_t                            ram_size;
	uint64_t                            brk_base;
	uint64_t                            brk_cur;
	int                                 inited;
} nbsd101_personality_t;

/* Big-endian 32-bit store into guest memory at gaddr. */
static void
nbsd101_wbe32(nix_mem_if_t *mem, uint64_t gaddr, uint32_t v)
{
	nix_memflg_t mf = 0;
	uint8_t     *h = (uint8_t *)nix_mem_gtoh(mem, gaddr, &mf);

	if (mf != 0 || h == NULL)
		return;

	h[0] = (uint8_t)(v >> 24);
	h[1] = (uint8_t)(v >> 16);
	h[2] = (uint8_t)(v >> 8);
	h[3] = (uint8_t)v;
}

static void
nbsd101_ensure_init(nbsd101_personality_t *self, nix_cpu_if_t *cpu)
{
	if (self->inited)
		return;

	self->mem = cpu->mem;
	self->ram_size = cpu->ram_size;
	self->us = nbsd101_us_syscall_create(cpu->mem);

	self->guest_info.name = "NetBSD/m88k";
	self->guest_info.endian = NIX_ENDIAN_BIG;
	self->guest_info.byte_size = 8;
	self->guest_info.word_size = 32;
	self->guest_info.page_size = 4096;

	self->monitor = nix_monitor_create(&self->guest_info, cpu->mem,
	                                   &self->ctx, NULL);
	self->inited = 1;
}

/*
 * Lay out the user-space entry stack ("uframe") near the top of guest RAM:
 * argc, the argv[] pointer vector (NULL-terminated), the envp[] pointer
 * vector (NULL-terminated), and the argument/environment strings packed
 * above the vectors.  argc and every guest pointer are big-endian 32-bit.
 * r31 (SP) is left pointing at argc, exactly where m88k crt0 expects it.
 */
static void
nbsd101_setup(nix_personality_t *base, nix_cpu_if_t *cpu,
              char const *const *argv, size_t argc, char *const *envp,
              uint64_t brk_base)
{
	nbsd101_personality_t *self = (nbsd101_personality_t *)base;
	size_t                 nenv = 0;
	uint64_t               p, sp, a;
	size_t                 i;
	uint64_t              *arg_addr;
	uint64_t              *env_addr;
	uint64_t               nwords;

	nbsd101_ensure_init(self, cpu);
	self->brk_base = self->brk_cur = brk_base;

	for (char *const *e = envp; e != NULL && *e != NULL; ++e)
		nenv++;

	arg_addr = (uint64_t *)nix_alloc_ntype(uint64_t, argc + 1, 0);
	env_addr = (uint64_t *)nix_alloc_ntype(uint64_t, nenv + 1, 0);
	if (arg_addr == NULL || env_addr == NULL) {
		nix_free(arg_addr);
		nix_free(env_addr);
		return;
	}

	/* Pack strings downward from a 16-aligned point just below top of RAM. */
	p = (self->ram_size - 16) & ~(uint64_t)15;
	for (i = 0; i < argc; i++) {
		size_t       len = strlen(argv[i]) + 1;
		nix_memflg_t mf = 0;
		uint8_t     *h;

		p -= len;
		h = (uint8_t *)nix_mem_gtoh(self->mem, p, &mf);
		if (mf == 0 && h != NULL)
			memcpy(h, argv[i], len);
		arg_addr[i] = p;
	}
	for (i = 0; i < nenv; i++) {
		size_t       len = strlen(envp[i]) + 1;
		nix_memflg_t mf = 0;
		uint8_t     *h;

		p -= len;
		h = (uint8_t *)nix_mem_gtoh(self->mem, p, &mf);
		if (mf == 0 && h != NULL)
			memcpy(h, envp[i], len);
		env_addr[i] = p;
	}

	/* The pointer vector sits below the strings, SP 16-aligned. */
	nwords = 1 + argc + 1 + nenv + 1;
	sp = (p - nwords * 4) & ~(uint64_t)15;
	a = sp;
	nbsd101_wbe32(self->mem, a, (uint32_t)argc);
	a += 4;
	for (i = 0; i < argc; i++, a += 4)
		nbsd101_wbe32(self->mem, a, (uint32_t)arg_addr[i]);
	nbsd101_wbe32(self->mem, a, 0);
	a += 4;
	for (i = 0; i < nenv; i++, a += 4)
		nbsd101_wbe32(self->mem, a, (uint32_t)env_addr[i]);
	nbsd101_wbe32(self->mem, a, 0);

	nix_free(arg_addr);
	nix_free(env_addr);

	nix_cpu_reg_set(cpu, NBSD101_R_SP, sp);  /* r31 -> argc */
	nix_cpu_reg_set(cpu, NBSD101_R_LINK, 0); /* crt0 calls exit(); link unused */
}

static int
nbsd101_syscall(nix_personality_t *base, nix_cpu_if_t *cpu, uint64_t vector)
{
	nbsd101_personality_t *self = (nbsd101_personality_t *)base;
	unsigned               n;
	int                    scno;

	(void)vector; /* m88k passes the call number in r13, not via the vector */

	nbsd101_ensure_init(self, cpu);

	/* Mirror the host register file into the m88k context the dispatch
	 * machinery reads. */
	for (n = 0; n < 32; n++)
		self->ctx.gpr[n] = (uint32_t)nix_cpu_reg_get(cpu, n);
	self->ctx.psr = 0;

	scno = (int)self->ctx.gpr[NBSD101_R_SCNO];

	if (getenv("LCX_STRACE") != NULL)
		fprintf(stderr, "lcx strace: r13=%d (%#x, %#x, %#x)\n", scno,
		        self->ctx.gpr[NBSD101_R_RET], self->ctx.gpr[3],
		        self->ctx.gpr[4]);

	if (scno == NBSD101_SYS_EXIT)
		return (0); /* stop the guest */

	if (scno == NBSD101_SYS_OBREAK) {
		/* A flat-RAM model has no page tables; brk just bumps a pointer
		 * and validates it against RAM. */
		uint64_t addr = self->ctx.gpr[NBSD101_R_RET];

		if (addr == 0) {
			nix_cpu_reg_set(cpu, NBSD101_R_RET, self->brk_cur);
			nix_cpu_set_carry(cpu, 0);
		} else if (addr < self->brk_base || addr >= self->ram_size - 0x10000) {
			nix_cpu_reg_set(cpu, NBSD101_R_RET, (uint64_t)(uint32_t)ENOMEM);
			nix_cpu_set_carry(cpu, 1);
		} else {
			self->brk_cur = addr;
			nix_cpu_reg_set(cpu, NBSD101_R_RET, addr);
			nix_cpu_set_carry(cpu, 0);
		}
		return (1);
	}

	nix_us_syscall_dispatch(self->us, self->monitor);

	/* set_result wrote r2 (and r3 for dword returns) and, on error, the
	 * PSR carry -- propagate both back to the host CPU. */
	nix_cpu_reg_set(cpu, NBSD101_R_RET, self->ctx.gpr[NBSD101_R_RET]);
	nix_cpu_reg_set(cpu, NBSD101_R_RET2, self->ctx.gpr[NBSD101_R_RET2]);
	nix_cpu_set_carry(cpu, (self->ctx.psr & NBSD101_PSR_CARRY) ? 1 : 0);
	return (1);
}

static void
nbsd101_destroy(nix_personality_t *base)
{
	nix_free(base);
}

static struct _nix_personality_vtbl const nbsd101_personality_vtbl = {
    nbsd101_setup,
    nbsd101_syscall,
    nbsd101_destroy};

nix_personality_t *
nix_personality_create(void)
{
	nbsd101_personality_t *self =
	    (nbsd101_personality_t *)nix_alloc_type(nbsd101_personality_t, 0);

	if (self != NULL)
		self->vtbl = &nbsd101_personality_vtbl;

	return (nix_personality_t *)self;
}
