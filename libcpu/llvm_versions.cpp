/*
 * libcpu LLVM Version Compatibility Implementation
 */

#include "llvm_versions.h"
#include "libcpu_llvm.h"

#include <llvm/IR/Module.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Support/TargetSelect.h>

/* Legacy JIT (LLVM 3.0-3.5) */
#if LLVM_HAS_LEGACY_JIT
#include <llvm/ExecutionEngine/JIT.h>
#endif

/* MCJIT (LLVM 3.5+) */
#if LLVM_HAS_MCJIT
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#endif

/* ORC JIT v1 (LLVM 5.0-8.0) */
#if LLVM_HAS_ORC
#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/ExecutionEngine/Orc/IRCompileLayer.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#if LLVM_VERSION_MAJOR < 7
#include <llvm/ExecutionEngine/Orc/LambdaResolver.h>
#else
#include <llvm/ExecutionEngine/Orc/Legacy.h>
#endif
#endif

/* ORC JIT v2 (LLVM 9.0+) */
#if LLVM_HAS_ORCv2
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#endif

#include <memory>
#include <string>

using namespace llvm;

/***************************************************************************
 * JIT Context Structures
 ***************************************************************************/

typedef struct LLVMJITContext {
	llvm_jit_type_t jit_type;
	LLVMContext *context;
	Module *module;

	/* Engine-specific data */
	union {
#if LLVM_HAS_LEGACY_JIT
		struct {
			ExecutionEngine *engine;
		} legacy;
#endif

#if LLVM_HAS_MCJIT
		struct {
			ExecutionEngine *engine;
		} mcjit;
#endif

#if LLVM_HAS_ORC
		struct {
			orc::ExecutionSession *exec_session;
			orc::RTDyldObjectLinkingLayer *object_layer;
			orc::IRCompileLayer *compile_layer;
#if LLVM_VERSION_MAJOR >= 7
			orc::JITDylib *main_jd;
#endif
		} orc_v1;
#endif

#if LLVM_HAS_ORCv2
		struct {
			orc::LLJIT *jit;
		} orc_v2;
#endif
	} engine;
} LLVMJITContext;

/***************************************************************************
 * Version Detection
 ***************************************************************************/

uint32_t llvm_get_available_jits(llvm_jit_type_t *jits, uint32_t max_count)
{
	uint32_t count = 0;

#if LLVM_HAS_LEGACY_JIT
	if (count < max_count)
		jits[count++] = LLVM_JIT_LEGACY;
#endif

#if LLVM_HAS_MCJIT
	if (count < max_count)
		jits[count++] = LLVM_JIT_MCJIT;
#endif

#if LLVM_HAS_ORC
	if (count < max_count)
		jits[count++] = LLVM_JIT_ORC_V1;
#endif

#if LLVM_HAS_ORCv2
	if (count < max_count)
		jits[count++] = LLVM_JIT_ORC_V2;
#endif

	return count;
}

const char* llvm_jit_get_name(llvm_jit_type_t jit)
{
	switch (jit) {
	case LLVM_JIT_LEGACY: return "Legacy JIT";
	case LLVM_JIT_MCJIT: return "MCJIT";
	case LLVM_JIT_ORC_V1: return "ORC JIT v1";
	case LLVM_JIT_ORC_V2: return "ORC JIT v2";
	case LLVM_JIT_AUTO: return "Auto";
	default: return "Unknown";
	}
}

int llvm_jit_is_available(llvm_jit_type_t jit)
{
	switch (jit) {
#if LLVM_HAS_LEGACY_JIT
	case LLVM_JIT_LEGACY: return 1;
#endif
#if LLVM_HAS_MCJIT
	case LLVM_JIT_MCJIT: return 1;
#endif
#if LLVM_HAS_ORC
	case LLVM_JIT_ORC_V1: return 1;
#endif
#if LLVM_HAS_ORCv2
	case LLVM_JIT_ORC_V2: return 1;
#endif
	default: return 0;
	}
}

llvm_jit_type_t llvm_jit_get_recommended(void)
{
	/* Prefer newer JIT engines */
#if LLVM_HAS_ORCv2
	return LLVM_JIT_ORC_V2;
#elif LLVM_HAS_ORC
	return LLVM_JIT_ORC_V1;
#elif LLVM_HAS_MCJIT
	return LLVM_JIT_MCJIT;
#elif LLVM_HAS_LEGACY_JIT
	return LLVM_JIT_LEGACY;
#else
	return LLVM_JIT_AUTO;
#endif
}

