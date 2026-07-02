/*
 * pdp11unix-personality.c -- the classic PDP-11 UNIX (V6/V7 lineage) syscall
 * personality behind the generic nix_personality_t plugin seam.  All the
 * PDP-11 `sys`-trap ABI lives here: the call number and inline arguments are
 * encoded in the instruction stream (not registers), so this reads them
 * relative to the trap-resume PC and advances that PC past the inline args --
 * exactly as the kernel does.  Results come back in r0, with the carry flag
 * signalling an error (errno in r0).  The host (lcx) knows none of it.
 */
#include "nix-personality.h"
#include "nix.h"
#include "LibCPU/LcLog.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TRAP vector 034 == 0x1c: the gate a UNIX `sys` instruction traps through. */
#define PDP11_SYS_VECTOR 0x1c

/* PDP-11 register numbers this ABI assigns meaning to (r0..r7; r6=sp, r7=pc). */
enum { PDP11_R_RESULT = 0,
       PDP11_R_SP = 6 };

/*
 * The V7 system-call table (number -> name + inline argument count).  The
 * shared V6 core matches it; the later PDP-11 systems (System III, PWB,
 * 2.xBSD, Ultrix-11, Venix) extend it.  One table drives the common core.
 */
typedef struct {
	char const *name;
	int         nargs;
} pdp11_sysent_t;

static pdp11_sysent_t const g_pdp11_table[] = {
    {"indir", 0}, {"exit", 1}, {"fork", 0}, {"read", 3}, {"write", 3}, {"open", 2}, {"close", 1}, {"wait", 0}, {"creat", 2}, {"link", 2}, {"unlink", 1}, {"exec", 2}, {"chdir", 1}, {"time", 0}, {"mknod", 3}, {"chmod", 2}, {"chown", 3}, {"break", 1}, {"stat", 2}, {"lseek", 3}, {"getpid", 0}, {"mount", 3}, {"umount", 1}, {"setuid", 1}, {"getuid", 0}, {"stime", 1}, {"ptrace", 4}, {"alarm", 1}, {"fstat", 2}, {"pause", 0}, {"utime", 2}, {"", 0}, {"", 0}, {"access", 2}, {"nice", 1}, {"ftime", 1}, {"sync", 0}, {"kill", 2}, {"", 0}, {"", 0}, {"", 0}, {"dup", 1}, {"pipe", 0}, {"times", 1}, {"profil", 4}, {"", 0}, {"setgid", 1}, {"getgid", 0}, {"signal", 2}};

#define PDP11_NSYS (sizeof(g_pdp11_table) / sizeof(g_pdp11_table[0]))

typedef struct _pdp11_personality {
	struct _nix_personality_vtbl const *vtbl;
	nix_env_t                          *env;
	int                                 inited;
} pdp11_personality_t;

static void
pdp11_ensure_init(pdp11_personality_t *self)
{
	if (!self->inited) {
		self->env = nix_env_create(NULL);
		self->inited = 1;
	}
}

/* Host base of the guest's flat RAM, plus its size. */
static uint8_t *
pdp11_ram(nix_cpu_if_t *cpu, uint64_t *psize)
{
	nix_memflg_t mf = 0;

	*psize = cpu->ram_size;
	return (uint8_t *)nix_mem_gtoh(cpu->mem, 0, &mf);
}

/* Little-endian 16-bit fetch from guest RAM. */
static uint32_t
pdp11_m16(uint8_t const *ram, uint64_t size, uint64_t a)
{
	return (a + 1 < size) ? (uint32_t)(ram[a] | (ram[a + 1] << 8)) : 0;
}

/* Little-endian 16-bit store into guest RAM. */
static void
pdp11_w16(uint8_t *ram, uint64_t size, uint64_t a, uint32_t v)
{
	if (a + 1 < size) {
		ram[a] = (uint8_t)v;
		ram[a + 1] = (uint8_t)(v >> 8);
	}
}

/*
 * PDP-11 UNIX process entry: the stack (r6) points at argc, followed by the
 * argv[] and envp[] pointer vectors (each NULL-terminated) with the argument
 * and environment strings packed above.  argc and every guest pointer are
 * little-endian 16-bit.
 */
