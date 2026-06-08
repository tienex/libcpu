# UPCL: Generating CPU Emulators, and Using It Dynamically for IEEE FPU & SIMD

> How to finish UPCL (the *Universal Processor Crafting Language*) into a tool
> that **writes complete libcpu frontends from a declarative `.def`**, and how to
> use the same UPCL semantic engine **dynamically** — at translation time — to
> implement the parts that are most painful to hand-write per architecture:
> **IEEE-754 floating point** and **SIMD/vector ISAs**.
>
> Read `ARCHITECTURE.md` (frontend model) and
> `docs/com-architecture-and-backends.md` (the abstract emitter / multi-backend
> design) first — the "dynamic" half of this document builds directly on the
> abstract-emitter seam proposed there.

---

## 0. What UPCL already is (grounded in the tree)

UPCL is a declarative CPU-description compiler under `upcl/`. Its pipeline:

```
 .def  ──►  lexer (ast/lexer.l) + parser (ast/parser.y)  ──►  AST (ast/ast.h)
        ──►  semantic analysis (sema/, c/sema_analyzer.cpp)
        ──►  typed semantic IR  (the c::  namespace)
        ──►  code generation   (cg/generate.*)            ──►  libcpu C++ frontend
```

### It already targets libcpu frontends
`upcl/cg/generate.h` declares generators that emit **exactly the files a libcpu
frontend is made of**:

| UPCL generator | Produces (libcpu frontend piece) |
|----------------|----------------------------------|
| `generate_arch_h` / `generate_regfile_h` / `generate_arch_cpp` | register file + `arch_*_init`, the `cpu_archinfo_t`/`reg_*_t` |
| `generate_opc_h` | opcode table |
| `generate_tag_cpp` | `tag_instr` (from `jump_instruction`s) |
| `generate_tcond_cpp` | `translate_cond` |
| `generate_libcpu_expression` | the **`frontend.h` macros** (`ADD`, `SUB`, `MUL`, `COM`, `NEG`, `NOT`, …) from a `c::expression` tree |

So UPCL's back end *already speaks libcpu's helper API* — `generate_libcpu_expression`
walks the semantic expression tree and prints `ADD(...)`, etc.

### Its semantic IR is already FP- and SIMD-aware
`upcl/c/type.h` defines `INTEGER, FLOAT, VECTOR, VECTOR_INTEGER, VECTOR_FLOAT`
with both `bits` *and* `elem_bits` (lane width) — i.e. **vectors/SIMD are
first-class in the type system**. There is a `float_expression`, a
`jump_instruction` whose kinds (`BRANCH/CALL/RETURN/TRAP`) map 1:1 onto libcpu's
`TAG_*`, and a `decoder_operand_def` carrying `CONSTANT` (evaluable at translate
time) and `CCFLAGS` (usable with the `@eval_cc` intrinsic) flags — the
decode-time-vs-run-time distinction is already modeled.

### What is *not* finished (the honest gaps)
- **`main.cpp` stops after semantics.** It parses and runs `sema_analyzer`, prints
  *"Semantic analysis succeeded"*, and returns — it never drives `cg`. The code
  generators exist but aren't wired into an end-to-end `def → files` flow.
- **The register-file grammar is the most complete part** (see `examples/*.def`
  and `docs/ebnf.txt`). The **instruction/decoder surface syntax** (encoding
  patterns → operands → semantics statements) is only partially expressed in the
  grammar even though the `c::` IR for it (`instruction`, `decoder_operand_def`,
  `statement`, `assign_statement`, `store_statement`, the expression classes)
  exists.
- **UPCL is disabled in the build** (`ADD_SUBDIRECTORY(upcl)` is commented out in
  the root `CMakeLists.txt`).

Items below are tagged **[EXISTS]** / **[BUILD]** accordingly.

---

## 1. Two ways to use UPCL

| Mode | What it does | Output | When |
|------|--------------|--------|------|
| **A. Static generator** *(its original design)* | `def → C++` compiled into libcpu | a hand-written-equivalent frontend | shipping a new architecture |
| **B. Dynamic semantics engine** *(the new ask)* | `def → c:: IR → abstract emitter` **at translation time** | IR fed to interpreter/LLVM/WASM, no C++ build | FPU/SIMD op libraries; rapid bring-up; data-driven ISAs |