/***************************************************************************
 * Legacy JIT Implementation (LLVM 3.0-3.5)
 ***************************************************************************/

#if LLVM_HAS_LEGACY_JIT
static LLVMJITContext* llvm_jit_create_legacy(void)
{
	LLVMJITContext *ctx = new LLVMJITContext();
	ctx->jit_type = LLVM_JIT_LEGACY;
	ctx->context = new LLVMContext();
	ctx->module = new Module("libcpu", *ctx->context);

	/* Create legacy JIT execution engine */
	std::string error;
	EngineBuilder builder(ctx->module);
	builder.setErrorStr(&error);
	builder.setEngineKind(EngineKind::JIT);

	ctx->engine.legacy.engine = builder.create();
	if (!ctx->engine.legacy.engine) {
		delete ctx->context;
		delete ctx;
		return nullptr;
	}

	return ctx;
}

static int llvm_jit_compile_legacy(LLVMJITContext *ctx, void *llvm_module)
{
	/* Legacy JIT compiles lazily */
	return 0;
}

static void* llvm_jit_get_address_legacy(LLVMJITContext *ctx, const char *name)
{
	Function *func = ctx->module->getFunction(name);
	if (!func)
		return nullptr;

	return ctx->engine.legacy.engine->getPointerToFunction(func);
}

static void llvm_jit_destroy_legacy(LLVMJITContext *ctx)
{
	delete ctx->engine.legacy.engine;
	delete ctx->context;
	delete ctx;
}
#endif

/***************************************************************************
 * MCJIT Implementation (LLVM 3.5+)
 ***************************************************************************/

#if LLVM_HAS_MCJIT
static LLVMJITContext* llvm_jit_create_mcjit(void)
{
	LLVMJITContext *ctx = new LLVMJITContext();
	ctx->jit_type = LLVM_JIT_MCJIT;
	ctx->context = new LLVMContext();
	ctx->module = new Module("libcpu", *ctx->context);

	/* Create MCJIT execution engine */
	std::string error;
	EngineBuilder builder(std::unique_ptr<Module>(ctx->module));
	builder.setErrorStr(&error);
	builder.setEngineKind(EngineKind::JIT);
	builder.setMCJITMemoryManager(std::make_unique<SectionMemoryManager>());

	ctx->engine.mcjit.engine = builder.create();
	if (!ctx->engine.mcjit.engine) {
		delete ctx->context;
		delete ctx;
		return nullptr;
	}

	return ctx;
}

static int llvm_jit_compile_mcjit(LLVMJITContext *ctx, void *llvm_module)
{
	/* Finalize object code generation */
	ctx->engine.mcjit.engine->finalizeObject();
	return 0;
}

static void* llvm_jit_get_address_mcjit(LLVMJITContext *ctx, const char *name)
{
	uint64_t addr = ctx->engine.mcjit.engine->getFunctionAddress(name);
	return (void*)addr;
}

static void llvm_jit_destroy_mcjit(LLVMJITContext *ctx)
{
	delete ctx->engine.mcjit.engine;
	delete ctx->context;
	delete ctx;
}
#endif

/***************************************************************************
 * ORC JIT v1 Implementation (LLVM 5.0-8.0)
 ***************************************************************************/

