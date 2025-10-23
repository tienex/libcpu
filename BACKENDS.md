# libcpu Backend Abstraction Layer

This document describes the backend abstraction layer introduced to libcpu, which allows support for multiple JIT compilation backends beyond LLVM.

## Overview

The libcpu backend abstraction layer provides a COM-style interface system that decouples the CPU emulation frontend from the code generation backend. This allows libcpu to support multiple JIT compilation backends including:

- **LLVM** - Full-featured optimizing compiler (default, fully implemented)
- **QBE** - Quick Backend, lightweight compiler (stub implementation)
- **GCCJIT** - GNU GCC JIT library (stub implementation)
- **TCG** - Tiny Code Generator from QEMU (stub implementation)

## Architecture

### COM-Style Interfaces

The backend system uses Component Object Model (COM) style interfaces with the following characteristics:

1. **Reference Counting** - All objects implement `AddRef()` and `Release()` for lifetime management
2. **Interface Query** - Objects support `QueryInterface()` for discovering capabilities
3. **Pure Virtual Interfaces** - Clean separation between interface and implementation
4. **Language Neutral** - C-compatible ABI for easy binding

### Interface Hierarchy

```
IBackend (Factory & Manager)
  │
  ├─> IModule (Compilation Unit)
  │     │
  │     ├─> IFunction (Function Definition)
  │     │     └─> IBasicBlock (Basic Block)
  │     │
  │     ├─> IBuilder (IR Builder)
  │     │
  │     └─> IType (Type System)
  │
  └─> IValue (Values & Instructions)
```

### Key Interfaces

#### IBackend
Main backend factory and manager. Responsibilities:
- Backend initialization and shutdown
- Module creation
- Feature detection
- Optimization level configuration
- Target information queries

```c
typedef struct IBackend {
    IUnknown base;
    const char* (*GetName)(IBackend *self);
    const char* (*GetVersion)(IBackend *self);
    int (*Initialize)(IBackend *self);
    void (*Shutdown)(IBackend *self);
    IModule* (*CreateModule)(IBackend *self, const char *name);
    void (*SetOptimizationLevel)(IBackend *self, uint32_t level);
    // ... more methods
} IBackend;
```

#### IModule
Represents a compilation unit. Responsibilities:
- Type creation (integers, floats, pointers, structs, etc.)
- Function creation and retrieval
- Global variable management
- IR Builder creation
- JIT compilation
- Function address resolution

```c
typedef struct IModule {
    IUnknown base;
    IType* (*GetInt32Type)(IModule *self);
    IFunction* (*CreateFunction)(IModule *self, const char *name, IType *function_type);
    IBuilder* (*CreateBuilder)(IModule *self);
    int (*Compile)(IModule *self);
    void* (*GetFunctionAddress)(IModule *self, const char *name);
    // ... more methods
} IModule;
```

#### IBuilder
IR instruction builder. Responsibilities:
- Instruction emission (arithmetic, logic, memory, control flow)
- Type conversions
- Comparisons
- Function calls
- PHI nodes

```c
typedef struct IBuilder {
    IUnknown base;
    void (*SetInsertPoint)(IBuilder *self, IBasicBlock *bb);
    IValue* (*CreateAdd)(IBuilder *self, IValue *lhs, IValue *rhs, const char *name);
    IValue* (*CreateLoad)(IBuilder *self, IType *type, IValue *ptr, const char *name);
    IValue* (*CreateStore)(IBuilder *self, IValue *val, IValue *ptr);
    IValue* (*CreateBr)(IBuilder *self, IBasicBlock *dest);
    // ... ~60+ instruction creation methods
} IBuilder;
```

#### IFunction
Function definition. Responsibilities:
- Basic block creation
- Argument access
- Verification
- Optimization
- Debugging output

#### IBasicBlock
Basic block in control flow graph. Responsibilities:
- Parent function access
- Terminator checking
- Name management

#### IType
Type system. Responsibilities:
- Type introspection
- Bit width queries
- Element type access (for pointers/arrays)

