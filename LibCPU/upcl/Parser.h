/** @file
  UPCL recursive-descent parser.

  Built to extend:
    * Directives are dispatched by KEYWORD SPELLING in ParseArchItem / ParseInsnItem.
      Adding "decode", "format", "cycles", ... is a new case there -- the lexer and
      every other rule are untouched. Anything unrecognised is captured as a generic
      Directive (the AST escape hatch), so the grammar degrades gracefully.
    * Expressions use a Pratt (precedence-climbing) parser driven by InfixBp(): adding
      an operator is one row there plus its TOKEN_KIND.
    * Errors are reported through the clang-style DiagnosticEngine and recovered from
      by skipping to a synchronising token, so one mistake does not cascade.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_UPCL_PARSER_H
#define LIBCPU_UPCL_PARSER_H

#include "Lexer.h"
#include "Ast.h"

namespace LibCPU {
namespace Upcl {

class Parser {
public:
    Parser (SourceManager *pSm, FILE_ID File, DiagnosticEngine *pDiag);

    // Parse the whole file into a module (caller owns it). Always returns a module;
    // check the DiagnosticEngine's error count for success.
    Module *ParseModule ();

private:
    // token stream
    void   Advance ();
    Token CONST &Cur () CONST { return m_Cur; }
    bool   At (TOKEN_KIND Kind) CONST { return m_Cur.Kind == Kind; }
    bool   AtKeyword (CHAR8 CONST *pWord) CONST;
    bool   Accept (TOKEN_KIND Kind);
    bool   AcceptListSep ();                     // a ';' or ',' between list items
    bool   Expect (TOKEN_KIND Kind, CHAR8 CONST *pContext);
    void   ErrorAt (SRC_LOC Loc, std::string CONST &Msg);
    void   SyncTo (TOKEN_KIND Kind);            // error recovery

    // grammar
    Arch      *ParseArch ();
    void       ParseArchItem (Arch *pArch);
    void       ParseRegisters (Arch *pArch);
    void       ParseFeatures (Arch *pArch);      // the `features { ... }` block
    void       ParseCpu (Arch *pArch);           // a `cpu "..." { ... }` model
    void       ParseFormats (Arch *pArch);      // the `formats { ... }` block
    Insn      *ParseInsnDecl ();                 // optional [attrs] then `insn ...`
    void       ParseAttributes (Insn *pInsn);    // `[ format(..), disasm(..), .. ]`
    Directive *ParseGenericDirective ();         // the escape hatch
    Stmt      *ParseStmt ();
    void       ParseBlock (std::vector<Stmt *> *pOut);   // `{ <stmt>* }`
    Stmt      *FinishAssignOrExpr (SRC_LOC Loc, Expr *pLhs, Type *pLhsType);
    Stmt      *ParseSimpleAssign ();                      // a for-loop init/step assignment (no ';')

    // expressions (Pratt)
    Expr *ParseExpr (UINT32 MinBp = 0);
    Expr *ParsePrefix ();
    Expr *ParsePrimary ();
    Expr *ParsePostfix (Expr *pBase);            // bit-slice e[a:b], member e.f
    Expr *ParseMemRef (SRC_LOC Loc, Type *pVType); // the `[expr]` tail of %M / %MEM (meta consumed)
    void  ParseCallArgs (Expr *pCall);           // `( <expr> (, <expr>)* )` -> pCall->Args
    static UINT32 InfixBp (TOKEN_KIND Kind);

    // old .def helpers
    Type *ParseType ();                          // a `#i16` / `#f80` / `#v4:32` literal
    static TOKEN_KIND AssignOpOf (TOKEN_KIND Kind);   // a compound-assign token -> its op (or TokUnknown)

    bool   ExpectInt (UINT64 *pOut, CHAR8 CONST *pContext);

    SourceManager    *m_pSm;
    DiagnosticEngine *m_pDiag;
    Lexer             m_Lexer;
    Token        m_Cur;
};

} // namespace Upcl
} // namespace LibCPU

#endif // LIBCPU_UPCL_PARSER_H