#if LLVM_HAS_ORC
static LLVMJITContext* llvm_jit_create_orc_v1(void)
{
	LLVMJITContext *ctx = new LLVMJITContext();
	ctx->jit_type = LLVM_JIT_ORC_V1;
	ctx->context = new LLVMContext();
	ctx->module = new Module("libcpu", *ctx->context);

#if LLVM_VERSION_MAJOR >= 7
	/* LLVM 7+ ORC v1 API */
	ctx->engine.orc_v1.exec_session = new orc::ExecutionSession();
	ctx->engine.orc_v1.object_layer = new orc::RTDyldObjectLinkingLayer(
		*ctx->engine.orc_v1.exec_session,
		[]() { return std::make_unique<SectionMemoryManager>(); }
	);

	auto target_machine = EngineBuilder().selectTarget();
	ctx->engine.orc_v1.compile_layer = new orc::IRCompileLayer(
		*ctx->engine.orc_v1.exec_session,
		*ctx->engine.orc_v1.object_layer,
		orc::SimpleCompiler(*target_machine)
	);

	ctx->engine.orc_v1.main_jd = &ctx->engine.orc_v1.exec_session->createJITDylib("main");
#else
	/* LLVM 5-6 ORC v1 API */
	ctx->engine.orc_v1.exec_session = nullptr; /* Not needed in old API */
	ctx->engine.orc_v1.object_layer = new orc::RTDyldObjectLinkingLayer(
		[]() { return std::make_shared<SectionMemoryManager>(); }
	);

	auto target_machine = EngineBuilder().selectTarget();
	ctx->engine.orc_v1.compile_layer = new orc::IRCompileLayer(
		*ctx->engine.orc_v1.object_layer,
		orc::SimpleCompiler(*target_machine)
	);
#endif

	return ctx;
}

static int llvm_jit_compile_orc_v1(LLVMJITContext *ctx, void *llvm_module)
{
#if LLVM_VERSION_MAJOR >= 7
	/* Add module to JIT */
	auto module_ptr = std::unique_ptr<Module>(static_cast<Module*>(llvm_module));
	auto err = ctx->engine.orc_v1.compile_layer->add(
		*ctx->engine.orc_v1.main_jd,
		orc::ThreadSafeModule(std::move(module_ptr),
		                       std::make_unique<LLVMContext>())
	);
	if (err) {
		return -1;
	}
#else
	/* Old API */
	auto module_ptr = std::shared_ptr<Module>(static_cast<Module*>(llvm_module));
	auto resolver = orc::createLambdaResolver(
		[](const std::string &name) { return nullptr; },
		[](const std::string &name) { return nullptr; }
	);
	ctx->engine.orc_v1.compile_layer->addModule(module_ptr, std::move(resolver));
#endif

	return 0;
}

static void* llvm_jit_get_address_orc_v1(LLVMJITContext *ctx, const char *name)
{
#if LLVM_VERSION_MAJOR >= 7
	auto symbol = ctx->engine.orc_v1.exec_session->lookup(
		{ctx->engine.orc_v1.main_jd}, name
	);
	if (!symbol)
		return nullptr;
	return (void*)symbol->getAddress();
#else
	auto symbol = ctx->engine.orc_v1.compile_layer->findSymbol(name, false);
	if (!symbol)
		return nullptr;
	return (void*)symbol.getAddress();
#endif
}

static void llvm_jit_destroy_orc_v1(LLVMJITContext *ctx)
{
	delete ctx->engine.orc_v1.compile_layer;
	delete ctx->engine.orc_v1.object_layer;
#if LLVM_VERSION_MAJOR >= 7
	delete ctx->engine.orc_v1.exec_session;
#endif
	delete ctx->context;
	delete ctx;
}
#endif

/***************************************************************************
 * ORC JIT v2 Implementation (LLVM 9.0+)
 ***************************************************************************/

#if LLVM_HAS_ORCv2
static LLVMJITContext* llvm_jit_create_orc_v2(void)
{
	LLVMJITContext *ctx = new LLVMJITContext();
	ctx->jit_type = LLVM_JIT_ORC_V2;
	ctx->context = new LLVMContext();

	/* Create LLJIT */
	auto jit_or_err = orc::LLJITBuilder().create();
	if (!jit_or_err) {
		delete ctx->context;
		delete ctx;
		return nullptr;
	}

	ctx->engine.orc_v2.jit = jit_or_err->release();
	ctx->module = nullptr; /* Module managed by ORC v2 */

	return ctx;
}

static int llvm_jit_compile_orc_v2(LLVMJITContext *ctx, void *llvm_module)
{
	Module *mod = static_cast<Module*>(llvm_module);

	/* Wrap in ThreadSafeModule */
	auto tsm = orc::ThreadSafeModule(
		std::unique_ptr<Module>(mod),
		std::make_unique<LLVMContext>()
	);

	/* Add to JIT */
	auto err = ctx->engine.orc_v2.jit->addIRModule(std::move(tsm));
	if (err) {
		return -1;
	}

	return 0;
}

