# UPCL - Universal Processor Coding Language

UPCL is a domain-specific language for describing processor architectures, instruction sets, and their behavior. It enables automatic generation of CPU emulation code for the libcpu dynamic binary translation framework.

## Overview

UPCL allows you to define:
- **Processor architectures** with their fundamental characteristics (endianness, word size, etc.)
- **Register files** including general-purpose registers, special registers, and register aliasing
- **Instruction semantics** through high-level expressions that describe the behavior of each instruction
- **Decoder operands** for parsing instruction encodings

The UPCL compiler (`upcc`) processes `.def` files and generates C++ code that integrates with libcpu's LLVM-based JIT compilation infrastructure.

## Architecture

```
.def files (UPCL source)
    ↓
upcc compiler (Parser + Semantic Analysis + Code Generation)
    ↓
Generated C++ files (arch.h, regfile.h, translate.cpp, etc.)
    ↓
libcpu framework
    ↓
LLVM IR + JIT Compilation
```

## Language Features

### 1. Architecture Definition

```upcl
arch "example" {
    name "Example Processor";
    endian little;           // or big
    byte_size 8;            // bits per byte
    word_size 32;           // bits per word
    psr_size 32;            // processor status register size
    address_size 32;        // address bus width

    register_file {
        // ... register definitions ...
    }
}
```

### 2. Register File Definition

```upcl
register_file {
    // General purpose registers
    group R {
        [ #i32 r0, r1, r2, r3, r4, r5, r6, r7 ]
    }

    // Special registers
    group S {
        [ #i32 pc -> %PC ],      // Program counter
        [ #i32 psr -> %PSR ]     // Processor status register
    }

    // Register aliasing (e.g., AX = AH:AL in x86)
    group A {
        [ #i16 ax { #i8 ah, #i8 al } ]
    }
}
```

### 3. Instruction Definition

```upcl
insn add_immediate {
    decode "0100" @ opcode:4 @ rd:3 @ imm:8;

    // Instruction semantics
    R[rd] = R[rd] + #i32(imm);

    // Update flags
    %PSR.Z = (R[rd] == 0);
    %PSR.N = (R[rd] < 0);
}
```

### 4. Expression System

UPCL provides a rich expression language:

| Operation | Syntax | Description |
|-----------|--------|-------------|
| Arithmetic | `+`, `-`, `*`, `/`, `%` | Basic arithmetic |
| Bitwise | `&`, `|`, `^`, `~` | Bitwise operations |
| Shift | `<<`, `>>`, `<<<`, `>>>` | Logical and arithmetic shifts |
| Rotate | `rol`, `ror`, `rolc`, `rorc` | Rotate with/without carry |
| Comparison | `==`, `!=`, `<`, `<=`, `>`, `>=` | Comparisons |
| Cast | `#i32(expr)`, `#u16(expr)` | Type casting |
| Bit Slice | `expr[start:count]` | Extract bit range |
| Memory | `M[addr]` | Memory access |

### 5. Type System

- **Integers**: `#i8`, `#i16`, `#i32`, `#i64` (signed)
- **Unsigned**: `#u8`, `#u16`, `#u32`, `#u64` (unsigned)
- **Floats**: `#f32`, `#f64` (floating point)

## Building UPCL

### Prerequisites

- CMake 2.8+
- C++ compiler with C++11 support
- Flex (lexical analyzer generator)
- Bison (parser generator)
- LLVM development libraries

### Build Instructions

```bash
mkdir build
cd build
cmake ..
make upcc
```

The compiled `upcc` executable will be located in `build/upcl/upcc`.

## Using the UPCL Compiler

### Basic Usage

```bash
upcc input.def -o output_prefix
```

This generates:
- `output_prefix_arch.h` - Architecture metadata
- `output_prefix_regfile.h` - Register file definitions
- `output_prefix_arch.cpp` - Architecture initialization
- `output_prefix_opc.h` - Opcode enumeration
- `output_prefix_tag_stub.cpp` - Instruction tagging
- `output_prefix_translate_stub.cpp` - Translation stubs

### Example Architectures

UPCL includes several example architecture definitions:

| Architecture | File | Description |
|--------------|------|-------------|
| MOS 6502 | `examples/6502.def` | 8-bit microprocessor |
| Motorola 68000 | `examples/m68k.def` | 32-bit processor |
| MIPS | `examples/mips.def` | RISC processor |
| Intel 8086 | `examples/8086.def` | 16-bit x86 processor |
| SPARC | `examples/sparc.def` | RISC processor |

## Code Organization

```
upcl/
├── README.md              # This file
├── CMakeLists.txt         # Build configuration
├── main.cpp               # Compiler entry point
│
├── ast/                   # Abstract Syntax Tree
│   ├── ast.h             # AST node definitions
│   ├── parser.y          # Bison grammar
│   ├── lexer.l           # Flex lexer
│   └── parse.cpp         # Parser implementation
│
├── sema/                  # Semantic Analysis
│   ├── register_file_builder.cpp  # Register file processing
│   ├── register_dep_tracker.cpp   # Dependency tracking
│   ├── expr_convert.cpp           # Expression conversion
│   └── simple_expr_evaluator.cpp  # Constant folding
│
├── c/                     # C++ Intermediate Representation
│   ├── sema_analyzer.cpp # Main semantic analyzer
│   ├── expression.cpp    # Expression hierarchy
│   ├── instruction.h     # Instruction representation
│   └── type.h            # Type system
│
├── cg/                    # Code Generation
│   ├── generate.cpp      # Main code generator
│   └── libcpu_expression_generator.cpp  # Expression codegen
│
├── docs/                  # Documentation
│   ├── EBNF              # Grammar specification
│   └── TEST_EXPRESSIONS  # Expression test cases
│
└── examples/              # Example architecture definitions
    ├── 6502.def
    ├── m68k.def
    ├── mips.def
    └── ... (others)
```

## Recent Improvements (2025)

### Build System Fixes
- ✅ Re-enabled UPCL compiler build (was disabled since 2014)
- ✅ Fixed 64-bit Linux compilation issues
- ✅ Resolved ambiguous `fromInteger()` overload calls
- ✅ Fixed format string warnings for `uint64_t` (now uses `PRIu64`)
- ✅ Added virtual destructors to polymorphic classes

### Code Quality
- ✅ Eliminated compiler warnings on modern toolchains
- ✅ Improved type safety with explicit casts
- ✅ Better C++11/14/17 compatibility

### Documentation
- ✅ Added comprehensive README (this document)
- ✅ Documented language features and architecture
- ✅ Provided usage examples

## Expression Visitor Pattern (Planned)

Future improvements will include a proper visitor pattern for traversing the expression tree, enabling:
- Easier addition of new expression transformations
- Better separation of concerns
- More extensible optimization passes

## Contributing

When modifying UPCL:

1. **Maintain backward compatibility** with existing `.def` files
2. **Update grammar documentation** in `docs/EBNF`
3. **Add test cases** for new features
4. **Follow the existing code style** (K&R braces, tabs for indentation)
5. **Test with multiple architectures** to ensure changes work broadly

## Known Limitations

- Some TODOs remain for advanced features (see comments in source)
- Register aliasing has some edge cases with bitfield naming
- Condition code handling needs refinement in some cases
- Large sema_analyzer.cpp could benefit from modularization

## References

- **libcpu**: The main CPU emulation framework
- **LLVM**: Backend for JIT compilation
- **Examples**: See `examples/` directory for real-world UPCL code

## License

See the main libcpu LICENSE file for licensing information.

## Contact

For bugs, questions, or contributions, please file issues on the libcpu GitHub repository.
