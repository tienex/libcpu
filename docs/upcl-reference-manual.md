# UPCL — Programmer's Reference Manual

> A complete reference for the **Universal Processor Crafting Language**, derived
> from the actual implementation: `upcl/ast/lexer.l`, `upcl/ast/parser.y`,
> `upcl/ast/ast.h`, the `upcl/c/` semantic IR, and the ten `upcl/examples/*.def`
> files. Part I is the language reference (everything here is in the current
> grammar). Part II is a cookbook for specific architectures and CPU features;
> recipes there are tagged **[GRAMMAR]** (expressible today) or **[PROPOSED]**
> (a forward-compatible extension, consistent with the language but not yet in
> the grammar). Sibling design docs are cross-referenced for deep mechanisms.

---

# PART I — LANGUAGE REFERENCE

## 1. Lexical structure (`ast/lexer.l`)

### 1.1 Comments & whitespace
`// line comment` to end of line; whitespace is insignificant.

### 1.2 Numbers
| Form | Lexer rule | Example | Value |
|------|-----------|---------|-------|
| Decimal | `{DEC}+` | `42` | 42 |
| Hex | `0[xX]{HEX}+` | `0xffff0000` | … |
| Binary | `0[bB]{BIN}+` | `0b1001` | 9 |
| **Octal** | `0{OCT}*` | `0100` | **64** |

> ⚠️ A leading `0` means **octal** (`0100` = 64). This is deliberate for PDP-1/
> PDP-11 (`pc = 0100;` in `pdp1.def`). Plain `0` is octal-zero = 0.

### 1.3 Identifiers and sigils
| Token | Lexer | Meaning |
|-------|-------|---------|
| `identifier` | `[a-zA-Z_][a-zA-Z0-9_]*` | register/operand/macro/field name |
| **meta-identifier** `%X` | `"%"{ID}` | a *special register* role: `%PC`, `%NPC`, `%PSR`, `%N`, `%V`, `%Z`, `%C`, `%P` |
| **macro-identifier** `@X` | `"@"{ID}` | a macro/intrinsic call: `@ea`, `@trap`, `@call`, `@eval_cc`, `@debug`, user macros |
| **repeat-identifier** `X?` | `{ID}"?"` | a register-array element; `?` is the repetition placeholder |
| string | `"…"` | architecture name / assert text |

### 1.4 Types (`#…`)
| Syntax | Lexer | Meaning |
|--------|-------|---------|
| `#i<N>` | `#[if]{DEC}+` | integer of N bits (`#i1`,`#i8`,`#i32`,`#i64`,`#i128`,`#i18`,`#i48`…) |
| `#f<N>` | `#[if]{DEC}+` | float of N bits (`#f32`,`#f64`,`#f80`,`#f128`) |
| `#v<N>` | `#v{DEC}+` | vector, N bits total (element type generic) |
| `#v<E>:<N>` | `#v{DEC}+:{DEC}+` | vector, element width E, total N bits — **SIMD lanes** |

Bit widths are arbitrary (`#i18` for PDP-1, `#i48` for 80386 far pointer).

### 1.5 Operators & punctuation (token spellings)
Binding `->` `<-` `<->`; range `..`; repeat `**`; compares `==` `!=` `<=` `>=`
`<` `>`; logical `||` `&&`; shifts `<<` `>>`; **rotates `<<>` (rol) `>><` (ror)**;
complement-combinators `&~` `|~` `^~`; arithmetic `+ - * / %`; bitwise `& | ^ ~`;
augment sigils `%CC` `%OFTRAP` `%S` `%U` `%ORD` `%UNO`; memory `%M` / `%MEM`;
compound assignments `+= -= *= /= %= |= &= ^= <<= >>= <<>= >><= &~= |~= ^~=`.

---

## 2. Program structure (`parser.y: root`)

A `.def` is exactly:

```
root ::= arch_decl  [ decoder_operands_decl ]  { toplevel_decl }
```

1. **one** `arch "name" { … }` block (§3–§4),
2. an optional `decoder_operands [ … ];` declaration (§11),
3. any number of top-level `insn` / `jump insn` / `group insn` / `macro`
   declarations (§7–§10).

---

## 3. Architecture declaration (`arch_stmts`)

Inside `arch "ID" { … }`, terminated with `;` each:

| Statement | Example | Notes |
|-----------|---------|-------|
| `name "…";` | `name "MIPS R4000";` | full name |
| `endian little\|big\|both;` | `endian both;` | `both` = bi-endian |
| `default_endian little\|big;` | `default_endian big;` | when `both` |
| `byte_size <expr>;` | `byte_size 18;` | **bits per byte** (PDP-1 = 18) |
| `word_size <expr>;` | `word_size 32;` | natural word |
| `float_size <expr>;` | `float_size 80;` | native FP width |
| `address_size <expr>;` | `address_size 24;` | PC/address width |
| `psr_size <expr>;` | `psr_size 32;` | status register width |
| `min_page_size`/`max_page_size`/`default_page_size`/`page_size <expr>;` | | MMU page metadata |
| `register_file { … }` | | §4 |