Mode A is the shortest path to "UPCL writes CPU emulators" (§2). Mode B is what
makes "implement IEEE FPU / SIMD easily" tractable (§4–§5), and it depends on the
**`ICpuEmitter`** abstraction from `docs/com-architecture-and-backends.md` §3:
instead of `generate_libcpu_expression` printing `ADD(...)` *text*, a new lowering
calls `pEmitter->Add(...)` *directly*.

```
A:  c::expression ──► generate_libcpu_expression ──► "ADD(a,b)" text ──► C++ ──► LLVM
B:  c::expression ──► lower_to_emitter           ──► ICpuEmitter->Add(a,b) ──► {interp | LLVM | WASM}
```

The two share everything up to the `c::` IR; only the final lowering differs.

---

## 2. Making UPCL write a complete CPU emulator (Mode A)  **[BUILD on EXISTS]**

### 2.1 Close the loop in `main.cpp`
After `sema_analyzer` succeeds, drive the existing generators to emit the frontend
files (`generate_arch_cpp`, `generate_regfile_h`, `generate_opc_h`,
`generate_tag_cpp`, `generate_tcond_cpp`, and per-instruction
`generate_libcpu_expression` into a `*_translate.cpp`). The generators are
**[EXISTS]**; the driver/glue is **[BUILD]**.

### 2.2 Complete the instruction/decoder surface syntax
The register-file half is done; the instruction half needs grammar + sema to match
the IR that already exists. A natural shape (proposed — *not* yet final grammar):

```
insn "add" {
    encoding  ( op:6=0b000000, rs:5, rt:5, rd:5, 0:5, fn:6=0b100000 );  // decode pattern → operands
    decode    rd, rs, rt;                                               // operand binding
    semantics { rd <- rs + rt; }                                       // c::assign_statement over c::expressions
}

jump "beq" branch if (rs == rt) {
    encoding ( op:6=0b000100, rs:5, rt:5, off:16 );
    target   PC + (%S(off) << 2);                                      // jump_instruction(BRANCH, cond)
}
```

- **`encoding(...)`** → bit-pattern decoder → `decoder_operand_def`s
  (with `CONSTANT` for immediates known at translate time). Feeds
  `generate_opc_h` and the disassembler.
- **`jump ... branch/call/return/trap if (cond)`** → `jump_instruction` →
  `generate_tag_cpp` (`tag_instr`) and `generate_tcond_cpp` (`translate_cond`).
  This is why the `c::jump_instruction` kinds already mirror libcpu's `TAG_*`.
- **`semantics { ... }`** → `c::statement`s over `c::expression`s →
  `generate_libcpu_expression` → `*_translate.cpp`.

### 2.3 What you get
A `.def` that already declares the register file (proven by `examples/6502.def`,
`mips32.def`, etc.) plus the instruction blocks above compiles to a full
`arch_func_t` frontend — register layout, `tag_instr`, `disasm_instr`,
`translate_cond`, `translate_instr` — with no hand-written C++.

### 2.4 Re-enable in the build
Uncomment `ADD_SUBDIRECTORY(upcl)`; add a CMake rule `def → generated/<arch>/`
so adding an architecture is "drop a `.def`, rebuild."

---

## 3. Why FPU and SIMD are the high-value targets for *dynamic* UPCL

Hand-writing FP and SIMD in each `*_translate.cpp` is where the per-architecture
cost explodes, because the semantics are intricate and *identical across
architectures*:

- **IEEE-754 FPU:** rounding modes, NaN/Inf propagation, denormals/flush-to-zero,
  sticky exception flags (invalid/overflow/underflow/inexact/divbyzero), fused
  multiply-add, format conversions (fp80/fp128 are already special-cased in
  libcpu via `CPU_FLAG_FP80/FP128`).
- **SIMD:** the same scalar op replicated lane-wise, with saturation, lane
  masks/predication, shuffles/permutes, horizontal reductions, widening/narrowing.

UPCL already has the right *types* (`FLOAT`, `VECTOR_FLOAT`, `VECTOR_INTEGER` with
`elem_bits`). The leverage: **describe the operation once, declaratively, and let
the lowering produce correct, backend-appropriate code** — LLVM vector/FP
intrinsics for the JIT, scalarized loops for the interpreter, WASM SIMD where
available. That is exactly Mode B.

