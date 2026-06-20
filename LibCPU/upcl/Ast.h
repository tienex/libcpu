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

// ---- expressions ----------------------------------------------------------

typedef enum _EXPR_KIND {
    ExprInt,        // integer literal           (Int)
    ExprName,       // identifier reference       (Name)
    ExprUnary,      // Op Args[0]                 (Op, Args)
    ExprBinary,     // Args[0] Op Args[1]         (Op, Args)
    ExprCall,       // Name(Args...)              (Name, Args)
    ExprIndex       // Args[0][Args[1]]           (Args)
} EXPR_KIND;

class Expr {
public:
    EXPR_KIND           Kind;
    SRC_LOC             Loc  = 0;
    UINT64              Int  = 0;       // ExprInt
    std::string         Name;          // ExprName / ExprCall callee
    TOKEN_KIND          Op   = TokUnknown;   // ExprUnary / ExprBinary
    std::vector<Expr *> Args;

    explicit Expr (EXPR_KIND K) : Kind (K) {}
    ~Expr () { for (Expr *E : Args) { delete E; } }
};

// ---- statements (instruction semantics) -----------------------------------

typedef enum _STMT_KIND {
    StmtAssign      // Lhs = Rhs;
} STMT_KIND;

class Stmt {
public:
    STMT_KIND Kind;
    SRC_LOC   Loc = 0;
    Expr     *Lhs = nullptr;
    Expr     *Rhs = nullptr;

    explicit Stmt (STMT_KIND K) : Kind (K) {}
    ~Stmt () { delete Lhs; delete Rhs; }
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

class Arch {
public:
    std::string              Name;          // arch "<name>"
    std::string              FullName;      // name "...";
    SRC_LOC                  Loc = 0;
    bool                     Little = true;
    UINT32                   WordSize = 0;
    UINT32                   AddressSize = 0;
    std::vector<Reg>         Registers;
    std::vector<Feature>     Features;       // declared ISA features (features { ... })
    std::vector<Cpu *>       Cpus;           // CPU models (cpu "..." { ... })
    std::vector<Format *>    Formats;
    std::vector<Insn *>      Insns;
    std::vector<Directive *> Directives;

    ~Arch () {
        for (Cpu *C : Cpus) { delete C; }
        for (Format *F : Formats) { delete F; }
        for (Insn *I : Insns) { delete I; }
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
