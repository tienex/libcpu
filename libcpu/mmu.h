/*
 * libcpu: mmu.h
 *
 * Soft MMU (Memory Management Unit) support
 * Supports TLB, hierarchical page tables, and system mode
 */

#ifndef _MMU_H_
#define _MMU_H_

#include "libcpu.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MMU configuration */
#define MMU_TLB_SIZE 1024
#define MMU_TLB_MASK (MMU_TLB_SIZE - 1)
#define MMU_PAGE_SIZE 4096
#define MMU_PAGE_SHIFT 12
#define MMU_PAGE_MASK (MMU_PAGE_SIZE - 1)

/* Page table levels */
#define MMU_PT_LEVELS 4
#define MMU_PT_ENTRIES_PER_LEVEL 512
#define MMU_PT_ENTRY_SHIFT 9

/* Access permissions */
#define MMU_PERM_READ    (1 << 0)
#define MMU_PERM_WRITE   (1 << 1)
#define MMU_PERM_EXEC    (1 << 2)
#define MMU_PERM_USER    (1 << 3)
#define MMU_PERM_PRESENT (1 << 4)
#define MMU_PERM_DIRTY   (1 << 5)
#define MMU_PERM_ACCESSED (1 << 6)

/* MMU modes */
typedef enum {
	MMU_MODE_USER = 0,
	MMU_MODE_SUPERVISOR = 1,
	MMU_MODE_KERNEL = 2,
	MMU_MODE_HYPERVISOR = 3
} mmu_mode_t;

/* Access types */
typedef enum {
	MMU_ACCESS_READ = 0,
	MMU_ACCESS_WRITE = 1,
	MMU_ACCESS_EXEC = 2
} mmu_access_t;

/* MMU fault types */
typedef enum {
	MMU_FAULT_NONE = 0,
	MMU_FAULT_PAGE_NOT_PRESENT,
	MMU_FAULT_PROTECTION_VIOLATION,
	MMU_FAULT_INVALID_ADDRESS,
	MMU_FAULT_TLB_MISS,
	MMU_FAULT_PAGE_FAULT
} mmu_fault_t;

/* TLB entry structure */
typedef struct mmu_tlb_entry {
	addr_t vaddr;           /* Virtual address (page aligned) */
	addr_t paddr;           /* Physical address (page aligned) */
	uint32_t flags;         /* Permission and status flags */
	uint32_t asid;          /* Address Space ID */
	bool valid;             /* Entry is valid */
	uint64_t access_count;  /* Number of times accessed */
} mmu_tlb_entry_t;

/* Page table entry structure */
typedef struct mmu_pte {
	addr_t paddr;           /* Physical address */
	uint32_t flags;         /* Permission and status flags */
	bool present;           /* Page is present */
} mmu_pte_t;

/* Page table structure (multi-level) */
typedef struct mmu_page_table {
	mmu_pte_t *entries;     /* Page table entries */
	uint32_t level;         /* Level in page table hierarchy */
	uint32_t num_entries;   /* Number of entries */
	struct mmu_page_table **next_level; /* Pointers to next level tables */
} mmu_page_table_t;

/* TLB structure */
typedef struct mmu_tlb {
	mmu_tlb_entry_t entries[MMU_TLB_SIZE];
	uint64_t hits;          /* TLB hit count */
	uint64_t misses;        /* TLB miss count */
	uint32_t current_asid;  /* Current address space ID */
} mmu_tlb_t;

/* MMU statistics */
typedef struct mmu_stats {
	uint64_t translations;
	uint64_t tlb_hits;
	uint64_t tlb_misses;
	uint64_t page_faults;
	uint64_t protection_faults;
} mmu_stats_t;

/* MMU context structure */
typedef struct mmu_context {
	mmu_tlb_t tlb;                    /* TLB for fast translations */
	mmu_page_table_t *page_table_root; /* Root page table */
	mmu_mode_t current_mode;          /* Current execution mode */
	uint32_t current_asid;            /* Current address space ID */
	bool enabled;                     /* MMU is enabled */
	mmu_stats_t stats;                /* Statistics */

	/* Configuration */
	uint32_t page_size;               /* Page size in bytes */
	uint32_t page_shift;              /* log2(page_size) */
	uint32_t addr_width;              /* Address width in bits */

	/* Fault handling */
	mmu_fault_t last_fault;           /* Last fault type */
	addr_t fault_addr;                /* Address that caused fault */

	/* Callbacks for physical memory access */
	uint8_t *(*get_ram_ptr)(struct cpu *cpu, addr_t paddr);
} mmu_context_t;

