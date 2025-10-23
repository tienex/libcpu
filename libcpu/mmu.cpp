/*
 * libcpu: mmu.cpp
 *
 * Soft MMU implementation with TLB and page table support
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "mmu.h"
#include "libcpu.h"

/* Initialize MMU context */
void
mmu_init(mmu_context_t *mmu, uint32_t addr_width)
{
	memset(mmu, 0, sizeof(mmu_context_t));

	mmu->addr_width = addr_width;
	mmu->page_size = MMU_PAGE_SIZE;
	mmu->page_shift = MMU_PAGE_SHIFT;
	mmu->enabled = false;
	mmu->current_mode = MMU_MODE_KERNEL;
	mmu->current_asid = 0;

	/* Initialize TLB */
	memset(&mmu->tlb, 0, sizeof(mmu_tlb_t));

	/* Create root page table */
	mmu->page_table_root = mmu_pt_create(0);

	LOG("MMU initialized: addr_width=%d, page_size=%d\n",
		addr_width, mmu->page_size);
}

/* Free MMU resources */
void
mmu_free(mmu_context_t *mmu)
{
	if (mmu->page_table_root) {
		mmu_pt_free(mmu->page_table_root);
		mmu->page_table_root = NULL;
	}
}

/* Reset MMU state */
void
mmu_reset(mmu_context_t *mmu)
{
	mmu_tlb_flush(mmu);
	mmu_reset_stats(mmu);
	mmu->last_fault = MMU_FAULT_NONE;
	mmu->fault_addr = 0;
}

/* Enable/disable MMU */
void
mmu_enable(mmu_context_t *mmu, bool enable)
{
	mmu->enabled = enable;
	if (enable) {
		mmu_tlb_flush(mmu);
		LOG("MMU enabled\n");
	} else {
		LOG("MMU disabled\n");
	}
}

/* ========== TLB Operations ========== */

/* Flush entire TLB */
void
mmu_tlb_flush(mmu_context_t *mmu)
{
	memset(mmu->tlb.entries, 0, sizeof(mmu->tlb.entries));
	LOG("TLB flushed\n");
}

/* Flush specific page from TLB */
void
mmu_tlb_flush_page(mmu_context_t *mmu, addr_t vaddr)
{
	uint32_t idx = mmu_addr_to_tlb_index(vaddr);
	mmu_tlb_entry_t *entry = &mmu->tlb.entries[idx];

	if (entry->valid && mmu_page_align(entry->vaddr) == mmu_page_align(vaddr)) {
		entry->valid = false;
	}
}

/* Flush all entries for specific ASID */
void
mmu_tlb_flush_asid(mmu_context_t *mmu, uint32_t asid)
{
	for (int i = 0; i < MMU_TLB_SIZE; i++) {
		mmu_tlb_entry_t *entry = &mmu->tlb.entries[i];
		if (entry->valid && entry->asid == asid) {
			entry->valid = false;
		}
	}
}

/* Lookup TLB entry */
mmu_tlb_entry_t *
mmu_tlb_lookup(mmu_context_t *mmu, addr_t vaddr, uint32_t asid)
{
	uint32_t idx = mmu_addr_to_tlb_index(vaddr);
	mmu_tlb_entry_t *entry = &mmu->tlb.entries[idx];

	addr_t vpage = mmu_page_align(vaddr);

	if (entry->valid &&
		entry->vaddr == vpage &&
		entry->asid == asid) {
		entry->access_count++;
		mmu->tlb.hits++;
		mmu->stats.tlb_hits++;
		return entry;
	}

	mmu->tlb.misses++;
	mmu->stats.tlb_misses++;
	return NULL;
}

/* Insert entry into TLB */
void
mmu_tlb_insert(mmu_context_t *mmu, addr_t vaddr, addr_t paddr,
               uint32_t flags, uint32_t asid)
{
	uint32_t idx = mmu_addr_to_tlb_index(vaddr);
	mmu_tlb_entry_t *entry = &mmu->tlb.entries[idx];

	entry->vaddr = mmu_page_align(vaddr);
	entry->paddr = mmu_page_align(paddr);
	entry->flags = flags;
	entry->asid = asid;
	entry->valid = true;
	entry->access_count = 0;
}

/* ========== Page Table Operations ========== */