(Endian token `none` for word machines is reserved; `pdp1.def` shows it commented.)

---

## 4. Register file (`register_file_decl`)

```
register_file { group … group … }
```

### 4.1 Groups
- **Complex:** `group NAME { decl , decl , … }`
- **Simple:** `group NAME : decl ;` (single register — e.g. `group T : [ #i5 trapno ];`)

### 4.2 Register declaration (`reg_full_decl`)
```
[ TYPE NAME ]                              // plain
[ TYPE NAME  -> binding ]                  // bound / split
[ TYPE NAME  <- expr ]                     // hardwired
[ COUNT ** TYPE NAME? … ]                  // repetition (array)
```

### 4.3 Repetition (`COUNT ** TYPE id?`)
`id?` is an array; `?` is the element placeholder. Start index variants
(`repeatible_identifier`): `id?` (from 0), `id?:N` (from N), `id?:(expr)`.
Inside binding expressions, **`_` is the current repetition index.**

```
[ 31 ** #i32 r?:1 ]     // r1..r31 (mips/m88k)
[ 8 ** #f80 st? ]       // st0..st7 (x87)
[ ( 32 * 16 ) ** #i32 w? ]   // 512 window slots (sparc)
```

### 4.4 Bindings
| Operator | Name | Meaning |
|----------|------|---------|
| `-> %META` | bind-special | this register *is* the PC/NPC/PSR/flag (`pc -> %PC`) |
| `-> REG` / `-> id?` | alias | this register aliases another (`ccr -> sr`) |
| `<- expr` | hardwired | read-only computed value (`r0 <- 0`, `sfip <- (snip+8)`) |
| `-> splitter` | split | partition/overlay into sub-fields (§4.5) |

Field-level bind directions inside a splitter: `field -> %META` (field drives a
special flag), `field <- src` (field reads from src), `field <-> src`
(bidirectional alias), `field <- (expr)` (hardwired field). `_` names an
anonymous (unnamed) field.

### 4.5 Splitters — sub-registers, flags, unions
Two forms (fields are listed **most-significant first**, as the examples show):

**(a) Bitfield partition** `TYPE ( f0 : f1 : … )` — adjacent fields:
```
[ #i16 bx -> #i8 ( bh : bl ) ]     // bx = bh(hi):bl(lo)        (8086)
[ #i32 psr -> %PSR #i32 explicit [ #i3 0, #i1 C->%C, #i27 0 ] ]   // m88k
[ #i8 P -> %PSR #i1 explicit ( N->%N:V->%V:0:B:D:I:Z->%Z:C->%C ) ] // 6502
```
`explicit` = the fields literally occupy successive bits of the parent;
`0` = a constant/reserved bit. A field `name->%FLAG` exports that bit as the
architectural N/V/Z/C/P used by `%CC` (§5.6).

**(b) Union / overlay** `TYPE [ view0, view1, … ]` or `[ … ]` — overlapping views
of the same bits (x87 status word):
```
[ #i16 sw -> explicit [ #i1 [ B, C3 ], #i3 TOP, #i1 [ C2,C1,C0,ES,SF,PE,UE,OE,ZE,DE,IE ] ] ]
```

**(c) Evaluated binding** `evaluate ( expr )` — the register's value is computed
by an expression/macro (segmented PC):
```
[ #i48 pc -> %PC explicit evaluate ( @ea ( seg, off ) )
       [ #i16 seg <- cs, #i32 off <-> eip ] ]    // 80386 CS:EIP
```

### 4.6 Indexed / windowed bindings (`-> id?:( expr )`)
A register maps to another bank at a **computed index** — the basis for register
windows (SPARC) and overlapping aliases:
```
// SPARC: the 8 "in"/"local"/"out" regs are a window into 512 physical slots.
[ 8 ** #i32 i? -> w?:( [ #i9 ( cwp * 16 ) - 8 ] ) ]
[ 8 ** #i32 l? -> w?:( [ #i9 ( cwp * 16 ) ] ) ]
[ 8 ** #i32 o? -> w?:( [ #i9 ( cwp * 16 ) + 8 ] ) ]
```
Here `_` (the repetition index 0..7) and another register (`cwp`) jointly index
the physical bank `w?`. The same mechanism builds the x86 `rax→eax→ax→al/ah`
alias chain (`80386.def`, `8086_1.def`).

---

## 5. Expressions

### 5.1 Operands & literals
`number`, `identifier`, `register.field` (qualified, e.g. `flags.C`, `sw.TOP`),
`%META`, parenthesised groups, memory refs, bit-combines, casts.

