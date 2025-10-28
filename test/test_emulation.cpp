/*
 * Test for Backend Emulation Layer
 *
 * Demonstrates transparent emulation of operations not supported by backends.
 */

#include "../libcpu/backend_emulation.h"
#include <stdio.h>
#include <math.h>

/* Test helper to print capabilities */
static void print_backend_capabilities(IBackend *backend)
{
	uint64_t caps = backend_get_capabilities(backend);
	const char *name = backend->GetName(backend);

	printf("\n=== %s Backend Capabilities ===\n", name);
	printf("Integer Division:     %s\n", (caps & BACKEND_CAP_INT_DIV) ? "YES" : "NO (emulated)");
	printf("Integer Remainder:    %s\n", (caps & BACKEND_CAP_INT_REM) ? "YES" : "NO (emulated)");
	printf("FP Basic:             %s\n", (caps & BACKEND_CAP_FP_BASIC) ? "YES" : "NO (emulated)");
	printf("FP Sqrt:              %s\n", (caps & BACKEND_CAP_FP_SQRT) ? "YES" : "NO (emulated)");
	printf("FP Trigonometric:     %s\n", (caps & BACKEND_CAP_FP_TRIG) ? "YES" : "NO (emulated)");
	printf("FP Transcendental:    %s\n", (caps & BACKEND_CAP_FP_TRANSCEND) ? "YES" : "NO (emulated)");
	printf("SIMD Integer:         %s\n", (caps & BACKEND_CAP_SIMD_INT) ? "YES" : "NO (emulated)");
	printf("SIMD Float:           %s\n", (caps & BACKEND_CAP_SIMD_FP) ? "YES" : "NO (emulated)");
	printf("Bit Manipulation:     %s\n", (caps & BACKEND_CAP_CTZ) ? "YES" : "NO (emulated)");
	printf("Atomic Operations:    %s\n", (caps & BACKEND_CAP_ATOMIC_CAS) ? "YES" : "NO (emulated)");
}

/* Test basic emulation */
static int test_basic_emulation(backend_type_t type)
{
	printf("\n### Testing %s backend ###\n", backend_get_name(type));

	/* Create emulated backend */
	IBackend *backend = backend_create_emulated(type);
	if (!backend) {
		printf("Failed to create backend\n");
		return -1;
	}

	print_backend_capabilities(backend);

	/* Initialize */
	if (backend->Initialize(backend) != 0) {
		printf("Failed to initialize backend\n");
		backend->base.Release(backend);
		return -1;
	}

	/* Create module */
	IModule *module = backend->CreateModule(backend, "test_emulation");
	if (!module) {
		printf("Failed to create module\n");
		backend->Shutdown(backend);
		backend->base.Release(backend);
		return -1;
	}

	/* Create function: int64_t test_div(int64_t a, int64_t b) */
	IType *i64 = module->GetInt64Type(module);
	IType *param_types[2] = {i64, i64};
	IType *func_type = module->GetFunctionType(module, i64, param_types, 2, 0);
	IFunction *func = module->CreateFunction(module, "test_div", func_type);

	/* Create entry block */
	IBasicBlock *entry = func->CreateBasicBlock(func, "entry");

	/* Create builder */
	IBuilder *builder = module->CreateBuilder(module);
	builder->SetInsertPoint(builder, entry);

	/* Get arguments */
	IValue *arg_a = func->GetArg(func, 0);
	IValue *arg_b = func->GetArg(func, 1);

	/* Test division - may be emulated depending on backend */
	IValue *div_result = builder->CreateSDiv(builder, arg_a, arg_b, "div");

	/* Test remainder - may be emulated */
	IValue *rem_result = builder->CreateSRem(builder, arg_a, arg_b, "rem");

	/* Add them together */
	IValue *sum = builder->CreateAdd(builder, div_result, rem_result, "sum");

	/* Return result */
	builder->CreateRet(builder, sum);

	/* Dump IR */
	printf("\nGenerated IR:\n");
	func->Dump(func);

	/* Compile */
	printf("\nCompiling...\n");
	if (module->Compile(module) != 0) {
		printf("Compilation failed\n");
	} else {
		printf("Compilation succeeded!\n");

		/* Get function pointer */
		typedef int64_t (*test_func_t)(int64_t, int64_t);
		test_func_t test_func = (test_func_t)module->GetFunctionAddress(module, "test_div");

		if (test_func) {
			/* Test execution */
			int64_t result = test_func(100, 7);
			int64_t expected = (100 / 7) + (100 % 7);  /* 14 + 2 = 16 */
			printf("Execution: test_div(100, 7) = %lld (expected %lld)\n",
			       (long long)result, (long long)expected);

			if (result == expected) {
				printf("✓ Test PASSED\n");
			} else {
				printf("✗ Test FAILED\n");
			}
		}
	}

	/* Cleanup */
	builder->base.Release(builder);
	module->base.Release(module);
	backend->Shutdown(backend);
	backend->base.Release(backend);

	return 0;
}