static void
pdp11_setup(nix_personality_t *base, nix_cpu_if_t *cpu, char const *const *argv,
            size_t argc, char *const *envp, uint64_t brk_base)
{
	pdp11_personality_t *self = (pdp11_personality_t *)base;
	uint64_t             size;
	uint8_t             *ram = pdp11_ram(cpu, &size);
	uint64_t             p, sp, a;
	size_t               i, nenv = 0;
	uint64_t            *arg_addr;
	uint64_t            *env_addr;

	(void)brk_base; /* the classic break(2) heap model is not modelled here */
	pdp11_ensure_init(self);
	if (ram == NULL)
		return;

	for (char *const *e = envp; e != NULL && *e != NULL; ++e)
		nenv++;

	arg_addr = (uint64_t *)nix_alloc_ntype(uint64_t, argc + 1, 0);
	env_addr = (uint64_t *)nix_alloc_ntype(uint64_t, nenv + 1, 0);
	if (arg_addr == NULL || env_addr == NULL) {
		nix_free(arg_addr);
		nix_free(env_addr);
		return;
	}

	/* Pack strings downward from a word-aligned point just below top of RAM. */
	p = (size - 2) & ~(uint64_t)1;
	for (i = 0; i < argc; i++) {
		size_t len = strlen(argv[i]) + 1;

		p -= len;
		if (p + len <= size)
			memcpy(ram + p, argv[i], len);
		arg_addr[i] = p;
	}
	for (i = 0; i < nenv; i++) {
		size_t len = strlen(envp[i]) + 1;

		p -= len;
		if (p + len <= size)
			memcpy(ram + p, envp[i], len);
		env_addr[i] = p;
	}

	/* The pointer vector sits below the strings; the stack is word-aligned. */
	sp = (p - (1 + argc + 1 + nenv + 1) * 2) & ~(uint64_t)1;
	a = sp;
	pdp11_w16(ram, size, a, (uint32_t)argc);
	a += 2;
	for (i = 0; i < argc; i++, a += 2)
		pdp11_w16(ram, size, a, (uint32_t)arg_addr[i]);
	pdp11_w16(ram, size, a, 0);
	a += 2;
	for (i = 0; i < nenv; i++, a += 2)
		pdp11_w16(ram, size, a, (uint32_t)env_addr[i]);
	pdp11_w16(ram, size, a, 0);

	nix_free(arg_addr);
	nix_free(env_addr);

	nix_cpu_reg_set(cpu, PDP11_R_SP, sp); /* r6 -> argc */
}

static int
pdp11_ok(nix_cpu_if_t *cpu, uint64_t v)
{
	nix_cpu_reg_set(cpu, PDP11_R_RESULT, v & 0xffff);
	nix_cpu_set_carry(cpu, 0);
	return (1);
}

static int
pdp11_fail(nix_cpu_if_t *cpu, nix_env_t *env)
{
	nix_cpu_reg_set(cpu, PDP11_R_RESULT,
	                (uint64_t)(uint32_t)nix_env_get_errno(env) & 0xffff);
	nix_cpu_set_carry(cpu, 1);
	return (1);
}

