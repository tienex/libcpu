/** @file  UPCL lexer. See Lexer.h. */

#include "Lexer.h"
#include <cstring>
#include <cstdlib>

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
    case TokType:     return "type (#i16/#f80/#v...)";
    case TokMeta:     return "%meta";
    case TokMacroIdent: return "@macro";
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
    case TokBindLeft: return "'<-'";
    case TokBindBidi: return "'<->'";
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
    case TokRol:      return "'<<>'";
    case TokRor:      return "'>><'";
    case TokAndCom:   return "'&~'";
    case TokOrCom:    return "'|~'";
    case TokXorCom:   return "'^~'";
    case TokStarStar: return "'**'";
    case TokPlusEq:   return "'+='";
    case TokMinusEq:  return "'-='";
    case TokStarEq:   return "'*='";
    case TokSlashEq:  return "'/='";
    case TokPercentEq: return "'%='";
    case TokPipeEq:   return "'|='";
    case TokAmpEq:    return "'&='";
    case TokCaretEq:  return "'^='";
    case TokShlEq:    return "'<<='";
    case TokShrEq:    return "'>>='";
    case TokRolEq:    return "'<<>='";
    case TokRorEq:    return "'>><='";
    case TokAndComEq: return "'&~='";
    case TokOrComEq:  return "'|~='";
    case TokXorComEq: return "'^~='";
    default:            return "token";
    }
}

Lexer::Lexer (SourceManager *pSm, FILE_ID File, DiagnosticEngine *pDiag)
    : m_pSm (pSm), m_pDiag (pDiag), m_pText (&pSm->Text (File)),
      m_Base (pSm->FileBegin (File)), m_Pos (0)
{
}

CHAR8
Lexer::Peek (UINT32 Ahead) CONST
{
    UINT32 P = m_Pos + Ahead;
    return (P < m_pText->size ()) ? (*m_pText)[P] : '\0';
}