/* Create page table at given level */
mmu_page_table_t *
mmu_pt_create(uint32_t level)
{
	mmu_page_table_t *pt = (mmu_page_table_t *)calloc(1, sizeof(mmu_page_table_t));
	if (!pt) return NULL;

	pt->level = level;
	pt->num_entries = MMU_PT_ENTRIES_PER_LEVEL;
	pt->entries = (mmu_pte_t *)calloc(pt->num_entries, sizeof(mmu_pte_t));

	if (!pt->entries) {
		free(pt);
		return NULL;
	}

	/* Allocate next level pointers if not at leaf level */
	if (level < MMU_PT_LEVELS - 1) {
		pt->next_level = (mmu_page_table_t **)calloc(pt->num_entries,
													 sizeof(mmu_page_table_t *));
		if (!pt->next_level) {
			free(pt->entries);
			free(pt);
			return NULL;
		}
	} else {
		pt->next_level = NULL;
	}

	return pt;
}

/* Free page table recursively */
void
mmu_pt_free(mmu_page_table_t *pt)
{
	if (!pt) return;

	/* Free next level tables recursively */
	if (pt->next_level) {
		for (uint32_t i = 0; i < pt->num_entries; i++) {
			if (pt->next_level[i]) {
				mmu_pt_free(pt->next_level[i]);
			}
		}
		free(pt->next_level);
	}

	free(pt->entries);
	free(pt);
}

/* Get page table index for address at given level */
static uint32_t
mmu_pt_get_index(addr_t vaddr, uint32_t level, uint32_t addr_width)
{
	/* Calculate bit position for this level */
	uint32_t bits_per_level = MMU_PT_ENTRY_SHIFT;
	uint32_t shift = MMU_PAGE_SHIFT + (MMU_PT_LEVELS - 1 - level) * bits_per_level;

	uint32_t idx = (vaddr >> shift) & (MMU_PT_ENTRIES_PER_LEVEL - 1);
	return idx;
}

/* Lookup page table entry */
mmu_pte_t *
mmu_pt_lookup(mmu_page_table_t *root, addr_t vaddr, uint32_t addr_width)
{
	mmu_page_table_t *pt = root;

	for (uint32_t level = 0; level < MMU_PT_LEVELS; level++) {
		uint32_t idx = mmu_pt_get_index(vaddr, level, addr_width);

		if (!pt->entries[idx].present) {
			return NULL;
		}

		/* If at leaf level, return entry */
		if (level == MMU_PT_LEVELS - 1) {
			return &pt->entries[idx];
		}

		/* Move to next level */
		if (!pt->next_level || !pt->next_level[idx]) {
			return NULL;
		}
		pt = pt->next_level[idx];
	}

	return NULL;
}

/* Map virtual address to physical address in page table */
int
mmu_pt_map(mmu_page_table_t *root, addr_t vaddr, addr_t paddr,
           uint32_t flags, uint32_t addr_width)
{
	mmu_page_table_t *pt = root;

	for (uint32_t level = 0; level < MMU_PT_LEVELS; level++) {
		uint32_t idx = mmu_pt_get_index(vaddr, level, addr_width);

		/* If at leaf level, set mapping */
		if (level == MMU_PT_LEVELS - 1) {
			pt->entries[idx].paddr = mmu_page_align(paddr);
			pt->entries[idx].flags = flags;
			pt->entries[idx].present = true;
			return 0;
		}

		/* Create next level table if needed */
		if (!pt->next_level[idx]) {
			pt->next_level[idx] = mmu_pt_create(level + 1);
			if (!pt->next_level[idx]) {
				return -1;
			}
		}

		/* Mark intermediate entry as present */
		pt->entries[idx].present = true;
		pt = pt->next_level[idx];
	}

	return 0;
}

/* Unmap virtual address from page table */
int
mmu_pt_unmap(mmu_page_table_t *root, addr_t vaddr, uint32_t addr_width)
{
	mmu_pte_t *pte = mmu_pt_lookup(root, vaddr, addr_width);
	if (pte) {
		pte->present = false;
		pte->flags = 0;
		return 0;
	}
	return -1;
}

/* ========== Address Translation ========== */