/* MMU initialization and management */
void mmu_init(mmu_context_t *mmu, uint32_t addr_width);
void mmu_free(mmu_context_t *mmu);
void mmu_reset(mmu_context_t *mmu);
void mmu_enable(mmu_context_t *mmu, bool enable);

/* TLB operations */
void mmu_tlb_flush(mmu_context_t *mmu);
void mmu_tlb_flush_page(mmu_context_t *mmu, addr_t vaddr);
void mmu_tlb_flush_asid(mmu_context_t *mmu, uint32_t asid);
mmu_tlb_entry_t *mmu_tlb_lookup(mmu_context_t *mmu, addr_t vaddr, uint32_t asid);
void mmu_tlb_insert(mmu_context_t *mmu, addr_t vaddr, addr_t paddr, uint32_t flags, uint32_t asid);

/* Page table operations */
mmu_page_table_t *mmu_pt_create(uint32_t level);
void mmu_pt_free(mmu_page_table_t *pt);
mmu_pte_t *mmu_pt_lookup(mmu_page_table_t *root, addr_t vaddr, uint32_t addr_width);
int mmu_pt_map(mmu_page_table_t *root, addr_t vaddr, addr_t paddr, uint32_t flags, uint32_t addr_width);
int mmu_pt_unmap(mmu_page_table_t *root, addr_t vaddr, uint32_t addr_width);

/* Address translation */
int mmu_translate(mmu_context_t *mmu, addr_t vaddr, addr_t *paddr,
                  mmu_access_t access, mmu_mode_t mode);

/* Memory access with MMU */
int mmu_read_byte(mmu_context_t *mmu, cpu_t *cpu, addr_t vaddr, uint8_t *value, mmu_mode_t mode);
int mmu_write_byte(mmu_context_t *mmu, cpu_t *cpu, addr_t vaddr, uint8_t value, mmu_mode_t mode);
int mmu_read_word(mmu_context_t *mmu, cpu_t *cpu, addr_t vaddr, uint16_t *value, mmu_mode_t mode);
int mmu_write_word(mmu_context_t *mmu, cpu_t *cpu, addr_t vaddr, uint16_t value, mmu_mode_t mode);
int mmu_read_dword(mmu_context_t *mmu, cpu_t *cpu, addr_t vaddr, uint32_t *value, mmu_mode_t mode);
int mmu_write_dword(mmu_context_t *mmu, cpu_t *cpu, addr_t vaddr, uint32_t value, mmu_mode_t mode);
int mmu_read_qword(mmu_context_t *mmu, cpu_t *cpu, addr_t vaddr, uint64_t *value, mmu_mode_t mode);
int mmu_write_qword(mmu_context_t *mmu, cpu_t *cpu, addr_t vaddr, uint64_t value, mmu_mode_t mode);

/* Mode management */
void mmu_set_mode(mmu_context_t *mmu, mmu_mode_t mode);
mmu_mode_t mmu_get_mode(mmu_context_t *mmu);

/* ASID management */
void mmu_set_asid(mmu_context_t *mmu, uint32_t asid);
uint32_t mmu_get_asid(mmu_context_t *mmu);

/* Statistics */
void mmu_print_stats(mmu_context_t *mmu);
void mmu_reset_stats(mmu_context_t *mmu);

/* Helper functions */
static inline addr_t mmu_page_align(addr_t addr) {
	return addr & ~MMU_PAGE_MASK;
}

static inline addr_t mmu_page_offset(addr_t addr) {
	return addr & MMU_PAGE_MASK;
}

static inline uint32_t mmu_addr_to_tlb_index(addr_t vaddr) {
	return (uint32_t)((vaddr >> MMU_PAGE_SHIFT) & MMU_TLB_MASK);
}

#ifdef __cplusplus
}
#endif

#endif /* _MMU_H_ */
