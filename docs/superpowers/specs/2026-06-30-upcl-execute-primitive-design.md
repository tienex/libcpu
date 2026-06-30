# UPCL Execute-Instruction Primitive — Design

**Goal:** Add a general, reusable UPCL engine capability — "execute the single instruction at a
computed address, then continue" — and use it to implement DEC PDP-1 `XCT` and to complete the
DEC PDP-10 `XCT` stub. The frontend never re-implements the ISA: the engine re-decodes the target
instruction (single source of truth).

**Scope (this cycle):** the execute primitive + PDP-1 XCT + PDP-10 XCT + tests, on the
**interpreter** backend. **Explicitly OUT of scope** (deferred to a separate follow-up cycle): the
PDP-1 sequence-break/interrupt system and the real-time clock that running the full Spacewar! game
additionally requires. This cycle does NOT make Spacewar plot points; it removes the XCT blocker.

**Prior art:** an exploratory, unreviewed implementation of this exact mechanism lives on the
branch `pdp1-spacewar-wip-reference` (commits `94a0313..9a37d8a`). It validated the shape below; we
re-do it properly (spec → plan → reviewed SDD). Do not merge that branch.

---

## 1. UPCL surface — the `$` builtin sigil

UPCL today has three identifier sigils: `@name` (user-defined macros), `%name` (metas /
meta-registers like `%PC`, `%M`), and bare names (registers/operands). We add a fourth:

- **`$name`** — a **framework builtin (intrinsic)**: an operation the engine implements directly,
  distinct from user macros. The lexer produces a `TokBuiltinIdent` (Text = the word after `$`),
  exactly mirroring how `@` produces `TokMacroIdent`. This reserves a clean namespace for current
  and future intrinsics; this cycle defines exactly one: **`$exec`**.

- **`$exec(addr)`** — execute the one instruction located at word/byte address `addr`, then
  continue. Usable as a statement inside an instruction body. `addr` is any UPCL expression
  (typically an effective-address macro call, e.g. `$exec(@ea(I, Y))`).

The lexer change is additive: `$` is not currently a token starter in UPCL grammar, so introducing
`$word` cannot change the meaning of any existing `.upcl` file. (Verify during implementation that
no example uses a bare `$`.)

---

## 2. Engine mechanism

`$exec(addr)` lowers (in `Semantics.cpp`) to two effects, emitted in order:
1. Store `addr` into a dedicated `CPU_STATE` field **`DispPc`** (the "dispatch PC").
2. Emit a **trap** carrying the reserved syscall vector **`CPU_EXEC_ONE`**, with the trap's return
   address = the instruction immediately after the `$exec` site (e.g. `XCT`+1).

This reuses the existing trap/`SyscallVector`/`TrapPc` seam (the same mechanism `@trap` already
uses) — no new lowering machinery. The host driver's run loop handles `CPU_EXEC_ONE`:

```
on SyscallVector == CPU_EXEC_ONE:
    Ret  = State.TrapPc            // address after the $exec site (XCT+1)
    Disp = State.DispPc            // address of the instruction to execute
    State.PC = Ret                 // sequential-next for the executed instruction (SIMH: a skip
                                   //   in the target lands at Ret+1, a jump at its own target,
                                   //   fall-through at Ret) -- see PC-context note below
    execute exactly ONE instruction at Disp (GenerateAotCfg over a single-instruction region)
    resume the run loop at State.PC
```

**PC context (faithful XCT semantics, SIMH `pdp1_cpu.c` / `pdp10_cpu.c`):** the executed
instruction behaves as if it were at the XCT location — a skip skips the word *after* the XCT, a
jump transfers, a fall-through returns to XCT+1. Setting `State.PC = Ret` before executing the one
instruction achieves this: instructions that don't touch PC leave it at `Ret`; skips do `PC+1`
(→ Ret+1); jumps overwrite PC. The plan pins the exact ordering against SIMH.

**Single-instruction execution:** the run loop generates a region of exactly one instruction at
`Disp` via the existing `GenerateAotCfg(arch, backend, Disp, Disp+oneUnit, …)` and executes it.

**`CPU_ARCH_INFO.PcRegIndex`:** a new field exposing the architectural PC register's slot index, so
the arch-neutral run loop can read/write the guest PC for the `State.PC = Ret` step without
hard-coding a register number.

**Nesting (XCT-of-XCT):** the executed instruction may itself be an `XCT`, which traps
`CPU_EXEC_ONE` again — natural recursion through the run loop. A depth counter bounds it to
**16** (SIMH `xct_max`); on overflow the run loop stops with a clear "XCT nesting too deep" trap
(mirroring SIMH `STOP_XCT`).

**Backends:** the interpreter. PDP-1 and PDP-10 are word-addressed and already *require* the
interpreter (existing guard in `CmdRun`). If `$exec` is reached under a non-interpreter backend,
emit a clear unsupported-capability error rather than miscompiling — consistent with the existing
word-addressed→interpreter guard.

