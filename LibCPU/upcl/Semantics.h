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
    ICpuValue *V      = nullptr;
    UINT32     Bits   = 0;
    bool       Float  = false;   // the value is an IEEE float (drives BinF*/CastF* selection)
    bool       Signed = false;   // %S marks the value signed (drives sext widening + signed compare/div)
    UINT32     Lanes  = 0;       // >0 => a packed SIMD vector of this many lanes (lane width = Bits/Lanes)
    // A compile-time constant: K holds its value (masked to Bits). It is materialised into a ConstInt
    // node LAZILY -- only when actually consumed (see Translator::Use) -- so a folded-away or dead
    // constant computation emits nothing. Operators over constants fold instead of emitting.
    bool       IsConst = false;
    UINT64     K       = 0;
};

// A decoded operand: the storage a decoder operand (src/dst/...) resolved to. The location
// model -- so `dst = src` in the semantics reads and writes real storage. An immediate has
// no write-back; a register operand may be a sub-register window of its physical parent.
class Operand {
public:
    enum KIND { Imm, Reg, Mem } Kind = Imm;
    UINT32      Bits     = 0;   // operand width
    UINT64      ImmValue = 0;   // Imm
    UINT32      RegIndex = 0;   // Reg: physical register index (RegisterLayout)
    UINT32      SubLo    = 0;   // Reg: sub-register window low bit
    UINT32      SubWidth = 0;   // Reg: window width (0 => the whole physical register)
    std::string RegName;        // Reg: the register's name as written (a sub-register's own
                                //   name, e.g. "b", not its parent "bc") -- for disassembly
    // Mem: the operand is the memory cell at Base1 (+ Base2) + Disp -- a computed address.
    UINT32      Base1    = ~(UINT32) 0;   // first base register index (~0 = none)
    UINT32      Base2    = ~(UINT32) 0;   // second base register index (~0 = none)
    INT64       Disp     = 0;             // signed displacement
    // Scaled index (the NS32000 [Rn:B/W/D/Q] mode): EA gains IndexReg * IndexScale, on top of the
    // Base1/Base2/Disp the nested base mode resolved. IndexReg is a physical register index (~0 =
    // none); IndexScale is 1/2/4/8. Filled by a nested scaled-index addrmode term.
    UINT32      IndexReg   = ~(UINT32) 0; // scaled index register index (~0 = none)
    UINT32      IndexScale = 1;           // scale applied to the index register
    std::string MemText;                  // address rendering for disassembly, e.g. "bx+si"
    // Addressing-mode side-effect blocks (PDP-11 autoincrement/decrement, etc.): the matched addr
    // rule's `pre { }` runs BEFORE the EA (autodecrement), `post { }` AFTER the operand is used
    // (autoincrement). Pointers into the owning AddrRule's statement lists; null = none.
    std::vector<Stmt *> CONST *Pre  = nullptr;
    std::vector<Stmt *> CONST *Post = nullptr;
    // The addrmode parameter values that selected this operand (e.g. dm, dr) -- decode constants
    // exposed during the pre/post block so `%REG[group, field]` resolves the very register the mode
    // names (collapsing one autoincrement/decrement rule per register into a single field-indexed one).
    std::map<std::string, UINT64> Fields;
};

class Translator {
public:
    Translator (RegisterLayout CONST &Layout, Arch *pArch, ICpuEmitter *pEmitter,
                UINT32 WordBits);

    // Make Name resolve to an already-emitted value (a decoder operand or macro argument)
    // of the given width. Shadows any register of the same name for this translation.
    void Bind (std::string CONST &Name, Value CONST &Val);

    // Make Name resolve to a decoded operand LOCATION (register or immediate). Reads emit a
    // register/const load; writes store back through the location. Used by the decoder.
    void BindOperand (std::string CONST &Name, Operand CONST &Op);

    // The return address a `@trap(vector)` resumes at (the next instruction). The host reads
    // the trapped vector and resumes here -- for HLT this is "wait on interrupt".
    void SetTrapReturn (UINT64 Pc) { m_TrapReturnPc = Pc; }

    // Generate phase (the `gen` static-frontend path): decoder fields are runtime, not yet known, so
    // compile-phase constant folding is disabled. Default is compile phase (JIT / decode / emit).
    void SetGenerate (bool On) { m_Generate = On; }

    // Translate a body (an instruction's Semantics or a macro's Body). Returns false on a
    // construct not yet handled (the caller can report it); already-emitted work stays.
    bool Emit (std::vector<Stmt *> CONST &Body);

    // Emit a branch condition: the expression as an i1 (a value wider than one bit becomes a
    // non-zero test, the C-like `if (x)`). Ownership of *ppOut transfers to the caller.
    HRESULT EmitCondition (Expr *pExpr, ICpuValue **ppOut);

