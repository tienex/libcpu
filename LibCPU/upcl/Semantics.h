/** @file
  The UPCL semantics translator -- drive an ICpuEmitter from an instruction body.

  Where RegisterLayout turns the `register_file` into concrete storage, this turns the
  instruction SEMANTICS (the Stmt/Expr body the parser built) into emitter calls. One
  Translator is created per instruction (it owns the temporaries it emits) and walks the
  body, mapping:

    * register / sub-register / flag names      -> GetRegister/PutRegister + bit-field
                                                    extract/insert, GetFlag/SetFlag;
    * the rich expression language               -> ConstInt/BinaryOp/UnaryOp/Cast/Select/
                                                    Load/Store (bit-slice, bit-combine,
                                                    %U/%S signedness, casts, %M memory);
    * %CC ( expr [, flags] )                     -> the value plus the architectural flag
                                                    writes it implies (add/sub derive
                                                    C/O/A; Z/N/P derive from the result);
    * @macro ( args )                            -> inline expansion of the macro body.

  Decoder operands (src, dst, ...) and macro parameters are supplied by the caller through
  Bind(): the translator has no opinion on how they were decoded, only on what the body
  does with them. That keeps the semantics independent of the (separate) instruction
  decoder.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_UPCL_SEMANTICS_H
#define LIBCPU_UPCL_SEMANTICS_H

#include "Ast.h"
#include "RegisterLayout.h"
#include "LibCPU/ICpu.h"
#include "LibCPU/PCom.h"
#include <map>
#include <string>
#include <vector>

namespace LibCPU {
namespace Upcl {

// A translator-emitted value together with the bit width it carries. Widths flow through
// the expression tree so casts, bit-fields and flag tests use the right size.
class Value {
public:
    ICpuValue *V    = nullptr;
    UINT32     Bits = 0;
};

class Translator {
public:
    Translator (RegisterLayout CONST &Layout, Arch *pArch, ICpuEmitter *pEmitter,
                UINT32 WordBits);

    // Make Name resolve to an already-emitted value (a decoder operand or macro argument)
    // of the given width. Shadows any register of the same name for this translation.
    void Bind (std::string CONST &Name, Value CONST &Val);

    // Translate a body (an instruction's Semantics or a macro's Body). Returns false on a
    // construct not yet handled (the caller can report it); already-emitted work stays.
    bool Emit (std::vector<Stmt *> CONST &Body);

private:
    // expressions
    Value EvalExpr (Expr *pExpr);
    Value EvalName (std::string CONST &Name);
    Value EvalMember (Expr *pExpr);          // a.b  -> a sub-field of register a
    Value EvalCC (Expr *pExpr);              // %CC ( expr [, flags] )
    Value EvalMacroCall (Expr *pExpr);       // @macro(args) used as a value (returns %result)

    // statements
    bool  EmitStmt (Stmt *pStmt);
    bool  EmitAssign (Stmt *pStmt);
    void  StoreTo (Expr *pLhs, Value CONST &Rhs);
    bool  EmitMacroStmt (Expr *pCall);       // @macro(args) as a statement

    // names -> storage
    void   WriteName (std::string CONST &Name, Value CONST &Rhs);
    UINT32 WidthOf (Expr *pExpr) CONST;            // a name/cast's static width (no emit)
    bool  FindSub (std::string CONST &Name, RegSub CONST **ppSub) CONST;
    bool  FindFlag (std::string CONST &Name, RegFlag CONST **ppFlag) CONST;
    bool  MapFlag (RegFlag CONST &Flag, CPU_FLAG *pFlag) CONST;  // false -> use PSR bit

    // flags
    void  SetFlagBit (RegFlag CONST &Flag, Value CONST &Bit);
    Value GetFlagBit (RegFlag CONST &Flag);
    void  DeriveFlags (Expr *pInner, Value CONST &Result, Value CONST &A, Value CONST &B,
                       bool HaveOperands, std::vector<std::string> CONST &CcFlags,
                       std::vector<bool> CONST &CcNeg);

    // emitter helpers (each pools the result so it outlives the call)
    Value Const (UINT32 Bits, UINT64 N);
    Value Bin (CPU_BINOP Op, Value CONST &A, Value CONST &B);
    Value Un (CPU_UNOP Op, Value CONST &A);
    Value Cmp (CPU_CMP Pred, Value CONST &A, Value CONST &B);
    Value CastTo (CPU_CAST Op, Value CONST &A, UINT32 Bits);
    Value Coerce (Value CONST &A, UINT32 Bits, bool Signed);  // trunc / zext / sext to Bits
    Value Extract (Value CONST &Parent, UINT32 Lo, UINT32 Width);
    Value Insert (Value CONST &Parent, Value CONST &Field, UINT32 Lo, UINT32 Width);
    Value Pool (ComPtr<ICpuValue> V, UINT32 Bits);

    RegisterLayout CONST            &m_Layout;
    Arch                            *m_pArch;
    ICpuEmitter                     *m_pE;
    UINT32                           m_WordBits;
    std::map<std::string, Value>     m_Env;        // bound operands / locals / %result
    std::map<std::string, Macro *>   m_Macros;
    std::vector<ComPtr<ICpuValue>>   m_Pool;       // keeps every emitted value alive
};

} // namespace Upcl
} // namespace LibCPU

#endif // LIBCPU_UPCL_SEMANTICS_H