---

## 4. IEEE FPU via UPCL  **[BUILD]**

### 4.1 Describe FP semantics declaratively
Extend UPCL with FP-status state and rounding-aware operators. The register-file
grammar already binds status bits (`%CC`, the `value_bind` flag formats); add an
FP status/control register and rounding-mode operand:

```
register_file {
    group FP {
        [ #f64 f0? : 32 ],                                  // 32 doubles  [EXISTS: repetition + #f type]
        [ #i32 FPCR -> %FPCR #i1 explicit                   // control/status, NVZC-style flag binding
            ( RM:2 : FZ : ... : IOC<-invalid : OFC<-overflow : UFC<-underflow : IXC<-inexact ) ]
    }
}

insn "fadd.d" {
    encoding ( ... fd:5, fa:5, fb:5 );
    semantics {
        fd <- fadd.d(fa, fb) round %FPCR.RM raises %FPCR;   // rounding mode + sticky flags  [BUILD ops]
    }
}
```

- **`round <mode>`** annotates the op with its rounding mode (the `@eval_cc`
  intrinsic precedent shows UPCL already supports op-attached evaluation hints).
- **`raises <status>`** declares which sticky exception flags the op updates —
  reusing the `CCFLAGS`/`value_bind` machinery that already exists for integer
  condition codes.

### 4.2 Lowering (Mode B)
The `c::float_expression`/typed FP ops lower to the **`ICpuEmitter`** FP surface,
which libcpu already has primitives for in `frontend.h`
(`arch_cast_fp32/64/80/128`, `arch_sqrt`) **[EXISTS]**:

- **LLVM backend:** emit constrained FP intrinsics (`llvm.experimental.constrained.fadd`
  with rounding/exception metadata) or `llvm.fma`, plus explicit status-flag
  updates. fp80/fp128 use the existing `CPU_FLAG_FP80/FP128` paths.
- **Interpreter backend:** call a shared soft-float / host-FP routine honoring the
  rounding mode and updating sticky flags — written **once**, reused by every
  architecture's `.def`.
- **WASM backend:** f32/f64 natively; fp80/fp128 via the soft-float routine
  (WASM has no extended types — see `docs/com-architecture-and-backends.md` §8).

### 4.3 The payoff
A new architecture's FPU is a handful of `fadd.d/fmul.d/fma.d/fcvt.*` lines
referencing shared rounding/flag semantics — not hundreds of lines of
hand-rolled LLVM per arch. The IEEE *behavior* lives in one lowering + one
soft-float library; each `.def` only states *which* ops exist and *how they
decode*.

---

## 5. SIMD ISAs via UPCL  **[BUILD]**

### 5.1 Lanes are already in the type system
`c::type::VECTOR_INTEGER/VECTOR_FLOAT` carry `elem_bits` **[EXISTS]**. A vector op
is "apply this scalar op across lanes," which UPCL can express with a lane-map
form (proposed syntax over the real `#v<elem>:<total>` vector type):

```
insn "vadd.b" {                                            // 16x8-bit add (128-bit)
    encoding ( ... vd:5, va:5, vb:5 );
    semantics {
        vd <- lanes #v8:128 (va[i] + vb[i]);               // map over lanes      [BUILD: lane intrinsic]
    }
}

insn "vqadd.b" { semantics { vd <- lanes #v8:128 sat(va[i] + vb[i]); } }   // saturating
insn "vpadd.w" { semantics { vd <- reduce #v32:128 (+) va; } }             // horizontal reduce
insn "vshuf.b" { semantics { vd <- shuffle va, vb, imm; } }                // permute
```

Primitives to add to UPCL/lowering: **`lanes`** (element-wise map), **`reduce`**
(horizontal), **`shuffle`/`select`** (permute/blend), **`sat`** (saturation),
and widen/narrow casts — a small, ISA-neutral vocabulary.

### 5.2 Lowering (Mode B)
- **LLVM backend:** emit native LLVM **vector types** and vector ops — `lanes`
  becomes a vector `add`, `reduce` becomes `llvm.vector.reduce.*`, `shuffle`
  becomes `shufflevector`, `sat` becomes `llvm.sadd.sat`. The optimizer/codegen
  then maps to host SSE/NEON/AVX automatically.