static int
pdp11_syscall(nix_personality_t *base, nix_cpu_if_t *cpu, uint64_t vector)
{
	pdp11_personality_t *self = (pdp11_personality_t *)base;
	uint64_t             size;
	uint8_t             *ram = pdp11_ram(cpu, &size);
	nix_env_t           *env;
	uint64_t             resume, argbase;
	uint32_t             num;
	int                  nargs, inline_args, i;
	uint64_t             a[6] = {0};

	/* Only the `sys` gate is a system call; any other trap (e.g. HALT) stops. */
	if (vector != PDP11_SYS_VECTOR || ram == NULL)
		return (0);

	pdp11_ensure_init(self);
	env = self->env;

	resume = nix_cpu_pc_get(cpu);                  /* PC just past the 2-byte trap */
	num = pdp11_m16(ram, size, resume - 2) & 0xff; /* trap low byte = call */
	if (num == 0) {                                /* indir: operand -> [sys N|args] */
		uint64_t blk = pdp11_m16(ram, size, resume);
		nix_cpu_pc_set(cpu, resume + 2);
		num = pdp11_m16(ram, size, blk) & 0xff;
		argbase = blk + 2;
		inline_args = 0;
	} else {
		argbase = resume;
		inline_args = 1;
	}
	nargs = (num < PDP11_NSYS) ? g_pdp11_table[num].nargs : 0;
	for (i = 0; i < nargs && i < 6; i++)
		a[i] = pdp11_m16(ram, size, argbase + 2 * (uint64_t)i);
	if (inline_args)
		nix_cpu_pc_set(cpu, resume + 2 * (uint64_t)nargs);

	if (getenv("LCX_STRACE") != NULL)
		fprintf(stderr, "lcx pdp11 sys %u (%s) %#llx %#llx %#llx\n", num,
		        (num < PDP11_NSYS && g_pdp11_table[num].name[0])
		            ? g_pdp11_table[num].name
		            : "?",
		        (unsigned long long)a[0], (unsigned long long)a[1],
		        (unsigned long long)a[2]);

	nix_env_set_errno(env, 0);
	switch (num) {
	case 1:
		return (0); /* exit -> stop the guest */
	case 3: {           /* read(fd, buf, n) */
		nix_ssize_t r;
		if (a[1] + a[2] > size)
			return pdp11_fail(cpu, env);
		r = nix_read((int)a[0], ram + a[1], (size_t)a[2], env);
		return (r < 0) ? pdp11_fail(cpu, env) : pdp11_ok(cpu, (uint64_t)r);
	}
	case 4: { /* write(fd, buf, n) */
		nix_ssize_t r;
		if (a[1] + a[2] > size)
			return pdp11_fail(cpu, env);
		r = nix_write((int)a[0], ram + a[1], (size_t)a[2], env);
		return (r < 0) ? pdp11_fail(cpu, env) : pdp11_ok(cpu, (uint64_t)r);
	}
	case 5: { /* open(path, flags, mode) */
		int fd;
		if (a[0] >= size)
			return pdp11_fail(cpu, env);
		fd = nix_open((char const *)(ram + a[0]), (int)a[1], (int)a[2], env);
		return (fd < 0) ? pdp11_fail(cpu, env) : pdp11_ok(cpu, (uint64_t)fd);
	}
	case 6: {
		int r = nix_close((int)a[0], env);
		return (r < 0) ? pdp11_fail(cpu, env) : pdp11_ok(cpu, 0);
	}
	case 8: { /* creat(path, mode) */
		int fd;
		if (a[0] >= size)
			return pdp11_fail(cpu, env);
		fd = nix_creat((char const *)(ram + a[0]), (int)a[1], env);
		return (fd < 0) ? pdp11_fail(cpu, env) : pdp11_ok(cpu, (uint64_t)fd);
	}
	case 10: {
		int r;
		if (a[0] >= size)
			return pdp11_fail(cpu, env);
		r = nix_unlink((char const *)(ram + a[0]), env);
		return (r < 0) ? pdp11_fail(cpu, env) : pdp11_ok(cpu, 0);
	}
	case 12: {
		int r;
		if (a[0] >= size)
			return pdp11_fail(cpu, env);
		r = nix_chdir((char const *)(ram + a[0]), env);
		return (r < 0) ? pdp11_fail(cpu, env) : pdp11_ok(cpu, 0);
	}
	case 19: { /* lseek(fd, off, whence) */
		nix_off_t off = nix_lseek((int)a[0], (nix_off_t)(int16_t)a[1],
		                          (int)a[2], env);
		return (off < 0) ? pdp11_fail(cpu, env) : pdp11_ok(cpu, (uint64_t)off);
	}
	case 20:
		return pdp11_ok(cpu, (uint64_t)nix_getpid(env));
	case 24:
		return pdp11_ok(cpu, (uint64_t)nix_getuid(env));
	case 41: {
		int fd = nix_dup((int)a[0], env);
		return (fd < 0) ? pdp11_fail(cpu, env) : pdp11_ok(cpu, (uint64_t)fd);
	}
	default:
		fprintf(stderr, "lcx: unhandled pdp11 unix syscall %u (%s)\n", num,
		        (num < PDP11_NSYS && g_pdp11_table[num].name[0])
		            ? g_pdp11_table[num].name
		            : "?");
		return pdp11_fail(cpu, env);
	}
}

static void
pdp11_destroy(nix_personality_t *base)
{
	nix_free(base);
}

static struct _nix_personality_vtbl const pdp11_personality_vtbl = {
    pdp11_setup, pdp11_syscall, pdp11_destroy};

nix_personality_t *
nix_personality_create(nix_version_t target)
{
	pdp11_personality_t *self =
	    (pdp11_personality_t *)nix_alloc_type(pdp11_personality_t, 0);

	/* The classic PDP-11 UNIX lineage (V6/V7/2BSD/Venix) is not version-gated in this work; the
	 * hand-written switch dispatch does not consult the table's since/until ranges. */
	(void)target;

	if (self != NULL)
		self->vtbl = &pdp11_personality_vtbl;

	return (nix_personality_t *)self;
}
