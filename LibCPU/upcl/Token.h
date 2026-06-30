/** @file
  UPCL lexical tokens.

  Design for extensibility: KEYWORDS ARE NOT TOKENS. The lexer emits one token kind
  for every word (TokIdent); the parser recognises keywords by spelling through its
  dispatch tables. So a new directive ("decode", "format", "if", ...) is added purely
  in the parser -- the lexer never changes. Only true punctuation/operators are
  distinct token kinds (the expression parser keys its precedence table on them, so a
  new operator is one lexer entry + one precedence-table row).

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_UPCL_TOKEN_H
#define LIBCPU_UPCL_TOKEN_H

#include "Diagnostic.h"
#include <string>

namespace LibCPU {
namespace Upcl {

typedef enum _TOKEN_KIND {
    TokEof,
    TokIdent,        // a word (keywords are recognised by the parser, by spelling)
    TokInt,          // integer literal (0x hex / 0b binary / 0 octal / decimal)
    TokFloat,        // floating-point literal (3.14159, 1e10) -- Real holds the value
    TokString,       // "..."
    TokType,         // a UPCL type literal: #i16, #f80, #v4:32 (Text holds the spelling)
    TokMeta,         // %word -- an augment (%CC/%S/%U/%M/%MEM/...) or meta-register (%PC/%V/...);
                     //   Text holds the word after '%'. The parser dispatches by spelling.
    TokMacroIdent,   // @word -- a macro reference; Text holds the word after '@'.
    TokBuiltinIdent, // $word -- a builtin (framework intrinsic) reference; Text holds the word
                     //   after '$'. '@' is reserved for user macros; '$' invokes a built-in
                     //   intrinsic ($exec/...).

    // punctuation
    TokLBrace, TokRBrace, TokLParen, TokRParen, TokLBracket, TokRBracket,
    TokSemi, TokComma, TokColon, TokColonColon, TokAt, TokHash, TokDollar, TokQuestion,
    TokArrow, TokDotDot, TokDot,
    TokBindLeft, TokBindBidi,   // <-  <->   ('->' is TokArrow = bind-right)

    // operators (the expression parser's precedence table keys on these)
    TokAssign, TokPlus, TokMinus, TokStar, TokSlash, TokPercent,
    TokAmp, TokPipe, TokCaret, TokTilde, TokShl, TokShr,
    TokRol, TokRor,                    // <<>  >><
    TokAndCom, TokOrCom, TokXorCom,    // &~  |~  ^~  (op then complement the rhs)
    TokEqEq, TokNotEq, TokLt, TokLtEq, TokGt, TokGtEq, TokAndAnd, TokOrOr, TokNot,
    TokStarStar,                       // **  (register repetition)

    // compound assignments (insn bodies: al += 6, flags <<= 1, ...)
    TokPlusEq, TokMinusEq, TokStarEq, TokSlashEq, TokPercentEq,
    TokPipeEq, TokAmpEq, TokCaretEq, TokShlEq, TokShrEq, TokRolEq, TokRorEq,
    TokAndComEq, TokOrComEq, TokXorComEq,

    TokUnknown
} TOKEN_KIND;

//
// A lexed token. Carries its kind, source span, spelling, and -- for an integer
// literal -- the parsed value.
//
class Token {
public:
    TOKEN_KIND  Kind = TokUnknown;
    SRC_LOC     Loc  = 0;        // offset of the first character
    SRC_LOC     End  = 0;        // one past the last character
    std::string Text;           // spelling (Ident/String value, raw int text)
    UINT64      Int  = 0;        // parsed value when Kind == TokInt
    double      Real = 0.0;      // parsed value when Kind == TokFloat

    bool Is (TOKEN_KIND K) CONST { return Kind == K; }
    SRC_RANGE Range () CONST { return SRC_RANGE { Loc, End }; }
};

// Human-readable name for a token kind (used in "expected X" diagnostics).
CHAR8 CONST *TokenName (TOKEN_KIND Kind);

} // namespace Upcl
} // namespace LibCPU

#endif // LIBCPU_UPCL_TOKEN_H
