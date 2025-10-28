/*
 * QBE Backend Example
 *
 * Demonstrates the QBE backend generating SSA-based IR and compiling
 * it to native code.
 */

#include <stdio.h>
#include <stdlib.h>
#include "libcpu.h"
#include "backend.h"

/* Simple function to compile: add two numbers */
typedef int (*add_func_t)(int, int);

int main(int argc, char **argv)
{
	printf("=== QBE Backend Example ===\n\n");

	/* Create QBE backend */
	IBackend *backend = backend_create(BACKEND_QBE);
	if (!backend) {
		fprintf(stderr, "Failed to create QBE backend\n");
		return 1;
	}

	printf("Backend: %s version %s\n",
	       backend->GetName(backend),
	       backend->GetVersion(backend));

	/* Initialize backend */
	if (backend->Initialize(backend) != 0) {
		fprintf(stderr, "Failed to initialize QBE backend\n");
		backend->base.Release(&backend->base);
		return 1;
	}

	/* Create module */
	IModule *module = backend->CreateModule(backend, "qbe_test");
	if (!module) {
		fprintf(stderr, "Failed to create module\n");
		backend->base.Release(&backend->base);
		return 1;
	}

	/* Create builder */
	IBuilder *builder = module->CreateBuilder(module);
	if (!builder) {
		fprintf(stderr, "Failed to create builder\n");
		module->base.Release(&module->base);
		backend->base.Release(&backend->base);
		return 1;
	}

	printf("\nBuilding function: int add(int a, int b) { return a + b; }\n");

	/* Create function: int add(int a, int b) */
	IType *int_type = builder->GetIntType(builder, 32);
	IType *param_types[2] = { int_type, int_type };
	IFunction *func = module->AddFunction(module, "add", int_type, param_types, 2);

	/* Create basic block */
	IBasicBlock *entry = module->CreateBasicBlock(module, func, "entry");
	builder->PositionAtEnd(builder, entry);

	/* Get function parameters */
	IValue *param_a = builder->CreateConstInt(builder, int_type, 0, "a");
	IValue *param_b = builder->CreateConstInt(builder, int_type, 1, "b");

	/* Create addition: result = a + b */
	IValue *result = builder->CreateAdd(builder, param_a, param_b, "result");

	/* Return result */
	builder->CreateRet(builder, result);

	printf("\nGenerated QBE IL:\n");
	printf("----------------------------------------\n");
	module->Dump(module);
	printf("----------------------------------------\n");

	/* Compile module */
	printf("\nCompiling with QBE...\n");
	if (module->Compile(module) != 0) {
		fprintf(stderr, "Warning: QBE compilation failed (QBE may not be installed)\n");
		fprintf(stderr, "Install QBE from: https://c9x.me/compile/\n");

		/* Clean up */
		builder->base.Release(&builder->base);
		module->base.Release(&module->base);
		backend->base.Release(&backend->base);
		return 0;  /* Not a failure - QBE just not available */
	}

	/* Get function pointer */
	add_func_t add_native = (add_func_t)module->GetFunctionAddress(module, "add");
	if (!add_native) {
		fprintf(stderr, "Failed to get function address\n");
		builder->base.Release(&builder->base);
		module->base.Release(&module->base);
		backend->base.Release(&backend->base);
		return 1;
	}

	/* Test the compiled function */
	printf("\nTesting compiled function:\n");
	int a = 42;
	int b = 23;
	int sum = add_native(a, b);
	printf("add(%d, %d) = %d\n", a, b, sum);

	if (sum == a + b) {
		printf("\n✓ Test PASSED!\n");
	} else {
		printf("\n✗ Test FAILED! Expected %d, got %d\n", a + b, sum);
	}

	/* Performance comparison */
	printf("\n=== Performance Test ===\n");
	const int iterations = 1000000;

	/* Time native function */
	struct timespec start, end;
	clock_gettime(CLOCK_MONOTONIC, &start);
	volatile int qbe_sum = 0;
	for (int i = 0; i < iterations; i++) {
		qbe_sum += add_native(i, i + 1);
	}
	clock_gettime(CLOCK_MONOTONIC, &end);
	double qbe_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

	/* Time C function */
	clock_gettime(CLOCK_MONOTONIC, &start);
	volatile int c_sum = 0;
	for (int i = 0; i < iterations; i++) {
		c_sum += i + (i + 1);
	}
	clock_gettime(CLOCK_MONOTONIC, &end);
	double c_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

	printf("QBE-compiled: %.3f ms (%d iterations)\n", qbe_time * 1000, iterations);
	printf("C native:     %.3f ms (%d iterations)\n", c_time * 1000, iterations);
	printf("Overhead:     %.1f%%\n", (qbe_time / c_time - 1.0) * 100);

	/* Clean up */
	builder->base.Release(&builder->base);
	module->base.Release(&module->base);
	backend->Shutdown(backend);
	backend->base.Release(&backend->base);

	printf("\n=== QBE Backend Example Complete ===\n");
	return 0;
}