    // Emit an expression, returning its value (ownership of *ppOut transfers to the caller) --
    // for a computed branch target (a return address popped off the stack).
    HRESULT EmitExpr (Expr *pExpr, ICpuValue **ppOut);

    // Emit one statement (public face of the statement walker), for selective translation.
    bool EmitOne (Stmt *pStmt) { return EmitStmt (pStmt); }

    // In indirect-PC mode, a write to the program counter (pc or pc.off) is CAPTURED as the
    // computed branch target rather than stored -- a ret popping its return address. The
    // caller emits the IndirectBranch after the whole body (so the stack adjust around the
    // pop still runs). IndirectTarget() returns the captured value (null if none).
    void       SetIndirectPc (bool On) { m_IndirectPc = On; }
    ICpuValue *IndirectTarget () CONST { return m_IndirectTarget; }

private:
    // expressions
    Value EvalExpr (Expr *pExpr);
    Value EvalName (std::string CONST &Name);
    Value ReadOperand (Operand CONST &Op);   // read a decoded operand location
    bool  TryConstIndex (Expr *pIdx, UINT64 *pVal) CONST; // a compile-time-constant register index
    bool  RegSelName (Expr *pExpr, std::string *pName) CONST; // %REG[group, field] -> the member's name
    // Register/memory aliasing (`group AC aliases_memory`): given a word-addressed %M[addr],
    // return the address to pass to Load/Store. When aliasing is in effect and the word index
    // falls in the AC group's range, the result carries the CPU_REGBANK_FLAG sentinel so the
    // backend routes the access to the register bank rather than RAM. For non-aliased accesses
    // the result is the plain byte-cell offset (the same as WordCellAddr). WordAddr must
    // already be in word-index form (i.e. NOT yet scaled by CPU_WORD_CELL_BYTES).
    Value MemAliasAddr (Expr *pAddrExpr, Value CONST &WordAddr);

    // COMPILE-PHASE FOLDING. Decoder fields and immediate operands are known when an instruction is
    // translated (compile phase), so a pure expression over them -- a `(sr >= 6) ? 2 : 1` byte step,
    // an operand comparison -- folds to a literal here, rather than emitting a runtime compare/select
    // (generate phase). Register / memory / flag reads are inherently runtime and never fold. The
    // split is automatic; %EVAL(e) forces a fold and %GEN(e) suppresses it.
    bool  FoldConst (Expr *pExpr, bool Signed, INT64 *pVal, UINT32 *pBits) CONST;
    bool  ZeroWiredSlot (RegArray CONST &Arr, Expr *pIdx) CONST; // index resolves to a hardwired-0 reg
    Value MemAddress (Operand CONST &Op);     // the effective address of a memory operand
    Value WordCellAddr (Value CONST &Addr);   // word-addressed: a word index -> its 64-bit-cell byte offset
    Value EvalMember (Expr *pExpr);          // a.b  -> a sub-field of register a
    Value EvalCC (Expr *pExpr);              // %CC ( expr [, flags] )
    Value EvalMacroCall (Expr *pExpr);       // @macro(args) used as a value (returns %result)

    // statements
    bool  EmitStmt (Stmt *pStmt);
    bool  EmitAssign (Stmt *pStmt);
    void  StoreTo (Expr *pLhs, Value CONST &Rhs);
    bool  IsPcTarget (Expr *pLhs) CONST;     // pc / %PC / pc.<composition-field>
    bool  PcField (Expr *pLhs, std::string *pReg) CONST;  // pc.off -> its source register (ip)
    bool  EmitMacroStmt (Expr *pCall);       // @macro(args) as a statement
    Macro *FindMacro (std::string CONST &Name, size_t ArgCount) CONST;  // overload by arity
    bool  EmitIf (Stmt *pStmt);              // if (Cond) Then [else Else]  -> a CFG diamond
    bool  TryConstCond (Expr *pCond, bool *pResult) CONST;  // a compile-time-known condition
    bool  EmitWhile (Stmt *pStmt);           // while (Cond) Body           -> head/body/end
    bool  EmitFor (Stmt *pStmt);             // for (Init; Cond; Step) Body
    ComPtr<ICpuBlock> NewBlock (CHAR8 CONST *pName);