- **Interpreter backend:** scalarize — a loop over `total/elem_bits` lanes calling
  the scalar op. Written once.
- **WASM backend:** the WASM **SIMD** proposal (128-bit `v128`) covers the common
  cases; wider vectors scalarize.

### 5.3 The payoff
An entire SIMD extension (NEON-like, MMX/SSE-like, AltiVec-like) is expressed as
decode patterns plus one-line lane/reduce/shuffle semantics. The hard part —
generating efficient host vector code — is delegated to the backend's vector
support, not rewritten per architecture.

---

## 6. The synergy: UPCL becomes the authoring layer for the whole stack

Putting the pieces together with the multi-backend design:

```
        one  .def  (register file + instructions + FPU + SIMD, declarative)
                                │  UPCL: parse → sema → c:: IR
                                ▼
                       lower_to_emitter (Mode B)
                                │  ICpuEmitter   (com-architecture doc §3)
            ┌───────────────────┼────────────────────┐
        Interpreter           LLVM JIT              WASM
        (tier 0)            (tier 2, opt)        (portable)
```

- One declarative description → **every backend** (interpreter/LLVM/WASM) and
  **every tier** (HotSpot model, com-architecture doc §5).
- Shared **soft-float** and **lane** libraries mean FPU/SIMD correctness is
  written once and reused across all architectures' `.def`s.
- Generated artifacts (micro-ops / objects) drop straight into the **on-disk
  cache** (com-architecture doc §6), keyed off the code SHA-1 libcpu already
  computes.

Mode A (C++ generation) and Mode B (dynamic emission) coexist: ship stable
architectures as generated C++ for build-time optimization; use dynamic lowering
for FPU/SIMD op libraries, experimental ISAs, and data-driven extensions.

---

## 7. Roadmap

```
Phase 1  Close Mode A: drive cg from main.cpp; emit a full frontend from a .def
         that has a register file + a few integer insns; re-enable in build.   (§2)
Phase 2  Instruction/decoder grammar + sema to match the existing c:: IR
         (encoding patterns, operands, semantics statements, jumps).           (§2.2)
Phase 3  Mode B lowering: c:: IR → ICpuEmitter (depends on com-arch doc §3);
         validate it matches generate_libcpu_expression output for integers.   (§1)
Phase 4  IEEE FPU: FP status/rounding model + `round`/`raises` ops; shared
         soft-float lib; LLVM constrained-FP + interpreter paths.              (§4)
Phase 5  SIMD: `lanes`/`reduce`/`shuffle`/`sat` primitives; LLVM vector +
         scalarized interpreter + WASM SIMD lowerings.                         (§5)
Phase 6  Port one real ISA's FPU+SIMD from hand-written C++ to .def as proof
         (m68k FPU or a MIPS/ARM SIMD subset); diff behavior against the
         existing frontend.                                                    (§6)
```

Phase 1 stands alone and proves "UPCL writes emulators." Phases 4–5 are where the
"easily implement FPU/SIMD" payoff lands and depend on the abstract emitter.

---

## 8. Risks & correctness

- [ ] **IR parity.** Mode B lowering must produce semantics identical to Mode A's
      `generate_libcpu_expression` for integer ops — diff before trusting it.
- [ ] **IEEE conformance.** Rounding, NaN payload propagation, denormal handling,
      and sticky-flag accumulation must match the modeled architecture — test
      against reference vectors (e.g. TestFloat-style suites).
- [ ] **fp80/fp128.** Honor `CPU_FLAG_FP80/FP128`; software-emulate where the host
      or backend (WASM) lacks them.
- [ ] **Lane semantics.** Saturation signedness, lane ordering/endianness, and
      narrowing/widening rounding must be explicit in the `.def`, not implied.
- [ ] **Decode completeness.** Encoding patterns must be exhaustive/unambiguous;
      generate a decoder-coverage check.
- [ ] **`%CC`/flag reuse.** FP exception flags reuse the integer condition-code
      machinery (`@eval_cc`, `value_bind`) — keep their semantics distinct
      (sticky vs. overwritten).