/* Test FPU emulation */
static int test_fpu_emulation(backend_type_t type)
{
	printf("\n### Testing FPU Emulation on %s ###\n", backend_get_name(type));

	IBackend *backend = backend_create_emulated(type);
	if (!backend || backend->Initialize(backend) != 0) {
		if (backend) backend->base.Release(backend);
		return -1;
	}

	IModule *module = backend->CreateModule(backend, "test_fpu");
	if (!module) {
		backend->Shutdown(backend);
		backend->base.Release(backend);
		return -1;
	}

	/* Create function: double test_sin(double x) */
	IType *f64 = module->GetDoubleType(module);
	IType *param_types[1] = {f64};
	IType *func_type = module->GetFunctionType(module, f64, param_types, 1, 0);
	IFunction *func = module->CreateFunction(module, "test_sin", func_type);

	IBasicBlock *entry = func->CreateBasicBlock(func, "entry");
	IBuilder *builder = module->CreateBuilder(module);
	builder->SetInsertPoint(builder, entry);

	IValue *arg_x = func->GetArg(func, 0);

	/* Use extended API for sin - automatically emulated */
	IValue *sin_result = builder_create_sin(builder, arg_x, "sin_x");

	if (sin_result) {
		builder->CreateRet(builder, sin_result);

		printf("Generated function with sin emulation\n");
		func->Dump(func);

		/* Compile and test */
		if (module->Compile(module) == 0) {
			typedef double (*sin_func_t)(double);
			sin_func_t sin_func = (sin_func_t)module->GetFunctionAddress(module, "test_sin");

			if (sin_func) {
				double result = sin_func(M_PI / 2.0);
				double expected = sin(M_PI / 2.0);
				printf("sin(π/2) = %.6f (expected %.6f)\n", result, expected);

				if (fabs(result - expected) < 0.0001) {
					printf("✓ FPU Test PASSED\n");
				} else {
					printf("✗ FPU Test FAILED\n");
				}
			}
		}
	} else {
		/* Fallback - just return the input */
		builder->CreateRet(builder, arg_x);
		printf("Sin emulation not available (non-emulated builder)\n");
	}

	builder->base.Release(builder);
	module->base.Release(module);
	backend->Shutdown(backend);
	backend->base.Release(backend);

	return 0;
}

/* Test capability detection for all backends */
static void test_all_capabilities(void)
{
	printf("\n========================================\n");
	printf("Backend Capability Detection Test\n");
	printf("========================================\n");

	backend_type_t types[] = {
		BACKEND_LLVM, BACKEND_QBE, BACKEND_GCCJIT, BACKEND_TCG,
		BACKEND_ASMJIT, BACKEND_DYNASM, BACKEND_SLJIT, BACKEND_NANOJIT,
		BACKEND_MIR, BACKEND_CRANELIFT, BACKEND_LIBJIT, BACKEND_NJ
	};

	for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
		IBackend *backend = backend_create(types[i]);
		if (backend) {
			print_backend_capabilities(backend);
			backend->base.Release(backend);
		}
	}
}

/* Test runtime emulation operations */
static void test_runtime_ops(void)
{
	printf("\n========================================\n");
	printf("Runtime Emulation Operations Test\n");
	printf("========================================\n");

	const emulation_fpu_ops_t *fpu = emulation_get_fpu_ops();
	const emulation_int_ops_t *intops = emulation_get_int_ops();
	const emulation_simd_ops_t *simd = emulation_get_simd_ops();

	/* Test FPU operations */
	printf("\nFPU Operations:\n");
	printf("sin(π/2) = %.6f\n", fpu->sin_f64(M_PI / 2.0));
	printf("cos(0) = %.6f\n", fpu->cos_f64(0.0));
	printf("sqrt(16) = %.6f\n", fpu->sqrt_f64(16.0));
	printf("pow(2, 8) = %.6f\n", fpu->pow_f64(2.0, 8.0));

	/* Test integer operations */
	printf("\nInteger Operations:\n");
	printf("sdiv(100, 7) = %lld\n", (long long)intops->sdiv_i64(100, 7));
	printf("srem(100, 7) = %lld\n", (long long)intops->srem_i64(100, 7));
	printf("clz(0xFF) = %u\n", intops->clz_i32(0xFF));
	printf("ctz(0xFF00) = %u\n", intops->ctz_i32(0xFF00));
	printf("popcnt(0xFF) = %u\n", intops->popcnt_i32(0xFF));

	/* Test SIMD operations */
	printf("\nSIMD Operations:\n");
	int32_t a_i32[4] = {1, 2, 3, 4};
	int32_t b_i32[4] = {5, 6, 7, 8};
	int32_t result_i32[4];
	simd->v4i32_add(result_i32, a_i32, b_i32);
	printf("v4i32_add([1,2,3,4], [5,6,7,8]) = [%d,%d,%d,%d]\n",
	       result_i32[0], result_i32[1], result_i32[2], result_i32[3]);

	float a_f32[4] = {1.0f, 2.0f, 3.0f, 4.0f};
	float b_f32[4] = {0.5f, 1.0f, 1.5f, 2.0f};
	float result_f32[4];
	simd->v4f32_mul(result_f32, a_f32, b_f32);
	printf("v4f32_mul([1,2,3,4], [0.5,1,1.5,2]) = [%.1f,%.1f,%.1f,%.1f]\n",
	       result_f32[0], result_f32[1], result_f32[2], result_f32[3]);
}

int main(void)
{
	printf("========================================\n");
	printf("libcpu Backend Emulation Layer Tests\n");
	printf("========================================\n");

	/* Test runtime operations */
	test_runtime_ops();

	/* Test capability detection */
	test_all_capabilities();

	/* Test emulation on different backends */
	test_basic_emulation(BACKEND_QBE);
	test_basic_emulation(BACKEND_TCG);
	test_basic_emulation(BACKEND_DYNASM);

	/* Test FPU emulation */
	test_fpu_emulation(BACKEND_QBE);

	printf("\n========================================\n");
	printf("All tests completed!\n");
	printf("========================================\n");

	return 0;
}