### 5.2 Operator set (from `binary_expr` / `unary_expr`)
| Class | Operators |
|-------|-----------|
| Unary | `-` (neg) `!` (logical not) `~` (complement) `+` (identity); augments `%S` `%U` (§5.5) |
| Multiplicative | `*` `/` `%` |
| Additive | `+` `-` |
| Shift | `<<` `>>` |
| **Rotate** | `<<>` (rol) `>><` (ror) |
| Bitwise | `&` `\|` `^` and complement-combinators `&~` `\|~` `^~` |
| Relational | `==` `!=` `<` `<=` `>` `>=` |
| Logical | `&&` `\|\|` |
| Type test | `expr is TYPE` (§5.7) |

Only `* / %` (tighter) and `+ -` have declared precedence; **parenthesise
everything else** — the examples do (`( src2 << 16 ) | 0xffff`).

### 5.3 Bit-slice — `operand[ … ]` (`range`)
| Form | Meaning |
|------|---------|
| `x[ HI .. LO ]` | bits HI down to LO inclusive (`value[4..7]`) |
| `x[ START : COUNT ]` | COUNT bits starting at START (`pf[N:1]`) |

### 5.4 Bit-combine (concatenation) — `( a : b : … )`
Concatenate operands, **most-significant first**: `( dx : ax )` is a 32-bit value
(dx high), `( dst : flags.C )` appends the carry as the low bit (used by `rcl`).

### 5.5 Sign augments — `%S(expr)` / `%U(expr)`
Interpret the wrapped expression as signed / unsigned, affecting `>>`, `/`, `%`,
comparisons, and width extension: `ext: dst = %S ( src1 >> (src2 & 0x1f) )`,
`cbw: ax = %S ( [ #i16 al ] )`.

### 5.6 Condition-code augment — `%CC(…)` ★ the CC mechanism
Wrapping a value in `%CC` marks it to **update the architectural flags** (those a
register field exported via `->%N/%V/%Z/%C/%P`):
```
%CC ( expr )                 // update ALL applicable flags from expr
%CC ( expr , flag )          // update only the named flag(s)
%CC ( expr , [ Z, N, P ] )   // update a list
%CC ( expr , !C )            // update all EXCEPT carry
```
Examples: `add: dst = %CC ( dst + src );`, `dec: dst = %CC ( dst - 1, !C );`,
`aad: al = %CC ( (ah*10)+al, [Z,N,P] );`, bare `cmp: %CC ( dst - src );` (flags
only, result discarded). This is how *every* arch's flag update is written (§Part II).

### 5.7 Other augments / forms
| Form | Meaning |
|------|---------|
| `%OFTRAP ( expr )` / `%OFTRAP ( expr, code )` | trap on signed overflow (m88k `add`) |
| `%ORD ( expr )` / `%UNO ( expr )` | floating-point ordered / unordered compare |
| `expr ? a : b` | select (ternary) |
| `[ TYPE expr ]` | **cast** (`[ #i16 al ]`, `[ #i32 #i16 %M[…] ]`) |
| `expr is TYPE` | type predicate (`address is #i16`) — overload dispatch |

### 5.8 Memory references — `%M[addr]` / `TYPE %M[addr]`
`%M[expr]` (alias `%MEM[…]`) is a memory access; prefix a type for width/signedness:
```
dst = #i32 %M[ src1 + src2 ];      // 32-bit load
#i8  %M[ src1 + src2 ] = dst;      // 8-bit store
dst = %S ( [ #i32 #i16 %M[a] ] );  // load 16, sign-extend to 32
```

### 5.9 Macro / intrinsic calls — `@name(args)`
User macros (§6) and built-ins: `@trap(code[,args])`, `@call(host_fn, args)`,
`@eval_cc(ccflags_operand)` (§10), `@debug()`. A macro that yields a value sets
`%result` (e.g. `@ea`, `@i8086_pop`).

---

## 6. Macros (`macro_decl`)

```
macro NAME ( params ) : single_statement ;        // expression/inline form
macro NAME ( params ) { statements }               // block form
```
`%result` is the return value; `@NAME(args)` invokes it. Overloading by arity and
by `is`-type is supported (8086 has two `i8086_jump` macros):
```
macro ea ( seg, off ) : #i24 %result = ( [ #i16 seg ] << 4 ) + [ #i16 off ];
macro i8086_push ( value ) { sp -= 2; %M[ @ea ( ss, sp ) ] = value; }
macro i8086_jump ( address ) {
    if ( address is #i16 ) pc.off = address;
    else                   pc.[ off, seg ] = address;
}
```

---

## 7. Instructions (`insn_decl`)

