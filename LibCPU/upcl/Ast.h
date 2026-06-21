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
    ExprName,       // identifier reference           (Name)
    ExprUnary,      // Op Args[0]                     (Op, Args)
    ExprBinary,     // Args[0] Op Args[1]             (Op, Args)
    ExprCall,       // @Name(Args...)                 (Name, Args)  -- a macro call
    ExprIndex,      // Args[0][Args[1]]               (Args)
    // old-.def additions:
    ExprMeta,       // %Name (augment or meta-register reference)   (Name)
    ExprMember,     // a.b  (Args[0]=base, Name=member)  or  a.[m,n] (Members)
    ExprCast,       // [ <VType> <expr> ]             (VType, Args[0])
    ExprMem,        // %M[addr]  or  <VType> %M[addr]  (VType?, Args[0]=addr)
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
    std::string              Name;             // ExprName / ExprCall / ExprMeta / ExprMember / ExprAugment
    TOKEN_KIND               Op   = TokUnknown; // ExprUnary / ExprBinary
    std::vector<Expr *>      Args;
    Type                    *VType = nullptr;   // ExprCast / ExprMem / ExprIs (owned)
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
    std::vector<Directive *> Directives;     // any other attribute (escape hatch)

    ~Insn () {
        for (Field *F : Bindings) { delete F; }
        for (Stmt *S : Semantics) { delete S; }
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
    SRC_LOC             Loc = 0;
    std::string         Name;
    std::string         JumpType;          // branch / call / return / trap
    Expr               *Delay = nullptr;    // owned
    Expr               *Condition = nullptr;// owned
    std::vector<Stmt *> Pre;
    std::vector<Stmt *> Action;
    ~JumpInsn () {
        delete Delay; delete Condition;
        for (Stmt *S : Pre) { delete S; }
        for (Stmt *S : Action) { delete S; }
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
    std::vector<Reg>         Registers;     // new-syntax flat register list
    RegisterFile            *RegFile = nullptr;  // old-syntax register_file (owned)
    std::vector<Feature>     Features;       // declared ISA features (features { ... })
    std::vector<Cpu *>       Cpus;           // CPU models (cpu "..." { ... })
    std::vector<Format *>    Formats;
    std::vector<Insn *>      Insns;
    std::vector<Macro *>     Macros;         // old-syntax macros (owned)
    std::vector<JumpInsn *>  Jumps;          // old-syntax jump instructions (owned)
    std::vector<DecoderOperand *> DecoderOps; // old-syntax decoder_operands (owned)
    std::vector<Directive *> Directives;

    ~Arch () {
        delete RegFile;
        for (Cpu *C : Cpus) { delete C; }
        for (Format *F : Formats) { delete F; }
        for (Insn *I : Insns) { delete I; }
        for (Macro *M : Macros) { delete M; }
        for (JumpInsn *J : Jumps) { delete J; }
        for (DecoderOperand *D : DecoderOps) { delete D; }
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