    // Loop-carried body locals.  A body local (`#i18 e = ...`) assigned INSIDE a loop and read
    // before/inside/after it cannot be a plain SSA Value: the value assigned in the body block does
    // not dominate the loop's head/end blocks, so a read after the loop (or on the next iteration)
    // would reference an out-of-scope temp -- and at runtime, the value of a temp from a block that
    // ran zero times is undefined (the interpreter leaves it 0).  To carry such a local across the
    // loop the way architectural register state already is (re-read per block, persisted by the
    // backend), each one is promoted to a synthesised SCRATCH REGISTER for the rest of the
    // translation: reads emit a bank Load, writes emit a bank Store.  PromoteLoopLocals scans a
    // loop's statements, allocates a scratch slot per assigned env local, and spills the live value
    // into it before the loop is entered.
    void   PromoteLoopLocals (Stmt *pStmt);              // promote a loop's carried locals to slots
    void   CollectAssignedNames (Stmt *pStmt, std::map<std::string, UINT32> &Out) CONST;
    void   CollectAssignedNames (std::vector<Stmt *> CONST &Body,
                                 std::map<std::string, UINT32> &Out) CONST;

    // names -> storage
    void   WriteName (std::string CONST &Name, Value CONST &Rhs);
    void   SetReservation (Value CONST &Addr);     // %LL: remember the reserved address + set LLbit
    std::vector<std::vector<Stmt *> CONST *> m_PostBlocks;  // addrmode post { } blocks, run after the body
    UINT32 WidthOf (Expr *pExpr) CONST;            // a name/cast's static width (no emit)
    bool  FindSub (std::string CONST &Name, RegSub CONST **ppSub) CONST;
    bool  FindFlag (std::string CONST &Name, RegFlag CONST **ppFlag) CONST;
    RegArray CONST *FindArray (Expr *pBase) CONST;  // the register array a `name[i]` base names
    Value RegBankAddr (RegArray CONST &Arr, Value CONST &Idx);  // the Load/Store sentinel address
    bool  MapFlag (RegFlag CONST &Flag, CPU_FLAG *pFlag) CONST;  // false -> use PSR bit

    // flags
    void  SetFlagBit (RegFlag CONST &Flag, Value CONST &Bit);
    Value GetFlagBit (RegFlag CONST &Flag);
    void  DeriveFlags (bool IsAdd, Value CONST &Result, Value CONST &A, Value CONST &B,
                       Value CONST &CarryBit, bool HaveOperands,
                       std::vector<std::string> CONST &CcFlags, std::vector<bool> CONST &CcNeg);

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
    Value      ConstVal (UINT32 Bits, UINT64 K) CONST;   // a lazy compile-time constant (no emission)
    ICpuValue *Use (Value CONST &V);                      // materialise (emit a ConstInt for a lazy const)
    // Register-read memoization: a physical register read once is reused until the register is written
    // or the basic block changes, so an instruction's repeated GetRegister (an effective-address base
    // and the same register's autoincrement) emits ONE load. Writes/flags/blocks invalidate it.
    Value GetReg (UINT32 Index, UINT32 Width);
    void  PutReg (UINT32 Index, ICpuValue *pVal, UINT32 Width);   // write + invalidate the cached read
    void  ClearRegCache () { m_RegCache.clear (); }               // a block boundary / unknown-index write

    RegisterLayout CONST            &m_Layout;
    Arch                            *m_pArch;
    ICpuEmitter                     *m_pE;
    UINT32                           m_WordBits;
    bool                             m_IndirectPc = false; // pc-write -> captured as a branch
    ICpuValue                       *m_IndirectTarget = nullptr; // the captured computed target
    UINT64                           m_TrapReturnPc = 0;    // @trap resume address
    std::map<std::string, Value>     m_Env;        // bound values / locals / %result
    // Loop-carried env locals promoted to synthesised scratch registers (see PromoteLoopLocals).
    // Maps a local name to its scratch register slot (used with CPU_REGBANK_FLAG) and the slot's
    // width.  A name present here is register-backed: EvalName / WriteName route through a bank
    // Load / Store instead of the SSA m_Env entry.  m_NextScratch is the next free slot, seeded
    // above the architectural register file so a scratch never overlaps a real register.
    std::map<std::string, UINT32>    m_EnvSlot;     // local name -> scratch register slot index
    std::map<std::string, UINT32>    m_EnvSlotWidth;// local name -> scratch register width (bits)
    UINT32                           m_NextScratch = 0;  // next free scratch slot (0 = uninitialised)
    std::map<std::string, Operand>   m_Operands;   // decoded operand locations (decoder path)
    std::map<std::string, UINT64>    m_Fields;      // addrmode field values in scope (%REG[group, field])
    std::map<UINT32, Value>          m_RegCache;    // memoized physical-register reads (GetReg)
    bool                             m_Generate = false;   // generate phase: do not auto-fold (gen path)
    UINT32                           m_NoFold = 0;          // %GEN(...) nesting: suppress auto-folding
    std::map<std::string, std::vector<Macro *>> m_Macros;  // name -> overloads (by arity)
    std::vector<ComPtr<ICpuValue>>   m_Pool;       // keeps every emitted value alive
};

} // namespace Upcl
} // namespace LibCPU

#endif // LIBCPU_UPCL_SEMANTICS_H
