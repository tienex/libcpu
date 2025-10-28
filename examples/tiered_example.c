/*
 * libcpu Tiered Compilation Example
 *
 * This example demonstrates:
 * 1. Enabling tiered compilation
 * 2. Monitoring function profiling
 * 3. Observing tier transitions
 * 4. Viewing compilation statistics
 */

#include "../libcpu/libcpu.h"
#include "../libcpu/tiered_compilation.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* Simple ARM binary code for testing */
static uint8_t arm_code[] = {
	/* Function 1: Simple addition loop */
	0x00, 0x00, 0x00, 0xE3,  /* MOV R0, #0 */
	0x0A, 0x10, 0x00, 0xE3,  /* MOV R1, #10 */
	/* loop: */
	0x01, 0x00, 0x80, 0xE2,  /* ADD R0, R0, #1 */
	0x01, 0x10, 0x51, 0xE2,  /* SUBS R1, R1, #1 */
	0xFC, 0xFF, 0xFF, 0x1A,  /* BNE loop */
	0x0E, 0xF0, 0xA0, 0xE1   /* MOV PC, LR (return) */
};

void print_tier_info(tier_manager_t *mgr, addr_t address)
{
	function_profile_t *profile = tier_manager_get_profile(mgr, address);
	if (!profile) {
		printf("  No profile found for address 0x%llx\n", (unsigned long long)address);
		return;
	}

	printf("  Address: 0x%llx\n", (unsigned long long)profile->address);
	printf("  Invocations: %llu\n", (unsigned long long)profile->invocation_count);
	printf("  Total cycles: %llu\n", (unsigned long long)profile->total_cycles);
	if (profile->invocation_count > 0) {
		printf("  Avg cycles/call: %llu\n",
		       (unsigned long long)(profile->total_cycles / profile->invocation_count));
	}
	printf("  Current tier: %s (%d)\n",
	       tier_get_name(profile->current_tier), profile->current_tier);
	printf("  Target tier: %s (%d)\n",
	       tier_get_name(profile->target_tier), profile->target_tier);
	printf("  Hot: %s\n", profile->is_hot ? "YES" : "no");
	printf("  Recompilation pending: %s\n", profile->recompilation_pending ? "YES" : "no");
	printf("  Recompilation failed: %s\n", profile->recompilation_failed ? "YES" : "no");

	/* Show which tiers have compiled code */
	printf("  Compiled code:");
	for (int i = 0; i < TIER_MAX; i++) {
		if (profile->compiled_code[i] != NULL) {
			printf(" %s", tier_get_name((compilation_tier_t)i));
		}
	}
	printf("\n");
}