/* Translate virtual address to physical address */
int
mmu_translate(mmu_context_t *mmu, addr_t vaddr, addr_t *paddr,
              mmu_access_t access, mmu_mode_t mode)
{
	mmu->stats.translations++;

	/* If MMU disabled, identity mapping */
	if (!mmu->enabled) {
		*paddr = vaddr;
		return 0;
	}

	/* Check TLB first */
	mmu_tlb_entry_t *tlb_entry = mmu_tlb_lookup(mmu, vaddr, mmu->current_asid);
	if (tlb_entry) {
		/* Check permissions */
		uint32_t required_perm = 0;
		switch (access) {
			case MMU_ACCESS_READ:  required_perm = MMU_PERM_READ; break;
			case MMU_ACCESS_WRITE: required_perm = MMU_PERM_WRITE; break;
			case MMU_ACCESS_EXEC:  required_perm = MMU_PERM_EXEC; break;
		}

		/* Check user mode access */
		if (mode == MMU_MODE_USER && !(tlb_entry->flags & MMU_PERM_USER)) {
			mmu->last_fault = MMU_FAULT_PROTECTION_VIOLATION;
			mmu->fault_addr = vaddr;
			mmu->stats.protection_faults++;
			return -1;
		}

		if (!(tlb_entry->flags & required_perm)) {
			mmu->last_fault = MMU_FAULT_PROTECTION_VIOLATION;
			mmu->fault_addr = vaddr;
			mmu->stats.protection_faults++;
			return -1;
		}

		/* TLB hit - return translated address */
		*paddr = tlb_entry->paddr | mmu_page_offset(vaddr);

		/* Set accessed/dirty bits */
		tlb_entry->flags |= MMU_PERM_ACCESSED;
		if (access == MMU_ACCESS_WRITE) {
			tlb_entry->flags |= MMU_PERM_DIRTY;
		}

		return 0;
	}

	/* TLB miss - walk page table */
	mmu_pte_t *pte = mmu_pt_lookup(mmu->page_table_root, vaddr, mmu->addr_width);
	if (!pte || !pte->present) {
		mmu->last_fault = MMU_FAULT_PAGE_NOT_PRESENT;
		mmu->fault_addr = vaddr;
		mmu->stats.page_faults++;
		return -1;
	}

	/* Check permissions */
	uint32_t required_perm = 0;
	switch (access) {
		case MMU_ACCESS_READ:  required_perm = MMU_PERM_READ; break;
		case MMU_ACCESS_WRITE: required_perm = MMU_PERM_WRITE; break;
		case MMU_ACCESS_EXEC:  required_perm = MMU_PERM_EXEC; break;
	}

	if (mode == MMU_MODE_USER && !(pte->flags & MMU_PERM_USER)) {
		mmu->last_fault = MMU_FAULT_PROTECTION_VIOLATION;
		mmu->fault_addr = vaddr;
		mmu->stats.protection_faults++;
		return -1;
	}

	if (!(pte->flags & required_perm)) {
		mmu->last_fault = MMU_FAULT_PROTECTION_VIOLATION;
		mmu->fault_addr = vaddr;
		mmu->stats.protection_faults++;
		return -1;
	}

	/* Insert into TLB */
	mmu_tlb_insert(mmu, vaddr, pte->paddr, pte->flags, mmu->current_asid);

	/* Return translated address */
	*paddr = pte->paddr | mmu_page_offset(vaddr);

	/* Update flags */
	pte->flags |= MMU_PERM_ACCESSED;
	if (access == MMU_ACCESS_WRITE) {
		pte->flags |= MMU_PERM_DIRTY;
	}

	return 0;
}

/* ========== Memory Access Functions ========== */

int
mmu_read_byte(mmu_context_t *mmu, cpu_t *cpu, addr_t vaddr, uint8_t *value, mmu_mode_t mode)
{
	addr_t paddr;
	if (mmu_translate(mmu, vaddr, &paddr, MMU_ACCESS_READ, mode) != 0) {
		return -1;
	}

	*value = cpu->RAM[paddr];
	return 0;
}

int
mmu_write_byte(mmu_context_t *mmu, cpu_t *cpu, addr_t vaddr, uint8_t value, mmu_mode_t mode)
{
	addr_t paddr;
	if (mmu_translate(mmu, vaddr, &paddr, MMU_ACCESS_WRITE, mode) != 0) {
		return -1;
	}

	cpu->RAM[paddr] = value;
	return 0;
}

int
mmu_read_word(mmu_context_t *mmu, cpu_t *cpu, addr_t vaddr, uint16_t *value, mmu_mode_t mode)
{
	addr_t paddr;
	if (mmu_translate(mmu, vaddr, &paddr, MMU_ACCESS_READ, mode) != 0) {
		return -1;
	}

	*value = *(uint16_t *)&cpu->RAM[paddr];
	return 0;
}

