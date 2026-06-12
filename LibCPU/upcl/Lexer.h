/** @file
  UPCL lexer: one source file (in a SourceManager) -> token stream, with global
  locations for diagnostics.

  To add a new operator/punctuation: add its TOKEN_KIND in Token.h and one case
  in Next(). Words and numbers need no change. Comments: // line and slash-star block.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_UPCL_LEXER_H
#define LIBCPU_UPCL_LEXER_H

#include "Token.h"
#include "Diagnostic.h"

namespace LibCPU {
namespace Upcl {

class Lexer {
public:
    Lexer (SourceManager *pSm, FILE_ID File, DiagnosticEngine *pDiag);

    // Produce the next token (TokEof repeats at end of input).
    Token Next ();

private:
    void        SkipTrivia ();
    Token  Make (TOKEN_KIND Kind, UINT32 Begin);
    Token  LexWord (UINT32 Begin);
    Token  LexNumber (UINT32 Begin);
    Token  LexString (UINT32 Begin);

    CHAR8        Peek (UINT32 Ahead = 0) CONST;
    CHAR8        Advance ();
    bool         Eat (CHAR8 c);
    SRC_LOC Loc (UINT32 Local) CONST { return m_Base + Local; }

    SourceManager     *m_pSm;
    DiagnosticEngine  *m_pDiag;
    std::string CONST &m_Text;
    SRC_LOC       m_Base;       // global location of this file's first char
    UINT32             m_Pos;        // local offset into m_Text
};

} // namespace Upcl
} // namespace LibCPU

#endif // LIBCPU_UPCL_LEXER_H
