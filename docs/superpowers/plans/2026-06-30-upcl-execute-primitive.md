# UPCL Execute-Instruction Primitive (`$exec`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a general UPCL engine primitive `$exec(addr)` — "execute the one instruction at `addr`, then continue" — and use it to implement DEC PDP-1 `XCT` and complete the DEC PDP-10 `XCT` stub, on the interpreter backend.

**Architecture:** `$exec(addr)` lowers to: stash `addr` in the EXISTING `CPU_STATE.DispPc` dispatch scratch (via the existing `ICpuSmcEmitter::SetDispatchTarget`) and emit a trap with the reserved vector `CPU_EXEC_ONE` and return address = the next instruction (via the existing `ICpuSyscallEmitter::EmitSyscall`). The host run loop, on `CPU_EXEC_ONE`, generates a one-instruction region at `DispPc`, executes it, and resumes with faithful XCT control-flow semantics. The engine re-decodes the target — no per-frontend ISA copy.

**Tech Stack:** UPCL DSL + C++20 engine (`LibCPU/upcl/`, `LibCPU/include/LibCPU/`) + the `lcx` interpreter run loop. SIMH `pdp1_cpu.c` / `pdp10_cpu.c` = XCT semantics oracle.

## Global Constraints

- **Reuse, don't reinvent:** `CPU_STATE.DispPc` (CpuState.h:35) and `ICpuSmcEmitter::SetDispatchTarget` (ICpu.h:293) and `ICpuSyscallEmitter::EmitSyscall` ALREADY EXIST — `$exec` reuses them. New state is ONLY `CPU_EXEC_ONE` and `CPU_ARCH_INFO.PcRegIndex`.
- **`CPU_EXEC_ONE = UINT64_C(0xE5EC)`** — a reserved `SyscallVector` sentinel; must never collide with a guest vector.
- **Sigils:** `@name` = user macro, `%name` = meta, `$name` = framework builtin (this cycle: only `$exec`). A `$name` that matches no builtin is an ERROR (never falls through to the user-macro table).
- **Backend:** interpreter only (PDP-1/PDP-10 are word-addressed → already interpreter-only via the existing `CmdRun` guard).
- **XCT semantics (SIMH oracle):** execute the target as if it were at the XCT location — fall-through → continue at XCT+1; a skip in the target → XCT+2; a jump → the jump target. Nesting (XCT of XCT) bounded to 16 (SIMH `xct_max`); deeper → stop.
- **Style (CLAUDE.md):** 4-space indent; `if (x) { oneline }` braced; no useless `{}`; `LL/ULL` via `INT64_C/UINT64_C`; `~UINT64_C(0)`; NT/UEFI C++ types (`UINT32`/`CONST`); match existing file style by hand; NO clang-format. Match existing `.upcl` style.
- **Build:** `cmake --build LibCPU/build --target lcx`. **Test:** `ctest --test-dir LibCPU/build -R <name> --output-on-failure`. Run from the worktree root.
- **Prior art (reference only, do NOT merge):** branch `pdp1-spacewar-wip-reference` prototyped this; the code below is drawn from it but the run-loop skip-handling is corrected here.
- **No weakening tests; fix causes not symptoms.** XCT tests assert real computed values + correct continuation PC.

---

### Task 1: `$` builtin sigil + state scaffolding

**Files:**
- Modify: `LibCPU/upcl/Token.h`, `LibCPU/upcl/Lexer.cpp`, `LibCPU/upcl/Ast.h`, `LibCPU/upcl/Parser.cpp`
- Modify: `LibCPU/include/LibCPU/CpuState.h`, `LibCPU/include/LibCPU/ICpu.h`, `LibCPU/upcl/UpclArch.cpp`

**Interfaces:**
- Produces: lexer emits `TokBuiltinIdent` for `$word`; parser builds an `ExprCall` with `Builtin=true`; `CPU_EXEC_ONE` constant; `CPU_ARCH_INFO.PcRegIndex` populated from the register layout's PC index.

- [ ] **Step 1: Add `TokBuiltinIdent` to `Token.h`** (after `TokMacroIdent`):
```cpp
    TokBuiltinIdent, // $word -- a builtin (framework intrinsic) reference; Text holds the word
                     //   after '$'. '@' is reserved for user macros; '$' invokes a built-in
                     //   intrinsic ($exec/...).
```