- [ ] **Grammar stability.** The instruction/decoder syntax is *new* — version the
      language and the `.def`s.

---

## 9. A design decision worth your input

How FPU/SIMD operations are *represented in the `c::` IR* determines how much
work each backend does and how reusable the descriptions are. Two positions:

```
/*
 * Two representations for a complex op (e.g. fadd.d-with-rounding, or vqadd.b).
 * The choice fixes how much each backend implements and how portable .def is.
 */

// (1) INTRINSIC: the op stays a single high-level node; each backend implements it.
//     c::expression(FADD_D, fa, fb, round=RM, raises=FPCR)
//       LLVM      -> llvm.experimental.constrained.fadd + flag updates
//       interp    -> softfloat_add_d(...)
//       WASM      -> f64.add (+ soft flags)
//   + tiny .def, backends pick the best native instruction/intrinsic
//   - every backend must implement every intrinsic (N backends × M ops)

// (2) DECOMPOSED: UPCL expands the op into primitive c:: ops once; backends only
//     implement a small primitive set.
//     fadd.d -> {align, add-mantissa, normalize, round(RM), set-flags} primitives
//   + backends implement few primitives; semantics defined once, in UPCL
//   - large/slow IR; defeats native FP/vector instructions; harder to optimize
```

The trade-off: **(1) intrinsic** keeps `.def`s tiny and lets each backend use the
host's real FP/SIMD instructions (fast), at the cost of implementing each
intrinsic per backend; **(2) decomposed** centralizes the semantics in UPCL and
minimizes backend work, but generates bulky IR and throws away native FP/vector
instructions. A pragmatic hybrid is **intrinsic for the hot, well-supported ops
(add/mul/fma, vadd/vmul) and decomposed for the rare/odd ones** — but the default
stance shapes the whole effort. Which matters more here: *backend simplicity*, or
*using the host's native FPU/SIMD instructions*?

---

## 10. References / prior art

- **UPCL itself** — `upcl/docs/ebnf.txt`, `upcl/examples/*.def`, `upcl/c/` (the
  semantic IR), `upcl/cg/generate.*` (libcpu code generation).
- **Architecture description languages** — LISA, ArchC, Sail, nMP/nML, SLED —
  prior art for ISA-from-spec and decoder generation.
- **IEEE-754 / soft-float** — Berkeley SoftFloat + TestFloat (conformance), LLVM
  constrained-FP intrinsics.
- **SIMD lowering** — LLVM vector types & `llvm.vector.reduce.*`/`shufflevector`,
  the WASM SIMD (`v128`) proposal.
- **Sibling docs** — `docs/com-architecture-and-backends.md` (the `ICpuEmitter`
  and backends Mode B targets), `ARCHITECTURE.md` (the `arch_func_t` Mode A fills
  in), `docs/system-emulation.md` (FP/vector status registers as system state).
```

---

### One-paragraph summary

UPCL already contains most of what's needed to **write libcpu frontends from a
`.def`**: its code generators emit the register file, `tag_instr`,
`translate_cond`, and the `frontend.h` semantic macros, and its semantic IR is
already typed for `FLOAT`, `VECTOR_INTEGER`, and `VECTOR_FLOAT` (with lane
widths). The missing pieces are mechanical: drive the generators from `main.cpp`,
finish the instruction/decoder *surface* grammar to match the IR that already
exists, and re-enable the subproject in the build (Mode A). The high-value move
is **Mode B** — lowering the same `c::` IR not to C++ text but directly to the
abstract **`ICpuEmitter`** from the multi-backend design — because it lets you
**describe IEEE FPU and SIMD once, declaratively** (rounding/`raises` for FP;
`lanes`/`reduce`/`shuffle`/`sat` for vectors) and have shared lowerings produce
correct code for the interpreter, LLVM (native FP/vector intrinsics), and WASM,
with the IEEE/lane *behavior* written once in shared soft-float/lane libraries
instead of re-implemented in every architecture's hand-written frontend. The
pivotal design choice is whether complex ops stay **intrinsic** (tiny `.def`s,
backends use native instructions) or are **decomposed** into primitives (minimal
backend work, bulky IR) — likely a hybrid favoring intrinsics for hot ops.