```
insn NAME : ;                       // empty (nop/hlt)
insn NAME : single_statement ;      // inline
insn NAME { statements }            // block
```
Statements (`insn_stmts`): assignments (`= += -= *= /= %= |= &= ^= <<= >>= <<>= >><= &~= |~= ^~=`),
`if/else`, `for`, `while`, blocks, bare augment-expressions (`%CC(…)`), macro calls.
```
insn add  : dst = %CC ( dst + src );
insn and  { dst = %CC ( dst & src ); flags.[ C, O ] = 0; }
insn movsw {
    if ( rep ) {
        for (; %S ( cx > 0 ); cx = cx - 1) {
            #i16 %M[ @i8086_str_dst() ] = #i16 %M[ @i8086_str_src() ];
            @i8086_str_inc(2);
        }
    } else { #i16 %M[ @i8086_str_dst() ] = #i16 %M[ @i8086_str_src() ]; @i8086_str_inc(2); }
}
```

---

## 8. Jump instructions (`jump_insn_decl`)

```
jump insn NAME : type TYPE  [ delay EXPR ]  [ pre STMTS ]  [ condition EXPR ]  { ACTION }
```
- **`type`** ∈ `branch | call | return | trap` → maps to libcpu `TAG_*`.
- **`delay EXPR`** — delay-slot count (§Part II, MIPS): `jmp_n : type call delay 1 { … }`.
- **`pre STMTS`** — run before the branch resolves (PDP-1 `isp : … pre Y += 1 …`).
- **`condition EXPR`** — conditional branch predicate.
- **`{ ACTION }`** — the control-flow effect.
```
jump insn jcc  : type branch condition @eval_cc ( cond ) { @i8086_jump ( src ); }
jump insn isp  : type branch pre Y += 1 condition %S( Y > 0 ) { pc += Y; }
jump insn jsr_n: type call delay 1 { r1 = snip; sxip = src1; }
```

---

## 9. Instruction groups (`insn_group_decl`) — bulk conditionalisation

```
group insn NAME [ insn, insn, … ] condition EXPR ;
```
Applies a shared condition to a list of already-defined instructions — **this is
how ARM predicated execution is expressed** (every instruction gets a condition):
```
group insn ALUcond [ add, sub ] condition @eval_cc ( cond );   // 8086.def
```
(The grammar accepts this; codegen wiring is in progress.)

---

## 10. Decoder operands (`decoder_operands_decl`)

```
decoder_operands [ TYPE id , const TYPE id , ccflags TYPE id , … ] ;
```
Operands the decoder extracts and the semantics reference. Qualifiers:
- **`const`** — known at translate time (immediate); foldable.
- **`ccflags`** — usable with `@eval_cc(cond)` to turn an encoded condition field
  into a runtime predicate (ARM/x86 condition codes).
```
decoder_operands [ #i16 src, #i16 dst, ccflags #i16 cond, const #i1 rep ];   // 8086
decoder_operands [ #i32 dst, …, const #i32 wmask, const #i9 vec9 ];          // m88k
```

> UPCL today declares operand *names/types/flags*; the bit-level **encoding** and
> the full decoder DSL (patterns, escapes, addressing modes, leading-state) are
> specified in **`docs/upcl-instruction-decoders.md`**.

---

# PART II — COOKBOOK

Each recipe is tagged **[GRAMMAR]** (works today) or **[PROPOSED]** (extension).

## 11. Register files for hard architectures

### 11.1 SPARC — register windows  **[GRAMMAR]**
Already in `sparc.def` (§4.6): 512 physical slots `w?`, with `g/i/l/o` views
indexed by `cwp`, and `r0..r31` aliasing `g/l/i/o`. FP register overlays
(`f?`/`d?`/`x?` over `int_f?`/`int_d?`/`int_x?`) show 32×32→16×64→8×128 packing
via bidirectional `<->` index bindings.

### 11.2 IA-64 — rotating / stacked registers  **[PROPOSED]**
Extend the windowed binding (§4.6) with state-driven renaming over CFM/RRB:
```
group GR {
    [ 128 ** #i64 phys? ],
    [ 96  ** #i64 r?:32 -> phys?:( rse_rename( _, %CFM ) ) ],   // stack frame
    // rotating subset for software pipelining:
    [ 96  ** #i64 rot?:32 -> phys?:( ( _ + %RRB ) % 96 + 32 ) ]
}
```
See `docs/upcl-complex-cpu-ia64.md` §5 for the full RSE/rotation model.

### 11.3 AM29K — 192 regs with a local register stack  **[GRAMMAR-ish]**
Same windowed pattern as SPARC: 128 globals + 128 locals where `lrN` indexes a
bank relative to the stack pointer:
```
group R {
    [ 128 ** #i32 gr? ],
    [ 128 ** #i32 lr? -> gr?:( ( ( gr1 >> 2 ) + _ ) % 128 ) ]   // circular local stack
}
```

