/*
 * libcpu: cache.cpp
 *
 * On-disk caching of translated code using LLVM bitcode
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include "llvm/IR/Module.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include "libcpu.h"
#include "libcpu_llvm.h"
#include "cache.h"
#include "sha1.h"

#define CACHE_VERSION 1

static const char *
get_cache_dir(void)
{
	static char cache_dir[512] = {0};

	if (cache_dir[0] == '\0') {
		const char *home = getenv("HOME");
		if (home) {
			snprintf(cache_dir, sizeof(cache_dir), "%s/.libcpu/cache", home);
		} else {
			snprintf(cache_dir, sizeof(cache_dir), "/tmp/libcpu_cache");
		}
	}

	return cache_dir;
}

int
cache_init(cpu_t *cpu)
{
	const char *dir = get_cache_dir();

	/* Create cache directory if it doesn't exist */
	struct stat st;
	if (stat(dir, &st) != 0) {
		/* Create parent directory first */
		char parent[512];
		snprintf(parent, sizeof(parent), "%s/.libcpu", getenv("HOME") ?: "/tmp");
		mkdir(parent, 0755);

		/* Create cache directory */
		if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
			fprintf(stderr, "Warning: Could not create cache directory %s: %s\n",
					dir, strerror(errno));
			return -1;
		}
	}

	return 0;
}

void
cache_compute_key(cpu_t *cpu, char *key_out, size_t key_size)
{
	SHA1_CTX ctx;
	unsigned char digest[20];

	SHA1_Init(&ctx);

	/* Hash the code region */
	if (cpu->RAM && cpu->code_start < cpu->code_end) {
		size_t code_size = cpu->code_end - cpu->code_start;
		SHA1_Update(&ctx, cpu->RAM + cpu->code_start, code_size);
	}

	/* Hash architecture and flags to ensure compatibility */
	SHA1_Update(&ctx, &cpu->info.type, sizeof(cpu->info.type));
	SHA1_Update(&ctx, &cpu->info.common_flags, sizeof(cpu->info.common_flags));
	SHA1_Update(&ctx, &cpu->info.arch_flags, sizeof(cpu->info.arch_flags));
	SHA1_Update(&ctx, &cpu->flags_codegen, sizeof(cpu->flags_codegen));

	/* Hash entry point */
	SHA1_Update(&ctx, &cpu->code_entry, sizeof(cpu->code_entry));

	SHA1_Final(digest, &ctx);

	/* Convert to hex string */
	char hex_digest[41];
	for (int i = 0; i < 20; i++) {
		snprintf(hex_digest + i*2, 3, "%02x", digest[i]);
	}
	hex_digest[40] = '\0';

	/* Format: version_archname_hash */
	snprintf(key_out, key_size, "v%d_%s_%s",
			CACHE_VERSION, cpu->info.name, hex_digest);
}

int
cache_load(cpu_t *cpu, const char *cache_key)
{
	char cache_path[1024];
	const char *cache_dir = get_cache_dir();

	snprintf(cache_path, sizeof(cache_path), "%s/%s.bc", cache_dir, cache_key);

	LOG("Attempting to load cache from: %s\n", cache_path);

	/* Check if cache file exists */
	struct stat st;
	if (stat(cache_path, &st) != 0) {
		LOG("Cache file not found\n");
		return -1;
	}

	/* Load the bitcode file */
	auto buffer_or_error = llvm::MemoryBuffer::getFile(cache_path);
	if (auto ec = buffer_or_error.getError()) {
		LOG("Failed to read cache file: %s\n", ec.message().c_str());
		return -1;
	}

	/* Parse the bitcode */
	auto module_or_error = llvm::parseBitcodeFile(
		buffer_or_error.get()->getMemBufferRef(), *cpu->ctx);

	if (auto error = module_or_error.takeError()) {
		LOG("Failed to parse bitcode: ");
		llvm::handleAllErrors(std::move(error), [](const llvm::ErrorInfoBase &EI) {
			LOG("%s\n", EI.message().c_str());
		});
		return -1;
	}

	/* Replace the current module with the cached one */
	std::unique_ptr<Module> cached_module = std::move(module_or_error.get());

	/* Transfer ownership to execution engine */
	cpu->exec_engine->addModule(std::move(cached_module));

	LOG("Successfully loaded cached translation\n");
	return 0;
}

int
cache_save(cpu_t *cpu, const char *cache_key)
{
	char cache_path[1024];
	const char *cache_dir = get_cache_dir();

	snprintf(cache_path, sizeof(cache_path), "%s/%s.bc", cache_dir, cache_key);

	LOG("Saving cache to: %s\n", cache_path);

	/* Open output file */
	std::error_code ec;
	llvm::raw_fd_ostream out(cache_path, ec, llvm::sys::fs::OF_None);

	if (ec) {
		LOG("Failed to open cache file for writing: %s\n", ec.message().c_str());
		return -1;
	}

	/* Write the module as bitcode */
	llvm::WriteBitcodeToFile(*cpu->mod, out);
	out.flush();

	LOG("Successfully saved translation to cache\n");
	return 0;
}

void
cache_clear(cpu_t *cpu)
{
	/* Remove all .bc files from cache directory */
	const char *cache_dir = get_cache_dir();
	char cmd[1024];

	snprintf(cmd, sizeof(cmd), "rm -f %s/*.bc", cache_dir);
	system(cmd);

	LOG("Cache cleared\n");
}