static void* llvm_jit_get_address_orc_v2(LLVMJITContext *ctx, const char *name)
{
	auto symbol_or_err = ctx->engine.orc_v2.jit->lookup(name);
	if (!symbol_or_err)
		return nullptr;

	return (void*)symbol_or_err->getAddress();
}

static void llvm_jit_destroy_orc_v2(LLVMJITContext *ctx)
{
	delete ctx->engine.orc_v2.jit;
	delete ctx->context;
	delete ctx;
}
#endif

/***************************************************************************
 * Unified API Implementation
 ***************************************************************************/

LLVMJITContext* llvm_jit_create_context(llvm_jit_type_t jit)
{
	/* Initialize LLVM targets */
	static int initialized = 0;
	if (!initialized) {
		InitializeNativeTarget();
		InitializeNativeTargetAsmPrinter();
		InitializeNativeTargetAsmParser();
		initialized = 1;
	}

	/* Auto-detect if requested */
	if (jit == LLVM_JIT_AUTO) {
		jit = llvm_jit_get_recommended();
	}

	/* Create appropriate context */
	switch (jit) {
#if LLVM_HAS_LEGACY_JIT
	case LLVM_JIT_LEGACY:
		return llvm_jit_create_legacy();
#endif

#if LLVM_HAS_MCJIT
	case LLVM_JIT_MCJIT:
		return llvm_jit_create_mcjit();
#endif

#if LLVM_HAS_ORC
	case LLVM_JIT_ORC_V1:
		return llvm_jit_create_orc_v1();
#endif

#if LLVM_HAS_ORCv2
	case LLVM_JIT_ORC_V2:
		return llvm_jit_create_orc_v2();
#endif

	default:
		return nullptr;
	}
}

void llvm_jit_destroy_context(LLVMJITContext *ctx)
{
	if (!ctx)
		return;

	switch (ctx->jit_type) {
#if LLVM_HAS_LEGACY_JIT
	case LLVM_JIT_LEGACY:
		llvm_jit_destroy_legacy(ctx);
		break;
#endif

#if LLVM_HAS_MCJIT
	case LLVM_JIT_MCJIT:
		llvm_jit_destroy_mcjit(ctx);
		break;
#endif

#if LLVM_HAS_ORC
	case LLVM_JIT_ORC_V1:
		llvm_jit_destroy_orc_v1(ctx);
		break;
#endif

#if LLVM_HAS_ORCv2
	case LLVM_JIT_ORC_V2:
		llvm_jit_destroy_orc_v2(ctx);
		break;
#endif

	default:
		delete ctx;
	}
}

int llvm_jit_compile_module(LLVMJITContext *ctx, void *llvm_module)
{
	if (!ctx)
		return -1;

	switch (ctx->jit_type) {
#if LLVM_HAS_LEGACY_JIT
	case LLVM_JIT_LEGACY:
		return llvm_jit_compile_legacy(ctx, llvm_module);
#endif

#if LLVM_HAS_MCJIT
	case LLVM_JIT_MCJIT:
		return llvm_jit_compile_mcjit(ctx, llvm_module);
#endif

#if LLVM_HAS_ORC
	case LLVM_JIT_ORC_V1:
		return llvm_jit_compile_orc_v1(ctx, llvm_module);
#endif

#if LLVM_HAS_ORCv2
	case LLVM_JIT_ORC_V2:
		return llvm_jit_compile_orc_v2(ctx, llvm_module);
#endif

	default:
		return -1;
	}
}

void* llvm_jit_get_function_address(LLVMJITContext *ctx, const char *name)
{
	if (!ctx || !name)
		return nullptr;

	switch (ctx->jit_type) {
#if LLVM_HAS_LEGACY_JIT
	case LLVM_JIT_LEGACY:
		return llvm_jit_get_address_legacy(ctx, name);
#endif

#if LLVM_HAS_MCJIT
	case LLVM_JIT_MCJIT:
		return llvm_jit_get_address_mcjit(ctx, name);
#endif

#if LLVM_HAS_ORC
	case LLVM_JIT_ORC_V1:
		return llvm_jit_get_address_orc_v1(ctx, name);
#endif

#if LLVM_HAS_ORCv2
	case LLVM_JIT_ORC_V2:
		return llvm_jit_get_address_orc_v2(ctx, name);
#endif

	default:
		return nullptr;
	}
}