### 11.4 MMIX — 256 regs with dynamic local/global split  **[PROPOSED]**
256 registers, but reads of "marginal" registers (between `rL` and `rG`) return 0.
Use a hardwired/conditional binding:
```
group R {
    [ 256 ** #i64 g? ],
    // architectural $k: marginal range reads as zero (rL..rG)
    [ 256 ** #i64 $?  <- ( ( _ >= rL && _ < rG ) ? 0 : g?:( _ ) ) ]   // [PROPOSED] conditional bind
}
[ #i64 rL ], [ #i64 rG ]    // dynamic thresholds; PUSH/POP adjust via @rse-style macros
```

### 11.5 x86-64 **APX** extension — 32 GPRs  **[GRAMMAR]**
APX adds R16–R31 (32 GPRs total) reachable via the REX2/EVEX *decoder*. For the
**register file** it is just more GPRs with the standard alias chain already shown
in `8086_1.def` (`rax -> eax -> ax -> ah/al`):
```
group R {
    [ 32 ** #i64 r? ],
    [ 32 ** #i32 e?  -> #i32 ( _ : _ <-> r?:( _ ) ) ],   // low 32
    [ 32 ** #i16 w?  -> #i16 ( _ : _ <-> e?:( _ ) ) ],   // low 16
    [ 32 ** #i8  b?  -> #i8  ( _ : _ <-> w?:( _ ) ) ]    // low 8
}
```
The new encoding (extra register bit) is handled by the agnostic **decode-time
state** mechanism (`docs/upcl-instruction-decoders.md` §5), *not* the register file.

---

## 12. Condition-code updates across ISAs (the `%CC` mechanism)

The rule everywhere: bind the architectural flags in the register file
(`field -> %N/%V/%Z/%C/%P`), then wrap the producing value in `%CC(…)`.

### 12.1 x86/x64 — FLAGS  **[GRAMMAR]**
Flag bindings from `8086.def`; updates via `%CC`:
```
[ #i16 flags -> %PSR #i1 explicit ( …:O->%V:…:S->%N:Z->%Z:…:A<-C:…:P->%P:…:C->%C ) ]
insn add : dst = %CC ( dst + src );          // all flags
insn inc : dst = %CC ( dst + 1, !C );        // x86 INC leaves CF unchanged
insn and { dst = %CC ( dst & src ); flags.[ C, O ] = 0; }   // logicals clear CF/OF
```

### 12.2 ARM — NZCV + the S-bit  **[GRAMMAR]**
Bind N/Z/C/V; only the `S`-form updates flags. Two instruction variants (`add`,
`adds`) where `adds` uses `%CC`. Predication is §13.

### 12.3 PowerPC — CR0 + XER  **[GRAMMAR]** (XER detail in §15)
PPC dot-forms update CR0; `%CC` drives the CR0 bits (LT/GT/EQ/SO) bound as flags;
carrying/overflowing forms additionally update **XER** (§15).

### 12.4 IA-64 — compares write *predicates*  **[PROPOSED]**
A compare writes a predicate pair, not a flag word — see §16 and
`docs/upcl-complex-cpu-ia64.md` §10:
```
insn "cmp.eq" : ( p1 : p2 ) = %CC ( src1 == src2 );   // [PROPOSED] predicate-pair target
```

### 12.5 SPARC — icc/xcc  **[GRAMMAR]**
Bind the integer condition codes in the PSR; `cc`-setting forms wrap in `%CC`;
64-bit `xcc` is a second flag view of the same compare.

---

## 13. ARM conditional execution  **[GRAMMAR]**

Every ARM instruction is predicated on a 4-bit condition. Express it once with an
**instruction group** plus a `ccflags` operand and `@eval_cc`:
```
decoder_operands [ …, ccflags #i4 cond ];
group insn ALUcond [ add, sub, mov, orr, and, eor, … ] condition @eval_cc ( cond );
```
`@eval_cc(cond)` turns the encoded condition field into a runtime predicate; the
group applies it to every listed instruction (exactly the `8086.def` pattern,
generalised to the whole ALU set).

---

## 14. MIPS delay slots  **[GRAMMAR]**

Use the `delay` clause on the jump (count of following instructions executed
before the branch takes effect):
```
jump insn beq  : type branch delay 1 condition R[rs] == R[rt] { pc += offset << 2; }
jump insn jr   : type return delay 1 { pc = R[rs]; }
```
libcpu's translator already models the delay-slot CFG (`translate.cpp`,
`TAG_DELAY_SLOT`); `delay 1` lowers to it. `m88k.def` uses `delay 1` on `jmp_n`.

---

## 15. PowerPC XER register  **[GRAMMAR]**

