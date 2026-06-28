/** @file
  UPCL abstract syntax tree.

  Extensibility: expressions and statements are each ONE node class tagged by a kind
  enum, so a new expression form or statement kind is added by extending the enum and
  one parser/visitor case -- existing code is untouched. Declarations carry typed
  fields plus a Directives escape hatch the parser fills for any directive it does not
  model specially, so the language can grow without breaking the AST contract.

  Nodes are PascalCase classes; kind tags are UPPERCASE enums. Ownership is by raw
  pointer with RAII destructors (a node owns its children).

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_UPCL_AST_H
#define LIBCPU_UPCL_AST_H

#include "Token.h"
#include <map>
#include <string>
#include <vector>

namespace LibCPU {
namespace Upcl {

// ---- types ----------------------------------------------------------------

// A UPCL type literal: #i<width> (integer), #f<width> (float), #v<lanes>:<width>
// (vector). Parsed from a TokType spelling like "#i16" / "#f80" / "#v4:32".
typedef enum _TYPE_KIND { TypeInt, TypeFloat, TypeVector } TYPE_KIND;

class Type {
public:
    TYPE_KIND   Kind  = TypeInt;
    UINT32      Width = 0;       // element width in bits
    UINT32      Lanes = 0;       // vector lane count (#v only)
    std::string Spelling;       // "#i16"
    SRC_LOC     Loc   = 0;
};

// ---- expressions ----------------------------------------------------------

typedef enum _EXPR_KIND {
    ExprInt,        // integer literal               (Int)
    ExprFloat,      // floating-point literal         (Real)
    ExprName,       // identifier reference           (Name)
    ExprUnary,      // Op Args[0]                     (Op, Args)
    ExprBinary,     // Args[0] Op Args[1]             (Op, Args)
    ExprCall,       // @Name(Args...)                 (Name, Args)  -- a macro call
    ExprIndex,      // Args[0][Args[1]]               (Args)
    // old-.def additions:
    ExprMeta,       // %Name (augment or meta-register reference)   (Name)
    ExprMember,     // a.b  (Args[0]=base, Name=member)  or  a.[m,n] (Members)
    ExprCast,       // [ <VType> <expr> ]             (VType, Args[0])
    ExprMem,        // %M[addr]  or  <VType> %M[addr]  (VType?, Args[0]=addr); Linked => %LL[addr]
    ExprStoreCond,  // %SC[addr] <- value  (VType?, Args[0]=addr, Args[1]=value); yields 0/1 success
    ExprBitSlice,   // operand[a:b] or operand[a..b]  (Args[0]=op, Args[1]=a, Args[2]=b; RangeInclusive)
    ExprBitCombine, // ( a : b : c )                  (Args, MSB-first)
    ExprSelect,     // cond ? a : b                   (Args[0..2])
    ExprAugment,    // %S/%U/%OFTRAP/%ORD/%UNO (Name = which)        (Name, Args)
    ExprCC,         // %CC(expr [, flags])            (Args[0]=expr, CcFlags / CcNeg)
    ExprIs          // expr is <VType>                (Args[0], VType)
} EXPR_KIND;

class Expr {
public:
    EXPR_KIND                Kind;
    SRC_LOC                  Loc  = 0;
    UINT64                   Int  = 0;          // ExprInt
    double                   Real = 0.0;        // ExprFloat
    std::string              Name;             // ExprName / ExprCall / ExprMeta / ExprMember / ExprAugment
    TOKEN_KIND               Op   = TokUnknown; // ExprUnary / ExprBinary
    std::vector<Expr *>      Args;
    Type                    *VType = nullptr;   // ExprCast / ExprMem / ExprIs (owned)
    bool                     Linked = false;     // ExprMem: a load-linked (%LL) memory reference
    bool                     RangeInclusive = true;   // ExprBitSlice: [a:b] (true) vs [a..b] (false)
    std::vector<std::string> Members;           // ExprMember: a.[m,n]
    std::vector<std::string> CcFlags;           // ExprCC: which condition bits
    std::vector<bool>        CcNeg;             // ExprCC: per-flag '!' negation

    explicit Expr (EXPR_KIND K) : Kind (K) {}
    ~Expr () { for (Expr *E : Args) { delete E; } delete VType; }
};

// ---- statements (instruction semantics) -----------------------------------

typedef enum _STMT_KIND {
    StmtAssign,     // [<LhsType>]? Lhs <AssignOp> Rhs;   (AssignOp is the DESUGARED base op:
                    //   TokAssign = plain store; TokPlus for +=, TokShl for <<=, ... -- the
                    //   parser's AssignOpOf already mapped the *Eq token to its operator)
    StmtExpr,       // a bare expression statement (%CC(...) / @macro(...))  -- Rhs holds it
    StmtBlock,      // { Body... }
    StmtIf,         // if (Cond) Then [else Else]
    StmtFor,        // for (Init...; Cond; Step...) Body
    StmtWhile       // while (Cond) Body
} STMT_KIND;

class Stmt {
public:
    STMT_KIND           Kind;
    SRC_LOC             Loc = 0;
    Expr               *Lhs = nullptr;          // StmtAssign target
    Expr               *Rhs = nullptr;          // StmtAssign value / StmtExpr expression
    TOKEN_KIND          AssignOp = TokAssign;    // StmtAssign: =, +=, -=, <<=, <<>=, ...
    Type               *LhsType = nullptr;       // StmtAssign: optional `<type> lhs = ...` (owned)
    Expr               *Cond = nullptr;          // StmtIf / StmtFor / StmtWhile
    std::vector<Stmt *> Body;                    // StmtBlock / loop body
    std::vector<Stmt *> Then;                    // StmtIf then-branch
    std::vector<Stmt *> Else;                    // StmtIf else-branch
    std::vector<Stmt *> Init;                    // StmtFor init list
    std::vector<Stmt *> Step;                    // StmtFor step list

    explicit Stmt (STMT_KIND K) : Kind (K) {}
    ~Stmt () {
        delete Lhs; delete Rhs; delete LhsType; delete Cond;
        for (Stmt *S : Body) { delete S; }
        for (Stmt *S : Then) { delete S; }
        for (Stmt *S : Else) { delete S; }
        for (Stmt *S : Init) { delete S; }
        for (Stmt *S : Step) { delete S; }
    }
};

// ---- declarations ---------------------------------------------------------

// A directive the parser did not model with a typed field (the growth escape hatch):
// its keyword and the raw expressions/strings that followed.
class Directive {
public:
    std::string         Keyword;
    SRC_LOC             Loc = 0;
    std::vector<Expr *> Args;
    std::string         Str;            // a trailing string argument, if any

    ~Directive () { for (Expr *E : Args) { delete E; } }
};

// A name = expr pairing -- reused for an instruction's opcode-field bindings.
class Field {
public:
    std::string Name;
    SRC_LOC     Loc = 0;
    Expr       *Value = nullptr;
    ~Field () { delete Value; }
};

// One field of an encoding format: a name and a width in bits, laid out MSB-first.
class FormatField {
public:
    std::string Name;
    UINT32      Width = 0;
    SRC_LOC     Loc = 0;
};

// A reusable encoding template: the bit layout shared by a family of instructions.
// An instruction references it and binds some fields to constants (the match); the
// rest are operands. Declaring the layout once is what removes the per-instruction
// match/length/field boilerplate.
class Format {
public:
    std::string              Name;
    SRC_LOC                  Loc = 0;
    std::vector<FormatField> Fields;

    UINT32 TotalBits () CONST {
        UINT32 N = 0;
        for (FormatField CONST &F : Fields) { N += F.Width; }
        return N;
    }
};

// ---- addressing modes (generic ModR/M-style operand resolution) -----------
//
// addrmode modrm16 ( mod, rm ) disp ( mod == 1 ? 8 : mod == 2 ? 16 : 0 ) {
//     mod == 3          => reg [ ax, cx, dx, bx, sp, bp, si, di ] ;   // register-direct
//     mod == 0 & rm == 6 => mem [ disp16 ] ;                          // [disp16]
//     rm == 0           => mem [ bx + si + disp ] ;                   // [BX+SI(+disp)]
//     ...
//   }
// A rule's condition (over the selector fields) picks it; the operand is then a register
// (the bound field indexes the regset) or memory at the sum of the base registers plus a
// displacement (bare `disp` = the default-size clause, or `disp8` / `disp16` fixed).
class AddrTerm {
public:
    std::string Reg;     // a base register name ("" if a displacement term)
    bool        Disp = false;   // a displacement term
    UINT32      DispBits = 0;    // 0 = the addrmode's default disp clause; else fixed (8/16)
};
class AddrRule {
public:
    SRC_LOC                  Loc = 0;
    Expr                    *Cond = nullptr;     // the selecting condition (null = always)
    bool                     IsReg = false;
    std::vector<std::string> RegMap;             // IsReg: the registers the bound field selects
    std::vector<AddrTerm>    Mem;                // !IsReg: base registers + displacement terms
    // Side effects on the base register: `pre ( Rn -= N )` is applied BEFORE the EA is taken
    // (autodecrement -(Rn)); `post ( Rn += N )` AFTER the operand is used (autoincrement (Rn)+).
    std::string              PreReg;             // "" = none
    INT32                    PreDelta = 0;
    std::string              PostReg;            // "" = none
    INT32                    PostDelta = 0;
    ~AddrRule () { delete Cond; }
};
class AddrMode {
public:
    SRC_LOC                  Loc = 0;
    std::string              Name;
    std::vector<std::string> Params;             // the selector field names, e.g. ( mod, rm )
    Expr                    *DispSize = nullptr;  // the default displacement width (bits) expr
    std::vector<AddrRule *>  Rules;
    ~AddrMode () { delete DispSize; for (AddrRule *R : Rules) { delete R; } }
};

// One field of an encoding word: a named slice of `Width` bits, laid out MSB-first. It is
// either MATCHED to a constant (an opcode bit-pattern), BOUND to a decoder operand (the
// field's bits become that operand's value), or left free (a reserved / don't-care field).
// This is architecture-neutral: a RISC word is one list of fields (op/rs/rt/...), and any
// fixed-width form of any ISA is described the same way -- no byte/ModR/M assumptions.
class EncField {
public:
    std::string              Name;        // field name (op, rs, imm) -- for diagnostics/disasm
    SRC_LOC                  Loc = 0;
    UINT32                   Width = 0;
    bool                     HasConst = false;
    UINT64                   Const = 0;   // `= <value>`  : the opcode match
    std::string              Operand;     // `-> <operand>`: the decoder operand this field feeds
    std::vector<std::string> RegMap;      // `-> op[r0, r1, ...]`: field value SELECTS a register
                                          //   (the operand is that register location); empty =>
                                          //   the operand is the immediate field value.
    bool                     Relative = false; // `-> op rel`: the field is a PC-relative
                                          //   displacement -- the operand value is the NEXT
                                          //   instruction's address plus the sign-extended
                                          //   field (a branch target). Architecture-neutral.
    std::string              AddrMode;    // `-> op @ <addrmode>`: the field selects through an
                                          //   addressing mode (a register or a memory address,
                                          //   reading a variable-length displacement).
    bool                     HasImplicitImm = false; // an implicit operand carrying no encoding
    UINT64                   ImplicitImm = 0;     //   bits: `name = <const>` binds the operand to
                                          //   a fixed immediate (e.g. a shift-by-1's count), and
                                          //   `name <- <reg>` (a single-entry RegMap, width 0)
                                          //   binds it to a fixed register (a shift-by-CL count).
    bool                     SignExt = false; // `-> op sx`: sign-extend the field value to the
                                          //   machine word width (e.g. `0x83 /digit ib`'s imm8
                                          //   becomes a 16-bit operand). Architecture-neutral.
    bool                     Tail = false; // the field follows an `@addrmode` operand, so it is
                                          //   positioned in the byte tail AFTER that mode's
                                          //   variable-length displacement (e.g. the immediate of
                                          //   `0x81 /digit iw`), not inside the fixed opcode word.
};

// One encoding alternative: a word of `WordBits` bits split into fields. An instruction may
// list several alternatives (e.g. register-form vs immediate-form) separated by `|`.
class EncAlt {
public:
    SRC_LOC                Loc = 0;
    UINT32                 WordBits = 0;   // total instruction width (from the `#iN` word type)
    std::vector<EncField>  Fields;         // MSB-first; widths sum to WordBits

    UINT32 TotalBits () CONST {
        UINT32 N = 0;
        for (EncField CONST &F : Fields) { N += F.Width; }
        return N;
    }

    // The bits of the fixed opcode word -- the non-tail fields. Tail fields (immediates after a
    // variable-length addressing mode) are appended as extra bytes, not part of the `#iN` word.
    UINT32 WordFieldBits () CONST {
        UINT32 N = 0;
        for (EncField CONST &F : Fields) { if (!F.Tail) { N += F.Width; } }
        return N;
    }
};

// ---- disassembly syntax styles --------------------------------------------
//
// disasm features { styles { att; intel } size { word:"w" } mnemonic { style att {...} } ... }
//
// The systematic differences between syntaxes (AT&T vs Intel) captured once: per-style
// mnemonic casing + size suffix, register prefix + casing, immediate prefix/suffix, and
// operand ordering. Each instruction then declares only `disasm ( mnemonic:"mov", size:word,
// operands: dst, src )` and the engine renders it for the active style.
class DisasmStyle {
public:
    std::string Name;                     // att / intel
    std::string MnemCasing;               // "lower" / "upper" / ""
    bool        MnemSizeSuffix = false;   // append the size code to the mnemonic (movw)
    std::string RegPrefix;                // "%" / ""
    std::string RegCasing;                // "lower" / "upper" / ""
    std::string IntPrefix;                // "$0x" / "0x" / ""
    std::string IntSuffix;                // "h" / ""
    std::string IntMacro;                 // `integer { style X { call : <macro> } }`
    std::string RegMacro;                 // `register { style X { call : <macro> } }`
    std::string DispPrefix;               // memory-displacement prefix ("0x" / "")  -- AT&T: no `$`
    std::string DispSuffix;               // memory-displacement suffix ("h" / "")
    std::string DispMacro;                // `displacement { style X { call : <macro> } }`
    bool        ReverseOperands = false;  // AT&T prints operands source-first
};

class DisasmFeatures {
public:
    std::vector<std::string>           Styles;     // declared style names (first = default)
    std::map<std::string, std::string> Sizes;      // size name -> code (word -> "w")
    std::vector<DisasmStyle>           Rules;      // per-style rendering rules

    DisasmStyle *Find (std::string CONST &Name) {
        for (DisasmStyle &S : Rules) { if (S.Name == Name) { return &S; } }
        return nullptr;
    }
};

// A disassembly format expression -- the body of a `macro disasm`. It evaluates, with the
// macro's parameters bound (a value/bits number or a register name string), to a string:
//   "0x" + value:hex(bits)   ->  DFmtConcat [ DFmtLit "0x", DFmtHex value width=bits ]
typedef enum _DFMT_KIND {
    DFmtLit,      // a string literal
    DFmtParam,    // a bare parameter (a number rendered decimal, a string as-is)
    DFmtHex,      // <param>:hex(<width-param>?)  -- hex, optionally zero-padded to the width
    DFmtDec,      // <param>:dec / :sdec          -- decimal, unsigned or signed
    DFmtCase,     // <param>:upper / :lower       -- a string parameter recased
    DFmtConcat,   // a + b + ...
    DFmtCond,     // ( <expr> ) ? a : b           -- choose a format by a condition over params
    DFmtCall      // @<macro>( <param>, ... )      -- a nested disasm macro
} DFMT_KIND;

class DisasmFmt {
public:
    DFMT_KIND                Kind = DFmtLit;
    std::string              Text;        // literal / parameter name / macro name
    std::string              WidthParam;  // DFmtHex: the parameter giving the bit width ("" none)
    bool                     Signed = false;   // DFmtDec
    std::string              Casing;      // DFmtCase: "upper" / "lower"
    Expr                    *Cond = nullptr;  // DFmtCond: the condition (owned)
    std::vector<DisasmFmt *> Kids;        // DFmtConcat parts / DFmtCond [then, else]
    std::vector<std::string> Args;        // DFmtCall: the argument parameter names
    ~DisasmFmt () { delete Cond; for (DisasmFmt *K : Kids) { delete K; } }
};

// macro disasm <name> ( <params> ) => <format> ;  -- a reusable operand formatter.
class DisasmMacro {
public:
    std::string              Name;
    SRC_LOC                  Loc = 0;
    std::vector<std::string> Params;
    DisasmFmt               *Body = nullptr;
    ~DisasmMacro () { delete Body; }
};

// The declarative per-instruction disassembly: the mnemonic, its size class (for the
// AT&T suffix), and the operands in base (Intel) order. Rendered through DisasmFeatures.
class DisasmSpec {
public:
    std::string              Mnemonic;
    std::string              Size;        // a size name from the features `size { }` map
    std::vector<std::string> Operands;    // operand names, Intel order (AT&T reverses)
};

class Insn {
public:
    std::string              Name;
    SRC_LOC                  Loc = 0;
    std::string              Super;          // inherited instruction (insn name : super)
    SRC_LOC                  SuperLoc = 0;
    std::string              Format;        // referenced encoding format ("" = none yet)
    SRC_LOC                  FormatLoc = 0;
    std::vector<Field *>     Bindings;       // opcode-field bindings from format(.. :: a=b)
    bool                     HasDisasm = false;
    std::string              Disasm;
    std::string              Feature;        // gating ISA feature (feature(...) attr; "" = base ISA)
    SRC_LOC                  FeatureLoc = 0;
    std::vector<Stmt *>      Semantics;      // the instruction body (assignment statements)
    std::vector<EncAlt *>    Encodings;      // old .def: `encode <alt> | <alt> ...` byte patterns
    DisasmSpec              *DisasmDecl = nullptr;  // declarative `disasm ( ... )` (owned)
    std::vector<Directive *> Directives;     // any other attribute (escape hatch)

    ~Insn () {
        for (Field *F : Bindings) { delete F; }
        for (Stmt *S : Semantics) { delete S; }
        for (EncAlt *E : Encodings) { delete E; }
        delete DisasmDecl;
        for (Directive *D : Directives) { delete D; }
    }
};

class Reg {
public:
    std::string Name;
    SRC_LOC     Loc = 0;
};

// A named ISA feature -- a unit of behaviour a CPU model may include. Declared once in
// `features { ... }`; an instruction opts into one via the `feature(<name>)` attribute
// (no attribute = the always-present base ISA).
class Feature {
public:
    std::string Name;
    SRC_LOC     Loc = 0;
    std::string Doc;        // optional "..." description
};

// A CPU model: a named bundle of features (`cpu "v30" { base; ext_186; nec; }`).
// Selecting it enables exactly those features; an instruction gated on a feature not in
// the set is excluded from decode/translate.
class Cpu {
public:
    std::string              Name;
    SRC_LOC                  Loc = 0;
    std::string              Doc;
    std::vector<std::string> Features;
};

// ---- old .def register file -----------------------------------------------
//
// register_file { group R { [ #i16 bx -> #i8 ( bh : bl ) ], ... } SEG { ... } }
//
// A register declaration is `[ (repeat **)? <type> <name> ( -> <binding> | <- <alias> )? ]`.
// The binding describes sub-register splits (ax -> #i8 (ah:al)), meta-register binds
// (pc -> %PC ...), flag bit-maps (flags -> %PSR #i1 explicit ( O->%V : ... )) and
// evaluated aliases (pc ... evaluate ( @ea(seg,off) )).

// One field of a register splitter: a sub-register / flag name and how it binds. The
// direction (Name -> %Meta read-map, Name <- Src write-bind, Name <-> Src bidi) and an
// optional hardwired value / sub-list (union) cover every form 8086.def uses.
class BitBind {
public:
    std::string            Name;            // sub-register / flag name ("" for a pure constant)
    SRC_LOC                Loc = 0;
    bool                   IsConst = false; // a hardwired bit (e.g. the 0s in the flags layout)
    UINT64                 Const = 0;
    Type                  *SubType = nullptr;  // union typed bind (#i3 TOP)  (owned)
    std::string            MetaMap;         // Name -> %Meta  (the meta target spelling, "" if none)
    std::string            SrcBind;         // Name <- Src / <-> Src  (the source name, "" if none)
    bool                   Bidi = false;    // the bind was '<->'
    Expr                  *HardExpr = nullptr; // Name <- ( expr )   (owned)
    std::vector<BitBind *> Sub;             // a nested union list  [ ... ]
    ~BitBind () { delete SubType; delete HardExpr; for (BitBind *B : Sub) { delete B; } }
};

// The part after `->`: an optional meta-register target and the field splitter.
class Splitter {
public:
    SRC_LOC                Loc = 0;
    Type                  *FieldType = nullptr;  // element type of the binds (#i8/#i1/...)  (owned)
    bool                   Explicit = false;
    bool                   Union = false;        // [ ... ] union vs ( ... ) field/colon list
    Expr                  *Evaluate = nullptr;   // evaluate ( ... )  (owned)
    std::vector<BitBind *> Binds;
    ~Splitter () { delete FieldType; delete Evaluate; for (BitBind *B : Binds) { delete B; } }
};

// The optional binding on a register declaration.
class RegBinding {
public:
    SRC_LOC      Loc = 0;
    std::string  Meta;                  // `-> %PC` / `-> %PSR` target ("" if none)
    Splitter    *Split = nullptr;       // `-> ... <splitter>`  (owned)
    Expr        *AliasExpr = nullptr;   // `<- <expr>`  (owned)
    std::string  AliasId;               // `-> <id>`  (a simple alias)
    ~RegBinding () { delete Split; delete AliasExpr; }
};

class RegDecl {
public:
    SRC_LOC      Loc = 0;
    Type        *VType = nullptr;        // owned
    std::string  Name;
    bool         Repeatable = false;     // declared as `name?` (st?)
    UINT64       RepeatStart = 0;         // `name?:N` start index (r?:1 -> r1..rN); default 0
    Expr        *RepeatCount = nullptr;  // `N ** <type> <name>`  (owned)
    RegBinding  *Binding = nullptr;      // owned
    ~RegDecl () { delete VType; delete RepeatCount; delete Binding; }
};

class Group {
public:
    SRC_LOC                 Name_Loc = 0;
    std::string             Name;
    std::vector<RegDecl *>  Regs;
    ~Group () { for (RegDecl *R : Regs) { delete R; } }
};

class RegisterFile {
public:
    std::vector<Group *> Groups;
    ~RegisterFile () { for (Group *G : Groups) { delete G; } }
};

// ---- macros, decoder operands, jump instructions --------------------------

// macro name ( params ) : <inline stmt> ;   |   macro name ( params ) { body }
class Macro {
public:
    SRC_LOC                  Loc = 0;
    std::string              Name;
    std::vector<std::string> Params;
    std::vector<Stmt *>      Body;
    ~Macro () { for (Stmt *S : Body) { delete S; } }
};

// regset gpr16 [ ax, cx, dx, bx, sp, bp, si, di ];  -- a named register list. An encoding's
// `-> op[ gpr16 ]` references it instead of spelling the registers out every time; the
// decoder expands the alias to its registers (the field value selects one).
class RegSet {
public:
    std::string              Name;
    SRC_LOC                  Loc = 0;
    std::vector<std::string> Regs;
};

// decoder_operands [ #i16 src, #i16 dst, ccflags #i16 cond, const #i1 rep ];
typedef enum _DECOP_KIND { DecopNormal, DecopConst, DecopCcflags } DECOP_KIND;
class DecoderOperand {
public:
    SRC_LOC     Loc = 0;
    Type       *VType = nullptr;     // owned
    std::string Name;
    DECOP_KIND  Kind = DecopNormal;
    ~DecoderOperand () { delete VType; }
};

// jump insn <name> : type <t> [delay e] [pre {...}] [condition e] { action }
class JumpInsn {
public:
    SRC_LOC               Loc = 0;
    std::string           Name;
    std::string           JumpType;          // branch / call / return / trap
    Expr                 *Delay = nullptr;    // owned
    Expr                 *Condition = nullptr;// owned
    std::vector<Stmt *>   Pre;
    std::vector<Stmt *>   Action;
    std::vector<EncAlt *> Encodings;          // `encode <alt> | ...` byte patterns (owned)
    bool                  HasDisasm = false;
    std::string           Disasm;            // `disasm "..."` override format string
    DisasmSpec           *DisasmDecl = nullptr;  // declarative `disasm ( ... )` (owned)
    std::string           Feature;           // gating ISA feature ("" = base ISA)
    ~JumpInsn () {
        delete Delay; delete Condition;
        for (Stmt *S : Pre) { delete S; }
        for (Stmt *S : Action) { delete S; }
        for (EncAlt *E : Encodings) { delete E; }
        delete DisasmDecl;
    }
};

// The memory-management unit description: how the architecture translates a virtual address to a
// physical one. `page_size` is the granularity libcpu's internal TLB caches at; `translate(...)` is
// the hardware table-walk (the TLB-refill handler) -- it reads page-table entries from PHYSICAL
// memory (%PM[..]), checks validity/protection, assigns the result to %PA, and raises @fault(vec)
// on a translation fault. libcpu provides the TLB; this expresses what fills it.
class Mmu {
public:
    SRC_LOC                       Loc = 0;
    UINT32                        PageSize = 0;   // TLB page granularity in bytes
    std::vector<DecoderOperand *> Params;         // translate(...) parameters (va, acc, ...) -- owned
    std::vector<Stmt *>           Body;           // the table-walk statements -- owned
    ~Mmu () {
        for (DecoderOperand *P : Params) { delete P; }
        for (Stmt *S : Body) { delete S; }
    }
};

class Arch {
public:
    std::string              Name;          // arch "<name>"
    std::string              FullName;      // name "...";
    SRC_LOC                  Loc = 0;
    bool                     Little = true;
    UINT32                   ByteSize = 0;   // old .def: byte_size
    UINT32                   WordSize = 0;
    UINT32                   FloatSize = 0;  // old .def: float_size
    UINT32                   AddressSize = 0;
    UINT32                   PsrSize = 0;    // old .def: psr_size
    UINT32                   AddrSegShift = 0;  // `address_display segmented shift N offset M`:
    UINT32                   AddrOffBits = 0;   //   show code addresses as seg:off (e.g. CS:IP);
                                          //   0 shift = a flat hexadecimal address (the default).
    std::vector<Reg>         Registers;     // new-syntax flat register list
    RegisterFile            *RegFile = nullptr;  // old-syntax register_file (owned)
    Mmu                     *Mmu = nullptr;  // optional MMU description (mmu { ... }) (owned)
    std::vector<Feature>     Features;       // declared ISA features (features { ... })
    std::vector<Cpu *>       Cpus;           // CPU models (cpu "..." { ... })
    std::vector<Format *>    Formats;
    std::vector<Insn *>      Insns;
    std::vector<Macro *>     Macros;         // old-syntax macros (owned)
    std::vector<JumpInsn *>  Jumps;          // old-syntax jump instructions (owned)
    std::vector<DecoderOperand *> DecoderOps; // old-syntax decoder_operands (owned)
    std::vector<RegSet *>    RegSets;        // named register lists (regset) (owned)
    std::vector<AddrMode *>  AddrModes;      // addressing-mode tables (addrmode) (owned)
    DisasmFeatures          *Disasm = nullptr;  // `disasm features { ... }` (owned)
    std::vector<DisasmMacro *> DisasmMacros;  // `macro disasm ...` formatters (owned)
    std::vector<Directive *> Directives;

    ~Arch () {
        delete RegFile;
        delete Mmu;
        for (Cpu *C : Cpus) { delete C; }
        for (Format *F : Formats) { delete F; }
        for (Insn *I : Insns) { delete I; }
        for (Macro *M : Macros) { delete M; }
        for (JumpInsn *J : Jumps) { delete J; }
        for (DecoderOperand *D : DecoderOps) { delete D; }
        for (RegSet *R : RegSets) { delete R; }
        for (AddrMode *A : AddrModes) { delete A; }
        delete Disasm;
        for (DisasmMacro *D : DisasmMacros) { delete D; }
        for (Directive *D : Directives) { delete D; }
    }
};

class Module {
public:
    std::vector<Arch *> Archs;
    ~Module () { for (Arch *A : Archs) { delete A; } }
};

} // namespace Upcl
} // namespace LibCPU

#endif // LIBCPU_UPCL_AST_H