int main(int argc, char **argv)
{
	printf("libcpu Tiered Compilation Example\n");
	printf("==================================\n\n");

	/* Create CPU with ARM architecture */
	printf("1. Creating ARM CPU...\n");
	cpu_t *cpu = cpu_new(CPU_ARCH_ARM,
	                     CPU_FLAG_ENDIAN_LITTLE,
	                     0);
	if (!cpu) {
		fprintf(stderr, "Failed to create CPU\n");
		return 1;
	}

	/* Initialize tiered compilation */
	printf("2. Initializing tiered compilation...\n");
	if (cpu_init_tiered_compilation(cpu) != 0) {
		fprintf(stderr, "Failed to initialize tiered compilation\n");
		cpu_free(cpu);
		return 1;
	}

	tier_manager_t *mgr = cpu->tier_mgr;
	printf("   - Tier manager created\n");
	printf("   - Max tier: %s\n", tier_get_name((compilation_tier_t)mgr->max_tier));
	printf("   - Background compilation: %s\n",
	       mgr->enable_background_compilation ? "enabled" : "disabled");
	printf("   - Profiling: %s\n", mgr->enable_profiling ? "enabled" : "disabled");
	printf("   - Worker threads: %u\n", mgr->num_workers);

	/* Configure compilation */
	printf("\n3. Configuring compilation...\n");

	/* For demo, use lower tier for faster demonstration */
	cpu_set_max_tier(cpu, TIER_LLVM_O2);
	printf("   - Set max tier to LLVM-O2\n");

	/* Set up memory */
	printf("\n4. Setting up memory...\n");
	uint8_t *RAM = (uint8_t*)calloc(1024 * 1024, 1); /* 1MB */
	memcpy(RAM, arm_code, sizeof(arm_code));
	cpu_set_ram(cpu, RAM);
	printf("   - Allocated 1MB RAM\n");
	printf("   - Loaded %zu bytes of ARM code\n", sizeof(arm_code));

	/* Simulate function execution with increasing invocation counts */
	printf("\n5. Simulating function execution...\n");
	addr_t func_address = 0x0;

	struct {
		int iterations;
		const char *description;
	} phases[] = {
		{5, "Cold start (should use Interpreter or TCG)"},
		{50, "Warming up (should transition to QBE)"},
		{500, "Getting hot (should transition to GCCJIT)"},
		{5000, "Very hot (should transition to LLVM-O0)"},
		{50000, "Extremely hot (should transition to LLVM-O1)"},
		{100000, "Critical hotspot (should transition to LLVM-O2)"},
	};

	for (int phase = 0; phase < 6; phase++) {
		printf("\n   Phase %d: %s\n", phase + 1, phases[phase].description);

		for (int i = 0; i < phases[phase].iterations; i++) {
			/* Record invocation (simulate execution) */
			tier_manager_record_invocation(mgr, func_address, 1000);
		}

		/* Show current status */
		print_tier_info(mgr, func_address);

		/* Small delay to allow background compilation */
		usleep(100000); /* 100ms */
	}

	/* Print final statistics */
	printf("\n6. Final Statistics:\n");
	printf("==================\n");
	tier_manager_print_stats(mgr);

	/* Show backend capabilities */
	printf("\n7. Backend Information:\n");
	printf("======================\n");
	for (int tier = 0; tier < TIER_MAX; tier++) {
		if (mgr->backends[tier]) {
			IBackend *backend = mgr->backends[tier];
			printf("   Tier %d (%s):\n", tier, tier_get_name((compilation_tier_t)tier));
			printf("     Backend: %s\n", backend->GetName(backend));
			printf("     Version: %s\n", backend->GetVersion(backend));
			printf("     Opt level: %u\n", backend->GetOptimizationLevel(backend));
		}
	}

	/* Demonstrate manual tier control */
	printf("\n8. Manual Tier Control:\n");
	printf("======================\n");

	/* Force recompilation at specific tier */
	printf("   Requesting recompilation at LLVM-O3...\n");
	if (tier_manager_request_recompilation(mgr, func_address, TIER_LLVM_O3) == 0) {
		printf("   Recompilation request queued successfully\n");
		usleep(500000); /* Wait 500ms for background compilation */

		print_tier_info(mgr, func_address);
	}

	/* Demonstrate tier comparison */
	printf("\n9. Tier Comparison:\n");
	printf("==================\n");
	printf("   %-12s | %12s | %12s | %12s\n",
	       "Tier", "Compile Time", "Code Quality", "Estimate");
	printf("   %s\n", "-------------------------------------------------------");

	for (int tier = 0; tier < TIER_MAX; tier++) {
		printf("   %-12s | %9lluμs | %11u%% | %12llu\n",
		       tier_get_name((compilation_tier_t)tier),
		       (unsigned long long)tier_estimate_compile_time((compilation_tier_t)tier),
		       tier_estimate_code_quality((compilation_tier_t)tier),
		       (unsigned long long)tier_estimate_compile_time((compilation_tier_t)tier));
	}

	/* Cleanup */
	printf("\n10. Cleanup:\n");
	printf("===========\n");
	printf("   Shutting down tiered compilation...\n");
	cpu_shutdown_tiered_compilation(cpu);

	printf("   Freeing CPU...\n");
	cpu_free(cpu);

	printf("   Freeing RAM...\n");
	free(RAM);

	printf("\nExample completed successfully!\n");

	return 0;
}
