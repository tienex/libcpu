/*
 * nix-personality.h -- the host <-> guest-OS-personality plugin seam.
 *
 * A personality dylib (libobsd79, libnbsd101, ...) implements one guest
 * operating-system ABI: how a process is entered (argv/envp/stack + initial
 * registers + heap base) and how its system-call trap is serviced.  The host
 * (lcx) drives the personality through this interface WITHOUT knowing anything
 * about the guest CPU or OS, and the personality drives the host CPU through
 * nix_cpu_if_t WITHOUT knowing anything about the host's CPU-state layout.
 *
 * The register file is addressed by the guest ABI's own numbering (e.g. m88k
 * r13 = system-call number); only the personality assigns meaning to indices.
 */
#ifndef __nix_personality_h
#define __nix_personality_h

#include "nix-host.h"    /* nix_mem_if_t + the base integer types */
#include "nix-version.h" /* nix_version_t -- the resolved guest-OS version */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the host CPU, as the personality sees it -------------------------- */

typedef struct _nix_cpu_if nix_cpu_if_t;

struct _nix_cpu_if_vtbl {
	uint64_t (*reg_get)(nix_cpu_if_t *self, unsigned n);
	void (*reg_set)(nix_cpu_if_t *self, unsigned n, uint64_t value);
	/*
	 * Drive the host's carry/error flag.  The BSD kernel ABI signals a
	 * failed system call by setting the carry flag (with errno in the
	 * return register); libc's cerror stub branches on it.
	 */
	void (*set_carry)(nix_cpu_if_t *self, int set);
	/*
	 * The trap-resume program counter (the address the host will resume at
	 * after this trap).  A personality whose call number and arguments are
	 * encoded in the instruction stream (e.g. the PDP-11 `sys` trap) reads
	 * them relative to pc_get() and advances it with pc_set() past the
	 * inline arguments.  Register-passing ABIs (m88k) leave it untouched.
	 */
	uint64_t (*pc_get)(nix_cpu_if_t *self);
	void (*pc_set)(nix_cpu_if_t *self, uint64_t pc);
};

struct _nix_cpu_if {
	struct _nix_cpu_if_vtbl const *vtbl;
	nix_mem_if_t                  *mem;      /* guest memory (flat or mapped) */
	uint64_t                       ram_size; /* guest RAM size, in bytes */
};

#define nix_cpu_reg_get(self, n)     (self)->vtbl->reg_get((self), (n))
#define nix_cpu_reg_set(self, n, v)  (self)->vtbl->reg_set((self), (n), (v))
#define nix_cpu_set_carry(self, set) (self)->vtbl->set_carry((self), (set))
#define nix_cpu_pc_get(self)         (self)->vtbl->pc_get((self))
#define nix_cpu_pc_set(self, pc)     (self)->vtbl->pc_set((self), (pc))

/* ---- the personality --------------------------------------------------- */

typedef struct _nix_personality nix_personality_t;

struct _nix_personality_vtbl {
	/*
	 * Establish the initial process state: lay out the argv/envp entry
	 * stack in guest memory, set the entry registers (stack pointer, ...),
	 * and record the heap base (end of bss, page-aligned) for brk(2).
	 */
	void (*setup)(nix_personality_t *self, nix_cpu_if_t *cpu,
	              char const *const *argv, size_t argc, char *const *envp,
	              uint64_t brk_base);

	/* Service one trap taken through `vector` (the guest's trap/gate number).
	 * Returns 0 to stop the guest (the process called exit, or the trap is
	 * not this personality's system-call gate -- e.g. a HALT), 1 to resume
	 * after the trap. */
	int (*syscall)(nix_personality_t *self, nix_cpu_if_t *cpu, uint64_t vector);

	void (*destroy)(nix_personality_t *self);
};

struct _nix_personality {
	struct _nix_personality_vtbl const *vtbl;
};

/* The factory every personality bundle's IAbi shim calls to build the runtime personality for a
 * resolved guest-OS version (NIX_VERSION_LATEST = newest / ungated). */
nix_personality_t *nix_personality_create(nix_version_t target);

#define nix_personality_setup(p, cpu, av, ac, ep, brk) \
	(p)->vtbl->setup((p), (cpu), (av), (ac), (ep), (brk))
#define nix_personality_syscall(p, cpu, vec) (p)->vtbl->syscall((p), (cpu), (vec))
#define nix_personality_destroy(p)      (p)->vtbl->destroy((p))

#ifdef __cplusplus
}
#endif

#endif /* __nix_personality_h */