CHAR8
Lexer::Advance ()
{
    return (m_Pos < m_pText->size ()) ? (*m_pText)[m_Pos++] : '\0';
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
    T.Text = m_pText->substr (Begin, m_Pos - Begin);
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
    } else if (Peek () == '0' && (Peek (1) == 'o' || Peek (1) == 'O')) {
        m_Pos += 2;                                  // `0o...` octal literal (the PDP-10 word convention --
        while (Peek () >= '0' && Peek () <= '7') {   //   octal is how PDP-6/10 opcodes are written)
            Value = Value * 8 + (UINT32) (Advance () - '0');
        }
    } else if (Peek () == '0' && Peek (1) >= '0' && Peek (1) <= '7') {
        m_Pos++;                                      // leading 0 -> octal (old UPCL convention)
        while (Peek () >= '0' && Peek () <= '7') {
            Value = Value * 8 + (UINT32) (Advance () - '0');
        }
    } else {
        while (IsDigit (Peek ())) {
            Value = Value * 10 + (UINT32) (Advance () - '0');
        }
        // A fractional part (.<digit>, distinct from the `..` range token) or an exponent makes this
        // a floating-point literal -- rescan the whole span with strtod.
        if ((Peek () == '.' && IsDigit (Peek (1))) || Peek () == 'e' || Peek () == 'E') {
            if (Peek () == '.') { Advance (); while (IsDigit (Peek ())) { Advance (); } }
            if (Peek () == 'e' || Peek () == 'E') {
                Advance ();
                if (Peek () == '+' || Peek () == '-') { Advance (); }
                while (IsDigit (Peek ())) { Advance (); }
            }
            Token F = Make (TokFloat, Begin);
            F.Real = std::strtod (m_pText->c_str () + Begin, nullptr);
            return F;
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
    case '$': return Make (TokDollar, Begin);
    case '?': return Make (TokQuestion, Begin);
    case '~': return Make (TokTilde, Begin);
    case ':': return Eat (':') ? Make (TokColonColon, Begin) : Make (TokColon, Begin);
    case '.': return Eat ('.') ? Make (TokDotDot, Begin) : Make (TokDot, Begin);
    case '=': return Eat ('=') ? Make (TokEqEq, Begin) : Make (TokAssign, Begin);
    case '!': return Eat ('=') ? Make (TokNotEq, Begin) : Make (TokNot, Begin);
    case '+': return Eat ('=') ? Make (TokPlusEq, Begin) : Make (TokPlus, Begin);
    case '/': return Eat ('=') ? Make (TokSlashEq, Begin) : Make (TokSlash, Begin);
    case '-':
        if (Eat ('>')) { return Make (TokArrow, Begin); }                         // -> bind-right
        return Eat ('=') ? Make (TokMinusEq, Begin) : Make (TokMinus, Begin);
    case '*':
        if (Eat ('*')) { return Make (TokStarStar, Begin); }                      // ** repetition
        return Eat ('=') ? Make (TokStarEq, Begin) : Make (TokStar, Begin);
    case '^':
        if (Eat ('~')) { return Eat ('=') ? Make (TokXorComEq, Begin) : Make (TokXorCom, Begin); }
        return Eat ('=') ? Make (TokCaretEq, Begin) : Make (TokCaret, Begin);
    case '&':
        if (Eat ('&')) { return Make (TokAndAnd, Begin); }
        if (Eat ('~')) { return Eat ('=') ? Make (TokAndComEq, Begin) : Make (TokAndCom, Begin); }
        return Eat ('=') ? Make (TokAmpEq, Begin) : Make (TokAmp, Begin);
    case '|':
        if (Eat ('|')) { return Make (TokOrOr, Begin); }
        if (Eat ('~')) { return Eat ('=') ? Make (TokOrComEq, Begin) : Make (TokOrCom, Begin); }
        return Eat ('=') ? Make (TokPipeEq, Begin) : Make (TokPipe, Begin);
    case '<':
        if (Eat ('-')) { return Eat ('>') ? Make (TokBindBidi, Begin) : Make (TokBindLeft, Begin); }
        if (Eat ('<')) {
            if (Eat ('>')) { return Eat ('=') ? Make (TokRolEq, Begin) : Make (TokRol, Begin); }
            return Eat ('=') ? Make (TokShlEq, Begin) : Make (TokShl, Begin);
        }
        return Eat ('=') ? Make (TokLtEq, Begin) : Make (TokLt, Begin);
    case '>':
        if (Eat ('>')) {
            if (Eat ('<')) { return Eat ('=') ? Make (TokRorEq, Begin) : Make (TokRor, Begin); }
            return Eat ('=') ? Make (TokShrEq, Begin) : Make (TokShr, Begin);
        }
        return Eat ('=') ? Make (TokGtEq, Begin) : Make (TokGt, Begin);
    case '%':
        if (IsWordStart (Peek ())) {                                             // %CC, %S, %M, %PC, ...
            while (IsWordCont (Peek ())) { m_Pos++; }
            Token T = Make (TokMeta, Begin);
            T.Text = T.Text.substr (1);                                          // strip the '%'
            return T;
        }
        return Eat ('=') ? Make (TokPercentEq, Begin) : Make (TokPercent, Begin);
    case '@':
        if (IsWordStart (Peek ())) {                                             // @macro reference
            while (IsWordCont (Peek ())) { m_Pos++; }
            Token T = Make (TokMacroIdent, Begin);
            T.Text = T.Text.substr (1);                                          // strip the '@'
            return T;
        }
        return Make (TokAt, Begin);
    case '#': {
        CHAR8 K = Peek ();
        if ((K == 'i' || K == 'f' || K == 'v') && IsDigit (Peek (1))) {          // #i16 / #f80 / #v4:32 / #i32x4
            m_Pos++;                                                             // the kind letter
            while (IsDigit (Peek ())) { m_Pos++; }
            // Vector lane count, two interchangeable spellings:
            //   #v4:32  -- the legacy `#v` form: ':' followed by a DIGIT is `lanes:width`.
            //   #i32x4  -- the general `xN` form: any kind followed by 'x' then a lane count.
            // The ':digit' guard keeps the legacy form distinct from the alphabetic ':word' suffixes.
            if (K == 'v' && Peek () == ':' && IsDigit (Peek (1))) { m_Pos++; while (IsDigit (Peek ())) { m_Pos++; } }
            else if (Peek () == 'x' && IsDigit (Peek (1))) { m_Pos++; while (IsDigit (Peek ())) { m_Pos++; } }
            // Optional trailing suffixes, each an alphabetic tag after ':': a bit-order (`:lsb` / `:msb`)
            // then a byte-order (`:be` / `:le` / `:me` / `:big` / `:little` / `:mid`). Up to two, in that
            // order; both optional, distinct from the vector-lanes ':digit' form. Folded into the token
            // text -- ParseType decodes which is which. Lets any type literal pin its bit numbering and
            // byte order (a big-endian immediate in a little-endian arch, the PDP-11 middle-endian 32-bit
            // word), not bolted-on keywords.
            for (UINT32 N = 0; N < 2 && Peek () == ':' && IsWordStart (Peek (1)); N++) {
                m_Pos++;
                while (IsWordCont (Peek ())) { m_Pos++; }
            }
            return Make (TokType, Begin);                                        // Text = "#i16" / "#i32x4:msb:be"
        }
        return Make (TokHash, Begin);
    }
    default: {
        std::string Msg = std::string ("unexpected character '") + c + "'";
        m_pDiag->Report (SevError, Loc (Begin), Msg);
        return Make (TokUnknown, Begin);
    }
    }
}

} // namespace Upcl
} // namespace LibCPU
