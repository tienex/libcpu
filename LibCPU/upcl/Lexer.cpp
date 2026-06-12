/** @file  UPCL lexer. See Lexer.h. */

#include "Lexer.h"
#include <cstring>

namespace LibCPU {
namespace Upcl {

CHAR8 CONST *
TokenName (TOKEN_KIND Kind)
{
    switch (Kind) {
    case TokEof:      return "end of file";
    case TokIdent:    return "identifier";
    case TokInt:      return "integer";
    case TokString:   return "string";
    case TokLBrace:   return "'{'";
    case TokRBrace:   return "'}'";
    case TokLParen:   return "'('";
    case TokRParen:   return "')'";
    case TokLBracket: return "'['";
    case TokRBracket: return "']'";
    case TokSemi:     return "';'";
    case TokComma:    return "','";
    case TokColon:    return "':'";
    case TokColonColon: return "'::'";
    case TokAt:       return "'@'";
    case TokHash:     return "'#'";
    case TokDollar:   return "'$'";
    case TokQuestion: return "'?'";
    case TokArrow:    return "'->'";
    case TokDotDot:   return "'..'";
    case TokDot:      return "'.'";
    case TokAssign:   return "'='";
    case TokPlus:     return "'+'";
    case TokMinus:    return "'-'";
    case TokStar:     return "'*'";
    case TokSlash:    return "'/'";
    case TokPercent:  return "'%'";
    case TokAmp:      return "'&'";
    case TokPipe:     return "'|'";
    case TokCaret:    return "'^'";
    case TokTilde:    return "'~'";
    case TokShl:      return "'<<'";
    case TokShr:      return "'>>'";
    case TokEqEq:     return "'=='";
    case TokNotEq:    return "'!='";
    case TokLt:       return "'<'";
    case TokLtEq:     return "'<='";
    case TokGt:       return "'>'";
    case TokGtEq:     return "'>='";
    case TokAndAnd:   return "'&&'";
    case TokOrOr:     return "'||'";
    case TokNot:      return "'!'";
    default:            return "token";
    }
}

Lexer::Lexer (SourceManager *pSm, FILE_ID File, DiagnosticEngine *pDiag)
    : m_pSm (pSm), m_pDiag (pDiag), m_Text (pSm->Text (File)),
      m_Base (pSm->FileBegin (File)), m_Pos (0)
{
}

CHAR8
Lexer::Peek (UINT32 Ahead) CONST
{
    UINT32 P = m_Pos + Ahead;
    return (P < m_Text.size ()) ? m_Text[P] : '\0';
}

CHAR8
Lexer::Advance ()
{
    return (m_Pos < m_Text.size ()) ? m_Text[m_Pos++] : '\0';
}

bool
Lexer::Eat (CHAR8 c)
{
    if (Peek () == c) { m_Pos++; return true; }
    return false;
}

void
Lexer::SkipTrivia ()
{
    for (;;) {
        CHAR8 c = Peek ();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            m_Pos++;
        } else if (c == '/' && Peek (1) == '/') {
            while (Peek () != '\0' && Peek () != '\n') { m_Pos++; }
        } else if (c == '/' && Peek (1) == '*') {
            m_Pos += 2;
            while (Peek () != '\0' && !(Peek () == '*' && Peek (1) == '/')) { m_Pos++; }
            if (Peek () != '\0') { m_Pos += 2; }
        } else {
            break;
        }
    }
}

Token
Lexer::Make (TOKEN_KIND Kind, UINT32 Begin)
{
    Token T;
    T.Kind = Kind;
    T.Loc  = Loc (Begin);
    T.End  = Loc (m_Pos);
    T.Text = m_Text.substr (Begin, m_Pos - Begin);
    T.Int  = 0;
    return T;
}

static bool IsWordStart (CHAR8 c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static bool IsWordCont (CHAR8 c) { return IsWordStart (c) || (c >= '0' && c <= '9'); }
static bool IsDigit (CHAR8 c) { return c >= '0' && c <= '9'; }
static bool IsHex (CHAR8 c) { return IsDigit (c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

Token
Lexer::LexWord (UINT32 Begin)
{
    while (IsWordCont (Peek ())) { m_Pos++; }
    return Make (TokIdent, Begin);
}

Token
Lexer::LexNumber (UINT32 Begin)
{
    UINT64 Value = 0;
    if (Peek () == '0' && (Peek (1) == 'x' || Peek (1) == 'X')) {
        m_Pos += 2;
        while (IsHex (Peek ())) {
            CHAR8 c = Advance ();
            UINT32 D = IsDigit (c) ? (c - '0') : ((c | 0x20) - 'a' + 10);
            Value = Value * 16 + D;
        }
    } else if (Peek () == '0' && (Peek (1) == 'b' || Peek (1) == 'B')) {
        m_Pos += 2;                                  // binary literal (handy for opcode bit patterns)
        while (Peek () == '0' || Peek () == '1') {
            Value = Value * 2 + (UINT32) (Advance () - '0');
        }
    } else {
        while (IsDigit (Peek ())) {
            Value = Value * 10 + (UINT32) (Advance () - '0');
        }
    }
    Token T = Make (TokInt, Begin);
    T.Int = Value;
    return T;
}

Token
Lexer::LexString (UINT32 Begin)
{
    m_Pos++;                                     // opening quote
    std::string Value;
    while (Peek () != '"' && Peek () != '\0') {
        CHAR8 c = Advance ();
        if (c == '\\' && Peek () != '\0') {
            CHAR8 e = Advance ();
            switch (e) {
            case 'n': Value.push_back ('\n'); break;
            case 't': Value.push_back ('\t'); break;
            case 'r': Value.push_back ('\r'); break;
            case '\\': Value.push_back ('\\'); break;
            case '"': Value.push_back ('"'); break;
            default: Value.push_back (e); break;
            }
        } else {
            Value.push_back (c);
        }
    }
    SRC_LOC OpenLoc = Loc (Begin);
    if (Peek () == '"') {
        m_Pos++;
    } else {
        std::string Msg = "unterminated string literal";
        m_pDiag->Report (SevError, OpenLoc, Msg);
    }
    Token T = Make (TokString, Begin);
    T.Text = Value;                              // store the decoded value
    return T;
}

Token
Lexer::Next ()
{
    SkipTrivia ();
    UINT32 Begin = m_Pos;
    CHAR8 c = Peek ();
    if (c == '\0') { return Make (TokEof, Begin); }
    if (IsWordStart (c)) { return LexWord (Begin); }
    if (IsDigit (c))     { return LexNumber (Begin); }
    if (c == '"')        { return LexString (Begin); }

    m_Pos++;                                     // consume the first punctuation char
    switch (c) {
    case '{': return Make (TokLBrace, Begin);
    case '}': return Make (TokRBrace, Begin);
    case '(': return Make (TokLParen, Begin);
    case ')': return Make (TokRParen, Begin);
    case '[': return Make (TokLBracket, Begin);
    case ']': return Make (TokRBracket, Begin);
    case ';': return Make (TokSemi, Begin);
    case ',': return Make (TokComma, Begin);
    case ':': return Eat (':') ? Make (TokColonColon, Begin) : Make (TokColon, Begin);
    case '@': return Make (TokAt, Begin);
    case '#': return Make (TokHash, Begin);
    case '$': return Make (TokDollar, Begin);
    case '?': return Make (TokQuestion, Begin);
    case '+': return Make (TokPlus, Begin);
    case '*': return Make (TokStar, Begin);
    case '/': return Make (TokSlash, Begin);
    case '%': return Make (TokPercent, Begin);
    case '^': return Make (TokCaret, Begin);
    case '~': return Make (TokTilde, Begin);
    case '-': return Eat ('>') ? Make (TokArrow, Begin) : Make (TokMinus, Begin);
    case '.': return Eat ('.') ? Make (TokDotDot, Begin) : Make (TokDot, Begin);
    case '=': return Eat ('=') ? Make (TokEqEq, Begin) : Make (TokAssign, Begin);
    case '!': return Eat ('=') ? Make (TokNotEq, Begin) : Make (TokNot, Begin);
    case '&': return Eat ('&') ? Make (TokAndAnd, Begin) : Make (TokAmp, Begin);
    case '|': return Eat ('|') ? Make (TokOrOr, Begin) : Make (TokPipe, Begin);
    case '<': return Eat ('<') ? Make (TokShl, Begin) : (Eat ('=') ? Make (TokLtEq, Begin) : Make (TokLt, Begin));
    case '>': return Eat ('>') ? Make (TokShr, Begin) : (Eat ('=') ? Make (TokGtEq, Begin) : Make (TokGt, Begin));
    default: {
        std::string Msg = std::string ("unexpected character '") + c + "'";
        m_pDiag->Report (SevError, Loc (Begin), Msg);
        return Make (TokUnknown, Begin);
    }
    }
}

} // namespace Upcl
} // namespace LibCPU