- [ ] **Step 2: Lex `$word` in `Lexer.cpp`.** Add the `TokBuiltinIdent` name in `TokenName` (`case TokBuiltinIdent: return "$builtin";`), and replace the `case '$':` in `Lexer::Next ()`:
```cpp
    case '$':
        if (IsWordStart (Peek ())) {                                             // $builtin reference
            while (IsWordCont (Peek ())) { m_Pos++; }
            Token T = Make (TokBuiltinIdent, Begin);
            T.Text = T.Text.substr (1);                                          // strip the '$'
            return T;
        }
        return Make (TokDollar, Begin);
```

- [ ] **Step 3: Add the `Builtin` flag to `Ast.h`** (in the `Expr` fields, after `Linked`):
```cpp
    bool                     Builtin = false;     // ExprCall: a $-builtin (framework intrinsic),
                                                  //   not an @-user-macro ($exec/...)
```

- [ ] **Step 4: Parse `$name(args)` in `Parser.cpp` `ParsePrimary ()`** — extend the macro-call branch:
```cpp
    // @macro(args)  -- a user macro;   $builtin(args)  -- a framework intrinsic
    if (m_Cur.Kind == TokMacroIdent || m_Cur.Kind == TokBuiltinIdent) {
        Expr *E = new Expr (ExprCall); E->Loc = Loc; E->Name = m_Cur.Text;
        E->Builtin = (m_Cur.Kind == TokBuiltinIdent);
        Advance ();
        ParseCallArgs (E);
        return E;
    }
```

- [ ] **Step 5: Add `CPU_EXEC_ONE` to `CpuState.h`** (after the `CPU_NO_SYSCALL` define):
```cpp
// Reserved syscall vector emitted by the UPCL `$exec` builtin (an "execute one instruction"
// intrinsic, e.g. the PDP-1/PDP-10 XCT). The instruction puts the effective address of the word to
// execute in CPU_STATE.DispPc and traps with this vector and a return address (the instruction
// after the $exec). The host run loop runs EXACTLY ONE instruction at DispPc, then resumes with
// faithful execute semantics -- without the frontend re-implementing the ISA. Picked high
// (0xE5EC, "EXEC") so it never collides with a guest vector.
#define CPU_EXEC_ONE              UINT64_C (0xE5EC)
```

- [ ] **Step 6: Add `PcRegIndex` to `CPU_ARCH_INFO` in `ICpu.h`** (after `AddrOffBits`):
```cpp
    //
    // Index of the program-counter register within CPU_STATE.Reg[]. Used by the host run loop for
    // the $exec ("execute one instruction") intrinsic to pre-set the guest PC before running the
    // target instruction. Frontends that do not use $exec may leave it 0 (Reg[0]).
    //
    UINT32       PcRegIndex;
```