int
mmu_write_word(mmu_context_t *mmu, cpu_t *cpu, addr_t vaddr, uint16_t value, mmu_mode_t mode)
{
	addr_t paddr;
	if (mmu_translate(mmu, vaddr, &paddr, MMU_ACCESS_WRITE, mode) != 0) {
		return -1;
	}

	*(uint16_t *)&cpu->RAM[paddr] = value;
	return 0;
}

int
mmu_read_dword(mmu_context_t *mmu, cpu_t *cpu, addr_t vaddr, uint32_t *value, mmu_mode_t mode)
{
	addr_t paddr;
	if (mmu_translate(mmu, vaddr, &paddr, MMU_ACCESS_READ, mode) != 0) {
		return -1;
	}

	*value = *(uint32_t *)&cpu->RAM[paddr];
	return 0;
}

int
mmu_write_dword(mmu_context_t *mmu, cpu_t *cpu, addr_t vaddr, uint32_t value, mmu_mode_t mode)
{
	addr_t paddr;
	if (mmu_translate(mmu, vaddr, &paddr, MMU_ACCESS_WRITE, mode) != 0) {
		return -1;
	}

	*(uint32_t *)&cpu->RAM[paddr] = value;
	return 0;
}

int
mmu_read_qword(mmu_context_t *mmu, cpu_t *cpu, addr_t vaddr, uint64_t *value, mmu_mode_t mode)
{
	addr_t paddr;
	if (mmu_translate(mmu, vaddr, &paddr, MMU_ACCESS_READ, mode) != 0) {
		return -1;
	}

	*value = *(uint64_t *)&cpu->RAM[paddr];
	return 0;
}

int
mmu_write_qword(mmu_context_t *mmu, cpu_t *cpu, addr_t vaddr, uint64_t value, mmu_mode_t mode)
{
	addr_t paddr;
	if (mmu_translate(mmu, vaddr, &paddr, MMU_ACCESS_WRITE, mode) != 0) {
		return -1;
	}

	*(uint64_t *)&cpu->RAM[paddr] = value;
	return 0;
}

/* ========== Mode Management ========== */

void
mmu_set_mode(mmu_context_t *mmu, mmu_mode_t mode)
{
	mmu->current_mode = mode;
	/* Flush TLB on mode change for security */
	mmu_tlb_flush(mmu);
}

mmu_mode_t
mmu_get_mode(mmu_context_t *mmu)
{
	return mmu->current_mode;
}

/* ========== ASID Management ========== */

void
mmu_set_asid(mmu_context_t *mmu, uint32_t asid)
{
	if (mmu->current_asid != asid) {
		mmu->current_asid = asid;
		mmu->tlb.current_asid = asid;
	}
}

uint32_t
mmu_get_asid(mmu_context_t *mmu)
{
	return mmu->current_asid;
}

/* ========== Statistics ========== */

void
mmu_print_stats(mmu_context_t *mmu)
{
	printf("MMU Statistics:\n");
	printf("  Translations:      %llu\n", (unsigned long long)mmu->stats.translations);
	printf("  TLB hits:          %llu\n", (unsigned long long)mmu->stats.tlb_hits);
	printf("  TLB misses:        %llu\n", (unsigned long long)mmu->stats.tlb_misses);

	uint64_t total = mmu->stats.tlb_hits + mmu->stats.tlb_misses;
	if (total > 0) {
		double hit_rate = (100.0 * mmu->stats.tlb_hits) / total;
		printf("  TLB hit rate:      %.2f%%\n", hit_rate);
	}

	printf("  Page faults:       %llu\n", (unsigned long long)mmu->stats.page_faults);
	printf("  Protection faults: %llu\n", (unsigned long long)mmu->stats.protection_faults);
	printf("  Current mode:      %d\n", mmu->current_mode);
	printf("  Current ASID:      %u\n", mmu->current_asid);
	printf("  MMU enabled:       %s\n", mmu->enabled ? "yes" : "no");
}

void
mmu_reset_stats(mmu_context_t *mmu)
{
	memset(&mmu->stats, 0, sizeof(mmu_stats_t));
	mmu->tlb.hits = 0;
	mmu->tlb.misses = 0;
}
