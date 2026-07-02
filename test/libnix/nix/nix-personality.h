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

#include "nix-host.h" /* nix_mem_if_t + the base integer types */

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
};

struct _nix_cpu_if {
	struct _nix_cpu_if_vtbl const *vtbl;
	nix_mem_if_t                  *mem;      /* guest memory (flat or mapped) */
	uint64_t                       ram_size; /* guest RAM size, in bytes */
};

#define nix_cpu_reg_get(self, n)     (self)->vtbl->reg_get((self), (n))
#define nix_cpu_reg_set(self, n, v)  (self)->vtbl->reg_set((self), (n), (v))
#define nix_cpu_set_carry(self, set) (self)->vtbl->set_carry((self), (set))

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

	/* Service one system-call trap.  Returns 0 to stop the guest (the
	 * process called exit), 1 to resume after the trap. */
	int (*syscall)(nix_personality_t *self, nix_cpu_if_t *cpu);

	void (*destroy)(nix_personality_t *self);
};

struct _nix_personality {
	struct _nix_personality_vtbl const *vtbl;
};

/* The factory every personality dylib exports. */
nix_personality_t *nix_personality_create(void);

#define nix_personality_setup(p, cpu, av, ac, ep, brk) \
	(p)->vtbl->setup((p), (cpu), (av), (ac), (ep), (brk))
#define nix_personality_syscall(p, cpu) (p)->vtbl->syscall((p), (cpu))
#define nix_personality_destroy(p)      (p)->vtbl->destroy((p))

#ifdef __cplusplus
}
#endif

#endif /* __nix_personality_h */