- [ ] **Step 7: Populate `PcRegIndex` in `UpclArch.cpp`** (in `GetInfo`, after `AddrOffBits`):
```cpp
        UINT32 PcIdx        = m_Layout.PcIndex ();     // for the $exec intrinsic (execute-one)
        pInfo->PcRegIndex   = (PcIdx != ~(UINT32) 0) ? PcIdx : 0;
```
(Verify `RegisterLayout::PcIndex ()` exists and returns the `%PC` register's slot, or `~0u` if none. If the accessor is named differently, use the existing one that yields the `%PC` slot index.)

- [ ] **Step 8: Build to verify it compiles** (`cmake --build LibCPU/build --target lcx`). Expected: clean build (no behavior yet). This task has no standalone runtime test — its surface is exercised by Task 2's `lcx upcl check`. Confirm `lcx` builds.

- [ ] **Step 9: Commit**
```bash
git add LibCPU/upcl/Token.h LibCPU/upcl/Lexer.cpp LibCPU/upcl/Ast.h LibCPU/upcl/Parser.cpp \
        LibCPU/include/LibCPU/CpuState.h LibCPU/include/LibCPU/ICpu.h LibCPU/upcl/UpclArch.cpp
git commit -m "upcl: \$ builtin sigil + CPU_EXEC_ONE + PcRegIndex scaffolding"
```

---

### Task 2: `$exec` lowering + PDP-1 XCT wiring + decode test

**Files:**
- Modify: `LibCPU/upcl/Semantics.cpp`, `upcl/examples/pdp1.upcl`, `LibCPU/CMakeLists.txt`

**Interfaces:**
- Consumes: `Builtin` flag, `CPU_EXEC_ONE`, the existing `ICpuSmcEmitter::SetDispatchTarget` / `ICpuSyscallEmitter::EmitSyscall` / `m_TrapReturnPc`.
- Produces: a `$exec(addr)` statement that emits `SetDispatchTarget(addr)` + `EmitSyscall(CPU_EXEC_ONE, returnPc)`; PDP-1 `xct` re-wired to `{ $exec(@ea(I,Y)); }`.

- [ ] **Step 1: Add the failing decode test to `LibCPU/CMakeLists.txt`** (the PDP-1 XCT, once wired to `$exec`, must lower a `CPU_EXEC_ONE` trap; `0o100100` = XCT Y=0o100). Add to the existing `upcl.decode-pdp1` test command, before its closing quote:
```
   \"$E\" upcl decode \"$P\" 0o100100 > '${CMAKE_BINARY_DIR}/pdp1.xct' && \
   grep -q '0x0000: xct' '${CMAKE_BINARY_DIR}/pdp1.xct' && \
   grep -q 'syscall\\|trap\\|0xe5ec\\|58860' '${CMAKE_BINARY_DIR}/pdp1.xct'
```
(`0xE5EC` = 58860 decimal; the lowered IR shows the syscall/trap with that vector. Adjust the grep token to whatever the RecordingEmitter prints for an `EmitSyscall` — confirm by running `lcx upcl decode` in Step 4 and pick the literal that appears, e.g. a `syscall`/`exec`/vector line; do NOT settle for matching merely `xct`.)

- [ ] **Step 2: Build + run `upcl.decode-pdp1` to verify FAIL** (PDP-1 xct currently lowers `pc = @ea` as a branch, no trap). Expected FAIL.

- [ ] **Step 3: Add the `$exec` lowering to `Semantics.cpp` `EmitMacroStmt`** (before the `FindMacro` user-macro lookup):
```cpp
    // $exec ( addr ) -- execute-one-instruction-and-return intrinsic (e.g. PDP-1/PDP-10 XCT). The
    // argument is the effective address of the word to execute. We stash it in the dispatch-target
    // scratch (CPU_STATE.DispPc -- survives the trap and is NOT a guest register, so it clobbers no
    // architectural state) and trap with the reserved CPU_EXEC_ONE vector and a return address (the
    // instruction after this one). The host run loop runs exactly one instruction at DispPc, then
    // resumes with faithful execute semantics -- no per-frontend ISA copy.
    if (pCall->Name == "exec") {
        ICpuSmcEmitter *pSmc = nullptr;
        if (!pCall->Args.empty ()
            && SUCCEEDED (m_pE->QueryInterface (IID_ICpuSmcEmitter, (VOID **) &pSmc)) && pSmc != nullptr) {
            Value Ea = EvalExpr (pCall->Args[0]);
            pSmc->SetDispatchTarget (Use (Ea));
            pSmc->Release ();
        }
        ICpuSyscallEmitter *pSys = nullptr;
        if (SUCCEEDED (m_pE->QueryInterface (IID_ICpuSyscallEmitter, (VOID **) &pSys)) && pSys != nullptr) {
            Value Ret = Const (m_WordBits, m_TrapReturnPc);
            pSys->EmitSyscall ((UINT32) CPU_EXEC_ONE, Use (Ret));
            pSys->Release ();
        }
        return true;
    }
    // A `$name(...)` statement that matched no builtin above is an undefined intrinsic -- never fall
    // through to the user-macro table (the `@` sigil is for those). Fail the emit.
    if (pCall->Builtin) { return false; }
```
And in `EvalMacroCall` (value position), before its `FindMacro`:
```cpp
    // A `$`-builtin in value position that matched none of the intrinsics above is undefined --
    // do not resolve it against the user-macro table. Yield a zero constant.
    if (pCall->Builtin) { return Const (m_WordBits, 0); }
```

- [ ] **Step 4: Re-wire PDP-1 `xct` in `upcl/examples/pdp1.upcl`** — replace the current branch-approximation body:
```
// XCT -- execute the one instruction at the effective address, then continue (DEC PDP-1; SIMH
// pdp1_cpu.c). Uses the $exec engine intrinsic so the engine re-decodes the target -- the ISA is
// defined once here, not duplicated.
jump insn xct : type trap, encode #i18 ( op:5 = OPC_XCT, i:1 -> I, y:12 -> Y ),
    disasm ( mnemonic : "xct", operands : I, Y )
    { $exec ( @ea ( I, Y ) ); }
```

- [ ] **Step 5: Build + run `upcl.decode-pdp1` to verify PASS** (xct decodes + lowers the `CPU_EXEC_ONE` trap). If the grep token guessed in Step 1 doesn't match the actual IR, fix the test's grep to the real `EmitSyscall` rendering (without weakening — it must confirm the exec/trap, not just `xct`).

- [ ] **Step 6: Commit**
```bash
git add LibCPU/upcl/Semantics.cpp upcl/examples/pdp1.upcl LibCPU/CMakeLists.txt
git commit -m "upcl: \$exec lowering + PDP-1 XCT via \$exec"
```

---

### Task 3: run-loop `CPU_EXEC_ONE` handler + PDP-1 XCT execution tests

**Files:**
- Modify: `LibCPU/test/lcx.cpp`, `LibCPU/CMakeLists.txt`
- Uses: `LibCPU/test/pdp1_asm.py` (exists)

**Interfaces:**
- Consumes: `CPU_EXEC_ONE`, `State.DispPc`, `State.TrapPc`, `ArchInfo.PcRegIndex`.
- Produces: the run loop executes exactly one instruction at `DispPc` and resumes per XCT semantics (fall-through→XCT+1, skip→XCT+2, jump→target, nested-trap recursion, 16-deep cap).

- [ ] **Step 1: Add the failing exec tests `upcl.pdp1-xct` to `LibCPU/CMakeLists.txt`.** Three programs via `pdp1_asm.py` raw images (run with `--raw18 --start 0 --dump`):
  - **fall-through:** `M[0o30]=LAC 0o31` (`0o200031`), `M[0o31]=0o000007`; program: `XCT 0o30` (`0o100030`) ; `DAC 0o32` (`0o240032`) ; `HLT` (`0o760400`). XCT executes the `LAC` → AC=7, then continues at XCT+1 (the DAC) → `M[0o32]=7`.
  - **skip:** set `AC` to a value then `XCT` a `SAS`-that-skips so the instruction AFTER the next is reached. Program: `LAW 5`(`0o700005`); `DAC 0o31`(`0o240031`); `LAW 5`(`0o700005`); `XCT 0o30`(`0o100030`) where `M[0o30]=SAS 0o31`(`0o520031`, skip if AC==M[0o31]); then a `LAW 1`(`0o700001`, the skipped word) ; `DAC 0o32`(`0o240032`) ; `HLT`. AC=5==M[0o31]=5 → SAS skips → the `LAW 1` is skipped → AC stays 5 → `M[0o32]=5` (not 1). Asserts the skip landed at XCT+2.
  - **nesting:** `M[0o30]=XCT 0o31`(`0o100031`), `M[0o31]=LAC 0o32`(`0o200032`), `M[0o32]=0o000011`; program `XCT 0o30`; `DAC 0o33`; `HLT` → AC=0o11, `M[0o33]=0o11`.
```cmake
add_test(NAME upcl.pdp1-xct COMMAND sh -c
  "X='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp1.upcl'; E='$<TARGET_FILE:lcx>'; A='${CMAKE_CURRENT_SOURCE_DIR}/test/pdp1_asm.py'; B='${CMAKE_BINARY_DIR}'; \
   python3 \"$A\" --org 0 0o100030 0o240032 0o760400 --at 0o30 0o200031 0o000007 > \"$B/xctft.raw\" && \
   \"$E\" run \"$B/xctft.raw\" --arch \"upcl:$X@pdp1\" --raw18 --start 0 --dump 0o32 | grep -q '0o32. = 0x00007' && \
   python3 \"$A\" --org 0 0o700005 0o240031 0o700005 0o100030 0o700001 0o240032 0o760400 --at 0o30 0o520031 > \"$B/xctsk.raw\" && \
   \"$E\" run \"$B/xctsk.raw\" --arch \"upcl:$X@pdp1\" --raw18 --start 0 --dump 0o32 | grep -q '0o32. = 0x00005' && \
   python3 \"$A\" --org 0 0o100030 0o240033 0o760400 --at 0o30 0o100031 0o200032 0o000011 > \"$B/xctns.raw\" && \
   \"$E\" run \"$B/xctns.raw\" --arch \"upcl:$X@pdp1\" --raw18 --start 0 --dump 0o33 | grep -q '0o33. = 0x00011'")
set_tests_properties(upcl.pdp1-xct PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched|XCT nesting")
```

- [ ] **Step 2: Build + run `upcl.pdp1-xct` to verify FAIL** (no `CPU_EXEC_ONE` handler yet → the exec trap stops the machine, dump wrong/absent). Expected FAIL.

- [ ] **Step 3: Add the `CPU_EXEC_ONE` handler to the `lcx.cpp` run loop**, inside the `if (State.SyscallVector != CPU_NO_SYSCALL)` block (before the PDP-1 IOT/HLT branches is fine; it is arch-neutral, gated only by the vector). Use a depth counter declared before the run loop (`int XctDepth = 0;`):
```cpp
                if (State.SyscallVector == CPU_EXEC_ONE) {
                    if (++XctDepth > 16) {                         // SIMH xct_max -- runaway guard
                        std::printf ("lcx: XCT nesting too deep (>16)\n");
                        break;
                    }
                    CPU_ADDR RetPc  = (CPU_ADDR) State.TrapPc;     // instruction after $exec (XCT+1)
                    CPU_ADDR ExecPc = (CPU_ADDR) State.DispPc;     // EA of the word to execute
                    State.Reg[ArchInfo.PcRegIndex] = ExecPc;       // PC-relative effects see the EA
                    ComPtr<ICpuCode> One;
                    if (FAILED (GenerateAotCfg (A.pArch, pBackend, ExecPc, ExecPc + 1, &One, nullptr))
                        || One == nullptr) {
                        break;
                    }
                    State.TrapPc        = CPU_SMC_NO_TRAP;
                    State.SyscallVector = CPU_NO_SYSCALL;
                    One->Execute (Ram, &State, nullptr);
                    CPU_ADDR Target = (CPU_ADDR) State.TrapPc;
                    if (State.SyscallVector != CPU_NO_SYSCALL) {
                        // the executed instruction itself trapped (nested XCT, IOT, HLT): leave the
                        // vector set so the loop's normal dispatch handles it; its return resumes at
                        // RetPc (the post-XCT instruction).
                        State.TrapPc = RetPc;
                        continue;                                  // re-enter the dispatch switch
                    }
                    XctDepth = 0;                                  // completed without nesting
                    if (Target == (CPU_ADDR) CPU_SMC_NO_TRAP || Target == ExecPc) {
                        Pc = RetPc;                                // fall-through: continue at XCT+1
                    } else if (Target > ExecPc && Target <= ExecPc + 2) {
                        Pc = RetPc + (Target - ExecPc);            // skip: same delta, relative to XCT
                    } else {
                        Pc = Target;                               // jump: honor the transfer target
                    }
                    continue;
                }
```
> **Implementer note:** the skip case (`Target == ExecPc+1`, occasionally `+2`) MUST resume at `RetPc + delta`, NOT `ExecPc + delta` — that is the corrected behavior the skip test (Step 1) enforces; the WIP prototype got this wrong. Verify all three sub-tests; if the interpreter signals fall-through differently than `Target ∈ {CPU_SMC_NO_TRAP, ExecPc}`, adjust the predicate against observed behavior (do not weaken the asserts). Declare `int XctDepth = 0;` immediately before the `for (int I = 0; I < 100000; I++)` interpreter loop. Confirm `CPU_SMC_NO_TRAP` is the sentinel the run loop already uses for "no trap".

- [ ] **Step 4: Build + run `upcl.pdp1-xct` to verify PASS** (all three: fall-through, skip, nesting). Also re-run `ctest -R "upcl.pdp1|upcl.pdp10"` to confirm no regression.

- [ ] **Step 5: Commit**
```bash
git add LibCPU/test/lcx.cpp LibCPU/CMakeLists.txt
git commit -m "upcl: run-loop CPU_EXEC_ONE handler + PDP-1 XCT execution tests"
```

---

### Task 4: PDP-10 XCT stub → `$exec` + execution test

**Files:**
- Modify: `upcl/examples/pdp10.upcl`, `LibCPU/CMakeLists.txt`
- Uses: `LibCPU/test/` PDP-10 `.SAV` tooling (the pdp10-run test pattern + `pdp10_asm`/`.SAV` fixtures already exist).

**Interfaces:**
- Consumes: the `$exec` primitive + run-loop handler (Tasks 2-3).
- Produces: PDP-10 `xct` executes its target via `$exec`.

- [ ] **Step 1: Add the failing test `upcl.pdp10-xct` to `LibCPU/CMakeLists.txt`.** Reuse the PDP-10 `.SAV` hand-assembly approach used by `upcl.pdp10-run` (see that test + its fixture/comment for how a `.SAV` is built). Build a small program where `XCT E` executes a `MOVEI AC1,k` at `M[E]` and then continues; assert `ac1=k` via the register dump and that execution continued (a following instruction ran). If a dedicated fixture is needed, create `LibCPU/test/pdp10-xct.sav` the same way `pdp10-compute.sav` was created (document the word layout in a CMake comment, as `upcl.pdp10-run` does). Assert the AC result and a continuation marker (e.g. a second AC set after the XCT). Mirror the `upcl.pdp10-run` test's `--arch upcl:...@kl10` + `DumpRegs` grep style; `FAIL_REGULAR_EXPRESSION "cannot|error|no JRST|XCT nesting"`.

- [ ] **Step 2: Build + run `upcl.pdp10-xct` to verify FAIL** (pdp10 XCT is still the stub — it does not execute the target).

- [ ] **Step 3: Replace the PDP-10 XCT stub in `upcl/examples/pdp10.upcl`** (the `jump insn xct` at the `OP_XCT = 0o256` site). Replace the stub body and update the comment:
```
// XCT -- EXECUTE (DEC PDP-10 HRM "XCT"; opcode 0o256, SIMH pdp10_cpu.c). Executes the instruction
// at the effective address as if it were the current instruction, then continues. The AC field is
// ignored (conventionally 0). Implemented via the $exec engine intrinsic (execute-one-and-return);
// the engine re-decodes the target, so the ISA is defined once.
jump insn xct : type trap, encode #i36 ( op:9 = OP_XCT, ac:4 -> acn, i:1 -> i, x:4 -> x, y:18 -> y ),
    disasm ( mnemonic : "xct", operands : acn, i, x, y )
    { $exec ( @ea ( i, x, y ) ); }
```
(Keep the `OP_XCT` const and the encode fields; only the body + comment change, and `type branch` → `type trap`.)

- [ ] **Step 4: Build + run `upcl.pdp10-xct` to verify PASS**, and re-run `ctest -R "upcl.pdp10"` (pdp10-load/run still green) and `ctest -R "upcl.pdp1"` (no regression).

- [ ] **Step 5: Commit**
```bash
git add upcl/examples/pdp10.upcl LibCPU/CMakeLists.txt
git commit -m "upcl: PDP-10 XCT via \$exec (completes the deferred stub)"
```

---

## Self-Review

**Spec coverage:** `$` sigil + `$exec` surface (T1) ✓; `$exec` lowering = `SetDispatchTarget` + `EmitSyscall(CPU_EXEC_ONE)` (T2) ✓; `CPU_EXEC_ONE`/`PcRegIndex` state (T1) ✓; run-loop one-instruction handler with fall-through/skip/jump/nesting-cap (T3) ✓; PDP-1 XCT (T2) ✓; PDP-10 XCT stub completion (T4) ✓; tests for PDP-1 (fall-through+skip+nesting) and PDP-10 + decode (T2-T4) ✓; interpreter-only + reject-unknown-`$builtin` (T1-T2) ✓. Out-of-scope (interrupt/RTC, other intrinsics, non-interp backends) correctly absent.

**Placeholder scan:** all code steps carry concrete code. Two steps carry explicit "confirm the literal against observed output" directives (the decode-test grep token in T2-S1; the fall-through predicate in T3-S3) — these are TDD verification points against the running tool, not placeholders; the asserts themselves are concrete and must not be weakened.

**Type consistency:** `TokBuiltinIdent`, `Expr.Builtin`, `CPU_EXEC_ONE`, `CPU_ARCH_INFO.PcRegIndex`, `State.DispPc`, `State.TrapPc`, `ArchInfo.PcRegIndex`, `m_TrapReturnPc`, `ICpuSmcEmitter::SetDispatchTarget`, `ICpuSyscallEmitter::EmitSyscall` used consistently across tasks and match the existing engine signatures verified in the tree. PDP-1 `OPC_XCT` / PDP-10 `OP_XCT` match each frontend's existing convention.
