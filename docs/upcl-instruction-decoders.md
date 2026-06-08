# Integrating Instruction Decoders into UPCL's Syntax

> How to give UPCL a single, declarative **instruction-decoder sub-language** rich
> enough to express the full spectrum of real decoding strategies — from the
> trivially regular (MMIX) through addressing-mode operand specifiers (PDP-11,
> M68K, VAX) and bundle/template fetch (IA-64) to fully content-dependent
> variable-length CISC (x86) and non-octet word machines (PDP-1) — plus bespoke
> ISAs (TAOHE). The decoder lives *in the `.def` syntax*, so adding an
> architecture never means hand-writing a C++ `*_disasm.cpp` / decode loop.
>
> Read `docs/upcl-cpu-fpu-simd.md` (UPCL pipeline; the `encoding(...)` sketch),
> `docs/upcl-complex-cpu-ia64.md` (bundles/templates), and `ARCHITECTURE.md`
> (libcpu's `tag_instr`/`disasm_instr`/`translate_instr` contract) first.

---

## 0. The decoding-style spectrum (the spine of this design)

Each named architecture represents a *distinct* decoding strategy. A decoder DSL
that handles all of them handles essentially anything.

| Arch | Fetch granule | Length | Decode style | The hard capability it forces |
|------|---------------|--------|--------------|-------------------------------|
| **MMIX** | 8-bit byte | fixed 32-bit | `op[8] · X[8] · Y[8] · Z[8]` | *baseline* — clean bit-field match |
| **IA-64** | 128-bit bundle | fixed bundle | template → 3×41-bit slots | bundle/template fetch (separate doc) |
| **PDP-1** | **18-bit word** (non-octet, 1's-comp) | fixed 18-bit | `op[5] · I · addr[12]` | non-power-of-two word; octal; bit numbering |
| **PDP-11** | 16-bit word (octal) | **variable** 1–3 words | 6-bit operand specifiers (mode×reg) | operand decode pulls extra words |
| **M68K** | 16-bit word (big-endian) | **variable** 1–N words | 6-bit EA fields + extension words | brief/full extension words; many formats |
| **VAX** | byte (little-endian) | **highly variable** | 1-byte op + N operand specifiers | per-operand specifier decode; operand count varies per opcode |
| **x86** | byte (little-endian) | **1–15 bytes** | leading state bytes + opcode maps + ModRM/SIB + disp/imm | stateful/continued decode; escape maps; mode-dependent |
| **TAOHE** | *(custom / unknown)* | — | *bespoke* | must be expressible with **no UPCL compiler change** |

The DSL needs five composable mechanisms, each addressing one column of "hard
capability": **(1)** a fetch model, **(2)** bit-field pattern match, **(3)**
hierarchical opcode tables/escapes, **(4)** decode-time state with continuation
(the CPU-agnostic basis for "prefixes"), **(5)** recursive operand decoders that
thread a length cursor. §2–§7 build them.

> **A note on agnosticism.** UPCL fixes *no* architecture-specific concepts.
> There is no "prefix", "REX", "ModRM", or "operand-size override" in the
> language — those are all *descriptions written in a `.def`* using the five
> generic mechanisms. §5 in particular replaces any notion of an x86-shaped
> `prefix` construct with two primitives that know nothing about prefixes.

Tags: **[UPCL]** language/IR/codegen, **[CORE]** libcpu engine, **[EXISTS]**
present today.

---

## 1. What UPCL has today

UPCL already has the *semantic* operand model: `c::decoder_operand_def`
**[EXISTS]** with a `CONSTANT` flag ("evaluable at translate time," i.e. an
immediate) and `CCFLAGS`/`@eval_cc`. The register-file grammar is complete; the
**instruction-encoding surface grammar is the incomplete part** (noted in
`docs/upcl-cpu-fpu-simd.md` §0). UPCL also separates `byte_size`, `word_size`,
`address_size`, and supports `endian none` for word-oriented machines — the hooks
for PDP-1's 18-bit word exist conceptually.

This document specifies the missing surface syntax as a coherent decoder
sub-language, and how it lowers into libcpu's three decode-facing callbacks.

---

## 2. The fetch model — granule, word size, bit order  **[UPCL] [CORE]**

Decoding is defined over a **fetch granule**, not assumed to be a byte:

```
fetch {
    granule word;            // unit fields are defined over: byte | word | bundle
    word_size 18;            // PDP-1: non-octet, non-power-of-two       [CORE: 18-bit unit]
    bit_order msb0;          // bit numbering for field positions
    number_base octal;       // literals default to octal (PDP-1/PDP-11) [UPCL]
}
```

- **MMIX / x86 / VAX:** `granule byte`.
- **PDP-11 / M68K / PDP-1:** `granule word` (16 or 18 bits).
- **IA-64:** `granule bundle` (§ bundle doc).

This makes PDP-1 expressible: fields are sliced from an 18-bit word, not forced
into octets, and one's-complement value interpretation is a *type* concern, not a
decode concern. `endian none` **[EXISTS]** already covers word machines whose
word size isn't a power of two.

---

## 3. Bit-field pattern match — the baseline (MMIX, PDP-1)  **[UPCL]**

The primitive: an `encoding` lists fixed bits and captured operand fields over the
current granule.

```
// MMIX: opcode in top byte, three operand bytes.
insn "ADD" { encoding ( op:8 = 0x20, X:8, Y:8, Z:8 );
             asm "ADD $%X,$%Y,$%Z"; semantics { GR[X] <- GR[Y] + GR[Z]; } }

// PDP-1 (18-bit word): 5-bit op, indirect bit, 12-bit address.
insn "add" { encoding ( op:5 = 0o40, I:1, addr:12 );
             asm "add %I@ %addr"; semantics { AC <- AC + M[ea(I, addr)]; } }
```

`X:8` captures an operand; `op:8 = 0x20` matches a constant. Fields known at decode
time become `CONSTANT` `decoder_operand_def`s **[EXISTS]**. This alone fully
covers MMIX and PDP-1. Everything else is *composition* on top.

---

## 4. Hierarchical decode — opcode maps & escapes (x86, VAX, M68K)  **[UPCL]**

Real CISC decode is a tree of tables with escape bytes. Express it directly:

```
decode_table main {
    0x0F => decode_table twobyte;              // x86: escape to the 0F map
    0x00 => insn "add.Eb.Gb" ( modrm: ea_x86<8> );
    0xFD => decode_table vax_fd;               // VAX: 2-byte (FD) opcode escape
    ...
}
decode_table twobyte {
    0x38 => decode_table threebyte_38;         // x86: 0F38 map
    0x3A => decode_table threebyte_3A;
    0xA2 => insn "cpuid" ();
    ...
}
```

- **x86:** 1-byte → `0F` → `0F38`/`0F3A` maps.
- **VAX:** the `FD` (and `FF`) two-byte opcodes.
- **M68K:** the 4-bit "line" (top nibble) selects a sub-decoder; express as a
  `decode_table` keyed on `[15:12]`.

UPCL compiles the tables into a decision tree / DFA over the opcode bits (§15).

---

## 5. Decode-time state & continuation — the agnostic way to handle "prefixes"  **[UPCL]**

Some ISAs precede the opcode with granules that *change how the rest decodes* —
x86 prefixes/REX/VEX are the famous case, but the same shape appears as
mode-select words, escape-and-set bytes, or extension toggles on other machines.
**UPCL must not hard-code any of this.** It provides two CPU-agnostic primitives,
and a "prefix" becomes nothing more than an ordinary use of them.

### 5.1 Architecture-declared decode state
The `.def` declares whatever transient decode-time variables *it* needs. The
language fixes **no** field names — there is no built-in `rex`, `opsize`, or
`seg`. This is the decode-time analogue of UPCL's existing `temp_value`
**[EXISTS]**:

```
decode_state {                  // every name here is the ARCHITECTURE's, not the language's
    op_size  : #i8 = default;   // initialized from the arch's default mode
    addr_size: #i8 = default;
    ext_regs : #i8 = 0;         // e.g. holds REX/VEX-supplied register-number bits
    rep      : #i2 = 0;
    seg      : #i8 = none;
}
```

### 5.2 Continuation in decode tables
A `decode_table` arm may **set state and `continue`** to the next granule instead
of resolving to an instruction. A "prefix" is simply a self-looping arm; the loop
ends when an arm finally yields an `insn`.

```
decode_table main {
    byte 0x66        => { set op_size  = alt;  continue; }   // a "prefix": adjust state, keep going
    byte 0x67        => { set addr_size = alt;  continue; }
    byte 0xF2 0xF3   => { set rep = byte;        continue; }
    byte 0x40..0x4F  when mode == 64                          // an arch-defined leading byte (REX)
                     => { set ext_regs = field[3:0]; continue; }
    byte 0x0F        => decode_table twobyte;                 // escape (not a state change)
    byte 0x01        => insn "add.Ev.Gv" ( ... );             // an instruction terminates the loop
    default          => illegal;
}
```

Operand decoders (§6) and `semantics` read the declared state as ordinary
parameters (`op_size`, `ext_regs`, …). **Nothing in the language knows what a
prefix, a REX byte, or an operand-size override is** — those are entirely
descriptions in the `.def`. ISAs with no leading state (VAX, M68K, every RISC)
never write a `set`/`continue`, never declare `decode_state`, and pay nothing.

This is strictly more general than a dedicated prefix construct and keeps UPCL
closed under composition (§10): a novel ISA with some unforeseen "decode mode
selector" granule is expressed the same way, with no UPCL change.

---

## 6. Recursive operand decoders — the centerpiece (PDP-11, M68K, VAX, x86)  **[UPCL] [CORE]**

The unifying insight: an **operand specifier** (PDP-11/M68K 6-bit mode×reg, VAX
operand byte, x86 ModRM/SIB) is *itself* a small decoder that (a) reads its own
bits, (b) may **`fetch`** additional granules — advancing a shared **length
cursor** — and (c) yields a value/address expression plus optional pre/post side
effects. Make operand decoders **first-class, named, recursive, cursor-threading**
constructs:

```
// M68K effective address: one declaration covers all modes; size is a parameter.
operand ea_m68k<size> -> { value?, addr?, pre?, post? } {
    field mode:3, reg:3;
    match mode {
        0o0 => value <- D[reg];                                   // Dn
        0o1 => value <- A[reg];                                   // An
        0o2 => addr  <- A[reg];                                   // (An)
        0o3 => { addr <- A[reg]; post A[reg] <- A[reg] + size/8; } // (An)+
        0o4 => { pre  A[reg] <- A[reg] - size/8; addr <- A[reg]; } // -(An)
        0o5 => addr  <- A[reg] + %S(fetch #i16);                  // (d16,An)   +1 word
        0o6 => addr  <- brief_ext(A[reg]);                        // (d8,An,Xn) +ext word
        0o7 => match reg {
            0o0 => addr  <- %S(fetch #i16);                       // (xxx).W
            0o1 => addr  <- fetch #i32;                           // (xxx).L
            0o4 => value <- fetch #i<size>;                       // #imm        +operand size
            ...
        }
    }
}

insn "add" {
    encoding ( 0b1101, dn:3, op:3, ea: ea_m68k<size_of(op)> );    // compose: ea is a sub-decoder
    asm "add %ea, D%dn";
    semantics { D[dn] <- D[dn] + (ea.value ?? M[ea.addr]); }
}
```

The same construct expresses:
- **PDP-11:** `operand pdp11_ea` with the 8 modes (register, deferred, auto-inc/dec,
  index, and their deferred forms), each `fetch`-ing 0 or 1 extra words.
- **VAX:** `operand vax_spec` whose mode nibble selects literal/register/displacement
  /index modes, consuming 0–4+ displacement/immediate bytes; an instruction lists
  *N* such operands (`operands ( vax_spec, vax_spec, vax_spec )`), so operand
  **count varies per opcode** naturally.
- **x86:** `operand ea_x86<size>` decodes ModRM, optionally SIB, then a
  mode/`decode_state`-dependent displacement, producing a register or memory EA.

**Side-effect ordering** (`pre`/`post`) captures auto-increment/decrement
precisely — a frequent emulation-bug source done declaratively once.

### Core implication
Because operand decoders consume a **data-dependent number of granules**, total
instruction length is computed by the cursor, not a fixed stride. libcpu's
`tag_instr` already returns `next_pc` *explicitly* **[EXISTS]** — so this fits the
existing contract; the decoder fills `next_pc = pc + cursor_bytes`.

---

## 7. Content-dependent length & the cursor (VAX, x86)  **[UPCL] [CORE]**

A single **length cursor** threads through leading state → opcode → each operand
decoder. Each `fetch #iN` advances it; the final cursor value *is* the instruction
length. This is the only sound way to length-decode VAX (length depends on operand
specifiers) and x86 (prefixes + ModRM + SIB + disp + imm). The cursor is implicit
in the DSL — authors never compute lengths by hand — and lowers to the `next_pc`
that `tag_instr` returns and the `bytes` count `tag_recursive` consumes
(`tag.cpp`).

---

## 8. Bundles/templates as a fetch mode (IA-64)  **[UPCL] [CORE]**

`granule bundle` selects the bundle fetch model from
`docs/upcl-complex-cpu-ia64.md` §2: fetch 128 bits, read the 5-bit template, map
each 41-bit slot to a unit-typed sub-decoder, and mark stops. This is just another
fetch model plugged into the same DSL — slot decoders are ordinary `decode_table`s
keyed on the slot's opcode bits.

---

## 9. One declaration drives decode *and* disassembly  **[UPCL]**

Today every libcpu arch hand-writes a separate `*_disasm.cpp` that must stay in
sync with translation. Fold the assembly syntax into the same `insn` via an `asm`
format string referencing captured operands (`%X`, `%ea`, with operand decoders
supplying their own textual rendering). One source of truth →
`generate.h::generate_*` emits both `disasm_instr` and the
`tag_instr`/`translate_instr` paths from it. No drift.

```
operand ea_m68k<size> -> {...} {
    ...
    asm 0o3 => "(A%reg)+";        // each mode renders itself
    asm 0o5 => "%d16(A%reg)";
}
```

---

## 10. Bespoke / novel ISAs (TAOHE)  **[UPCL]**

> *Note:* TAOHE is not a standard published ISA I can describe from a spec; I
> treat it here as the representative of the **custom/experimental** case. That is
> the more important requirement anyway.

The decoder DSL must be **closed under composition**: `fetch` models, `decode_table`s,
arch-declared `decode_state` + continuation (§5), and `operand` decoders are all
**user-defined building blocks**.
A novel or private ISA — irregular field placement, odd word size, a unique
operand-specifier scheme — is expressed by *defining and composing these blocks in
the `.def`*, never by patching the UPCL compiler. Concretely, the DSL must allow:

- arbitrary `granule`/`word_size`/`bit_order` (handles unusual word machines),
- user `operand` decoders with arbitrary `fetch`/`match` bodies (handles novel
  addressing/operand schemes),
- nested/recursive `decode_table`s and `match` (handles bespoke escape structures),
- a `default =>` / `illegal` arm everywhere (handles undefined encodings safely).

If TAOHE (or any future ISA) needs a primitive none of these provide, that is a
signal to extend the DSL generically — not to special-case the architecture. The
litmus test for the design: *can a stranger's undocumented ISA be decoded purely
from a `.def`?*

---

## 11. How the decoder lowers into libcpu  **[UPCL]**

The decoder declaration compiles to libcpu's three decode-facing callbacks
(`ARCHITECTURE.md`):

- **`tag_instr`** ← control-flow class per `insn` (`jump`/`branch`/`call`/`return`/
  `trap` → `TAG_*` **[EXISTS]**), `next_pc = pc + cursor`, and `new_pc` from the
  declared branch target. Variable length is handled by the cursor (§7).
- **`disasm_instr`** ← the `asm` format strings (§9).
- **`translate_instr`** ← the `semantics { }` over captured operands; immediates
  flagged `CONSTANT` **[EXISTS]** are folded at translate time, the rest become
  runtime operand expressions (the `c::decoder_operand_expression`).

Operand decoders that `fetch` extra words emit the loads of those words from the
instruction stream as part of decode; their `value`/`addr`/`pre`/`post` results
become the operand expressions the semantics consume.

---

## 12. Micro-examples across the spectrum

```
// PDP-11 double-operand (octal world): MOV src,dst — both are 6-bit specifiers.
insn "mov" {
    encoding ( op:4 = 0o1, src: pdp11_ea<16>, dst: pdp11_ea<16> );
    asm "mov %src, %dst";
    semantics { dst <- src; }            // dst.addr/value resolved by the operand decoder
}

// VAX: operand count varies per opcode; here a 3-operand add.
insn "addl3" {
    encoding ( op:8 = 0xC1 );
    operands ( a: vax_spec<32>, b: vax_spec<32>, c: vax_spec<32> );
    asm "addl3 %a, %b, %c";
    semantics { c <- a + b; }
}

// x86: ADD r/m32, r32. Leading state bytes (§5) have already set op_size/ext_regs.
insn "add.Ev.Gv" {
    encoding ( op:8 = 0x01 );
    operands ( rm: ea_x86<op_size>, reg: gpr(modrm.reg, ext_regs) );
    asm "add %rm, %reg";
    semantics { rm <- rm + reg; /* +flags via %CC */ }
}
```

---

## 13. Roadmap

```
P1  Bit-field encoding + asm + semantics surface grammar; lower to tag/disasm/
    translate. Validate on MMIX (fixed) and PDP-1 (18-bit word, octal).        (§2,§3,§9)
P2  decode_table / nested match + decision-tree generation. M68K "lines",
    x86/VAX escape maps.                                                       (§4)
P3  Recursive operand decoders + length cursor. Bring up PDP-11 and M68K EA.   (§6,§7)
P4  Decode-time state + continuation (agnostic "prefix" handling); x86
    legacy/REX/66/67 as .def-declared decode_state.                           (§5)
P5  VAX N-operand specifiers; x86 SIB/VEX/EVEX.                                (§6)
P6  granule bundle (IA-64) reusing the ia64 doc's template layer.             (§8)
P7  Unified disasm coverage + decoder-coverage/ambiguity checker.             (§9,§14)
```

P1 proves the regular end; P3 proves the addressing-mode end (the bulk of
classic minis); P4–P5 conquer the variable-length CISC end.

---

## 14. Risks & correctness

- [ ] **Ambiguity / overlap.** Two `insn`/table arms matching the same bits must be
      a compile error; generate an exhaustiveness + disjointness check.
- [ ] **Coverage.** Every opcode space needs a `default =>`/`illegal` arm; verify
      the decision tree is total (no silently-undecoded encodings).
- [ ] **Length correctness.** The cursor must account for *every* fetched granule
      (leading-state byte, opcode, each operand) — under/over-count corrupts `next_pc` and
      mis-tags following code.
- [ ] **Word/bit-order.** PDP-1 18-bit fields and `msb0`/`octal` must be honored;
      no hidden octet assumptions.
- [ ] **Leading-state interactions.** x86 redundant/conflicting prefix bytes,
      REX-after-legacy ordering, and override semantics must be modeled correctly
      in the `.def`'s `decode_state` transitions — the language enforces none of
      this, so the description must.
- [ ] **Operand side effects.** Auto-inc/dec `pre`/`post` ordering and the
      same-register-twice cases (e.g. `mov -(sp),(sp)+`) must be exact.
- [ ] **Disasm/translate parity.** Since both come from one declaration, test that
      decode→asm→(reassemble) round-trips on a corpus.

---

## 15. A design decision worth your input

The decoder DSL can be **generated** two ways, and the choice trades compactness/
speed against debuggability and how cleanly stateful/continued decode (§5) is
handled:

```
/*
 * How UPCL turns the decode_table / operand declarations into a decoder.
 * The choice fixes decode speed, generated-code size, and leading-state handling.
 */

// (A) TABLE / DECISION-TREE: compile all encodings into a DFA over opcode bits
//     (à la modern disassembler generators); operands dispatch via small tables.
//   + compact, fast, uniform; great for fixed/regular ISAs (MMIX, RISC, IA-64 slots).
//   - stateful/continued decode (x86 leading state bytes, §5) and recursive operand
//     specifiers (VAX) don't fit a pure bit-DFA; need an escape hatch to procedural code.

// (B) RECURSIVE-DESCENT / PROCEDURAL: generate a decoder that mirrors the declared
//     structure (read leading state -> opcode -> operand decoders), threading state + cursor.
//   + natural for continued/stateful decode (§5) and recursive operand decoders
//     (x86/VAX/M68K); easy to step through and debug.
//   - larger/slower than a tuned DFA for the regular cases.
```

The pragmatic answer is almost certainly **hybrid**: a generated **decision tree
for opcode dispatch** (A) with **procedural operand decoders + a leading-state
pre-pass** (B) for the content-dependent parts — which matches how production x86/VAX
decoders are actually structured. The open question is *where to draw the A/B
boundary*: dispatch-only by table, or also table-drive the regular operand forms?
Which dominates your targets — *raw decode throughput on regular ISAs*, or
*clean handling of x86/VAX-class irregularity*?

---

## 16. References / prior art

- **Architecture description / decoder generators** — SLED / New Jersey Machine-Code
  Toolkit, Sail, LISA, ArchC, nML/SimnML, Ghidra SLEISGH (a particularly relevant
  decode-and-semantics DSL), LLVM TableGen `MCInst` decoders.
- **Per-ISA references** — *MMIXware* (Knuth); Intel® Itanium® SDM; DEC PDP-1 and
  PDP-11 processor handbooks; *M68000 PRM*; *VAX Architecture Reference Manual*;
  Intel® SDM Vol. 2 (x86 opcode maps, ModRM/SIB/VEX/EVEX).
- **Sibling docs** — `docs/upcl-cpu-fpu-simd.md` (`encoding`/semantics & dynamic
  lowering), `docs/upcl-complex-cpu-ia64.md` (`granule bundle`/templates),
  `ARCHITECTURE.md` (`tag_instr`/`disasm_instr`/`translate_instr` lowering targets).
```

---

### One-paragraph summary

Give UPCL a declarative **instruction-decoder sub-language** built from five
composable mechanisms: a **fetch model** (byte / word / bundle granule, with
non-octet word sizes and octal/bit-order for PDP-1 and PDP-11), **bit-field
pattern matching** (the regular baseline — MMIX), **hierarchical `decode_table`s
with escapes** (x86 `0F`/`0F38`/`0F3A`, VAX `FD`, M68K lines), **decode-time
state with continuation** — the CPU-agnostic basis for "prefixes" (x86
legacy/REX/VEX and operand/address size expressed as `.def`-declared
`decode_state`, *not* a language construct) — and, the centerpiece, **first-class
recursive `operand` decoders that
thread a length cursor**, which uniformly express PDP-11/M68K/VAX addressing-mode
specifiers and x86 ModRM/SIB by `fetch`-ing extra words and emitting
value/address/pre/post results. Content-dependent length falls out of the cursor
(matching libcpu's existing explicit `next_pc` from `tag_instr`), one `insn`
declaration drives both disassembly and translation (ending the `*_disasm.cpp`
drift), and the whole DSL is closed under composition so a bespoke/undocumented
ISA like TAOHE is expressed purely in a `.def` — never by patching the compiler.
The pivotal choice is decision-tree vs. recursive-descent decoder generation,
almost certainly a hybrid: table-driven opcode dispatch plus procedural operand
decoders and a leading-state pre-pass.