#### IValue
Values and instructions. Responsibilities:
- Type queries
- Name management
- Constant detection

## File Structure

```
libcpu/
├── backend.h              # Interface definitions (COM-style)
├── backend_impl.cpp       # Factory implementation
├── backend_llvm.cpp       # LLVM backend (complete)
├── backend_qbe.cpp        # QBE backend (stub)
├── backend_gccjit.cpp     # GCCJIT backend (stub)
└── backend_tcg.cpp        # TCG backend (stub)
```

## API Usage

### Creating a CPU with Default Backend (LLVM)

```c
cpu_t *cpu = cpu_new(CPU_ARCH_ARM, flags, arch_flags);
```

### Creating a CPU with Specific Backend

```c
cpu_t *cpu = cpu_new_with_backend(CPU_ARCH_ARM, flags, arch_flags, BACKEND_LLVM);
// or
cpu_t *cpu = cpu_new_with_backend(CPU_ARCH_ARM, flags, arch_flags, BACKEND_QBE);
```

### Querying Available Backends

```c
backend_type_t backends[BACKEND_MAX];
uint32_t count = backend_get_available(backends, BACKEND_MAX);

for (uint32_t i = 0; i < count; i++) {
    printf("Backend: %s\n", backend_get_name(backends[i]));
}
```

### Direct Backend API Usage

```c
// Create backend
IBackend *backend = backend_create(BACKEND_LLVM);
backend->Initialize(backend);

// Create module
IModule *module = backend->CreateModule(backend, "mymodule");

// Create types
IType *i32 = module->GetInt32Type(module);
IType *params[] = { i32, i32 };
IType *func_type = module->GetFunctionType(module, i32, params, 2, 0);

// Create function
IFunction *func = module->CreateFunction(module, "add", func_type);

// Create basic block
IBasicBlock *bb = func->CreateBasicBlock(func, "entry");

// Create builder and emit instructions
IBuilder *builder = module->CreateBuilder(module);
builder->SetInsertPoint(builder, bb);

IValue *arg0 = func->GetArg(func, 0);
IValue *arg1 = func->GetArg(func, 1);
IValue *result = builder->CreateAdd(builder, arg0, arg1, "sum");
builder->CreateRet(builder, result);

// Compile
module->Compile(module);

// Get function pointer
typedef int (*add_func_t)(int, int);
add_func_t add_fn = (add_func_t)module->GetFunctionAddress(module, "add");

// Execute
int sum = add_fn(5, 7); // Returns 12

// Cleanup
builder->base.Release(builder);
func->base.Release(func);
module->base.Release(module);
backend->base.Release(backend);
```

## Backend Implementation Guide

To implement a new backend:

### 1. Create Backend Factory

Implement the `backend_create_XXX()` function that returns a populated `IBackend` interface:

```c
extern "C" IBackend* backend_create_mybackend(void)
{
    MyBackend *backend = new MyBackend();
    backend->refcount = 1;

    // Setup interface function pointers
    backend->interface.base.AddRef = mybackend_addref;
    backend->interface.base.Release = mybackend_release;
    backend->interface.GetName = mybackend_get_name;
    backend->interface.Initialize = mybackend_initialize;
    // ... set all function pointers

    return (IBackend*)backend;
}
```

### 2. Implement Module Creation

The `CreateModule` method should create an `IModule` implementation with all required methods.

### 3. Implement Type System

Provide type creation methods that return `IType` wrappers around your backend's type representation.

### 4. Implement IR Builder

The `IBuilder` interface has ~60 methods for instruction emission. Each must:
- Accept backend-agnostic `IValue*` and `IType*` parameters
- Unwrap them to native backend types
- Emit the corresponding instruction
- Wrap the result in an `IValue*` and return it

### 5. Implement Compilation and Execution

- `IModule::Compile()` - Trigger JIT compilation
- `IModule::GetFunctionAddress()` - Return native function pointer

### 6. Register in Factory

Add your backend to `backend_impl.cpp`:

```c
case BACKEND_MYBACKEND:
    return backend_create_mybackend();
```

## Backend Comparison

| Feature | LLVM | QBE | GCCJIT | TCG |
|---------|------|-----|--------|-----|
| Status | Complete | Stub | Stub | Stub |
| Optimization | Excellent | Basic | Good | Minimal |
| Compile Speed | Slow | Fast | Medium | Very Fast |
| Code Quality | Excellent | Good | Good | Fair |
| FP80 Support | Yes (x86) | No | Yes (x86) | No |
| FP128 Support | No | No | Yes | No |
| Binary Size | Large | Small | Medium | Small |

## Integration with libcpu

The backend abstraction is integrated into the `cpu_t` structure:

```c
typedef struct cpu {
    // ... existing fields ...

    /* Backend abstraction */
    IBackend *backend;
    IModule *backend_module;
    backend_type_t backend_type;
} cpu_t;
```

During `cpu_new_internal()`:
1. Backend is created via `backend_create(backend_type)`
2. Backend is initialized via `backend->Initialize()`
3. For LLVM compatibility, traditional LLVM objects are still created
4. Module creation is deferred until first code generation

During `cpu_free()`:
1. Backend module is released if created
2. Backend is released
3. Traditional cleanup continues

## Design Principles

### 1. Backward Compatibility
The default `cpu_new()` uses LLVM backend, maintaining existing behavior.

### 2. Zero-Copy Semantics
Interface methods use pointer passing to avoid unnecessary copies.

### 3. Explicit Lifetime Management
Reference counting provides predictable resource cleanup.

### 4. Minimal Overhead
Wrapper objects are lightweight, containing only:
- Interface vtable pointer
- Reference count
- Pointer to native backend object
- Pointer to parent module

### 5. Type Safety
Each wrapper type is distinct, preventing accidental misuse.

## Future Work

### Short Term
1. Complete QBE backend implementation
2. Complete GCCJIT backend implementation
3. Complete TCG backend implementation
4. Add backend-specific optimization configuration
5. Add backend capability flags (SIMD, FP types, etc.)

### Long Term
1. Add interpreter backend for ultra-fast startup
2. Add tiered compilation (interpreter → TCG → LLVM)
3. Add ahead-of-time compilation support
4. Add IR serialization/deserialization
5. Add cross-compilation support

## Performance Considerations

### Memory
- Each wrapper object: ~32-64 bytes (vtable, refcount, native pointer, parent)
- Reference counting overhead: minimal (single integer increment/decrement)
- No deep copying of IR structures

### Speed
- Function call overhead: one indirect call per interface method
- Can be mitigated by:
  - Inline wrappers for hot paths
  - Batch operations where possible
  - Cache wrapper objects

### Optimization
- Backend-specific passes run during `IModule::Compile()`
- Optimization level controlled via `IBackend::SetOptimizationLevel()`
- Individual function optimization via `IFunction::Optimize()`

## Debugging

### IR Dumping
```c
// Dump module IR
module->Dump(module);

// Get IR as string
char *ir = module->ToString(module);
printf("%s\n", ir);
free(ir);

// Dump function IR
func->Dump(func);
```

### Verification
```c
char *error_msg = NULL;
if (func->Verify(func, &error_msg) != 0) {
    fprintf(stderr, "Verification failed: %s\n", error_msg);
    free(error_msg);
}
```

## License

This backend abstraction layer maintains the same license as libcpu.

## Contributing

When contributing backend implementations:

1. Ensure all interface methods are implemented
2. Add comprehensive error checking
3. Include backend-specific documentation
4. Add test cases for the new backend
5. Update this README with backend-specific notes

## References

- [LLVM Documentation](https://llvm.org/docs/)
- [QBE Documentation](https://c9x.me/compile/doc/il.html)
- [GCCJIT Documentation](https://gcc.gnu.org/onlinedocs/jit/)
- [TCG Documentation](https://wiki.qemu.org/Documentation/TCG)