XER is a status register with **CA** (carry), **OV** (overflow), **SO** (sticky
overflow) bits plus a byte-count field. Model it as a split register; CA doubles
as the architectural carry:
```
group S {
    [ #i32 xer -> explicit [ #i1 SO -> %?,        // sticky overflow (own bit)
                             #i1 OV,               // overflow
                             #i1 CA <-> %C,        // carry  (drives architectural %C)
                             #i22 0,
                             #i7 bytecnt ] ]
}
insn addc  : RT = %CC ( RA + RB );                 // sets CA via %C
insn adde  : RT = %CC ( RA + RB + xer.CA );        // add-extended consumes CA
insn addo  : RT = %OFTRAP ( RA + RB, 0 );          // OV/SO via overflow augment
```
CA participates like any flag because it is bound (`<-> %C`); `adde` reads
`xer.CA` directly as a field.

---

## 16. IA-64 NaT bits  **[PROPOSED]**

A GPR is effectively 65 bits (value + NaT poison). Model NaT as a **parallel
1-bit register file** (the same technique libcpu uses for exploded N/V/Z/C
flags), and let the lowering OR source NaTs into results:
```
group GR {
    [ 128 ** #i64 r?  ],
    [ 128 ** #i1  nat? ]            // [PROPOSED] parallel poison bits
}
// speculative load defers a fault into NaT:
insn "ld.s" { r[d] = #i64 %M:spec[ addr ]; nat[d] = @mem_faulted(); }   // [PROPOSED]
// consuming ops propagate NaT (auto-rule, like %CC for flags):
insn "add"  { r[d] = r[a] + r[b]; nat[d] = nat[a] | nat[b]; }
insn "chk.s": if ( nat[s] ) @trap ( NAT_CONSUMPTION );
```
Full design (auto-propagation, ALAT, speculation): `docs/upcl-complex-cpu-ia64.md`
§6–§7.

---

## 17. M88K comparison-result compression  **[GRAMMAR]** ★

The M88K `cmp` packs **14 condition outcomes** (eq, ne, gt, le, lt, ge, hi, ls,
lo, hs, plus byte/half equalities) into one result word. Naively that is ten
comparisons; the libcpu frontend computes it with **two** plus a bit trick.

### 17.1 The trick (from `arch/m88k/m88k_translate.cpp:567-606`)
```
//   if (s1 == s2)  dst = 0x5aa4;          // eq|le|ge|ls|hs|be|he
//   else { dst = 0xa008;                  // ne|nb|nh
//          if ((u)s1 > (u)s2) dst|=0x900; else dst|=0x600;
//          if ((s)s1 > (s)s2) dst|=0x90;  else dst|=0x60; }
//   // (((-X) ^ 6) & 0xf) << Y will do the trick.
```
`X` is a 1-bit compare result; `(((-X) ^ 6) & 0xf)` yields `6` (`0b0110`) when X=0
and `9` (`0b1001`) when X=1 — the two *complementary* condition-bit patterns —
placed by `<< Y` (Y=8 for unsigned hi/hs/ls/lo, Y=4 for signed gt/ge/le/lt). So
each `> `comparison fills its four dependent bits in one shot.

### 17.2 In UPCL  **[GRAMMAR]**
The same packed word is expressible directly; constant folding + the backend
reproduce the compressed code. Using `?:`, `%U`/`%S`, bit-combine and shifts:
```
decoder_operands [ #i32 dst, #i32 src1, #i32 src2 ];

// one comparison -> its 4-bit complementary pattern at position Y
macro cmp4 ( gt ) : #i4 %result = ( ( - [ #i4 gt ] ) ^ 6 ) & 0xf;   // 6 or 9

insn cmp : dst =
    ( src1 == src2 )
      ? 0x5aa4
      : ( 0xa008
          | ( [ #i32 @cmp4 ( %U ( src1 > src2 ) ) ] << 8 )    // hi/ls/lo/hs
          | ( [ #i32 @cmp4 ( %S ( src1 > src2 ) ) ] << 4 ) ); // gt/le/lt/ge
```
This mirrors the hand-written frontend exactly — the macro `cmp4` captures the
`(((-X)^6)&0xf)` compression, and the two `> ` tests cover all signed+unsigned
results. (Bit layout: bit2=eq, 3=ne, 4=gt, 5=le, 6=lt, 7=ge, 8=hi, 9=ls, 10=lo,
11=hs, per the source.)

---

## 18. Real vs. virtual memory  **[PROPOSED]**

UPCL today has one space: `%M[addr]`. Add an **address-space qualifier** so a
`.def` can request physical/real access bypassing translation (the system-mode
softmmu seam in `docs/system-emulation.md`):
```
%M[ addr ]          // default: virtual (MMU-translated)
%M:phys[ addr ]     // [PROPOSED] real/physical access (no translation)
%M:io[ addr ]       // [PROPOSED] MMIO space -> device callback
%M:spec[ addr ]     // [PROPOSED] speculative (IA-64 ld.s; faults -> NaT, §16)
```
The qualifier picks which `ICpuAddressSpace` the access lowers to
(`docs/com-architecture-and-backends.md`). RISC privileged loads, A20-gated
accesses, and IA-64 `ld.s` all use it.

