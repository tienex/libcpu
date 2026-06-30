# DEC PDP-1 UPCL Frontend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn `upcl/examples/pdp1.upcl` from a no-`encode` stub into a complete decoding/disassembling/executing DEC PDP-1 frontend that runs Spacewar! 3.1 through `lcx`.

**Architecture:** The CPU is described entirely in `upcl/examples/pdp1.upcl` (18-bit, word-addressed, one's-complement); the `IOT` instruction decodes to a host trap. A host I/O personality `LibCPU/test/Pdp1Io.cpp` interprets those traps (Type 30 display, control input, typewriter, paper tape, HLT). Two loaders (`LoadRim`, raw word-image) feed programs into word-cell core. The work reuses the existing word-addressing, `@ea`-style indirect-chain macro, `cpu extends` features, `PromoteLoopLocals`, and `@trap`→`State.SyscallVector` dispatch already on `feat/com-core`.

**Tech Stack:** UPCL DSL (`lcx upcl check|decode`), C++20 host (`LibCPU/test/lcx.cpp`, `Pdp1Io.cpp`), CMake/ctest. SIMH `PDP1/` is the encoding+semantics oracle.

## Global Constraints

- **Oracle:** every octal opcode, mask, and arithmetic rule is from SIMH `PDP1/` (`pdp1_defs.h`, `pdp1_cpu.c`, `pdp1_sys.c`, `pdp1_dpy.c`, `pdp1_stddev.c`). Where SIMH and DEC docs disagree, SIMH governs. Decode/exec tests assert real octal.
- **Word model:** 18-bit word, sign = bit 0 (`SIGN=0o400000`), `DMASK=0o777777`, 12-bit address (`DAMASK=0o007777`), indirect bit 5 (`IA=0o010000`), extended addr 16-bit (`AMASK=0o177777`, `EPCMASK=0o170000`). Word-addressed: `byte_size==word_size==18`.
- **DEC bit numbering:** bit 0 = MSB … bit 17 = LSB. Opcode = top 5 bits; `op` value = full-18-bit prototype.
- **Style (CLAUDE.md):** 4-space indent; `if (x) { oneline }` always braced; no useless `{}`; `LL/ULL` constants use `INT64_C/UINT64_C`; `~UINT64_C(0)` for all-ones; fix causes not symptoms; do not simplify tests. Match the existing `.upcl` and NT/UEFI C++ style — do NOT run bare `clang-format` (repo has no `.clang-format`; it mangles the hand-maintained style).
- **Register-order contract:** the `register_file` declares AC, IO first; the host reads `pState->Reg[0]`=AC, `Reg[1]`=IO, etc. Any change to register order must update `Pdp1Io.h`'s index constants in the same commit.
- **Trap vectors:** `IOT_VECTOR = 0o72` (routed to `Pdp1IoTrap`, resume); `HLT_VECTOR = 0o77` (stop the machine). Distinct so the run loop can tell them apart.
- **cpu variants:** base `cpu "pdp1"` (MUS/DIS step) and `cpu "pdp1mul"` (hardware MUL/DIV). Spacewar runs on `@pdp1mul`.
- **Build:** `cmake -S LibCPU -B LibCPU/build` (configure once), `cmake --build LibCPU/build --target lcx`. **Test:** `ctest --test-dir LibCPU/build -R <name> --output-on-failure`. Run all commands from the worktree root.
- **Word-cell memory:** word index `i` occupies `pRam[i * CPU_WORD_CELL_BYTES]` little-endian (same as `LoadSav`). `State.TrapPc` is a word index.

---

### Task 1: arch header, register file, constants — parse/lower clean

**Files:**
- Modify: `upcl/examples/pdp1.upcl` (replace the whole stub)
- Modify: `LibCPU/CMakeLists.txt` (add `upcl.check-pdp1`)

**Interfaces:**
- Produces: register file order `ac`(0), `io`(1), `pc`(2)→%PC, `ov`(3), `pf`(4), `ss`(5), `tw`(6); constants `SIGN DMASK IA DAMASK EPCMASK AMASK` and all `OP_*` opcode prototypes; `decoder_operands` declaring the fields used by later encodes.

- [ ] **Step 1: Add the failing test to `LibCPU/CMakeLists.txt`** (near the other `upcl.check-*` tests):

```cmake
add_test(NAME upcl.check-pdp1 COMMAND sh -c
  "P='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp1.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl check \"$P\"")
set_tests_properties(upcl.check-pdp1 PROPERTIES FAIL_REGULAR_EXPRESSION "error:|no encoding")
```

- [ ] **Step 2: Configure + build, run the test to verify it FAILS** (stub has no instructions; `check` may warn or the later tasks' encodes are absent — confirm baseline):

```
cmake -S LibCPU -B LibCPU/build
cmake --build LibCPU/build --target lcx
ctest --test-dir LibCPU/build -R upcl.check-pdp1 --output-on-failure
```
Expected: at this point the stub still parses (it has no encodes); the test may PASS trivially. That is acceptable — this task's real deliverable is the new header/registers/constants below, which Step 4 must keep parsing clean. (The decode tests in later tasks are the substantive gates.)

- [ ] **Step 3: Replace `upcl/examples/pdp1.upcl` header with:**

```
// DEC PDP-1 (1959) -- 18-bit, WORD-ADDRESSED, ONE'S-COMPLEMENT. A single accumulator (AC) and
// in-out register (IO), a 12/16-bit PC, an Overflow flip-flop, six program flags and six sense
// switches, and an 18-bit test-word register. Instruction word: op[0:4]:5, indirect[5]:1,
// address Y[6:17]:12. Sign bit is bit 0 (DEC numbers bit 0 = MSB). Encoding + semantics oracle:
// SIMH PDP1/ (pdp1_defs.h, pdp1_cpu.c, pdp1_sys.c, pdp1_dpy.c). WORD-ADDRESSING: byte_size ==
// word_size == 18 (!= 8) marks this word-addressed; the decoder fetches one 18-bit word per PC
// step and %M[a] indexes an 18-bit word cell.
arch "pdp1" {
    name "DEC PDP-1";
    byte_size 18;
    word_size 18;
    psr_size 18;
    address_size 16;            // Type-15 memory-extension address space; one field = 4096 words

    register_file {
        group R {
            [ #i18 ac ],        // Reg[0] accumulator
            [ #i18 io ]         // Reg[1] in-out register
        }
        group S {
            [ #i16 pc -> %PC ], // Reg[2] program counter (word index)
            [ #i1  ov ],        // Reg[3] overflow flip-flop
            [ #i6  pf ],        // Reg[4] program flags 1..6
            [ #i6  ss ]         // Reg[5] sense switches 1..6
        }
        group USER {
            [ #i18 tw ]         // Reg[6] test-word toggle switches
        }
    }
}

// --- machine constants (octal, from SIMH pdp1_defs.h) ----------------------
const SIGN    = 0o400000;       // bit 0 = sign / most-negative
const DMASK   = 0o777777;       // 18-bit word mask
const IA      = 0o010000;       // indirect bit (bit 5)
const DAMASK  = 0o007777;       // 12-bit direct address field
const EPCMASK = 0o170000;       // PC field bits (high address)
const AMASK   = 0o177777;       // 16-bit extended address

// Opcode prototypes -- the FULL 18-bit instruction value (5-bit op in bits 0..4).
const OP_AND = 0o020000;  const OP_IOR = 0o040000;  const OP_XOR = 0o060000;
const OP_XCT = 0o100000;  const OP_CALJDA = 0o160000;
const OP_LAC = 0o200000;  const OP_LIO = 0o220000;  const OP_DAC = 0o240000;
const OP_DAP = 0o260000;  const OP_DIP = 0o300000;  const OP_DIO = 0o320000;
const OP_DZM = 0o340000;  const OP_ADD = 0o400000;  const OP_SUB = 0o420000;
const OP_IDX = 0o440000;  const OP_ISP = 0o460000;  const OP_SAD = 0o500000;
const OP_SAS = 0o520000;  const OP_MUL = 0o540000;  const OP_DIV = 0o560000;
const OP_JMP = 0o600000;  const OP_JSP = 0o620000;  const OP_SKP = 0o640000;
const OP_SFT = 0o660000;  const OP_LAW = 0o700000;  const OP_IOT = 0o720000;
const OP_OPR = 0o760000;

// host trap vectors (run loop routes IOT_VECTOR -> Pdp1Io, HLT_VECTOR -> stop)
const IOT_VECTOR = 0o72;
const HLT_VECTOR = 0o77;

decoder_operands [ #i18 Y, #i13 F, #i1 I ];
```

Delete the stub's old `insn`/`jump insn` bodies entirely (they are rewritten in later tasks). Leave the file with only the header + constants + `decoder_operands` for now.

- [ ] **Step 4: Build + run test to verify it PASSES:**
```
cmake --build LibCPU/build --target lcx
ctest --test-dir LibCPU/build -R upcl.check-pdp1 --output-on-failure
```
Expected: PASS (parses + lowers clean, no `error:`).

- [ ] **Step 5: Commit**
```
git add upcl/examples/pdp1.upcl LibCPU/CMakeLists.txt
git commit -m "upcl: PDP-1 arch header, register file, constants"
```

---

### Task 2: `@ea` indirect-chain macro + LAC/DAC + decode test

**Files:**
- Modify: `upcl/examples/pdp1.upcl` (append macro + first two insns)
- Modify: `LibCPU/CMakeLists.txt` (add `upcl.decode-pdp1`)

**Interfaces:**
- Consumes: constants from Task 1.
- Produces: `macro ea ( i, y )` returning the effective address (chained indirect in normal mode); the `lac`/`dac` memory-reference pattern `encode #i18 ( op:5 = OP_x, i:1 -> I, y:12 -> Y )`.

- [ ] **Step 1: Add the failing decode test to `LibCPU/CMakeLists.txt`:**
```cmake
add_test(NAME upcl.decode-pdp1 COMMAND sh -c
  "P='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp1.upcl'; E='$<TARGET_FILE:lcx>'; \
   \"$E\" upcl decode \"$P\" 0o200100 > '${CMAKE_BINARY_DIR}/pdp1.lac' && \
   grep -q '0x0000: lac' '${CMAKE_BINARY_DIR}/pdp1.lac' && \
   \"$E\" upcl decode \"$P\" 0o240100 > '${CMAKE_BINARY_DIR}/pdp1.dac' && \
   grep -q '0x0000: dac' '${CMAKE_BINARY_DIR}/pdp1.dac' && \
   \"$E\" upcl decode \"$P\" 0o210100 > '${CMAKE_BINARY_DIR}/pdp1.laci' && \
   grep -q 'while' '${CMAKE_BINARY_DIR}/pdp1.laci'")
set_tests_properties(upcl.decode-pdp1 PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched")
```
(`0o200100` = LAC Y=0o100; `0o240100` = DAC Y=0o100; `0o210100` = LAC with indirect bit set → the body's indirect `while` loop appears in the lowered IR.)

- [ ] **Step 2: Build + run to verify FAIL** (`ctest ... -R upcl.decode-pdp1`): Expected FAIL — "no encoding matched" (no `lac`/`dac` yet).

- [ ] **Step 3: Append to `upcl/examples/pdp1.upcl`:**
```
// EFFECTIVE ADDRESS -- direct EA = (PC field) | Y; if the indirect bit is set, follow the chain:
// each fetched pointer word's own indirect bit is honored (normal-mode multi-level deferral,
// SIMH pdp1_cpu.c Ea()). The chain is runtime data-dependent, so it lives in a body macro.
macro ea ( i, y ) {
    result = ( %PC & EPCMASK ) | ( y & DAMASK );
    #i1 defer = i;
    while ( defer != 0 ) {
        #i18 w = %M[ result ];
        result = ( %PC & EPCMASK ) | ( w & DAMASK );
        defer  = ( w & IA ) ? 1 : 0;
    }
}

// LAC -- load AC from M[E].  DAC -- deposit AC to M[E].
insn lac : encode #i18 ( op:5 = OP_LAC, i:1 -> I, y:12 -> Y ),
    disasm ( mnemonic : "lac", operands : I, Y )  => ac = %M[ @ea ( I, Y ) ];
insn dac : encode #i18 ( op:5 = OP_DAC, i:1 -> I, y:12 -> Y ),
    disasm ( mnemonic : "dac", operands : I, Y )  => %M[ @ea ( I, Y ) ] = ac;
```

- [ ] **Step 4: Build + run to verify PASS.**

- [ ] **Step 5: Commit** (`git add upcl/examples/pdp1.upcl LibCPU/CMakeLists.txt` → `git commit -m "upcl: PDP-1 @ea indirect-chain macro + lac/dac"`)

---

### Task 3: one's-complement arithmetic (`@add1c`/`@sub1c`) + ADD/SUB/IDX/ISP + exec test

**Files:**
- Modify: `upcl/examples/pdp1.upcl`
- Modify: `LibCPU/CMakeLists.txt` (add `upcl.pdp1-arith`)
- Create: `LibCPU/test/fixtures/pdp1/arith.raw` is **not** hand-built here — instead the test assembles the program inline as octal words via a helper script (Step 1).

**Interfaces:**
- Consumes: `@ea`, constants.
- Produces: `macro add1c ( a, b )`, `macro sub1c ( a, b )` (set `ov`, return 18-bit result); `add`/`sub`/`idx`/`isp` insns. `isp` skips (`pc += 1`) when result ≥ 0.

- [ ] **Step 1: Add the failing exec test.** It writes a tiny raw word-image program (build it with `printf` into 8-byte little-endian word cells), runs to HLT, and dumps the result word. Program: `LAC 0o20; ADD 0o21; DAC 0o22; HLT` with `M[0o20]=0o000005`, `M[0o21]=0o777775` (−2 in one's-comp) → expect `M[0o22]=0o000002` (5 + (−2) = 3? compute: 5 + 777775. 777775 = −2. 5−2=3 → 0o000003). Use that.

```cmake
add_test(NAME upcl.pdp1-arith COMMAND sh -c
  "X='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp1.upcl'; E='$<TARGET_FILE:lcx>'; \
   B='${CMAKE_BINARY_DIR}/pdp1arith.raw'; \
   python3 '${CMAKE_CURRENT_SOURCE_DIR}/test/pdp1_asm.py' \
     --org 0 0o200020 0o400021 0o240022 0o760400 \
     --at 0o20 0o000005 0o777775 > \"$B\" && \
   \"$E\" run \"$B\" --arch \"upcl:$X@pdp1\" --raw18 --start 0 --dump 0o22 \
     | grep -q '0o22. = 0x00003'")
set_tests_properties(upcl.pdp1-arith PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched")
```
Create `LibCPU/test/pdp1_asm.py` — a tiny assembler emitting 8-byte little-endian word cells at given word offsets (`--org N w w w …` lays consecutive words from N; `--at N w w …` lays words at N). Full script:
```python
#!/usr/bin/env python3
import sys
CELL = 8
def main():
    words = {}
    args = sys.argv[1:]
    i = 0; org = 0
    while i < len(args):
        a = args[i]
        if a in ("--org", "--at"):
            org = int(args[i+1], 0); i += 2
            while i < len(args) and not args[i].startswith("--"):
                words[org] = int(args[i], 0); org += 1; i += 1
        else:
            i += 1
    hi = max(words) if words else 0
    buf = bytearray((hi + 1) * CELL)
    for idx, w in words.items():
        v = w & 0o777777
        for b in range(CELL):
            buf[idx*CELL + b] = (v >> (8*b)) & 0xff
    sys.stdout.buffer.write(buf)
main()
```
The `--raw18 --start N` flags are added to `lcx run` in Task 11; until then this test is RED (expected).

- [ ] **Step 2: Build + run to verify FAIL** (no `add`/`sub` yet, and `--raw18` unknown). Expected FAIL.

- [ ] **Step 3: Append to `upcl/examples/pdp1.upcl`:**
```
// ONE'S-COMPLEMENT ADD: end-around carry; overflow when both operands share a sign and the result
// sign differs (sticky -- only set, never cleared by ADD); then -0 (0o777777) is corrected to +0.
macro add1c ( a, b ) {
    #i32 s = ( a & DMASK ) + ( b & DMASK );
    #i18 r = s & DMASK;
    r = ( s > DMASK ) ? ( ( r + 1 ) & DMASK ) : r;
    ov = ( ( ( ~a ^ b ) & ( a ^ r ) ) & SIGN ) ? 1 : ov;
    result = ( r == DMASK ) ? 0 : r;
}
// ONE'S-COMPLEMENT SUB: complement AC, add with end-around carry, overflow test, recomplement.
// NO -0 cleanup (SIMH-faithful: (-0)-(+0) yields -0).
macro sub1c ( a, b ) {
    #i18 t = a ^ DMASK;
    #i32 s = ( t & DMASK ) + ( b & DMASK );
    #i18 r = s & DMASK;
    r = ( s > DMASK ) ? ( ( r + 1 ) & DMASK ) : r;
    ov = ( ( ( ~t ^ b ) & ( t ^ r ) ) & SIGN ) ? 1 : ov;
    result = r ^ DMASK;
}
// INDEX increment: +1 with end-around carry (carry test is >= DMASK, per the maintenance manual).
macro idx1c ( v ) {
    #i32 s = ( v & DMASK ) + 1;
    result = ( s >= DMASK ) ? ( ( s + 1 ) & DMASK ) : ( s & DMASK );
}

insn add : encode #i18 ( op:5 = OP_ADD, i:1 -> I, y:12 -> Y ),
    disasm ( mnemonic : "add", operands : I, Y )
    { ac = @add1c ( ac, %M[ @ea ( I, Y ) ] ); }
insn sub : encode #i18 ( op:5 = OP_SUB, i:1 -> I, y:12 -> Y ),
    disasm ( mnemonic : "sub", operands : I, Y )
    { ac = @sub1c ( ac, %M[ @ea ( I, Y ) ] ); }
// IDX: AC <- M[E]+1 ; M[E] <- AC.  (no overflow, no skip)
insn idx : encode #i18 ( op:5 = OP_IDX, i:1 -> I, y:12 -> Y ),
    disasm ( mnemonic : "idx", operands : I, Y )
    { #i18 e = @ea ( I, Y ); ac = @idx1c ( %M[ e ] ); %M[ e ] = ac; }
// ISP: AC <- M[E]+1 ; M[E] <- AC ; skip if AC >= 0 (sign clear).
jump insn isp : type branch, encode #i18 ( op:5 = OP_ISP, i:1 -> I, y:12 -> Y ),
    disasm ( mnemonic : "isp", operands : I, Y )
    {
        #i18 e = @ea ( I, Y );
        ac = @idx1c ( %M[ e ] );
        %M[ e ] = ac;
        pc = ( ( ac & SIGN ) == 0 ) ? ( pc + 1 ) : pc;
    }
```

- [ ] **Step 4: Run to verify PASS** (after Task 11 lands `--raw18`; if running tasks in order, this test stays RED until Task 11 — note this dependency in the ledger and re-run `upcl.pdp1-arith` after Task 11). The decode portion (that `add`/`sub`/`idx`/`isp` decode) can be verified now with `lcx upcl decode "$X" 0o400021` → `add`.

- [ ] **Step 5: Commit** (`git commit -m "upcl: PDP-1 one's-complement add/sub/idx/isp + arith exec test"`)

> **Cross-task note for the controller:** `upcl.pdp1-arith` depends on `--raw18`/`--start` (Task 11). When dispatching Task 3, tell the implementer to assert the **decode** of add/sub/idx/isp now (via `lcx upcl decode`) and leave the run-to-HLT assertion to be confirmed green after Task 11. Do not let the implementer weaken the exec test to make it pass early.

---

### Task 4: logical + load/store memory-reference family

**Files:** Modify `upcl/examples/pdp1.upcl`; extend `upcl.decode-pdp1` in `LibCPU/CMakeLists.txt`.

**Interfaces:** Consumes `@ea`. Produces `and ior xor lio dap dip dio dzm sad sas`.

- [ ] **Step 1: Extend `upcl.decode-pdp1`** with assertions (append `&&` clauses before the final quote):
```
   \"$E\" upcl decode \"$P\" 0o020100 | grep -q ': and' && \
   \"$E\" upcl decode \"$P\" 0o040100 | grep -q ': ior' && \
   \"$E\" upcl decode \"$P\" 0o060100 | grep -q ': xor' && \
   \"$E\" upcl decode \"$P\" 0o340100 | grep -q ': dzm' && \
   \"$E\" upcl decode \"$P\" 0o500100 | grep -q ': sad' && \
   \"$E\" upcl decode \"$P\" 0o520100 | grep -q ': sas'
```

- [ ] **Step 2: Build + run `upcl.decode-pdp1` to verify FAIL** (new mnemonics absent).

- [ ] **Step 3: Append to `pdp1.upcl`:**
```
// Logical (mem-ref): AC op= M[E].
insn and : encode #i18 ( op:5 = OP_AND, i:1 -> I, y:12 -> Y ),
    disasm ( mnemonic : "and", operands : I, Y )  => ac = ac & %M[ @ea ( I, Y ) ];
insn ior : encode #i18 ( op:5 = OP_IOR, i:1 -> I, y:12 -> Y ),
    disasm ( mnemonic : "ior", operands : I, Y )  => ac = ac | %M[ @ea ( I, Y ) ];
insn xor : encode #i18 ( op:5 = OP_XOR, i:1 -> I, y:12 -> Y ),
    disasm ( mnemonic : "xor", operands : I, Y )  => ac = ac ^ %M[ @ea ( I, Y ) ];
// Load/store.
insn lio : encode #i18 ( op:5 = OP_LIO, i:1 -> I, y:12 -> Y ),
    disasm ( mnemonic : "lio", operands : I, Y )  => io = %M[ @ea ( I, Y ) ];
insn dio : encode #i18 ( op:5 = OP_DIO, i:1 -> I, y:12 -> Y ),
    disasm ( mnemonic : "dio", operands : I, Y )  => %M[ @ea ( I, Y ) ] = io;
insn dzm : encode #i18 ( op:5 = OP_DZM, i:1 -> I, y:12 -> Y ),
    disasm ( mnemonic : "dzm", operands : I, Y )  => %M[ @ea ( I, Y ) ] = 0;
// DAP -- deposit address part (bits 6..17) only.  DIP -- deposit instruction part (bits 0..5) only.
insn dap : encode #i18 ( op:5 = OP_DAP, i:1 -> I, y:12 -> Y ),
    disasm ( mnemonic : "dap", operands : I, Y )
    { #i18 e = @ea ( I, Y ); %M[ e ] = ( %M[ e ] & ~DAMASK ) | ( ac & DAMASK ); }
insn dip : encode #i18 ( op:5 = OP_DIP, i:1 -> I, y:12 -> Y ),
    disasm ( mnemonic : "dip", operands : I, Y )
    { #i18 e = @ea ( I, Y ); %M[ e ] = ( %M[ e ] & DAMASK ) | ( ac & ~DAMASK ); }
// SAD -- skip if AC != M[E].  SAS -- skip if AC == M[E].
jump insn sad : type branch, encode #i18 ( op:5 = OP_SAD, i:1 -> I, y:12 -> Y ),
    disasm ( mnemonic : "sad", operands : I, Y )
    { pc = ( ac != %M[ @ea ( I, Y ) ] ) ? ( pc + 1 ) : pc; }
jump insn sas : type branch, encode #i18 ( op:5 = OP_SAS, i:1 -> I, y:12 -> Y ),
    disasm ( mnemonic : "sas", operands : I, Y )
    { pc = ( ac == %M[ @ea ( I, Y ) ] ) ? ( pc + 1 ) : pc; }
```

- [ ] **Step 4: Build + run `upcl.decode-pdp1` to verify PASS.**

- [ ] **Step 5: Commit** (`git commit -m "upcl: PDP-1 logical + load/store memory-reference family"`)

---

### Task 5: control transfer — JMP/JSP/CAL/JDA/XCT

**Files:** Modify `upcl/examples/pdp1.upcl`; extend `upcl.decode-pdp1`.

**Interfaces:** Consumes `@ea`. Produces `jmp jsp cal jda xct`. JSP saves `OV·extm·PC` word in AC; CAL/JDA share op 007 split by bit 5; XCT executes M[E] as an instruction via `@xct`.

- [ ] **Step 1: Extend `upcl.decode-pdp1`:**
```
   \"$E\" upcl decode \"$P\" 0o600100 | grep -q ': jmp' && \
   \"$E\" upcl decode \"$P\" 0o620100 | grep -q ': jsp' && \
   \"$E\" upcl decode \"$P\" 0o160100 | grep -q ': cal' && \
   \"$E\" upcl decode \"$P\" 0o170100 | grep -q ': jda' && \
   \"$E\" upcl decode \"$P\" 0o100100 | grep -q ': xct'
```
(CAL = op 007 with bit5=0; JDA = op 007 with bit5=1. `0o160100` has bit5=0 → cal; `0o170100` = `0o160000|0o010000|0o100` has bit5=1 → jda.)

- [ ] **Step 2: Build + run to verify FAIL.**

- [ ] **Step 3: Append to `pdp1.upcl`:**
```
// JMP -- PC <- E.
jump insn jmp : type branch, encode #i18 ( op:5 = OP_JMP, i:1 -> I, y:12 -> Y ),
    disasm ( mnemonic : "jmp", operands : I, Y )  => pc = @ea ( I, Y );
// JSP -- "jump and save program counter": AC <- (OV<<17 | extm<<16 | PC), PC <- E.
// (extm = 0 on the base machine; the saved word is the return linkage.)
jump insn jsp : type call, encode #i18 ( op:5 = OP_JSP, i:1 -> I, y:12 -> Y ),
    disasm ( mnemonic : "jsp", operands : I, Y )
    { #i18 e = @ea ( I, Y ); ac = ( ( ov & 1 ) << 17 ) | ( pc & AMASK ); pc = e; }
// CAL -- store AC at 0o100 of the current field, PC <- 0o101.  (op 007, bit5 = 0)
jump insn cal : type call, encode #i18 ( op:5 = OP_CALJDA, i:1 = 0, y:12 -> Y ),
    disasm ( mnemonic : "cal", operands : Y )
    {
        #i18 m = ( %PC & EPCMASK ) | 0o100;
        %M[ m ] = ( ( ov & 1 ) << 17 ) | ( pc & AMASK );
        pc = m + 1;
    }
// JDA -- "jump and deposit AC": store AC at E, PC <- E+1.  (op 007, bit5 = 1)
jump insn jda : type call, encode #i18 ( op:5 = OP_CALJDA, i:1 = 1, y:12 -> Y ),
    disasm ( mnemonic : "jda", operands : Y )
    {
        #i18 m = ( %PC & EPCMASK ) | ( Y & DAMASK );
        %M[ m ] = ( ( ov & 1 ) << 17 ) | ( pc & AMASK );
        pc = m + 1;
    }
// XCT -- execute M[E] as an instruction (host trap with the EA so the engine re-decodes there).
jump insn xct : type branch, encode #i18 ( op:5 = OP_XCT, i:1 -> I, y:12 -> Y ),
    disasm ( mnemonic : "xct", operands : I, Y )  => pc = @ea ( I, Y );
```
> **Note for implementer:** PDP-1 `XCT` executes the target word *without* permanently changing PC (it returns to the instruction after XCT). A faithful in-engine XCT requires re-dispatch of an arbitrary word, which UPCL models by jumping to the EA and relying on the target's own control flow; the canonical "execute one and return" form is approximated as a branch to E here. If the reviewer finds Spacewar relies on XCT's return-after-one semantics, escalate — Spacewar 3.1 does not use XCT in its main loop (confirm against the source). Keep the decode assertion regardless.

- [ ] **Step 4: Build + run to verify PASS.**

- [ ] **Step 5: Commit** (`git commit -m "upcl: PDP-1 control transfer (jmp/jsp/cal/jda/xct)"`)

---

### Task 6: OPR operate group

**Files:** Modify `upcl/examples/pdp1.upcl`; extend `upcl.decode-pdp1`.

**Interfaces:** Consumes constants. Produces a single `opr` insn (`op:5 = OP_OPR, f:13 -> F`) whose body applies the combinable micro-ops in SIMH evaluation order (CLI, CLA, LAT, LAP, CMA, flag set/clear, HLT). HLT does `@trap(HLT_VECTOR)`.

- [ ] **Step 1: Extend `upcl.decode-pdp1`:**
```
   \"$E\" upcl decode \"$P\" 0o760000 | grep -q ': opr' && \
   \"$E\" upcl decode \"$P\" 0o761200 | grep -q ': opr' && \
   \"$E\" upcl decode \"$P\" 0o760400 | grep -q 'trap'
```
(`0o761200` = CLA(0o200)+CMA(0o1000); `0o760400` = HLT → the lowered IR shows the trap.)

- [ ] **Step 2: Build + run to verify FAIL.**

- [ ] **Step 3: Append to `pdp1.upcl`:**
```
// OPERATE GROUP (op 037).  Combinable micro-ops in the low 13 bits, applied in SIMH order.
// Masks (octal, within F): CLI=04000 CLA=0200 LAT=02000 LAP=0100 CMA=01000 HLT=0400 ;
// program-flag set/clear: bit 010 selects set(1)/clear(0), low 3 bits = flag number 1..7.
jump insn opr : type trap, encode #i18 ( op:5 = OP_OPR, f:13 -> F ),
    disasm ( mnemonic : "opr", operands : F )
    {
        io = ( F & 0o04000 ) ? 0 : io;                              // CLI
        ac = ( F & 0o00200 ) ? 0 : ac;                              // CLA
        ac = ( F & 0o02000 ) ? ( ac | tw ) : ac;                    // LAT  (OR in test word)
        ac = ( F & 0o00100 ) ? ( ac | ( ( ( ov & 1 ) << 17 ) | ( pc & AMASK ) ) ) : ac;  // LAP
        ac = ( F & 0o01000 ) ? ( ac ^ DMASK ) : ac;                 // CMA
        // program flag set / clear: flag # = F & 7, set when F & 010, else clear.
        #i6 fbit = ( ( F & 7 ) != 0 ) ? ( 1 << ( ( F & 7 ) - 1 ) ) : 0;
        pf = ( ( F & 0o010 ) != 0 ) ? ( pf | fbit ) : ( pf & ~fbit );
        // HLT -- stop the machine via the host halt vector.
        if ( ( F & 0o00400 ) != 0 ) { @trap ( HLT_VECTOR ); }
    }
```
> **Note:** flag-number semantics: SIMH uses `fs_test[t]` for the 3-bit field. Implement flag *n* (1..6) → bit (n−1) of `pf`. If `F & 7 == 7` (all flags) SIMH treats it as "all"; replicate if the reviewer confirms the maintenance manual's "07 = all flags" convention, otherwise the single-bit map above is the documented behavior — adjudicate in review.

- [ ] **Step 4: Build + run to verify PASS.**

- [ ] **Step 5: Commit** (`git commit -m "upcl: PDP-1 OPR operate group"`)

---

### Task 7: SKP skip group

**Files:** Modify `upcl/examples/pdp1.upcl`; extend `upcl.decode-pdp1`.

**Interfaces:** Consumes constants. Produces a single `skp` insn; OR of SZA/SPA/SMA/SZO/SPI + sense-switch (bits 12-14) + program-flag (bits 15-17) tests; bit-5 `i` inverts the aggregate; SZO also clears OV.

- [ ] **Step 1: Extend `upcl.decode-pdp1`:**
```
   \"$E\" upcl decode \"$P\" 0o640100 | grep -q ': skp' && \
   \"$E\" upcl decode \"$P\" 0o650100 | grep -q ': skp'
```

- [ ] **Step 2: Build + run to verify FAIL.**

- [ ] **Step 3: Append to `pdp1.upcl`:**
```
// SKIP GROUP (op 032).  In F (low 13 bits): SZA=0100 SPA=0200 SMA=0400 SZO=01000 SPI=02000 ;
// sense-switch select = (F>>3)&07, program-flag select = F&07 ; invert bit 'i' = IA (0o010000).
jump insn skp : type branch, encode #i18 ( op:5 = OP_SKP, f:13 -> F ),
    disasm ( mnemonic : "skp", operands : F )
    {
        #i6 sv = ( F >> 3 ) & 7;                 // sense-switch number (1..7)
        #i6 tv = F & 7;                          // program-flag number (1..7)
        #i1 sk = 0;
        sk = ( ( F & 0o02000 ) && ( ( io & SIGN ) == 0 ) ) ? 1 : sk;          // SPI: IO >= 0
        sk = ( ( F & 0o01000 ) && ( ( ov & 1 ) == 0 ) )    ? 1 : sk;          // SZO: OV == 0
        sk = ( ( F & 0o00400 ) && ( ( ac & SIGN ) != 0 ) ) ? 1 : sk;          // SMA: AC < 0
        sk = ( ( F & 0o00200 ) && ( ( ac & SIGN ) == 0 ) ) ? 1 : sk;          // SPA: AC >= 0
        sk = ( ( F & 0o00100 ) && ( ( ac & DMASK ) == 0 ) ) ? 1 : sk;         // SZA: AC == 0
        sk = ( ( sv != 0 ) && ( ( ss & ( 1 << ( sv - 1 ) ) ) == 0 ) ) ? 1 : sk;  // SZSn
        sk = ( ( tv != 0 ) && ( ( pf & ( 1 << ( tv - 1 ) ) ) == 0 ) ) ? 1 : sk;  // SZFn
        sk = ( ( F & IA ) != 0 ) ? ( sk ^ 1 ) : sk;                           // 'i' inverts
        ov = ( ( F & 0o01000 ) != 0 ) ? 0 : ov;                               // SZO clears OV
        pc = ( sk != 0 ) ? ( pc + 1 ) : pc;
    }
```

- [ ] **Step 4: Build + run to verify PASS.**

- [ ] **Step 5: Commit** (`git commit -m "upcl: PDP-1 SKP skip group"`)

---

### Task 8: SFT shift/rotate group + shift exec test

**Files:** Modify `upcl/examples/pdp1.upcl`; extend `upcl.decode-pdp1`; add `upcl.pdp1-shift`.

**Interfaces:** Consumes constants. Produces a single `sft` insn; sub-op = `(F>>9)&017`; the shift count = number of 1-bits in `F & 0o777`, implemented by looping one shift/rotate step per set bit.

- [ ] **Step 1: Extend `upcl.decode-pdp1`** with `\"$E\" upcl decode \"$P\" 0o661000 | grep -q ': sft'` and add a new exec test (RED until Task 11's `--raw18`):
```cmake
add_test(NAME upcl.pdp1-shift COMMAND sh -c
  "X='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp1.upcl'; E='$<TARGET_FILE:lcx>'; \
   B='${CMAKE_BINARY_DIR}/pdp1shift.raw'; \
   python3 '${CMAKE_CURRENT_SOURCE_DIR}/test/pdp1_asm.py' \
     --org 0 0o200020 0o661007 0o240021 0o760400 --at 0o20 0o000001 > \"$B\" && \
   \"$E\" run \"$B\" --arch \"upcl:$X@pdp1\" --raw18 --start 0 --dump 0o21 \
     | grep -q '0o21. = 0x00008'")
set_tests_properties(upcl.pdp1-shift PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched")
```
(`0o661007` = RAL with 3 one-bits in the low 9 → rotate AC left 3; `M[0o20]=1` → `1<<3 = 0o10 = 8`.)

- [ ] **Step 2: Build + run `upcl.decode-pdp1` (FAIL: no `sft`).**

- [ ] **Step 3: Append to `pdp1.upcl`:**
```
// SHIFT / ROTATE GROUP (op 033).  sub-op = (F>>9)&017 ; count = #1-bits in (F & 0o777).
// Sub-ops: 001 RAL 002 RIL 003 RCL 005 SAL 006 SIL 007 SCL ; 011 RAR 012 RIR 013 RCR
//          015 SAR 016 SIR 017 SCR.  RxL/RxR rotate; SxL/SxR arithmetic (sign-preserving) shift.
// Combined (RCx/SCx) treat AC:IO as one 36-bit register.  The count = popcount is realized by one
// shift/rotate STEP per set bit in the low 9 (matching the hardware, which shifts once per 1-bit).
jump insn sft : type branch, encode #i18 ( op:5 = OP_SFT, f:13 -> F ),
    disasm ( mnemonic : "sft", operands : F )
    {
        #i4  sub  = ( F >> 9 ) & 0o17;
        #i9  bits = F & 0o777;
        while ( bits != 0 ) {
            #i1 step = bits & 1;
            if ( step != 0 ) {
                // AC rotates / shifts
                if ( sub == 0o01 ) { ac = ( ( ac << 1 ) | ( ( ac >> 17 ) & 1 ) ) & DMASK; }       // RAL
                if ( sub == 0o11 ) { ac = ( ( ac >> 1 ) | ( ( ac & 1 ) << 17 ) ) & DMASK; }        // RAR
                if ( sub == 0o05 ) { ac = ( ac & SIGN ) | ( ( ac << 1 ) & ( DMASK >> 1 ) ); }      // SAL
                if ( sub == 0o15 ) { ac = ( ac & SIGN ) | ( ( ac >> 1 ) & ( DMASK >> 1 ) ); }      // SAR
                // IO rotates / shifts
                if ( sub == 0o02 ) { io = ( ( io << 1 ) | ( ( io >> 17 ) & 1 ) ) & DMASK; }        // RIL
                if ( sub == 0o12 ) { io = ( ( io >> 1 ) | ( ( io & 1 ) << 17 ) ) & DMASK; }        // RIR
                if ( sub == 0o06 ) { io = ( io & SIGN ) | ( ( io << 1 ) & ( DMASK >> 1 ) ); }      // SIL
                if ( sub == 0o16 ) { io = ( io & SIGN ) | ( ( io >> 1 ) & ( DMASK >> 1 ) ); }      // SIR
                // combined AC:IO (36-bit)
                if ( sub == 0o03 ) {                                                               // RCL
                    #i1 hb = ( ac >> 17 ) & 1;
                    ac = ( ( ac << 1 ) | ( ( io >> 17 ) & 1 ) ) & DMASK;
                    io = ( ( io << 1 ) | hb ) & DMASK;
                }
                if ( sub == 0o13 ) {                                                               // RCR
                    #i1 lb = io & 1;
                    io = ( ( io >> 1 ) | ( ( ac & 1 ) << 17 ) ) & DMASK;
                    ac = ( ( ac >> 1 ) | ( lb << 17 ) ) & DMASK;
                }
                if ( sub == 0o07 ) {                                                               // SCL
                    ac = ( ( ac << 1 ) | ( ( io >> 17 ) & 1 ) ) & DMASK;
                    io = ( io & SIGN ) | ( ( io << 1 ) & ( DMASK >> 1 ) );
                }
                if ( sub == 0o17 ) {                                                               // SCR
                    #i1 lb = io & 1;
                    io = ( io & SIGN ) | ( ( ( io >> 1 ) | ( ( ac & 1 ) << 17 ) ) & ( DMASK >> 1 ) );
                    ac = ( ac & SIGN ) | ( ( ac >> 1 ) & ( DMASK >> 1 ) );
                }
            }
            bits = bits >> 1;
        }
    }
```
> **Note for implementer:** verify each sub-op's exact bit-routing against `pdp1_cpu.c` (the combined-register edge cases especially). The decode test only checks `sft` decodes; the exec test (`upcl.pdp1-shift`) checks RAL. Add the reviewer note to cross-check RCL/RCR/SCL/SCR carry routing against SIMH.

- [ ] **Step 4: Build + run `upcl.decode-pdp1` to verify PASS** (the `upcl.pdp1-shift` exec test stays RED until Task 11 — note in ledger).

- [ ] **Step 5: Commit** (`git commit -m "upcl: PDP-1 SFT shift/rotate group + shift exec test"`)

---

### Task 9: LAW + MUS/DIS + MUL/DIV + cpu variants

**Files:** Modify `upcl/examples/pdp1.upcl`; extend `upcl.decode-pdp1`; add a variant decode check.

**Interfaces:** Consumes constants. Produces `law` (immediate), feature `muldiv_step` (`mus`/`dis`), feature `muldiv_hw` (`mul`/`div`), `features { … }` block, and `cpu "pdp1"`/`cpu "pdp1mul"`.

- [ ] **Step 1: Extend `upcl.decode-pdp1`** (LAW) and add variant assertions:
```
   \"$E\" upcl decode \"$P\" 0o700100 | grep -q ': law' && \
   \"$E\" upcl decode \"$P@pdp1\"    0o540100 | grep -q ': mus' && \
   \"$E\" upcl decode \"$P@pdp1mul\" 0o540100 | grep -q ': mul'
```

- [ ] **Step 2: Build + run to verify FAIL.**

- [ ] **Step 3: Append to `pdp1.upcl`:**
```
// LAW -- load AC with the 12-bit Y as an immediate; bit5 'i' loads the complement.
insn law : encode #i18 ( op:5 = OP_LAW, i:1 -> I, y:12 -> Y ),
    disasm ( mnemonic : "law", operands : I, Y )
    { ac = ( Y & DAMASK ) ^ ( I ? DMASK : 0 ); }

// Multiply/divide -- gated. Base machine: multiply-step / divide-step (op 026/027). Variant:
// Type-10 hardware multiply/divide at the same opcodes. The two features are mutually exclusive
// (each cpu enables exactly one), so opcodes 026/027 have a single definition per cpu.
feature muldiv_step {
    // MUS -- one multiply step: if IO bit 17 (LSB) is set, AC <- AC + M[E] (one's-comp); then the
    // AC:IO pair shifts right one. (DEC 1962 maintenance manual; verify against the manual.)
    insn mus : encode #i18 ( op:5 = OP_MUL, i:1 -> I, y:12 -> Y ),
        disasm ( mnemonic : "mus", operands : I, Y )
        {
            ac = ( ( io & 1 ) != 0 ) ? @add1c ( ac, %M[ @ea ( I, Y ) ] ) : ac;
            #i1 lb = ac & 1;
            ac = ( ac >> 1 ) & DMASK;
            io = ( ( io >> 1 ) | ( lb << 17 ) ) & DMASK;
        }
    // DIS -- one divide step (non-restoring); verify exact step against the maintenance manual.
    insn dis : encode #i18 ( op:5 = OP_DIV, i:1 -> I, y:12 -> Y ),
        disasm ( mnemonic : "dis", operands : I, Y )
        {
            #i1 hb = ( ac >> 17 ) & 1;
            ac = ( ( ac << 1 ) | ( ( io >> 17 ) & 1 ) ) & DMASK;
            io = ( ( io << 1 ) | hb ) & DMASK;
            ac = ( ( io & 1 ) != 0 ) ? @sub1c ( ac, %M[ @ea ( I, Y ) ] ) : @add1c ( ac, %M[ @ea ( I, Y ) ] );
        }
}
feature muldiv_hw {
    // MUL -- hardware multiply: (AC:IO) <- AC * M[E], one's-complement (SIMH pdp1_cpu.c).
    insn mul : encode #i18 ( op:5 = OP_MUL, i:1 -> I, y:12 -> Y ),
        disasm ( mnemonic : "mul", operands : I, Y )
        {
            #i36 prod = @mul1c ( ac, %M[ @ea ( I, Y ) ] );
            ac = ( prod >> 17 ) & DMASK;
            io = prod & DMASK;
        }
    // DIV -- hardware divide: AC <- (AC:IO) / M[E], IO <- remainder; overflow on divide check.
    insn div : encode #i18 ( op:5 = OP_DIV, i:1 -> I, y:12 -> Y ),
        disasm ( mnemonic : "div", operands : I, Y )
        { @div1c ( %M[ @ea ( I, Y ) ] ); }
}

features { muldiv_step; muldiv_hw; }
cpu "pdp1"    { feature muldiv_step; }    // base PDP-1: multiply-step / divide-step
cpu "pdp1mul" { feature muldiv_hw;   }    // Type-10 hardware multiply/divide (Spacewar target)
```
And add the helper macros (near `@add1c`):
```
// One's-complement 35-bit hardware multiply (SIMH pdp1_cpu.c): magnitudes multiplied, sign applied,
// 36-bit product split AC:IO. Verify the sign/round behavior against SIMH.
macro mul1c ( a, b ) {
    #i1  sa = ( a & SIGN ) ? 1 : 0;
    #i1  sb = ( b & SIGN ) ? 1 : 0;
    #i36 ma = ( sa ? ( ( ~a ) & DMASK ) : ( a & DMASK ) );
    #i36 mb = ( sb ? ( ( ~b ) & DMASK ) : ( b & DMASK ) );
    #i36 p  = ( ma * mb ) & 0o7777777777;        // 35-bit product
    result  = ( sa ^ sb ) ? ( ( ~p ) & 0o7777777777 ) : p;
}
// One's-complement hardware divide -- AC:IO / divisor; quotient in AC, remainder in IO; OV on
// divide-check. Implement per SIMH pdp1_cpu.c div(); verify against the source.
macro div1c ( d ) {
    #i36 dividend = ( ( ac & DMASK ) << 17 ) | ( io & DMASK );
    #i36 dv = d & DMASK;
    ov = ( dv == 0 ) ? 1 : ov;
    ac = ( dv != 0 ) ? ( ( dividend / dv ) & DMASK ) : ac;
    io = ( dv != 0 ) ? ( ( dividend % dv ) & DMASK ) : io;
}
```
> **Note for implementer:** `mul1c`/`div1c`/`mus`/`dis` are sketches of the arithmetic; the AUTHORITATIVE behavior is SIMH `pdp1_cpu.c` `mul()`/`div()` (hardware) and the 1962 maintenance manual (step). Read those and correct sign handling, the 35- vs 36-bit boundary, and divide-check exactly. The Spacewar path uses `mul`/`div` — get those right; `mus`/`dis` are off-path (flag if unverifiable).

- [ ] **Step 4: Build + run to verify PASS** (both cpus' decode).

- [ ] **Step 5: Commit** (`git commit -m "upcl: PDP-1 law + mul/div features + cpu variants"`)

---

### Task 10: IOT decode → `@trap` + `Pdp1Io` skeleton (typewriter + HLT) wired into the run loop

**Files:**
- Modify: `upcl/examples/pdp1.upcl` (add `iot`)
- Create: `LibCPU/test/Pdp1Io.h`, `LibCPU/test/Pdp1Io.cpp`
- Modify: `LibCPU/test/lcx.cpp` (dispatch), `LibCPU/CMakeLists.txt` (add `Pdp1Io.cpp` to `lcx`)

**Interfaces:**
- Produces: `iot` insn → `@trap ( IOT_VECTOR )`. `bool Pdp1IoTrap (CPU_STATE *pState, UINT8 *pRam, UINT64 RamSize);` in `Pdp1Io.h` — returns `true` to resume, `false` to stop. Register index constants `PDP1_AC=0, PDP1_IO=1, PDP1_PC=2, PDP1_OV=3` and word-cell helpers.

- [ ] **Step 1: Add `iot` to `pdp1.upcl`:**
```
// IOT -- I/O transfer. The whole device/pulse field is left to the host: trap to Pdp1Io, which
// reads the IOT word at TrapPc-1, decodes dev = word & 0o77, and performs the I/O.
jump insn iot : type trap, encode #i18 ( op:5 = OP_IOT, f:13 -> F ),
    disasm ( mnemonic : "iot", operands : F )  => @trap ( IOT_VECTOR );
```
Add the decode assertion to `upcl.decode-pdp1`: `\"$E\" upcl decode \"$P\" 0o720007 | grep -q ': iot'`.

- [ ] **Step 2: Create `LibCPU/test/Pdp1Io.h`:**
```cpp
#ifndef LIBCPU_TEST_PDP1IO_H
#define LIBCPU_TEST_PDP1IO_H

#include <LibCPU/LibCPU.h>

//
// PDP-1 host I/O personality. The guest's IOT instruction traps here (State.SyscallVector ==
// 0o72). We re-read the IOT word at the trapping word index (TrapPc - 1), decode the device
// field (low 6 bits), and perform the I/O against the guest's AC / IO registers and the Type 30
// display point buffer. Returns TRUE to resume after the IOT, FALSE to stop the machine.
//
// REGISTER-ORDER CONTRACT: these indices MUST match the register_file order in pdp1.upcl.
//
enum {
    PDP1_AC = 0,
    PDP1_IO = 1,
    PDP1_PC = 2,
    PDP1_OV = 3
};

bool Pdp1IoTrap (CPU_STATE *pState, UINT8 *pRam, UINT64 RamSize);

#endif // LIBCPU_TEST_PDP1IO_H
```

- [ ] **Step 3: Create `LibCPU/test/Pdp1Io.cpp`** with the skeleton (typewriter out + HLT-class IOTs + a default no-op for unknown devices; display/control added in Task 12). Use the word-cell read/write helpers consistent with `LoadSav`:
```cpp
#include "Pdp1Io.h"
#include <cstdio>

// Read / write an 18-bit word cell (word index -> pRam[idx * CPU_WORD_CELL_BYTES], little-endian).
static UINT32
Pdp1ReadWord (UINT8 CONST *pRam, UINT64 RamSize, UINT32 WordIdx)
{
    UINT64 Off = (UINT64) WordIdx * CPU_WORD_CELL_BYTES;
    if (Off + CPU_WORD_CELL_BYTES > RamSize) { return 0; }
    UINT64 V = 0;
    for (UINT32 B = 0; B < CPU_WORD_CELL_BYTES; B++) { V |= (UINT64) pRam[Off + B] << (8 * B); }
    return (UINT32) (V & 0777777);
}

//
// FIO-DEC -> ASCII is not modelled here beyond raw octal; the typewriter prints the low 8 bits of
// IO for now (full FIO-DEC translation is a paper-tape/console concern, out of the Spacewar path).
//
bool
Pdp1IoTrap (CPU_STATE *pState, UINT8 *pRam, UINT64 RamSize)
{
    // The trap resumes at TrapPc; the IOT word is the instruction just executed (word index - 1).
    UINT32 WordIdx = (UINT32) (pState->TrapPc - 1);
    UINT32 Insn    = Pdp1ReadWord (pRam, RamSize, WordIdx);
    UINT32 Dev     = Insn & 077;

    switch (Dev) {
    case 003:                                          // TYO -- typewriter out (low 8 bits of IO)
        std::putchar ((int) (pState->Reg[PDP1_IO] & 0377));
        return true;
    default:                                           // unknown / unimplemented device: no-op
        return true;
    }
}
```

- [ ] **Step 4: Wire dispatch into `LibCPU/test/lcx.cpp`.** Add `#include "Pdp1Io.h"` near the other includes. In the run loop's `if (State.SyscallVector != CPU_NO_SYSCALL)` block, before the final `break;`, add a PDP-1 branch (gate on the arch id being `pdp1`/`pdp1mul` — derive `bool IsPdp1` from the `--arch` string containing `pdp1`):
```cpp
                if (IsPdp1 && State.SyscallVector == 0072) {        // PDP-1 IOT -> host I/O
                    if (!Pdp1IoTrap (&State, Ram, sizeof (Ram))) { break; }
                    Pc = (CPU_ADDR) State.TrapPc;
                    continue;
                }
                if (IsPdp1 && State.SyscallVector == 0077) { break; }   // PDP-1 HLT -> stop
```
Compute `IsPdp1` once where `Abi`/arch is known: `bool IsPdp1 = std::strstr (pArchName, "pdp1") != nullptr;` (pArchName is the `--arch` value, e.g. `upcl:.../pdp1.upcl@pdp1mul`).

- [ ] **Step 5: Add `Pdp1Io.cpp` to the `lcx` target in `LibCPU/CMakeLists.txt`** (find the `add_executable(lcx ...)` / target sources list and append `test/Pdp1Io.cpp`).

- [ ] **Step 6: Build + run `upcl.decode-pdp1` (iot decodes) to verify PASS;** confirm `lcx` links with `Pdp1Io.cpp`.

- [ ] **Step 7: Commit** (`git commit -m "upcl: PDP-1 IOT trap + Pdp1Io host personality skeleton"`)

---

### Task 11: loaders — `LoadRim` + raw word-image + `--raw18`/`--start`/`.rim` wiring

**Files:** Modify `LibCPU/test/lcx.cpp` (loaders + flags); add `upcl.pdp1-rim`; create `LibCPU/test/pdp1_asm.py` if not already created in Task 3.

**Interfaces:**
- Produces: `struct RimInfo { UINT32 Start; bool Ok; };` and `RimInfo LoadRim (UINT8 CONST *pFileBytes, UINT64 FileLen, UINT8 *pRam, UINT64 RamSize);`; a raw-image loader path triggered by `--raw18` (load flat 8-byte word cells as-is from file) with `--start N` setting the entry word index. After this task, `upcl.pdp1-arith` and `upcl.pdp1-shift` go GREEN.

- [ ] **Step 1: Write the failing `upcl.pdp1-rim` test.** Build a tiny RIM tape with the assembler's RIM mode (extend `pdp1_asm.py` with `--rim`), load it, and run to HLT, dumping the deposited word. Program on tape: deposit `0o000007` at `0o30`, then `JMP 0o31` to a `HLT` we also deposit. Simplest assertion: load the tape and dump `0o30`.
```cmake
add_test(NAME upcl.pdp1-rim COMMAND sh -c
  "X='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp1.upcl'; E='$<TARGET_FILE:lcx>'; \
   B='${CMAKE_BINARY_DIR}/pdp1.rim'; \
   python3 '${CMAKE_CURRENT_SOURCE_DIR}/test/pdp1_asm.py' --rim \
     --dio 0o30 0o000007 --dio 0o31 0o760400 --jmp 0o31 > \"$B\" && \
   \"$E\" run \"$B\" --arch \"upcl:$X@pdp1\" --rim --dump 0o30 | grep -q '0o30. = 0x00007'")
set_tests_properties(upcl.pdp1-rim PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched")
```

- [ ] **Step 2: Extend `pdp1_asm.py` with RIM tape emission** (`--rim`, `--dio A W` pairs, `--jmp A`): each word emits three 6-bit frames MSB-first with channel-8 (`0200`) set; a `--dio A W` emits the DIO control word `(0320000|A)` then the data word `W`; `--jmp A` emits `(0600000|A)`:
```python
# append to pdp1_asm.py
def emit_rim():
    args = sys.argv[2:]  # skip --rim
    out = bytearray()
    def frame_word(w):
        w &= 0o777777
        for sh in (12, 6, 0):
            out.append(0o200 | ((w >> sh) & 0o77))
    i = 0
    while i < len(args):
        if args[i] == "--dio":
            a = int(args[i+1], 0); w = int(args[i+2], 0)
            frame_word(0o320000 | (a & 0o7777)); frame_word(w); i += 3
        elif args[i] == "--jmp":
            a = int(args[i+1], 0); frame_word(0o600000 | (a & 0o7777)); i += 2
        else:
            i += 1
    sys.stdout.buffer.write(out)
if len(sys.argv) > 1 and sys.argv[1] == "--rim":
    emit_rim()
else:
    main()
```
(Adjust the `main()`/`emit_rim()` dispatch so `--rim` as the FIRST arg selects tape mode; otherwise word-cell mode.)

- [ ] **Step 3: Build + run `upcl.pdp1-rim` to verify FAIL** (`--rim` flag + `LoadRim` not yet in `lcx`).

- [ ] **Step 4: Implement `LoadRim` in `lcx.cpp`** (model on `LoadSav` at line ~741). Parse 6-bit frames (channel-8 = `0200` marks data; skip others), assemble 3 frames → 18-bit word MSB-first; loop reading control word + datum: `DIO`(0320000)/`DAC`(0240000) → `M[Y]=next`, `JMP`(0600000) → `Start=Y`, stop. Deposit words into RAM word cells. Add the raw-image loader: `--raw18` copies the file's 8-byte cells verbatim into `Ram` and uses `--start N` for the entry. Wire both into `CmdRun`: when `--rim` is given, call `LoadRim` and set `Entry = Start`; when `--raw18`, copy + `Entry = (--start)`.
```cpp
struct RimInfo { UINT32 Start; bool Ok; };

static RimInfo
LoadRim (UINT8 CONST *pFileBytes, UINT64 FileLen, UINT8 *pRam, UINT64 RamSize)
{
    RimInfo R = { 0, false };
    UINT64 P = 0;
    auto GetWord = [&] (UINT32 *pW) -> bool {
        UINT32 W = 0; int Got = 0;
        while (P < FileLen && Got < 3) {
            UINT8 Frame = pFileBytes[P++];
            if (Frame & 0200) { W = (W << 6) | (UINT32) (Frame & 077); Got++; }
        }
        if (Got < 3) { return false; }
        *pW = W & 0777777;
        return true;
    };
    auto Store = [&] (UINT32 Idx, UINT32 V) {
        UINT64 Off = (UINT64) Idx * CPU_WORD_CELL_BYTES;
        if (Off + CPU_WORD_CELL_BYTES > RamSize) { return; }
        for (UINT32 B = 0; B < CPU_WORD_CELL_BYTES; B++) { pRam[Off + B] = (UINT8) (V >> (8 * B)); }
    };
    for (;;) {
        UINT32 Ctl;
        if (!GetWord (&Ctl)) { break; }
        UINT32 Op = Ctl & 0760000;
        if (Op == 0320000 || Op == 0240000) {          // DIO / DAC : address + datum
            UINT32 Datum;
            if (!GetWord (&Datum)) { break; }
            Store (Ctl & 07777, Datum);
        } else if (Op == 0600000) {                     // JMP : start address, end of tape
            R.Start = Ctl & 07777; R.Ok = true; break;
        } else {
            break;                                      // malformed
        }
    }
    return R;
}
```

- [ ] **Step 5: Build + run to verify PASS** for `upcl.pdp1-rim`, **and re-run `upcl.pdp1-arith` and `upcl.pdp1-shift`** (now GREEN). Expected: all three PASS.

- [ ] **Step 6: Commit** (`git commit -m "upcl: PDP-1 RIM + raw word-image loaders; arith/shift exec green"`)

---

### Task 12: Type 30 display + control input in `Pdp1Io` + headless display smoke test

**Files:** Modify `LibCPU/test/Pdp1Io.{h,cpp}`, `LibCPU/test/lcx.cpp` (`--console`/`--keys` plumbing for pdp1); add `upcl.pdp1-display`.

**Interfaces:**
- Produces: in `Pdp1Io`: a point buffer (`std::vector<…>` owned by a `Pdp1Display` singleton/struct), `dpy` (dev 007) plotting `x=(AC>>8)&01777`, `y=(IO>>8)&01777`; control IOT (dev 011) loading the control word into IO from a key-state byte; an accessor `unsigned Pdp1DisplayPointCount ();` for the test. A debug env var `PDP1_DPY_DUMP=1` prints `DPY x y` lines.

- [ ] **Step 1: Write `upcl.pdp1-display`.** A raw program plots one point: `LAC 0o20` (AC=X), `LIO 0o21` (IO=Y), `IOT 0o7` (dpy), `HLT`; with `M[0o20]`, `M[0o21]` set. Run with `PDP1_DPY_DUMP=1` and assert a `DPY` line appears.
```cmake
add_test(NAME upcl.pdp1-display COMMAND sh -c
  "X='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp1.upcl'; E='$<TARGET_FILE:lcx>'; \
   B='${CMAKE_BINARY_DIR}/pdp1dpy.raw'; \
   python3 '${CMAKE_CURRENT_SOURCE_DIR}/test/pdp1_asm.py' \
     --org 0 0o200020 0o220021 0o720007 0o760400 --at 0o20 0o123400 0o056700 > \"$B\" && \
   PDP1_DPY_DUMP=1 \"$E\" run \"$B\" --arch \"upcl:$X@pdp1\" --raw18 --start 0 | grep -q 'DPY '")
set_tests_properties(upcl.pdp1-display PROPERTIES FAIL_REGULAR_EXPRESSION "no encoding matched")
```

- [ ] **Step 2: Build + run to verify FAIL** (dev 007 is a no-op).

- [ ] **Step 3: Implement the display + control in `Pdp1Io.cpp`.** Add a point buffer and the two device cases:
```cpp
#include <vector>
#include <cstdlib>

struct Pdp1Point { int X; int Y; int Intensity; };
static std::vector<Pdp1Point> g_Points;

unsigned Pdp1DisplayPointCount () { return (unsigned) g_Points.size (); }

// One's-complement 10-bit signed coordinate -> -511..+511.
static int
Pdp1Coord (UINT32 v)
{
    int c = (int) (v & 01777);
    if (c & 01000) { c = -(int) ((~c) & 0777); }    // negative: one's-comp magnitude
    return c;
}
```
In `Pdp1IoTrap`'s switch:
```cpp
    case 007: {                                        // DPY -- Type 30 point plot
        int X = Pdp1Coord ((UINT32) (pState->Reg[PDP1_AC] >> 8));
        int Y = Pdp1Coord ((UINT32) (pState->Reg[PDP1_IO] >> 8));
        int In = (int) ((Insn >> 6) & 077);
        g_Points.push_back (Pdp1Point { X, Y, In });
        if (std::getenv ("PDP1_DPY_DUMP") != nullptr) { std::printf ("DPY %d %d %d\n", X, Y, In); }
        return true;
    }
    case 011:                                          // spacewar controls -> IO
        pState->Reg[PDP1_IO] = Pdp1ControlWord ();
        return true;
```
Add `static UINT32 Pdp1ControlWord ();` returning the 18-bit control word from a key-state byte (P1 = bits 0..3 masks `0400000 0200000 0100000 0040000`; P2 = bits 14..17 masks `010 04 02 01`; rotate-left+right together = hyperspace). The key map is fed from `--keys` / live keyboard via `IKeyboardSink`; for the headless test it returns 0. Document the default key map in a comment.

- [ ] **Step 4: Build + run `upcl.pdp1-display` to verify PASS.**

- [ ] **Step 5: Commit** (`git commit -m "upcl: PDP-1 Type 30 display + control input"`)

---

### Task 13: Spacewar integration — fetch `spacewar.rim`, headless run, point-plot assertion

**Files:** Add `upcl.pdp1-spacewar`; create `LibCPU/test/fixtures/pdp1/fetch-spacewar.sh`; modify `LibCPU/test/lcx.cpp` if a cycle cap / point-count print is needed; wire interactive `--console`.

**Interfaces:** Consumes the full ISA (`@pdp1mul`), `LoadRim`, the display. Produces a `lcx run` that loads `spacewar.rim`, runs a bounded number of windows, and prints the display point count so the test can assert it is non-zero (the ships/star are drawn).

- [ ] **Step 1: Write the fetch script `LibCPU/test/fixtures/pdp1/fetch-spacewar.sh`:**
```sh
#!/bin/sh
# Fetch the SIMH Spacewar! RIM tape into the build tree. Skips if already present.
set -e
DEST="$1"
URL="https://raw.githubusercontent.com/simh/simh/master/PDP1/spacewar1/spacewar.rim"
if [ ! -f "$DEST" ]; then curl -fsSL "$URL" -o "$DEST"; fi
test -s "$DEST"
```

- [ ] **Step 2: Add a way to print the point count after a bounded run.** In `lcx.cpp`, when `IsPdp1` and a `--dpy-count` flag is given, after the run loop call `printf("DPY-COUNT %u\n", Pdp1DisplayPointCount());`. (Add `#include "Pdp1Io.h"`/declaration.) The run loop's existing 100000-window cap bounds the run.

- [ ] **Step 3: Write `upcl.pdp1-spacewar`:**
```cmake
add_test(NAME upcl.pdp1-spacewar COMMAND sh -c
  "X='${CMAKE_CURRENT_SOURCE_DIR}/../upcl/examples/pdp1.upcl'; E='$<TARGET_FILE:lcx>'; \
   S='${CMAKE_BINARY_DIR}/spacewar.rim'; \
   sh '${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures/pdp1/fetch-spacewar.sh' \"$S\" && \
   \"$E\" run \"$S\" --arch \"upcl:$X@pdp1mul\" --rim --dpy-count \
     | awk '/DPY-COUNT/ { if (\$2+0 > 0) ok=1 } END { exit ok?0:1 }'")
set_tests_properties(upcl.pdp1-spacewar PROPERTIES
  FAIL_REGULAR_EXPRESSION "no encoding matched|illegal|STOP_"
  TIMEOUT 120)
```
> If the CI/sandbox has no network, this test should be marked to skip gracefully when the fetch fails (the fetch script exits non-zero → the test fails). The controller decides whether to mark it `DISABLED`/`SKIP_RETURN_CODE` when offline — note this; do NOT delete the assertion to make it pass.

- [ ] **Step 4: Build + run `upcl.pdp1-spacewar` to verify** it loads, runs without an illegal-instruction stop, and plots points (DPY-COUNT > 0). If it stops on an unimplemented instruction or wrong arithmetic, debug against SIMH (this is the integration gate — fix the CPU, not the test). Expected eventually: PASS.

- [ ] **Step 5: Document interactive play** in the `lcx` usage text: `lcx run <spacewar.rim> --arch upcl:<…>/pdp1.upcl@pdp1mul --rim --console [--keys <map>]`. Verify it launches and renders points to the terminal (manual check; not a ctest).

- [ ] **Step 6: Run the full PDP-1 suite** `ctest --test-dir LibCPU/build -R upcl.pdp1 --output-on-failure` + `upcl.check-pdp1`/`upcl.decode-pdp1`. Expected: all green (modulo the offline-skip note for spacewar).

- [ ] **Step 7: Commit** (`git commit -m "upcl: PDP-1 Spacewar integration (RIM load, headless point-plot)"`)

---

## Self-Review

**Spec coverage:** arch/registers (T1) · `@ea` indirect chain (T2) · one's-complement arith (T3) · logical/load-store (T4) · control transfer (T5) · OPR (T6) · SKP (T7) · SFT (T8) · LAW + MUS/DIS + MUL/DIV + cpu variants (T9) · IOT + host handler (T10) · RIM + raw loaders (T11) · Type 30 display + control (T12) · Spacewar (T13). All six spec tests present: `check-pdp1`(T1), `decode-pdp1`(T2,+4..10), `pdp1-arith`(T3/green@T11), `pdp1-shift`(T8/green@T11), `pdp1-rim`(T11), `pdp1-spacewar`(T13); plus `pdp1-display`(T12). ✓

**Cross-task dependency:** `pdp1-arith` (T3) and `pdp1-shift` (T8) require `--raw18` (T11) — flagged in both tasks and the controller note; their decode portions verify in-task, exec portions go green at T11. This is the one ordering subtlety; the ledger must record T3/T8 exec assertions as "pending T11".

**Placeholder scan:** every code step has concrete UPCL/C++/Python. The `mul1c`/`div1c`/`mus`/`dis`/SFT-combined and XCT bodies carry explicit "verify against SIMH `pdp1_cpu.c`" implementer notes (these are correctness-refinement directives against the named oracle, not placeholders) — the Spacewar gate (T13) forces them correct. ✓

**Type consistency:** `@ea(I,Y)` signature used uniformly; `@add1c`/`@sub1c`/`@idx1c`/`@mul1c`/`div1c` defined before use; `Pdp1IoTrap` signature + `PDP1_AC/IO/PC/OV` indices match the T1 register order and the T10 handler; `LoadRim`/`RimInfo` consistent T11↔tests; `pdp1_asm.py` `--org`/`--at`/`--rim`/`--dio`/`--jmp` consistent across T3/T8/T11/T12. ✓
