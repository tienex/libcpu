# UPCL Language Reference Manual

UPCL (Unified Processor Characterization Language) is the CPU-description DSL used
by the LibCPU framework to describe instruction-set architectures. An architect writes
one `.upcl` file per ISA family; LibCPU interprets it at runtime through a generic
decoder, semantics translator, and register-layout builder — no per-ISA C++ glue is
required beyond the file itself.

This manual documents the language as implemented by the engine in
`LibCPU/upcl/` on the `feat/com-core` branch, cross-referenced against real examples
in `upcl/examples/`.

---

## Table of Contents

1. [Overview and Toolchain](#1-overview-and-toolchain)
2. [Lexical Structure](#2-lexical-structure)
3. [The `arch` Block](#3-the-arch-block)
4. [The `register_file` Declaration](#4-the-register_file-declaration)
5. [The `decoder_operands` Declaration](#5-the-decoder_operands-declaration)
6. [The `addrmode` Declaration](#6-the-addrmode-declaration)
7. [Instructions: `insn` and `jump insn`](#7-instructions-insn-and-jump-insn)
8. [Semantics Expression Language](#8-semantics-expression-language)
9. [Features and CPU Models: `feature` and `cpu`](#9-features-and-cpu-models-feature-and-cpu)
10. [Compile-Phase vs Generate-Phase Constant Folding](#10-compile-phase-vs-generate-phase-constant-folding)
11. [Worked Mini-ISA: End to End](#11-worked-mini-isa-end-to-end)
12. [Appendix: Quick-Reference Table](#12-appendix-quick-reference-table)

---

## 1. Overview and Toolchain

### What UPCL Is

UPCL is a declarative, runtime-interpreted description language for instruction-set
architectures. A description file contains:

- An `arch` block with machine-level attributes (word size, endianness, register
  file, etc.)
- Named addressing modes (`addrmode`) that describe variable-length operand decoding
- Instruction definitions (`insn` and `jump insn`) that combine an encoding pattern,
  a disassembly format, and a semantics body
- Optional `feature` and `cpu` blocks that model ISA variants

The engine (`LibCPU/upcl/`) parses the file into an AST (`Ast.h`), flattens the
register file into a physical layout (`RegisterLayout.cpp`), runs a generic table-
driven decoder (`Decoder.cpp`), and translates each instruction's semantics body
through an emitter interface (`Semantics.cpp`). No ISA-specific C++ is needed.

### Runtime-Interpreted Model

The decode path (`Decoder.cpp`) walks the enabled instruction encodings in order,
extracting fields from the byte stream via a big-endian bit-cursor over the opcode
word. When a match is found the decoder resolves each `@addrmode` operand (reading
any extension bytes from the byte tail), producing a flat `Fields` map that the
`Translator` in `Semantics.cpp` uses to evaluate the instruction body against the
emitter.

An architecture is selected at runtime — the same `lcx` binary handles every
UPCL-described ISA:

```
lcx upcl check  upcl/examples/pdp11.upcl
lcx upcl decode upcl/examples/pdp11.upcl  AA BB CC ...
lcx upcl produce upcl/examples/mips.upcl
lcx run    <binary> --arch upcl:upcl/examples/pdp11.upcl
lcx run    <binary> --arch upcl:upcl/examples/pdp11.upcl@pdp11/45
lcx disasm <binary> --arch upcl:upcl/examples/x86.upcl@v30
```

The optional `@<cpu>` suffix selects a CPU model (see §9). Without it the base ISA
(all ungated instructions) is used.

### Tool Commands

| Command | Effect |
|---------|--------|
| `lcx upcl check <file>` | Parse and validate; print diagnostics; exit 0 on success |
| `lcx upcl decode <file> [bytes...]` | Decode one instruction from the given hex bytes |
| `lcx upcl produce <file>` | Run the code-generation path (for AOT/static frontends) |
| `lcx run <bin> --arch upcl:<file>[@cpu]` | Execute a binary with the described ISA |
| `lcx disasm <bin> --arch upcl:<file>[@cpu]` | Disassemble using the `disasm` clauses |

---

## 2. Lexical Structure

### Source Files

UPCL source files are UTF-8 text. Comments use C++ syntax: `//` to end of line and
`/* ... */` blocks. Include directives splice external files:

```
include "x86/8086.upcl";       // relative to the including file
```
(See `upcl/examples/x86.upcl` line 120.)

### Identifiers and Keywords

Identifiers follow the rule `[A-Za-z_][0-9A-Za-z_]*`. UPCL has no reserved words:
every keyword is recognized by spelling in context. The lexer emits a single `TokIdent`
token for all words; the parser dispatches by text (`LibCPU/upcl/Token.h` lines 25–26,
`Parser.cpp` lines 31–34).

This means an identifier named `insn` is legal outside a declaration context.

### Meta-Identifiers

A percent-prefixed word (`%word`) is a `TokMeta` token (`Token.h` line 30). The parser
recognizes the following meta-identifiers:

| Meta-identifier | Meaning |
|-----------------|---------|
| `%PC` | Program counter meta-role (bind target in `register_file`) |
| `%PSR` | Processor status register meta-role (bind target) |
| `%NPC` | Next-PC slot (MIPS delay-slot; `mips.upcl` line 59) |
| `%N` | Negative / sign flag (condition code) |
| `%Z` | Zero flag |
| `%V` | Overflow flag |
| `%C` | Carry flag |
| `%P` | Parity flag |
| `%M` | Memory access expression (`%M[addr]`) |
| `%MEM` | Synonym for `%M` |
| `%PM` | Physical memory access (MMU bypass; used in `mmu { translate }` bodies) |
| `%LL` | Load-linked memory access (for `%SC` store-conditional pairs) |
| `%SC` | Store-conditional: `%SC[addr] <- value` |
| `%S` | Sign-extend augment: `%S(expr)` |
| `%U` | Zero-extend augment: `%U(expr)` |
| `%CC` | Condition-code recorder: `%CC(expr [, flags])` |
| `%OFTRAP` | Overflow-trap: `%OFTRAP(expr, trap_expr)` |
| `%FLT` | Bitcast integer bits to float: `%FLT(expr)` |
| `%INT` | Bitcast float bits to integer: `%INT(expr)` |
| `%EVAL` | Compile-phase constant marker (force evaluation at decode time) |
| `%GEN` | Generate-phase marker (suppress folding; emit a runtime node) |
| `%REG` | Field-indexed register reference: `%REG[group, field]` |
| `%result` | Macro return-value slot |

### Macro-Identifiers

An at-prefixed word (`@word`) is a `TokMacroIdent` token (`Token.h` line 33).
`@name(args)` calls a macro; `@lea(operand)` evaluates an operand's effective address
(see §8); `@trap(vector)` raises a trap.

### Numeric Literals

```
decimal     42
hexadecimal 0x2a
octal       052
binary      0b101010
```
(`ebnf.txt` line 4.)

Floating-point literals (`3.14`, `1e10`) are also accepted by the lexer
(`Token.h` line 28).

### Type Literals

A `#`-prefixed token names a UPCL type (`Token.h` line 29; `Ast.h` lines 31–40):

| Syntax | Meaning |
|--------|---------|
| `#iN` | N-bit unsigned/signed integer (e.g. `#i16`, `#i32`) |
| `#fN` | N-bit IEEE floating-point (e.g. `#f32`, `#f64`, `#f80`) |
| `#vL:W` | Vector of L lanes, each W bits wide (e.g. `#v4:32`) |

Type literals appear in register declarations, cast expressions, memory accesses, and
encoding-field width modifiers. The `Parser::ParseType` function decodes them from
their spelling (`Parser.cpp` lines 91–114).

### Operators and Punctuation

The full operator set (`Token.h` lines 41–54):

| Category | Tokens |
|----------|--------|
| Arithmetic | `+` `-` `*` `/` `%` |
| Bitwise | `&` `\|` `^` `~` `<<` `>>` `<<>` (rol) `>><` (ror) |
| Masked ops | `&~` (AndCom) `\|~` (OrCom) `^~` (XorCom) |
| Comparison | `==` `!=` `<` `<=` `>` `>=` |
| Logical | `&&` `\|\|` `!` |
| Binding | `->` `<-` `<->` |
| Range | `..` (exclusive bit range) |
| Repeat | `**` (register repetition) |
| Compound assign | `+=` `-=` `*=` `/=` `%=` `\|=` `&=` `^=` `<<=` `>>=` `<<>=` `>><=` `&~=` `\|~=` `^~=` |

---

## 3. The `arch` Block

An `arch` block is the top-level container. Its name is the architecture identifier
used in `--arch upcl:<file>` paths.

```
arch "pdp11" {
    name "DEC PDP-11";
    endian little;
    byte_size 8;
    word_size 16;
    address_size 16;
    float_size 32;

    register_file { ... }
}
```
(`upcl/examples/pdp11.upcl` lines 22–65.)

### Directives

| Directive | Type | Meaning |
|-----------|------|---------|
| `name "..."` | string | Human-readable full name (returned by `GetInfo`) |
| `endian little\|big\|both` | keyword | Architecture endianness |
| `default_endian little\|big` | keyword | Default when `both`; e.g. MIPS (`mips.upcl` line 27) |
| `byte_size N` | integer | Bits per byte (almost always 8) |
| `word_size N` | integer | Default machine word width in bits |
| `address_size N` | integer | Address register width in bits |
| `float_size N` | integer | Native float width in bits (for the FPU registers) |
| `psr_size N` | integer | Processor-status register width in bits |
| `address_display flat\|segmented shift N offset M` | | Address display format (§2.1 note) |
| `register_file { ... }` | block | Register declarations (§4) |
| `registers { r0, r1, ... }` | list | Flat new-syntax register list (alternative to `register_file`) |

The parser processes these in `Parser::ParseArchItem` (`Parser.cpp` lines 994–1043).
All directives except `register_file` are followed by a semicolon.

#### Note on `address_display`

For segmented architectures (real-mode x86) addresses are shown as `seg:off`:

```
address_display segmented shift 4 offset 16;
```
(`upcl/examples/x86.upcl` line 62.) `shift N` is the left-shift applied to the
segment, `offset M` is the bit width of the offset field.

---

## 4. The `register_file` Declaration

A `register_file` block contains one or more `group` declarations. Each group names
a set of physical registers and optionally binds them to meta-roles or sub-register
layouts.

### Groups

```
group R {
    [ 6 ** #i16 r?:0 ],          // r0 .. r5
    [ #i16 sp ],                 // r6 = SP
    [ #i16 pc -> %PC ]           // r7 = PC
}
```
(`upcl/examples/pdp11.upcl` lines 36–41.)

A group declaration has two syntactic forms (parsed in `Parser::ParseGroup`,
`Parser.cpp` lines 815–839):

```
group <name> : <reg_decl> ;               // single register in a group
group <name> { <reg_decl>, <reg_decl>, ... }   // multiple registers
```

### Register Declarations

Each register declaration is enclosed in square brackets:

```
[ (repeat **)? type name(?)? (-> binding | <- alias-expr)? ]
```

| Part | Meaning |
|------|---------|
| `N **` | Repeat the declaration N times (expands at layout time) |
| `type` | A type literal: `#i16`, `#f64`, `#v4:32`, etc. |
| `name` | The register's name |
| `name?` | Mark this register as auto-indexed (repeatable); use with `**` |
| `name?:N` | Auto-indexed, starting at index N (e.g. `r?:1` → r1..r31) |
| `-> %PC` | Bind this register as the program counter meta-role |
| `-> %PSR <splitter>` | Bind as the processor-status register; the splitter maps bit fields |
| `-> id` | Simple alias to another register name |
| `<- expr` | Hardwire the register to a constant expression (e.g. `r0 <- 0`) |

Examples:

```
[ 31 ** #i64 r?:1 ]    // r1..r31 (mips.upcl line 41)
[ #i64 r0 <- 0 ]       // zero register, writes discarded (mips.upcl line 40)
[ #i64 pc -> %PC ]     // PC binding (mips.upcl line 58)
```

### Positional Aliases

When a group named `R` contains N registers declared with `N ** #iW name?:0`, the
layout builder automatically generates `R0`, `R1`, ... aliases. The same encoding
field (e.g. `sr:3`) can then index the group by number (the `%REG[R, field]` form,
§6.5).

### Meta-Role Bindings: `-> %PC` and `-> %PSR`

A register bound to `%PC` becomes the program counter. The decoder reads it to
compute branch targets; the emitter uses it for PC-relative displacement resolution.
A register bound to `%PSR` is the processor status word; its splitter fields
map individual flag bits to named meta-roles.

### PSR Splitters: `explicit ( field : field : ... )`

The full PSR syntax binds a register to `%PSR` and then names its bit fields with
a colon-separated list (MSB-first):

```
[ #i16 psw -> %PSR #i16 explicit
    ( #i11 0 : #i1 T : #i1 N -> %N : #i1 Z -> %Z : #i1 V -> %V : #i1 C -> %C ) ]
```
(`upcl/examples/pdp11.upcl` lines 45–46.)

Each field entry in the list is:

| Form | Meaning |
|------|---------|
| `0` | Reserved/hardwired-zero bits (consume space, no name) |
| `name` | A named flag bit (1 bit, no meta binding) |
| `name -> %META` | Named flag bound to a meta condition code (`%N`, `%Z`, `%V`, `%C`, `%P`) |
| `name <- src` | Flag that reads from another field (e.g. `A <- C` in x86 flags) |
| `name <-> src` | Bidirectional alias |
| `#iN ...` | Per-field type to override the splitter's element width |

The `explicit` keyword requires the bit widths to sum exactly to the register's width
(a layout-validation check). Without `explicit` the engine infers widths.

A 1-bit sub-field produces a `RegFlag` in the layout (a named flag that the body can
read/write by name); a wider sub-field produces a sub-register entry.

The 6502 PSR declaration:

```
[ #i8 P -> %PSR #i1 explicit ( N->%N:V->%V:0:B:D:I:Z->%Z:C->%C ) ]
```
(`upcl/examples/6502.upcl` line 39.) The `0` constant consumes bit 5 (unused);
`B` and `D` are named but unbound flags.

### Union Form `[ type ... ]`

The square-bracket form of the splitter declares a union layout (typed fields sharing
storage). The x86 16-bit PC is composed from two separate registers:

```
[ #i24 pc -> %PC #i16 explicit evaluate ( @ea ( seg, off ) ) ( seg <- cs : off <-> ip ) ]
```
(`upcl/examples/x86.upcl` lines 40–41.) The `evaluate` clause provides the expression
used to compute the PC value from the component registers.

### Sub-Register Aliases

A register can describe its own sub-registers in-line:

```
[ #i16 bx -> #i8 ( bh : bl ) ]
```
(`upcl/examples/x86.upcl` line 24.) This gives `bx` two 8-bit sub-views: `bh`
(bits 15:8) and `bl` (bits 7:0).

---

## 5. The `decoder_operands` Declaration

`decoder_operands` names the typed placeholders that instruction encodings bind
their fields to. The decoder resolves each field into one of these operands and
exposes them by name to the semantics body.

```
decoder_operands [
    #i16 src, #i16 dst,        // resolved source / destination operands
    #i8  boff,                 // branch: signed 8-bit word offset
    #i6  trapn,                // EMT/TRAP code
    #i32 fsrc, #i32 fdst       // FP11: 32-bit floating source / destination
];
```
(`upcl/examples/pdp11.upcl` lines 67–75.)

Syntax (parsed in `Parser::ParseDecoderOperands`, `Parser.cpp` lines 1547–1565):

```
decoder_operands [ (const|ccflags)? type name (, ...)* ];
```

| Prefix | Meaning |
|--------|---------|
| (none) | Normal operand: the raw decoded value (immediate or memory address) |
| `const` | Compile-time constant; the decoder folds it immediately |
| `ccflags` | Condition-code flags operand (carries the flag set for a conditional) |

Operand types (`Ast.h` lines 519–528):

```cpp
typedef enum _DECOP_KIND { DecopNormal, DecopConst, DecopCcflags } DECOP_KIND;
```

The 6502 declaration (`6502.upcl` line 48):

```
decoder_operands [ #i8 imm, #i8 zp, #i16 abs, #i16 tgt ];
```

The x86 declaration includes a `ccflags` and a `const` operand (`x86.upcl` line 58):

```
decoder_operands [ #i16 src, #i16 dst, #i8 cnt, #i8 sti, ccflags #i16 cond, const #i1 rep ];
```

---

## 6. The `addrmode` Declaration

`addrmode` defines a named table of address-resolution rules. An instruction's
encoding field references an addrmode to decode a variable-length operand from the
byte tail following the opcode word.

### Basic Structure

```
addrmode <name> ( <param>, ... ) [disp ( <bits-expr> )] {
    <cond> => <resolution> ;
    ...
}
```

| Part | Meaning |
|------|---------|
| `name` | Identifier used in `-> op @<name>` field bindings |
| `( params )` | Comma-separated selector field names (e.g. `mod, rm`; `sm, sr`) |
| `disp( expr )` | Default displacement bit width expression |
| Rules | One or more `condition => resolution` lines |

The selector fields (`params`) are the decoded bit fields that drive rule selection.
Each rule's condition is evaluated against these fields.

### Rules and Conditions

```
addrmode modrm16 ( mod, rm ) disp ( mod == 1 ? 8 : mod == 2 ? 16 : 0 ) {
    mod == 3            => reg [ ax, cx, dx, bx, sp, bp, si, di ] ;
    mod == 0 && rm == 6 => mem [ disp16 ] ;
    rm == 0             => mem [ bx + si + disp ] ;
    ...
}
```
(`upcl/examples/x86.upcl` lines 71–82.)

The condition is a full UPCL expression over the selector fields. When no condition
matches, the operand is undefined (a decode error). A rule without a condition (the
keyword `default`) always matches; a rule with a trivially-true condition also always
matches.

Parsed in `Parser::ParseAddrRule` (`Parser.cpp` lines 1281–1335).

### Register-Direct Rules: `reg [...]`

```
sm == 0 => reg [ r0, r1, r2, r3, r4, r5, sp, pc ] ;
```
(`upcl/examples/pdp11.upcl` line 88.)

The field's decoded integer value selects a register from the list by position. The
operand resolves to that register's location (a readable/writable register cell).

### Memory Rules: `mem [...]` and `%M [...]`

A memory rule declares the operand is a memory cell at the computed effective address:

```
rm == 0  => mem [ bx + si + disp ] ;
sm == 6  => %M [ %REG[ R, sr ] + disp ] ;
```

The address expression is a sum of named registers and displacement terms. Both `mem`
and `%M` are accepted (the latter is consistent with memory expressions in semantics
bodies; parsed as identical in `Parser.cpp` line 1301).

### Displacement Terms

Within a memory rule's address expression, displacement terms consume bytes from the
instruction's byte tail:

| Term | Width | Description |
|------|-------|-------------|
| `disp` | addrmode's `disp(...)` value | Default-width displacement from the addrmode header |
| `disp8` | 8 bits | Fixed 8-bit displacement |
| `disp16` | 16 bits | Fixed 16-bit displacement |
| `disp varlen` | Variable | Self-describing variable-length displacement (NS32000 extension — see §6.7) |

The `disp` term refers to the width computed by the addrmode's `disp(expr)` clause at
decode time. A `disp(0)` addrmode has no displacement when that condition evaluates to
zero (`pdp11.upcl` line 87: `( sm == 6 || sm == 7 ) ? 16 : 0`).

Multiple displacement terms may appear in a single rule; they are consumed left-to-right
from the byte tail (see §6.7).

### `%REG[group, field]`: Field-Indexed Base Register

```
sm == 2 => %M [ %REG[ R, sr ] ] post { %REG[ R, sr ] += 2; } ;
```
(`upcl/examples/pdp11.upcl` line 90.)

`%REG[group, field]` resolves to the element of register group `group` at the position
given by the decoded value of `field` (a decoder operand or field name). This collapses
one rule per register into a single rule: instead of eight rules for modes 0..7 (one per
register), a single `%REG[R, sr]` covers all eight. The same construct works as an
lvalue in pre/post blocks and in semantics bodies (`Parser.cpp` lines 260–272).

### Per-Rule Operand Width Pinning

A leading type on the resolution side overrides the default operand data width for that
rule. This drives the PDP-11 byte vs word mode split:

```
addrmode ea_srcb ( sm, sr ) disp ( ... ) {
    sm == 0 => #i8 reg [ r0, r1, r2, r3, r4, r5, sp, pc ] ;  // byte: low byte of register
    sm == 1 => #i8 %M [ %REG[ R, sr ] ] ;                   // byte memory read
    ...
}
```
(`upcl/examples/pdp11.upcl` lines 119–125.)

The `#i8` before `reg` or `%M` pins the rule to 8-bit access; without it the addrmode
inherits the instruction's operand width. `DataBits` in `AddrRule` (`Ast.h` line 204)
holds this value; 0 means inherited.

The MOVB special case requires a register-direct rule that is full-width (`#i16`) while
memory rules remain byte-width:

```
addrmode ea_dstb_x ( dm, dr ) ... {
    dm == 0 => #i16 reg [ r0, r1, r2, r3, r4, r5, sp, pc ] ;  // sign-extend into 16-bit reg
    dm == 1 => #i8 %M [ %REG[ R, dr ] ] ;
    ...
}
```
(`upcl/examples/pdp11.upcl` lines 137–143.)

### `pre { }` and `post { }` Side-Effect Blocks

PDP-11 autodecrement `-(Rn)` decrements the register BEFORE the operand is read;
autoincrement `(Rn)+` increments AFTER. These are expressed as `pre`/`post` blocks:

```
sm == 2 => %M [ %REG[ R, sr ] ] post { %REG[ R, sr ] += 2; } ;  // (Rn)+
sm == 4 => pre { %REG[ R, sr ] -= 2; } %M [ %REG[ R, sr ] ] ;  // -(Rn)
```
(`upcl/examples/pdp11.upcl` lines 90–91.)

The `pre { }` block executes BEFORE the effective address is computed (so the operand
sees the already-decremented base); the `post { }` block is deferred until AFTER the
instruction body has used the operand. This ordering is maintained by
`Translator::BindOperand` in `Semantics.cpp` (lines 56–66): pre blocks are emitted
immediately, post blocks are queued and flushed at end of instruction.

Blocks are full statement sequences (assignments, if/for/while) over any in-scope
registers and the addrmode's selector fields.

### Named Register Sets: `regset`

A `regset` declares a reusable named list of registers. An encoding field's
`-> op[gpr16]` expands the alias when the field is decoded:

```
regset gpr16 [ ax, cx, dx, bx, sp, bp, si, di ];
regset gpr8  [ al, cl, dl, bl, ah, ch, dh, bh ];
```
(`upcl/examples/x86.upcl` lines 64–65.)

The decoder expands regsets transparently in `Decoder::ExpandRegMap`
(`Decoder.cpp` lines 64–77).

---

## 6.7. Engine Extensions E1–E3: Big-Endian Override, Variable-Length Displacement, Nested Scaled-Index

These three extensions were added to the engine to enable the NS32000 (`ns32k`) frontend.
They are additive — all existing ISA descriptions are completely unaffected (every new
AST field defaults to the old behavior).

Commits: E1 `441a71f`, E2 `0be964c`, E3 `209d10b` (branch `feat/com-core`).

---

### E1: Per-Field Big-Endian Override (`be`)

**Status: Implemented.**  Engine: `LibCPU/upcl/Ast.h` line 274 (`EncField::BigEndian`),
line 189 (`AddrTerm::DispBigEndian`); `LibCPU/upcl/Parser.cpp` line 1245 (field parser),
line 1360 (disp parser); `LibCPU/upcl/Decoder.cpp` lines 441, 466 (word/tail field
sites), line 370 (displacement site).

A `be` keyword placed after a field's width specification (in an `encode` clause) or
immediately after a `disp`/`disp8`/`disp16` term forces big-endian extraction for that
one field, regardless of the architecture's `endian` setting.

**Syntax — tail field:**

```
// Little-endian arch, one instruction: 1-byte opcode 0xAA then a 16-bit immediate
// that is stored big-endian. `be` on the field forces big-endian extraction.
arch "_test_be" { ... endian little; byte_size 8; word_size 16; address_size 16; ... }

insn ld : encode #i8 ( op:8 = 0xAA, imm:16 -> imm be ),
    disasm ( mnemonic : "ld", operands : imm )
    { r0 = imm; }
```

(`upcl/examples/_test_be.upcl` — the complete working test arch.)

**Actual decoded output:**

```
$ lcx upcl decode _test_be.upcl 0xAA 0x12 0x34
0x0000: ld     imm=0x1234   (3 byte(s))
```

Without `be` on the same little-endian arch the same bytes decode as `imm=0x3412`.
The `be` suffix forces `ExtractField(..., /*little=*/false)` regardless of the arch
`Little` flag (`F.BigEndian ? false : Little` in the decoder).

**Syntax — displacement:**

```
%M [ r0 + disp16 be ]    // big-endian 16-bit displacement in a little-endian arch
```

The `be` qualifier is accepted on `disp`, `disp8`, and `disp16`; it maps to
`AddrTerm::DispBigEndian` and the decoder passes `T.DispBigEndian ? false : m_pArch->Little`
to `ExtractField` at the displacement read site.

**Use case — NS32000:** The NS32000 is declared `endian little` (its register file and
opcode field extraction are little-endian) but its displacements and immediates are
big-endian. `be` on individual fields provides this per-field override without a
separate arch declaration.

---

### E2: Self-Describing Variable-Length Displacement (`disp varlen`)

**Status: Implemented.**  Engine: `LibCPU/upcl/Ast.h` lines 196–197 (`AddrTerm::DispEncoding`,
`AddrTerm::DispKind`); `LibCPU/upcl/Parser.cpp` line 1357 (`varlen` keyword acceptance);
`LibCPU/upcl/Decoder.cpp` lines 339–368 (varlen decode branch, advancing tail cursor).

The NS32000 encodes displacements self-describing: the top bits of the first displacement
byte select the encoded width:

| Top bits of byte 0 | Encoding | Signed value range |
|--------------------|----------|--------------------|
| `0xxxxxxx` (`b0 & 0x80 == 0`) | 1 byte, 7-bit signed | −64 .. +63 |
| `10xxxxxx` (`b0 & 0xC0 == 0x80`) | 2 bytes, 14-bit signed | −8192 .. +8191 |
| `11xxxxxx` (`b0 & 0xC0 == 0xC0`) | 4 bytes, 30-bit signed | −2^29 .. +2^29−1 |

The value is sign-extended from the encoded bit width (7/14/30 bits). All bytes within
one varlen displacement are read big-endian. The sign-extension is significant: a
2-byte field `0xA0 0x00` selects 14-bit mode and its 14-bit value is `0x2000`, which has
bit 13 set and sign-extends **negative** (to `0xffffffffffffe000`). The maximum positive
14-bit value is `0x1FFF` (bytes `0x9F 0xFF`).

**Syntax:**

```
addrmode ea ( m ) {
    m == 0 => %M [ r0 + disp varlen ] ;
}
```

(`upcl/examples/_test_varlen.upcl` — the complete working test arch.)

**Actual decoded outputs:**

```
$ lcx upcl decode _test_varlen.upcl 0x00 0x05
0x0000: ld     src=[r0+0x5] disp=0x5   (2 byte(s))          // 1 byte, +5

$ lcx upcl decode _test_varlen.upcl 0x00 0x7F
0x0000: ld     src=[r0+0xffffffffffffffff] disp=0xffffffffffffffff   (2 byte(s))   // 1 byte, 7-bit -1

$ lcx upcl decode _test_varlen.upcl 0x00 0x90 0x00
0x0000: ld     src=[r0+0x1000] disp=0x1000   (3 byte(s))    // 2 bytes, +0x1000

$ lcx upcl decode _test_varlen.upcl 0x00 0x9F 0xFF
0x0000: ld     src=[r0+0x1fff] disp=0x1fff   (3 byte(s))    // 2 bytes, max positive 14-bit

$ lcx upcl decode _test_varlen.upcl 0x00 0xA0 0x00
0x0000: ld     src=[r0+0xffffffffffffe000] disp=0xffffffffffffe000   (3 byte(s))   // 2 bytes, NEGATIVE

$ lcx upcl decode _test_varlen.upcl 0x00 0xC0 0x00 0x40 0x00
0x0000: ld     src=[r0+0x4000] disp=0x4000   (5 byte(s))    // 4 bytes, +0x4000
```

Instruction lengths are 1-byte opcode word + 1/2/4 tail bytes = 2/3/5 total.

**Multiple displacements per rule:** E2 also generalizes the model to allow several `disp`
terms in a single addrmode rule, consumed left-to-right from the byte tail. A running
`TailUsed` cursor in `ResolveAddrMode` advances after each term; the fixed-width path
is byte-identical to before (single disp, `TailUsed` stays at 0). NS32000 memory-relative
modes carry two varlen displacements:

```
g == 0x10 => #i32 %M [ %M [ fp + disp varlen ] + disp varlen ] ;
```

**`DispEncoding` enum:** `AddrTerm::DispEncoding::Fixed` (the default, all existing ISAs)
vs `AddrTerm::DispEncoding::VarLen` (set by `varlen`). The decoder branches on
`T.DispKind == AddrTerm::DispEncoding::VarLen` (`Decoder.cpp` line 339).

**Composition with E1:** `disp varlen be` parses (E2's varlen path is itself big-endian
and ignores `DispBigEndian`; `be` is redundant but accepted for symmetry).

---

### E3: Nested Scaled-Index Dispatch (`@addrmode[index]`)

**Status: Implemented.**  Engine: `LibCPU/upcl/Ast.h` lines 209–212 (`AddrTerm::NestedAddrMode`,
`NestedSelBits`, `NestedRegBits`, `Scale`); `LibCPU/upcl/Parser.cpp` line 1316
(`TokMacroIdent` branch in the addrmode term loop); `LibCPU/upcl/Decoder.cpp` line 277
(nested-dispatch branch in `ResolveAddrMode`). Also adds `Operand::IndexReg`/`IndexScale`
to `LibCPU/upcl/Semantics.h`.

An addrmode rule may consume one extra **index byte** laid out MSB-first as
`(basegen : NestedSelBits)(ireg : NestedRegBits)`, recurse into a named base addrmode
using `basegen` as the selector, and form `EA = base_EA + R[ireg] * scale`. The syntax
inside a `%M[...]` rule is:

```
%M [ @<addrmode>[index] + %REG[group, field] * <scale> ]
```

**Working test arch:**

```
arch "_test_index" {
    ...
    register_file { group R { [ 8 ** #i32 r?:0 ], [ #i32 pc -> %PC ] } }
}

addrmode base ( g ) {
    g < 0x08  => reg [ r0,r1,r2,r3,r4,r5,r6,r7 ] ;
    g == 0x15 => %M [ disp varlen ] ;             // absolute: varlen disp is the address
}

// Scaled index: index byte = (basegen:5)(ireg:3); EA = base + R[ireg]*4.
addrmode ea ( m ) {
    m == 0 => %M [ @base[index] + %REG[R, ireg] * 4 ] ;
}
```

(`upcl/examples/_test_index.upcl` — the complete working test arch.)

**Actual decoded outputs:**

```
// Index byte 0x0A = 0b00001_010 = (basegen=1)(ireg=2): base=r1, index=r2*4
$ lcx upcl decode _test_index.upcl 0x00 0x0A
0x0000: ld     src=[r1+r2*4] disp=0x0   (2 byte(s))

// Index byte 0xAB = 0b10101_011 = (basegen=0x15=absolute)(ireg=3), then 1-byte varlen 0x10
// EA = @0x10 + r3*4
$ lcx upcl decode _test_index.upcl 0x00 0xAB 0x10
0x0000: ld     src=[@0x10+r3*4] disp=0x10   (3 byte(s))
```

The first case (register base) is 2 bytes: 1-byte opcode word + 1-byte index byte.
The second case (absolute base using `disp varlen`) is 3 bytes: 1-byte opcode +
1-byte index byte + 1-byte varlen displacement — demonstrating that the recursion
composes with E2.

**Decode mechanics:** In `ResolveAddrMode`, a term with a non-empty `NestedAddrMode`
reads the index byte at `pTail[TailUsed]`, splits it MSB-first into `BaseGen` (upper
`NestedSelBits` bits, default 5) and `IReg` (lower `NestedRegBits` bits, default 3),
looks up the named base addrmode by name, maps `BaseGen` to its parameter, and recurses
(`ResolveAddrMode` calls itself with `pTail + TailUsed + 1` and a fresh `NestedExtra`
byte accumulator). Byte accounting: `TailUsed += 1 + NestedExtra`,
`*pExtraBytes += 1 + NestedExtra`. The resolved base operand is merged into `pOut`;
the index register is resolved via the group's positional alias (e.g. group `R`,
position `IReg` → physical `r2`) and stored in `Operand::IndexReg`/`IndexScale`.

**Default bit widths:** `NestedSelBits` defaults to 5 and `NestedRegBits` defaults to 3
(the NS32000 layout). These are set in the parser when it encounters `@name[index]`.

**Use case — NS32000:** The NS32000 `[Rn:B/W/D/Q]` scaled-index modes (scales 1/2/4/8)
all use this one-index-byte-plus-recursive-base pattern. E3 models that directly without
any per-scale-mode C++ specialization.

---

## 7. Instructions: `insn` and `jump insn`

### Regular Instructions: `insn`

The full form of a regular instruction:

```
insn <name> (: <super-insn>)? { <body-stmts> }
```

or (old `.def` style, still accepted):

```
insn <name> : [<inline-stmt>] [encode <alt> (| <alt>)*] [, disasm (...)] => <inline-stmt> ;
insn <name> : [<inline-stmt>] [encode <alt> (| <alt>)*] [, disasm (...)] { <block> }
```

The new-syntax form uses attributes in brackets before `insn`:

```
[format(R :: op=0x20, funct=0x21), disasm("addu %rt, %rs, %rt")] insn addu { ... }
```

The `.def` form used by all examples in `upcl/examples/` uses inline encode/disasm
clauses. In practice (based on `pdp11.upcl`, `mips.upcl`, `6502.upcl`), the form is:

```
insn <name> : encode <alt> [| <alt> ...] , disasm ( ... ) => <inline-stmt> ;
insn <name> : encode <alt> , disasm ( ... ) { <block-stmts> }
```

#### Encoding Alternatives: `encode #iN ( fields )`

An encoding alternative specifies the word type and its MSB-first field layout:

```
encode #i16 ( op:4 = 0x1, sm:3, sr:3 -> src @ea_src, dm:3, dr:3 -> dst @ea_dst )
```
(`upcl/examples/pdp11.upcl` line 149.)

The `#iN` word type (e.g. `#i16`, `#i32`, `#i24`) gives the total instruction word
width. Fields are listed MSB-first and their widths must sum to exactly N bits (for
non-tail fields). Any field whose bit offset reaches or exceeds N is a tail field — it
is appended in the byte stream after any variable-length addressing mode extensions.

Each field has the form (parsed in `Parser::ParseEncField`, `Parser.cpp` lines 1189–1248):

```
name : width [= const] [-> operand [rel] [sx] [@addrmode]]
```

| Part | Meaning |
|------|---------|
| `name : width` | A named field of `width` bits |
| `= const` | The field must match this constant (opcode bits) |
| `-> operand` | Bind the field's bits to a decoder operand |
| `-> op[r0,r1,...]` | Field value selects a register; operand is that register |
| `-> op rel` | PC-relative displacement; operand = next-insn PC + sign-extended field |
| `-> op sx` | Sign-extend the field value to machine word width |
| `-> op @addrmode` | Field selects through an addressing mode (reads extension bytes) |

A field without a width (`name` alone) is an implicit operand occupying no encoding
bits; it must carry either `= const` (a fixed immediate) or `<- reg` (a fixed register):

```
encode #i8 ( op:8 = 0x18 )   // all 8 bits are the opcode
```
(`upcl/examples/6502.upcl` line 110.)

Multiple alternatives (`|`) cover instruction forms with different encodings:

```
encode #i8 ( op:8 = 0xA9, imm:8 -> imm ) | #i8 ( op:8 = 0xA5, zp:8 -> zp )
```

#### The Disasm Declaration: `disasm ( ... )`

```
disasm ( mnemonic : "mov", operands : src, dst )
```

| Key | Value |
|-----|-------|
| `mnemonic : "string"` | The mnemonic spelling |
| `size : name` | Size code (e.g. `word`) from the `disasm features { size { } }` table |
| `operands : op1, op2, ...` | Operand names in Intel (destination-first) order |

The old form `disasm "format-string"` also exists for custom per-instruction formatting.

The declarative form is rendered through the architecture's `DisasmFeatures`
(registered by `disasm features { ... }`) which captures per-style rendering rules
(AT&T vs Intel register prefixes, operand ordering, etc.).

#### Instruction Bodies

A body is a sequence of statements (`Stmt`) that describe the instruction's effect:

```
=> dst = %CC ( src );                  // inline body (single assignment statement)
{ dst = %CC ( dst + src ); }           // block body
```

Statements are assignments (`lhs = rhs`, `lhs op= rhs`), bare expressions
(`%CC(expr)`), and control flow (`if`/`while`/`for`). An optional leading type on
an assignment introduces a typed temporary:

```
{ #i16 v = dst; #i16 c = v & 1; dst = ( v >> 1 ) | ( %C << 15 ); ... }
```
(`upcl/examples/pdp11.upcl` lines 191–192, ROR implementation.)

### Jump Instructions: `jump insn`

`jump insn` marks an instruction as a control-flow transfer. The engine uses this
to inform the JIT/AOT backends about block boundaries.

```
jump insn <name> : <clauses> { <action> }
```

Clauses (in any order, separated by commas; `Parser::ParseJumpInsn`,
`Parser.cpp` lines 1497–1545):

| Clause | Meaning |
|--------|---------|
| `type branch\|call\|return\|trap` | The transfer kind |
| `encode <alt>` | Encoding (same syntax as `insn`) |
| `disasm ( ... )` | Disassembly format (same syntax as `insn`) |
| `condition <expr>` | Condition expression (for conditional branches) |
| `delay <N>` | Delay-slot count (MIPS/m88k: N instructions after the branch always execute) |
| `pre { ... }` | Statements executed before the condition evaluation |

The action block is the full semantics body (same as `insn`).

Examples:

```
jump insn bne : type branch, encode #i16 ( op:8 = 0x02, boff:8 -> boff ),
    disasm ( mnemonic : "bne", operands : boff )
    { pc = ( %Z == 0 ) ? ( pc + ( %S ( boff ) << 1 ) ) : pc; }
```
(`upcl/examples/pdp11.upcl` lines 267–268.)

```
jump insn jmp : type branch, encode #i24 ( op:8 = 0x4C, addr:16 -> tgt ),
    disasm ( mnemonic : "jmp", operands : tgt )
    { pc = tgt; }
```
(`upcl/examples/6502.upcl` lines 121–123.)

```
jump insn jsr : type call, encode #i32 ( op:6 = 0x03, idx:26 -> target ),
    disasm ( mnemonic : "jal", operands : target )
    delay 1 { r[31] = pc; pc = ( pc & 0xf0000000 ) | ( target << 2 ); }
```
(MIPS JAL — delay slot; similar pattern in `mips.upcl`.)

#### Jump Types

| Type | Meaning |
|------|---------|
| `branch` | Conditional or unconditional branch; does not push a return address |
| `call` | Subroutine call; pushes a return address |
| `return` | Subroutine return; pops and jumps |
| `trap` | Software interrupt / exception vector |

### Macros

Macros are reusable named bodies callable from instruction bodies:

```
macro <name> ( <params> ) : <inline-stmt> ;
macro <name> ( <params> ) { <block> }
```

A macro's return value is written to `%result` and read by the caller:

```
macro pull6502 () {
    S = S + 1 ;
    %result = #i8 %M[ [ #i16 0x100 ] + [ #i16 S ] ] ;
}
```
(`upcl/examples/6502.upcl` lines 57–61.)

Called as: `#i8 lo = @pull6502 () ;`

---

## 8. Semantics Expression Language

Instruction bodies (and addrmode pre/post blocks) use a C-like expression language
with several UPCL-specific extensions.

### Arithmetic and Bitwise Operators

Standard binary operators (`+`, `-`, `*`, `/`, `%`, `&`, `|`, `^`, `~`, `<<`, `>>`)
and their compound-assignment forms (`+=`, etc.) are all supported. The Pratt parser
precedence table (`Parser::InfixBp`, `Parser.cpp` lines 139–155):

| Precedence (high → low) | Operators |
|------------------------|-----------|
| 10 | `*`, `/`, `%` |
| 9  | `+`, `-` |
| 8  | `<<`, `>>`, `<<>` (ROL), `>><` (ROR) |
| 7  | `<`, `<=`, `>`, `>=` |
| 6  | `==`, `!=` |
| 5  | `&`, `&~` |
| 4  | `^`, `^~` |
| 3  | `|`, `|~` |
| 2  | `&&` |
| 1  | `||` |
| 0  | `?:` (ternary, right-associative) |

### `%S` (Sign-Extend) and `%U` (Zero-Extend)

```
%S ( expr )    // sign-extend to machine word width
%U ( expr )    // zero-extend to machine word width
```

```
r[ rt ] = %S ( t );     // MIPS addiu: sign-extend 32-bit result to 64 bits (mips.upcl line 86)
pc + ( %S ( boff ) << 1 )   // PDP-11 branch: sign-extend 8-bit offset (pdp11.upcl line 267)
```

Parsed as `ExprAugment` with `Name = "S"` or `"U"` (`Parser.cpp` lines 236–245).
In the semantics translator, `%S` maps to `CastSExt` and `%U` to `CastZExt`.

### `%CC` (Condition Code Recorder)

```
%CC ( expr )                // record all condition codes from expr
%CC ( expr, N, Z, V, C )    // select which flags to update
%CC ( expr, !V, C )         // negated flag: update V = ~(condition)
```

`%CC` both evaluates `expr` and updates the architectural condition codes. The second
argument is an optional list of flag names; with no list, all bound flags are updated
(N, Z, V, C as available). A `!` prefix negates the derived value before storing.

```
=> A = %CC ( A + imm + %C );    // 6502 ADC: updates all flags (6502.upcl line 106)
=> dst = %CC ( dst + src );     // PDP-11 ADD (pdp11.upcl line 160)
```

The `%CC` expression is of type `ExprCC` (`Ast.h` line 60). `CcFlags` holds the
flag-name list; `CcNeg` holds the per-flag negation bits.

### `%M[addr]`: Memory Access

```
%M[ expr ]              // word-size load or store at addr
#i8 %M[ expr ]          // typed: 8-bit load or store
#i32 %M[ expr ]         // 32-bit
```

A typed prefix on `%M` narrows the access width. As an lvalue, `%M` is a memory
store; as an rvalue it is a load:

```
#i8 %M[ [ #i16 zp ] ] = A;         // 6502 STA $zp (6502.upcl line 77)
A = %CC ( #i8 %M[ [ #i16 zp ] ] ); // 6502 LDA $zp (6502.upcl line 72)
```

`%MEM` is an accepted synonym. `%PM[addr]` is the physical-memory variant (for MMU
table-walk code; bypasses TLB translation in the runtime).

### `%LL` and `%SC`: Load-Linked / Store-Conditional

```
#i32 %LL[ addr ]                    // load-linked (sets a reservation)
%SC[ addr ] <- value                // store-conditional (yields 0/1 success)
```

Used to model architectures with `LL/SC` atomic primitives. The trailing `<- value`
is required for `%SC` (`Parser.cpp` lines 173–185). Both forms accept an optional
leading type.

### `%OFTRAP`: Overflow Trap

```
%OFTRAP ( expr )            // raise a trap on signed overflow
%OFTRAP ( expr, trap_expr ) // raise trap_expr on signed overflow
```

```
{ #i32 t = %OFTRAP ( r[ rs ] + %S ( imm ), 1 ); r[ rt ] = %S ( t ); }
```
(`upcl/examples/mips.upcl` line 88, `addi`.)

Parsed as `ExprAugment` with `Name = "OFTRAP"` (`Parser.cpp` line 243 accepts an
optional second argument).

### `%FLT` and `%INT`: Float/Integer Bitcasts

```
%FLT ( expr )    // reinterpret the bit pattern of expr as a float
%INT ( expr )    // reinterpret the bit pattern of expr as an integer
```

These are bitcasts (no value conversion), used to hold IEEE-754 values in GPRs:

```
#f32 f = %FLT ( ac[ acn ] ) * %FLT ( fsrc );
ac[ acn ] = %INT ( f );
```
(`upcl/examples/pdp11.upcl` lines 410–411, FP11 MULF.)

### `@lea`: Effective-Address Extraction

```
@lea ( operand )
```

Returns the effective address of an operand rather than its value. Necessary for
`JMP`/`JSR` on the PDP-11, where the target is the address the mode names, not the
value stored there:

```
{ pc = @lea ( dst ); }              // PDP-11 JMP (pdp11.upcl line 305)
{ sp = sp - 2; %M[ sp ] = r[ trapn ]; r[ trapn ] = pc; pc = @lea ( dst ); }  // JSR
```
(`upcl/examples/pdp11.upcl` lines 305–308.)

### `@trap`: Raise a Trap

```
@trap ( vector_expr )
```

Routes the instruction through the CPU's trap delivery mechanism:

```
jump insn emt : ... { @trap ( 0x18 ); }   // PDP-11 EMT (pdp11.upcl line 328)
```

### `@macro(args)`: Macro Call

```
@macro_name ( arg1, arg2, ... )
```

Inline macro expansion. The macro may set `%result` for a return value.

```
@push6502 ( ret[ 15:8 ] ) ;    // 6502 JSR (6502.upcl line 131)
#i8 lo = @pull6502 () ;        // 6502 RTS (6502.upcl line 137)
```

### Bit Slices: `expr[a:b]` and `expr[a..b]`

```
expr[ hi : lo ]      // inclusive range: bits hi down to lo (both included)
expr[ hi .. lo ]     // exclusive-high range: bits from (hi-1) down to lo
```

The `RangeInclusive` flag on `ExprBitSlice` (`Ast.h` line 77) distinguishes the two.
For `[a:b]`, `a >= b` and the extracted width is `a - b + 1` bits; for `[a..b]`,
width is `a - b`.

```
ret[ 15:8 ]     // high byte of ret (6502.upcl line 131)
( v >> 15 ) & 1   // equivalent without slice syntax
```

As an lvalue, a bit slice narrows a store to that field:

```
pc.off = address;    // member-access form (x86.upcl lines 103–104)
```

### Bit Concatenation: `( a : b : c )`

Concatenates operands MSB-first into a wider value:

```
( [ #i16 hi ] << 8 ) | [ #i16 lo ]    // 6502 RTS: build 16-bit return address
```

The `ExprBitCombine` form (`Ast.h` line 59) uses the `:` separator inside parentheses
(`Parser.cpp` lines 305–317).

```
( seg : off )    // x86 far pointer (x86.upcl line 107)
```

### Ternary Select: `cond ? then : else`

```
( %Z == 0 ) ? ( pc + ( %S ( boff ) << 1 ) ) : pc
```
(`upcl/examples/pdp11.upcl` line 268, BNE.)

Parsed as `ExprSelect` at MinBp == 0 (lowest precedence, right-associative;
`Parser.cpp` lines 447–456).

### Type Casts: `[#iN expr]`

```
[ #i16 zp ]     // zero-extend zp to 16 bits (6502.upcl line 72)
[ #i32 q ]      // truncate or extend q to 32 bits
#i8 0           // prefix cast (same as [ #i8 0 ])
```

A bracketed leading type is a cast expression (`ExprCast`). A bare leading type
(outside brackets) is also accepted as a prefix cast that binds tighter than any
infix operator (`Parser.cpp` lines 399–423).

### Type Test: `expr is #iN`

```
if ( address is #i16 ) { pc.off = address; }
else { pc.[ off, seg ] = address; }
```
(`upcl/examples/x86.upcl` lines 102–106.)

`is` returns a 1-bit boolean: 1 if `expr` fits in the given type. Parsed as
`ExprIs` (`Parser.cpp` lines 373–379).

### `delay N`

On architectures with delay slots, `delay N` in a `jump insn` header declares that
N instructions following the branch always execute before the transfer takes effect:

```
jump insn j : type branch, encode #i32 ( op:6 = 0x02, tgt26:26 -> target )
    delay 1 { pc = ( pc & 0xf0000000 ) | ( target << 2 ); }
```

The `Delay` expression is on the `JumpInsn` AST node (`Ast.h` line 537).

### Flag Reads and Writes

Named condition-code meta-registers are directly readable and writable:

```
%C = 0;          // clear carry (6502 CLC: 6502.upcl line 110)
%V = 1;          // set overflow
%N = ( dst >> 15 ) & 1;   // set negative flag (pdp11.upcl line 192)
```

Named flag sub-registers (from the PSR splitter) are writable by assignment:

```
fps = ( fps & 0xfff0 ) | ( ( f < 0.0 ) ? 8 : 0 ) | ( ( f == 0.0 ) ? 4 : 0 );
```
(`upcl/examples/pdp11.upcl` line 411, FP11 MULF.)

---

## 9. Features and CPU Models: `feature` and `cpu`

### `feature` Blocks

A `feature` block wraps a set of instruction and register declarations that are only
active when the feature is enabled by the selected CPU model. A feature may contain
`insn`, `jump insn`, `addrmode`, `macro`, `decoder_operands`, and even group
declarations (for features that add new registers):

```
feature eis {
    insn mul : encode #i16 ( op:7 = 0x38, ... ) { ... }
    insn div : ...
    insn ash : ...
    insn xor : ...
}
```
(`upcl/examples/pdp11.upcl` lines 347–393, EIS feature.)

```
feature fpp {
    insn mulf : encode #i16 ( op:8 = 0xf2, ... ) { ... }
    ...
}
```
(`upcl/examples/pdp11.upcl` lines 405–490, FP11 feature.)

An instruction ungated by any feature is part of the base ISA and is always active.
An instruction inside `feature X { ... }` is only decoded/executed when feature `X`
is enabled.

The `Feature` field on `Insn` and `JumpInsn` (`Ast.h` lines 378, 546) is the feature
name; an empty string means base ISA. The runtime filters the enabled instruction set
in `UpclArch` (`UpclArch.cpp` lines 60–64).

### `features { }` Declaration

The global `features { }` block (inside or outside an `arch` block) declares which
feature names exist, with optional documentation strings:

```
features {
    eis "Extended Instruction Set (MUL/DIV/ASH/ASHC/XOR)";
    fpp "FP11 floating-point processor (single-precision F format)";
}
```
(`upcl/examples/pdp11.upcl` lines 497–500.)

Parsed in `Parser::ParseFeatures` (`Parser.cpp` lines 608–632).

### `cpu` Models

A `cpu` block names a CPU variant and lists the features it includes:

```
cpu "pdp11/20" { }
cpu "pdp11/40" { eis; }
cpu "pdp11/45" { eis; fpp; }
cpu "pdp11/70" { eis; fpp; }
```
(`upcl/examples/pdp11.upcl` lines 502–505.)

```
cpu "mips1"    { mips1; }
cpu "mips64r6" { mips1; mips2; mips3; mips32; mips3r2; mips64; r6; }
```
(pattern from `upcl/examples/mips.upcl`)

Selected on the command line: `--arch upcl:pdp11.upcl@pdp11/45`. A model enables
exactly the listed features; instructions gated on unlisted features are invisible.

Parsed in `Parser::ParseCpu` (`Parser.cpp` lines 634–656).

---

## 10. Compile-Phase vs Generate-Phase Constant Folding

The semantics translator has two modes, governed by the `m_Generate` flag
(`Semantics.cpp`, `Translator` class):

### Compile Phase (Interpret / JIT Mode)

In the default compile phase, integer constants are lazy (`Value::IsConst = true`,
`Value::K` holds the value, no emitter node is produced until `Use()` is called).
Binary operations on two compile-time constants are folded immediately without
emitting a node:

```cpp
if (A.IsConst && B.IsConst && ...) {
    // perform the operation at compile time and return a new ConstVal
}
```
(`Semantics.cpp` lines 151–172.)

Decode-time constants — fields bound from an `encode` clause — are always compile-time
constants, so `sm == 6 ? 16 : 0` (the PDP-11 displacement width expression) folds
completely at decode time, emitting no runtime test.

Algebraic identities are also applied: `x + 0 → x`, `x * 1 → x`, `x & ~0 → x`, etc.
(`Semantics.cpp` lines 176–198.)

### Generate Phase (AOT / Static Source)

When producing a static frontend (AOT source generation), `m_Generate = true`.
In this mode, every operand is treated as a runtime value — even constants emit a
`ConstInt` node — because the generated source emitter maps each `GetRegister` /
`ConstInt` to a C++ expression, not a physical register read:

```cpp
if (m_Generate) {
    ComPtr<ICpuValue> V;
    m_pE->ConstInt (Bits ? Bits : m_WordBits, N, &V);
    return Pool (std::move (V), Bits ? Bits : m_WordBits);
}
return ConstVal (Bits, N);   // lazy; will not be folded but also not yet emitted
```
(`Semantics.cpp` lines 97–103.)

### `%EVAL` and `%GEN` Markers

These augments override the automatic phase detection for individual subexpressions:

| Marker | Effect |
|--------|--------|
| `%EVAL( expr )` | Force the subexpression to be evaluated at compile/decode time (fold it even if we are in generate phase) |
| `%GEN( expr )` | Force the subexpression to emit a runtime node (suppress folding even in compile phase) |

Both are parsed as `ExprAugment` with `Name = "EVAL"` or `"GEN"` (`Parser.cpp`
line 237).

**Use case:** A displacement width expression that MUST fold (the result drives how many
bytes to consume from the tail) should use `%EVAL`. A counter that MUST remain a
runtime value in the generated source uses `%GEN`.

The `ConstVal` / `Const` divergence in `Translator::Const` (`Semantics.cpp` lines
82–103) is the mechanism: a lazy const is the compile-phase default; an emitted node is
the generate-phase default.

---

## 11. Worked Mini-ISA: End to End

This section presents a self-contained, correct UPCL mini-ISA based on the real CHIP-8
virtual machine. It is modeled after `upcl/examples/chip8.upcl` and exercises every
core construct.

### Architecture Header and Register File

```
arch "chip8" {
    name "CHIP-8 Virtual Machine";
    endian big;
    byte_size 8;
    word_size 8;
    address_size 16;

    register_file {

        // 16 general-purpose 8-bit registers V0..VF.
        // VF is the carry/borrow flag (written by arithmetic instructions).
        group V { [ 16 ** #i8 v?:0 ] }   // v0..v15; V.VN aliases auto-generated

        // Special registers.
        group S {
            [ #i16 pc -> %PC ],   // 12-bit program counter (CHIP-8 uses 12 bits, kept at 16)
            [ #i16 I ],           // index register (12-bit)
            [ #i8  sp ],          // stack pointer
            [ #i8  dt ],          // delay timer
            [ #i8  st ]           // sound timer
        }

    }
}
```

### Decoder Operands

```
decoder_operands [
    #i4  vx,    // first nibble register selector (Vx)
    #i4  vy,    // second nibble register selector (Vy)
    #i8  imm8,  // 8-bit immediate
    #i12 addr   // 12-bit jump target / index address
];
```

### A Simple Instruction: `insn`

The CHIP-8 `6xkk` instruction loads an 8-bit immediate into Vx:

```
// 6xkk: Vx = imm8 (no flags)
insn ld_vx_byte : encode #i16 ( op:4 = 0x6, vx:4 -> vx, kk:8 -> imm8 ),
    disasm ( mnemonic : "ld", operands : vx, imm8 )
    => v[ vx ] = imm8;
```

- `#i16` declares a 16-bit instruction word.
- `op:4 = 0x6` matches the high nibble as opcode.
- `vx:4 -> vx` binds the next 4 bits to the decoder operand `vx`.
- `kk:8 -> imm8` binds the low byte to `imm8`.
- `=> v[ vx ] = imm8;` is the inline semantic body.

### Arithmetic with Carry: `insn` with `%CC`

```
// 8xy4: Vx += Vy; VF = carry.
insn add_vx_vy : encode #i16 ( op:4 = 0x8, vx:4 -> vx, vy:4 -> vy, n:4 = 0x4 ),
    disasm ( mnemonic : "add", operands : vx, vy )
    { #i16 r = [ #i16 v[ vx ] ] + [ #i16 v[ vy ] ];
      v[ 0xf ] = ( r > 0xff ) ? 1 : 0;
      v[ vx ] = r; }
```

Alternatively using `%CC` for a carry-aware add:

```
    => v[ vx ] = %CC ( v[ vx ] + v[ vy ], C );   // only update the C (carry) flag
```

### A Jump Instruction

```
// 1nnn: unconditional jump to 12-bit address.
jump insn jp : type branch, encode #i16 ( op:4 = 0x1, addr:12 -> addr ),
    disasm ( mnemonic : "jp", operands : addr )
    { pc = addr; }
```

### A Conditional Branch

```
// 4xkk: skip next instruction if Vx != imm8.
jump insn sne : type branch, encode #i16 ( op:4 = 0x4, vx:4 -> vx, kk:8 -> imm8 ),
    disasm ( mnemonic : "sne", operands : vx, imm8 )
    { pc = ( v[ vx ] != imm8 ) ? ( pc + 2 ) : pc; }
```

### Register-File Indexing in a Body

```
// 8xy0: Vx = Vy.
insn ld_vx_vy : encode #i16 ( op:4 = 0x8, vx:4 -> vx, vy:4 -> vy, n:4 = 0x0 ),
    disasm ( mnemonic : "ld", operands : vx, vy )
    => v[ vx ] = v[ vy ];
```

### Addressing Mode with Side Effects

A stack-push pattern using pre/post blocks (the CHIP-8 call instruction):

```
addrmode stk () {
    default => %M [ [ #i16 0x100 ] + [ #i16 sp ] ]
               post { sp -= 2; } ;
}

jump insn call : type call, encode #i16 ( op:4 = 0x2, addr:12 -> addr ),
    disasm ( mnemonic : "call", operands : addr )
    { %M[ sp ] = pc; sp = sp - 2; pc = addr; }
```

(Inline body shown; the addrmode variant would require a push operand.)

---

## 12. Appendix: Quick-Reference Table

### Top-Level Declarations

| Directive | Meaning |
|-----------|---------|
| `arch "name" { }` | Architecture definition block |
| `register_file { }` | Register groups (inside `arch` or at top level) |
| `decoder_operands [ ... ]` | Named decoder operand types |
| `addrmode name ( params ) { }` | Addressing mode table |
| `regset name [ regs... ]` | Named register list (for encoding expansion) |
| `insn name : ... ;` | Regular instruction definition |
| `jump insn name : ... { }` | Control-flow instruction |
| `macro name ( params ) { }` | Reusable inline code macro |
| `features { }` | ISA feature name declarations |
| `feature name { }` | Gated ISA extension block |
| `cpu "name" { features... }` | CPU model (feature set selection) |
| `include "file"` | Include another UPCL file |
| `mmu { page_size N; translate ( params ) { } }` | MMU table-walk description |
| `disasm features { }` | Disassembly style rules |
| `macro disasm name ( params ) => fmt ;` | Disassembly format macro |
| `address_display flat\|segmented ...` | Address formatting |

### `arch` Block Keys

| Key | Meaning |
|-----|---------|
| `name "..."` | Human-readable architecture name |
| `endian little\|big\|both` | Byte order |
| `default_endian little\|big` | Default for `both` architectures |
| `byte_size N` | Bits per addressable unit |
| `word_size N` | Machine word width |
| `address_size N` | Address width |
| `float_size N` | Native float width |
| `psr_size N` | PSR width |

### Register Binding Suffixes

| Suffix | Meaning |
|--------|---------|
| `-> %PC` | Program counter meta-role |
| `-> %PSR ...` | PSR meta-role + splitter |
| `-> %NPC` | Next-PC (delay-slot) |
| `-> id` | Simple alias to another register |
| `<- expr` | Hardwire to a constant expression |
| `<- (expr)` | Hardwire to a computed expression |
| `N **` | Repeat N times (with auto-index) |
| `name?:N` | Auto-indexing starting at N |

### Encoding Field Modifiers

| Modifier | Meaning |
|----------|---------|
| `field:W = const` | Match constant opcode bits |
| `-> op` | Bind bits to decoder operand `op` |
| `-> op[r0, r1, ...]` | Field value selects register from list |
| `-> op rel` | PC-relative displacement |
| `-> op sx` | Sign-extend to machine word |
| `-> op @mode` | Resolve through addressing mode `mode` |
| `be` | Big-endian extraction (E1, NS32000) |

### Addrmode Address Terms

| Term | Meaning |
|------|---------|
| `regname` | A base register |
| `disp` | Default-width displacement (from `disp(expr)`) |
| `disp8` | Fixed 8-bit displacement |
| `disp16` | Fixed 16-bit displacement |
| `disp varlen` | NS32000 self-describing variable-length displacement (E2) |
| `disp varlen be` | Varlen displacement with big-endian byte order (E1+E2) |
| `%REG[G, f]` | Group-G register at position given by field `f` |
| `@mode[index]` | Nested scaled-index dispatch (E3) |

### Semantics Operators and Augments

| Form | Meaning |
|------|---------|
| `%S(expr)` | Sign-extend to word width |
| `%U(expr)` | Zero-extend to word width |
| `%CC(expr [, flags])` | Evaluate and record condition codes |
| `%M[addr]` | Memory load/store (natural width) |
| `#T %M[addr]` | Memory load/store at type T width |
| `%PM[addr]` | Physical memory (MMU bypass) |
| `%LL[addr]` | Load-linked |
| `%SC[addr] <- v` | Store-conditional (yields 0/1) |
| `%OFTRAP(e [, t])` | Raise trap on signed overflow |
| `%FLT(expr)` | Bitcast integer → float |
| `%INT(expr)` | Bitcast float → integer |
| `%EVAL(expr)` | Force compile-phase evaluation |
| `%GEN(expr)` | Force generate-phase (no folding) |
| `%REG[G, f]` | Group-indexed register |
| `@lea(op)` | Effective address of operand |
| `@trap(vec)` | Raise a trap |
| `@macro(args)` | Macro call |
| `expr[hi:lo]` | Inclusive bit slice |
| `expr[hi..lo]` | Exclusive-high bit slice |
| `(a : b : c)` | Bit concatenation, MSB-first |
| `cond ? a : b` | Ternary select |
| `[#T expr]` | Type cast to #T |
| `expr is #T` | Type test (1 if fits in #T) |

### Grammar Reference

The EBNF grammar is at `upcl/docs/ebnf.txt`. The authoritative implementation is the
recursive-descent parser in `LibCPU/upcl/Parser.cpp`; the grammar file documents
the original `register_file` syntax and expression forms but does not cover the newer
`insn`/`jump insn`/`addrmode` constructs. The parser source is the ground truth for
any ambiguity.

---

*This reference is accurate against the engine on the `feat/com-core` branch.
The three extensions E1 (`be`, commit `441a71f`), E2 (`disp varlen`, commit `0be964c`),
and E3 (nested scaled-index dispatch, commit `209d10b`) are fully implemented in the
parser and decoder; they were added to enable the NS32000 (`ns32k`) frontend. Verified
against synthetic test arches `upcl/examples/_test_be.upcl`, `_test_varlen.upcl`, and
`_test_index.upcl`; all pass `lcx upcl check` and their `upcl.engine-{be,varlen,index}`
ctests.*