---

## 3. Reserved values / state additions

- `CpuState.h`: `#define CPU_EXEC_ONE UINT64_C (0xE5EC)` (a sentinel `SyscallVector`, "EXEC", chosen
  high so it never collides with a guest trap vector); a new `CPU_STATE` field `DispPc` (the
  dispatch address set by `$exec`; frontends that never use `$exec` leave it 0).
- `ICpu.h` / `CPU_ARCH_INFO`: `PcRegIndex` (UINT32) — the PC register's slot index.

---

## 4. Frontends wired this cycle

- **PDP-1** (`upcl/examples/pdp1.upcl`): replace the branch-approximation XCT with the real form:
  ```
  jump insn xct : type trap, encode #i18 ( op:5 = OPC_XCT, i:1 -> I, y:12 -> Y ),
      disasm ( mnemonic : "xct", operands : I, Y )
      { $exec ( @ea ( I, Y ) ); }
  ```
  (`type trap` because the body emits a trap; the engine resumes per the executed instruction.)
- **PDP-10** (`upcl/examples/pdp10.upcl`): replace the documented XCT *stub* (opcode `0o256`) with
  `{ $exec ( @ea ( i, x, y ) ); }` (the AC field is ignored, per the HRM). Update the stub comment.

---

## 5. Testing (real octal, run-to-HLT, no weakening)

- **`upcl.pdp1-xct`**: a raw `--raw18` program where `XCT` targets a real instruction — e.g.
  `M[E] = LAC k` so that `XCT E` loads AC from `k`; assert AC got the loaded value AND execution
  continued at XCT+1 (store AC to a known word, `--dump`). Also a skip case: `XCT` of a `SAS`/`SAD`
  that skips → assert the skip landed at XCT+2.
- **`upcl.pdp10-xct`**: a `.SAV`/raw PDP-10 program where `XCT` executes e.g. a `MOVEI`/`ADDI` and
  continues; assert the AC result + continuation (register dump).
- **Nesting test**: `XCT` of an `XCT` of a real op → assert the innermost effect applied and control
  returned correctly; and (PDP-1) a 17-deep chain trips the depth cap (stops, does not loop).
- **Decode tests**: `XCT` still decodes (PDP-1 `0o10xxxx`, PDP-10 `0o256...`); the lowered IR shows
  the `$exec` trap (e.g. a `DispPc` store + the `CPU_EXEC_ONE` trap).
- Regression: the full `upcl.pdp1*` / `upcl.pdp10*` suites stay green; `$` sigil addition breaks no
  existing `.upcl` (run `lcx upcl check` across the example set).

---

## 6. File structure

| File | Change |
|---|---|
| `LibCPU/upcl/Token.h` | add `TokBuiltinIdent` |
| `LibCPU/upcl/Lexer.cpp` | lex `$word` → `TokBuiltinIdent` (mirror `@`) |
| `LibCPU/upcl/Ast.h` | a builtin-call node (name + arg expr list) |
| `LibCPU/upcl/Parser.cpp` | parse `$name(args)` as a builtin-call statement/expr |
| `LibCPU/upcl/Semantics.cpp` | lower `$exec(addr)` → `DispPc` store + `CPU_EXEC_ONE` trap (return = next PC) |
| `LibCPU/upcl/UpclArch.cpp` | set `CPU_ARCH_INFO.PcRegIndex` from the register file |
| `LibCPU/include/LibCPU/CpuState.h` | `CPU_EXEC_ONE`, `CPU_STATE.DispPc` |
| `LibCPU/include/LibCPU/ICpu.h` | `CPU_ARCH_INFO.PcRegIndex` |
| `LibCPU/test/lcx.cpp` | run-loop `CPU_EXEC_ONE` handler (one-instruction execute + resume + depth cap) |
| `upcl/examples/pdp1.upcl` | XCT → `$exec(@ea(I,Y))` |
| `upcl/examples/pdp10.upcl` | XCT stub → `$exec(@ea(i,x,y))` |
| `LibCPU/CMakeLists.txt` | `upcl.pdp1-xct`, `upcl.pdp10-xct`, nesting test |

---

## 7. Out of scope (YAGNI / deferred)

- The PDP-1 sequence-break/interrupt system and the cycle-driven RTC (needed for the *full*
  Spacewar run) — a separate follow-up cycle.
- Other `$` intrinsics ($lea/$trap/$fabs/etc.) — only `$exec` this cycle; the sigil is the
  extension point.
- Execute-primitive support on non-interpreter backends (JIT/AOT) — word-addressed XCT arches are
  interpreter-only today; revisit if a byte-addressed ISA needs `$exec` under a JIT.
- A host-side single-instruction interpreter — explicitly rejected; the engine re-decodes the
  target so the ISA is defined once in `.upcl`.