---

## 19. System Management Mode (SMM)  **[PROPOSED]**

SMM = a hidden mode entered by SMI, running from SMRAM, saving/restoring a state
image. Model it with arch **mode state** + a dedicated space + trap-style entry:
```
[ #i1 in_smm ]                                  // [PROPOSED] mode bit
macro enter_smm () {                            // [PROPOSED]
    %M:smram[ smbase + 0xFE00 ] = @save_state(); // dump CPU state image
    in_smm = 1; pc = smbase + 0x8000;            // SMM entry point
}
insn rsm : { @restore_state( %M:smram[ smbase + 0xFE00 ] ); in_smm = 0; }
```
`%M:smram` is an address space (§18) visible only when `in_smm`. SMI delivery
reuses the async-interrupt/exception machinery (`docs/system-emulation.md` §3–§4).

---

## 20. Virtual-machine (hardware-assisted) emulation  **[PROPOSED]**

Model guest/host with a **mode bit**, VM entry/exit as traps, and nested address
spaces (the system-mode MMU composed twice):
```
[ #i1 in_guest ]                                          // [PROPOSED]
insn vmlaunch : { @save_host_state(); @load_vmcs(); in_guest = 1; }   // VMRUN/VMLAUNCH
// a sensitive op while in_guest traps to the hypervisor (VM-exit):
insn cpuid : if ( in_guest ) @trap ( VMEXIT_CPUID ); else @host_cpuid();
%M:gpa[ addr ]   // guest-physical -> second-level (nested) translation  [PROPOSED]
```
VMCS/save-areas are register groups; VM-exit reasons are trap codes. The second
translation stage is the context-keyed cache from `docs/system-emulation.md` §8.

---

## 21. SIMD and matrix instructions  **[GRAMMAR types / PROPOSED ops]**

Vector **types exist today** (`#v<E>:<N>`, lexer §1.4; `c::type` VECTOR_INTEGER/
VECTOR_FLOAT with `elem_bits`). Lane/matrix *operators* are the proposed addition
(see `docs/upcl-cpu-fpu-simd.md` §5):
```
group V { [ 32 ** #v8:128 v? ] }                 // 32 × (16-lane × 8-bit) regs  [GRAMMAR type]

insn "vadd.b"  : v[d] = lanes  #v8:128 ( v[a] + v[b] );          // [PROPOSED] elementwise
insn "vqadd.b" : v[d] = lanes  #v8:128 sat( v[a] + v[b] );       // saturating
insn "vsum.w"  : r[d] = reduce #v32:128 (+) v[a];               // horizontal reduce
insn "vshuf.b" : v[d] = shuffle v[a], v[b], imm;                // permute
```
**Matrix / tile** instructions (AMX-style) extend this to 2-D: model a tile as a
register holding a `#v` of rows and a multiply-accumulate intrinsic:
```
group T { [ 8 ** #v512 tile? ] }                                 // [GRAMMAR type]
insn "tdpbf16" : tile[d] = @tmma( tile[d], tile[a], tile[b] );    // [PROPOSED] tile MAC intrinsic
```
LLVM backend → native vector/`llvm.matrix.*`; interpreter → scalarised loops;
WASM → `v128` (`docs/upcl-cpu-fpu-simd.md` §5).

---

## 22. A fixed-function unit: the PSX1 **MDEC**  **[PROPOSED]**

The PlayStation-1 MDEC is not a CPU — it is a command-driven macroblock decoder
(RLE → dequant → IDCT → colour). UPCL can model it as an **architecture whose
"instructions" are its commands**, with registers for the command/status FIFO and
semantics expressed in ordinary UPCL loops/expressions:

```
arch "psx-mdec" {
    name "Sony PSX1 MDEC"; endian little; byte_size 8; word_size 32; address_size 32;
    register_file {
        group CMD {
            [ #i32 cmd  -> explicit [ #i3 op, #i1 dp, #i1 sp, #i27 param ] ],  // command word
            [ #i32 stat ],                                                    // status/FIFO state
            [ #i64 qt_y ], [ #i64 qt_uv ],          // quant tables (loaded by cmd)
            [ #i64 scale ]                          // IDCT scale table
        }
        group BLK { [ 64 ** #i16 blk? ] }           // an 8x8 working block
    }
}
decoder_operands [ const #i3 op, #i32 word ];

macro idct ( ) {                                    // [PROPOSED] 8x8 inverse DCT over blk?
    for ( i = 0; %S ( i < 8 ); i = i + 1 ) @idct_rows( i );
    for ( j = 0; %S ( j < 8 ); j = j + 1 ) @idct_cols( j );
}

// "instructions" = MDEC commands:
insn cmd_set_quant : { qt_y = %M[ param ]; qt_uv = %M[ param + 8 ]; }
insn cmd_set_scale : { scale = %M[ param ]; }
insn cmd_decode {
    @rle_expand( word );                            // run-length -> blk?
    for ( i = 0; %S ( i < 64 ); i = i + 1 )
        blk[i] = %CC ( blk[i] * [ #i16 qt_y[i:1] ] );   // dequantise (illustrative)
    @idct();                                         // inverse DCT
    @yuv_to_rgb();                                   // colour convert -> output FIFO
}
```
This shows UPCL describing a **DMA/command-FIFO coprocessor**: the command word is
a split register, each command is an `insn`, and the DSP math (RLE/IDCT/colour) is
UPCL macros over a working-block register array. Tag heavily as illustrative — it
exercises loops, bit-fields, memory, and macros rather than a fetch/decode loop.

---

## 23. One's-complement arithmetic (PDP-1)  **[GRAMMAR]**

PDP-1 is 18-bit **one's complement**. Negation is bitwise complement (`cma:
ac = ~ac;` in `pdp1.def`). Addition needs **end-around carry**: compute in a wider
type and fold the carry-out back in:
```
arch "pdp1" { byte_size 18; word_size 18; address_size 12; psr_size 18; … }

insn add {
    t  = [ #i19 ac ] + [ #i19 %M[ Y ] ];        // 19-bit sum keeps the carry-out
    ac = #i18 ( t + [ #i19 ( t >> 18 ) ] );     // end-around carry: add bit-18 back
}
insn cma : ac = ~ac;                            // one's-complement negate
insn sub {                                      // a - b  ==  a + (~b), end-around
    t  = [ #i19 ac ] + [ #i19 ( ~ %M[ Y ] & 0x3ffff ) ];
    ac = #i18 ( t + [ #i19 ( t >> 18 ) ] );
}
```
Key points: widen with a cast (`[ #i19 ac ]`) so the carry survives, `>> 18` is the
carry-out, fold it back, then narrow with `#i18 ( … )`. One's-complement has two
zeros (`+0`/`-0`); the end-around carry is exactly what reconciles them. `~` is the
native complement operator (§1.5).

---

## 24. Quick syntax index

| Need | Syntax |
|------|--------|
| integer/float/vector type | `#i32` / `#f80` / `#v8:128` |
| octal literal | `0100` (= 64) |
| special register | `pc -> %PC`, `… -> %PSR`, `flag -> %C` |
| hardwired reg | `[ #i32 r0 <- 0 ]` |
| register array | `[ 31 ** #i32 r?:1 ]` (`_` = index) |
| sub-register split | `[ #i16 ax -> #i8 ( ah : al ) ]` |
| windowed/indexed | `i? -> w?:( expr )` |
| flags update | `%CC ( expr )`, `%CC ( expr, !C )`, `%CC ( expr, [Z,N] )` |
| signed/unsigned | `%S ( … )` / `%U ( … )` |
| overflow trap | `%OFTRAP ( expr, code )` |
| cast | `[ #i16 al ]` |
| bit-slice / combine | `x[hi..lo]`, `x[start:cnt]` / `( a : b )` |
| rotate | `a <<> n` / `a >>< n` |
| memory | `#i32 %M[ addr ]` (`%M:phys[…]` proposed) |
| ternary | `c ? a : b` |
| macro / call | `macro f(x){…}` / `@f(x)`, `%result` |
| instruction | `insn add : dst = %CC ( dst + src );` |
| jump | `jump insn b : type branch delay 1 pre … condition … { … }` |
| conditional group (ARM) | `group insn G [a,b] condition @eval_cc(cond);` |
| decoder operands | `decoder_operands [ #i32 src, const #i9 imm, ccflags #i4 cond ];` |

---

## 25. References
- **Implementation:** `upcl/ast/lexer.l`, `upcl/ast/parser.y`, `upcl/ast/ast.h`,
  `upcl/c/` (semantic IR), `upcl/cg/generate.*` (libcpu codegen),
  `upcl/examples/*.def`.
- **M88K compression:** `arch/m88k/m88k_translate.cpp:567-616`.
- **Sibling design docs:** `docs/upcl-cpu-fpu-simd.md` (FPU/SIMD lowering),
  `docs/upcl-instruction-decoders.md` (decoder DSL, decode-state),
  `docs/upcl-complex-cpu-ia64.md` (bundles, NaT, rotation, ALAT),
  `docs/system-emulation.md` (real memory, SMM, VM, MMU),
  `docs/com-architecture-and-backends.md` (address spaces, multi-backend).
```
