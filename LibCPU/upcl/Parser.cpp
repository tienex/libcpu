/** @file  UPCL recursive-descent parser. See Parser.h. */

#include "Parser.h"

namespace LibCPU {
namespace Upcl {

// One place builds a StmtAssign (lhs <op>= rhs, optional lhs type) so the statement and
// the for-loop clause paths cannot construct it two slightly-different ways.
static Stmt *
MakeAssign (SRC_LOC Loc, Expr *pLhs, TOKEN_KIND Op, Expr *pRhs, Type *pLhsType)
{
    Stmt *S = new Stmt (StmtAssign);
    S->Loc = Loc; S->Lhs = pLhs; S->AssignOp = Op; S->Rhs = pRhs; S->LhsType = pLhsType;
    return S;
}

Parser::Parser (SourceManager *pSm, FILE_ID File, DiagnosticEngine *pDiag)
    : m_pSm (pSm), m_pDiag (pDiag), m_Lexer (pSm, File, pDiag), m_CurFilePath (pSm->Name (File))
{
    Advance ();
}

void
Parser::Advance ()
{
    m_Cur = m_Lexer.Next ();
}

bool
Parser::AtKeyword (CHAR8 CONST *pWord) CONST
{
    return m_Cur.Kind == TokIdent && m_Cur.Text == pWord;
}

bool
Parser::Accept (TOKEN_KIND Kind)
{
    if (m_Cur.Kind == Kind) { Advance (); return true; }
    return false;
}

bool
Parser::AcceptListSep ()
{
    return Accept (TokSemi) || Accept (TokComma);   // ';' or ',' between list items
}

void
Parser::ErrorAt (SRC_LOC Loc, std::string CONST &Msg)
{
    m_pDiag->Report (SevError, Loc, Msg);
}

bool
Parser::Expect (TOKEN_KIND Kind, CHAR8 CONST *pContext)
{
    if (m_Cur.Kind == Kind) { Advance (); return true; }
    std::string Msg = std::string ("expected ") + TokenName (Kind) + " " + pContext
                      + ", found " + TokenName (m_Cur.Kind);
    m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), Msg);
    return false;
}

bool
Parser::ExpectInt (UINT64 *pOut, CHAR8 CONST *pContext)
{
    if (m_Cur.Kind == TokInt) { *pOut = m_Cur.Int; Advance (); return true; }
    std::string Msg = std::string ("expected an integer ") + pContext
                      + ", found " + TokenName (m_Cur.Kind);
    m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), Msg);
    return false;
}

// Error recovery: consume up to and including the next Kind (or end of file), so a
// single mistake doesn't cascade through the rest of the construct.
void
Parser::SyncTo (TOKEN_KIND Kind)
{
    while (m_Cur.Kind != Kind && m_Cur.Kind != TokEof) {
        Advance ();
    }
    if (m_Cur.Kind == Kind) { Advance (); }
}

// ---- old .def helpers -----------------------------------------------------

// Parse a #i16 / #f80 / #v4:32 type literal (m_Cur is TokType). The spelling carries
// everything; decode the kind letter, the element width, and the vector lane count.
Type *
Parser::ParseType ()
{
    Type *T = new Type ();
    T->Loc = m_Cur.Loc;
    T->Spelling = m_Cur.Text;                       // "#i16"
    std::string CONST &S = T->Spelling;
    if (S.size () >= 2) {
        CHAR8 K = S[1];
        T->Kind = (K == 'f') ? TypeFloat : (K == 'v') ? TypeVector : TypeInt;
        size_t I = 2;
        UINT32 A = 0;
        while (I < S.size () && S[I] >= '0' && S[I] <= '9') { A = A * 10 + (UINT32) (S[I] - '0'); I++; }
        if (T->Kind == TypeVector && I < S.size () && S[I] == ':') {
            T->Lanes = A; I++;
            UINT32 B = 0;
            while (I < S.size () && S[I] >= '0' && S[I] <= '9') { B = B * 10 + (UINT32) (S[I] - '0'); I++; }
            T->Width = B;
        } else {
            T->Width = A;
        }
    }
    Advance ();
    return T;
}

// A compound-assignment token -> the binary op it applies (TokAssign for plain '=', or
// TokUnknown if it is not an assignment operator). The interpreter expands `lhs OP= rhs`
// to `lhs = lhs OP rhs`; a returned TokAssign means a plain store.
TOKEN_KIND
Parser::AssignOpOf (TOKEN_KIND Kind)
{
    switch (Kind) {
    case TokAssign:    return TokAssign;
    case TokPlusEq:    return TokPlus;     case TokMinusEq:   return TokMinus;
    case TokStarEq:    return TokStar;     case TokSlashEq:   return TokSlash;
    case TokPercentEq: return TokPercent;  case TokPipeEq:    return TokPipe;
    case TokAmpEq:     return TokAmp;      case TokCaretEq:   return TokCaret;
    case TokShlEq:     return TokShl;      case TokShrEq:     return TokShr;
    case TokRolEq:     return TokRol;      case TokRorEq:     return TokRor;
    case TokAndComEq:  return TokAndCom;   case TokOrComEq:   return TokOrCom;
    case TokXorComEq:  return TokXorCom;
    default:           return TokUnknown;
    }
}

// ---- expressions (Pratt) --------------------------------------------------

UINT32
Parser::InfixBp (TOKEN_KIND Kind)
{
    // Higher binds tighter. Add an operator: one row here (and its TOKEN_KIND).
    switch (Kind) {
    case TokOrOr:                                            return 1;
    case TokAndAnd:                                          return 2;
    case TokPipe: case TokOrCom:                             return 3;
    case TokCaret: case TokXorCom:                           return 4;
    case TokAmp: case TokAndCom:                             return 5;
    case TokEqEq: case TokNotEq:                             return 6;
    case TokLt: case TokLtEq: case TokGt: case TokGtEq:      return 7;
    case TokShl: case TokShr: case TokRol: case TokRor:      return 8;
    case TokPlus: case TokMinus:                             return 9;
    case TokStar: case TokSlash: case TokPercent:            return 10;
    default:                                                 return 0;   // not an infix operator
    }
}

// The `[expr]` tail of a %M / %MEM reference -- the meta token is already consumed. One
// place builds the memory expression (optionally typed) so the three call sites (a bare
// %M operand, a typed #t %M operand, and a typed %M assignment target) cannot drift apart.
Expr *
Parser::ParseMemRef (SRC_LOC Loc, Type *pVType)
{
    Expr *E = new Expr (ExprMem);
    E->Loc = Loc; E->VType = pVType;
    Expect (TokLBracket, "after %M");
    E->Args.push_back (ParseExpr (0));
    Expect (TokRBracket, "to close %M[...]");
    return E;
}

// %SC[addr] <- value : a store-conditional. The `[addr]` is the target cell, the `<- value` is the
// datum to store if the reservation (set by a prior %LL) is still held; the expression yields the
// 0/1 success bit. The leading %SC (and any width type) is already consumed.
Expr *
Parser::ParseStoreCond (SRC_LOC Loc, Type *pVType)
{
    Expr *E = new Expr (ExprStoreCond);
    E->Loc = Loc; E->VType = pVType;
    Expect (TokLBracket, "after %SC");
    E->Args.push_back (ParseExpr (0));                       // the address
    Expect (TokRBracket, "to close %SC[...]");
    Expect (TokBindLeft, "after %SC[..] -- a store-conditional needs `<- value`");
    E->Args.push_back (ParseExpr (0));                       // the value to store
    return E;
}

// A parenthesised, comma-separated argument list into pCall->Args (the '(' is current).
void
Parser::ParseCallArgs (Expr *pCall)
{
    Expect (TokLParen, "to open the argument list");
    if (m_Cur.Kind != TokRParen) {
        do { pCall->Args.push_back (ParseExpr (0)); } while (Accept (TokComma));
    }
    Expect (TokRParen, "to close the argument list");
}

Expr *
Parser::ParsePrimary ()
{
    SRC_LOC Loc = m_Cur.Loc;

    if (m_Cur.Kind == TokInt) {
        Expr *E = new Expr (ExprInt);
        E->Loc = Loc; E->Int = m_Cur.Int;
        Advance ();
        return E;
    }

    if (m_Cur.Kind == TokFloat) {
        Expr *E = new Expr (ExprFloat);
        E->Loc = Loc; E->Real = m_Cur.Real;
        Advance ();
        return E;
    }

    // %CC(...) / %S(...) / %U(...) / %OFTRAP(...) / %ORD/%UNO / %M[...] / a meta-register.
    if (m_Cur.Kind == TokMeta) {
        std::string Name = m_Cur.Text;              // without the '%'
        Advance ();
        if (Name == "CC") {
            Expr *E = new Expr (ExprCC); E->Loc = Loc;
            Expect (TokLParen, "after %CC");
            E->Args.push_back (ParseExpr (0));
            if (Accept (TokComma)) {
                bool List = Accept (TokLBracket);
                do {
                    bool Neg = Accept (TokNot);
                    if (m_Cur.Kind == TokIdent) { E->CcFlags.push_back (m_Cur.Text); E->CcNeg.push_back (Neg); Advance (); }
                } while (List && Accept (TokComma));
                if (List) { Expect (TokRBracket, "to close the %CC flag list"); }
            }
            Expect (TokRParen, "to close %CC(...)");
            return E;
        }
        if (Name == "S" || Name == "U" || Name == "OFTRAP" || Name == "ORD" || Name == "UNO"
            || Name == "FLT" || Name == "INT") {
            // %FLT / %INT: reinterpret the operand's BITS as float / integer (a bitcast, not a value
            // conversion) at the same width -- e.g. an IEEE single held in a 32-bit GPR.
            Expr *E = new Expr (ExprAugment); E->Loc = Loc; E->Name = Name;
            Expect (TokLParen, "after the augment");
            E->Args.push_back (ParseExpr (0));
            if (Accept (TokComma)) { E->Args.push_back (ParseExpr (0)); }   // %OFTRAP(e, e)
            Expect (TokRParen, "to close the augment");
            return E;
        }
        if (Name == "M" || Name == "MEM") {
            return ParseMemRef (Loc, nullptr);
        }
        if (Name == "LL") {                                 // %LL[addr] -- a load-linked read
            Expr *E = ParseMemRef (Loc, nullptr);
            E->Linked = true;
            return E;
        }
        if (Name == "SC") {                                 // %SC[addr] <- value -- store-conditional
            return ParseStoreCond (Loc, nullptr);
        }
        Expr *E = new Expr (ExprMeta); E->Loc = Loc; E->Name = Name;   // %PC, %V, %result, ...
        return E;
    }

    // @macro(args)
    if (m_Cur.Kind == TokMacroIdent) {
        Expr *E = new Expr (ExprCall); E->Loc = Loc; E->Name = m_Cur.Text;
        Advance ();
        ParseCallArgs (E);
        return E;
    }

    // NB: a leading type (#t %M[..] typed memory, or a #t expr cast) is handled one level up in
    // ParsePrefix, so a type token never reaches here.

    // cast: [ #t expr ]
    if (m_Cur.Kind == TokLBracket) {
        Advance ();
        if (m_Cur.Kind == TokType) {
            Type *T = ParseType ();
            Expr *Inner = ParseExpr (0);
            Expect (TokRBracket, "to close the cast");
            Expr *E = new Expr (ExprCast); E->Loc = Loc; E->VType = T; E->Args.push_back (Inner);
            return E;
        }
        std::string M = "expected a type after '[' in a cast";
        m_pDiag->Report (SevError, Loc, M);
        SyncTo (TokRBracket);
        Expr *E = new Expr (ExprInt); E->Loc = Loc; return E;
    }

    // ( expr )  or  ( a : b : c ) bit-combine
    if (m_Cur.Kind == TokLParen) {
        Advance ();
        Expr *First = ParseExpr (0);
        if (m_Cur.Kind == TokColon) {
            Expr *E = new Expr (ExprBitCombine); E->Loc = Loc; E->Args.push_back (First);
            while (Accept (TokColon)) { E->Args.push_back (ParseExpr (0)); }
            Expect (TokRParen, "to close the bit-combine");
            return E;
        }
        Expect (TokRParen, "to close the parenthesised expression");
        return First;
    }

    // identifier (new-syntax call name(args) kept for compatibility; member/index/slice are postfix)
    if (m_Cur.Kind == TokIdent) {
        std::string Name = m_Cur.Text;
        Advance ();
        if (m_Cur.Kind == TokLParen) {
            Expr *E = new Expr (ExprCall); E->Loc = Loc; E->Name = Name;
            ParseCallArgs (E);
            return E;
        }
        Expr *E = new Expr (ExprName); E->Loc = Loc; E->Name = Name;
        return E;
    }

    std::string Msg = std::string ("expected an expression, found ") + TokenName (m_Cur.Kind);
    m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), Msg);
    Expr *E = new Expr (ExprInt); E->Loc = Loc;     // placeholder so callers stay simple
    return E;
}

// Postfix: member (a.b / a.[m,n]), bit slice (e[a:b] / e[a..b]) or new-syntax index (e[i]),
// and the `is` type test. Binds tighter than any infix operator.
Expr *
Parser::ParsePostfix (Expr *pBase)
{
    for (;;) {
        if (m_Cur.Kind == TokDot) {
            SRC_LOC Loc = m_Cur.Loc; Advance ();
            Expr *M = new Expr (ExprMember); M->Loc = Loc; M->Args.push_back (pBase);
            if (m_Cur.Kind == TokLBracket) {            // a.[m, n]
                Advance ();
                while (m_Cur.Kind == TokIdent) { M->Members.push_back (m_Cur.Text); Advance (); if (!Accept (TokComma)) { break; } }
                Expect (TokRBracket, "to close the member list");
            } else if (m_Cur.Kind == TokIdent) {
                M->Name = m_Cur.Text; Advance ();
            } else {
                std::string Msg = "expected a member name after '.'";
                m_pDiag->Report (SevError, Loc, Msg);
            }
            pBase = M;
        } else if (m_Cur.Kind == TokLBracket) {
            SRC_LOC Loc = m_Cur.Loc; Advance ();
            Expr *A = ParseExpr (0);
            if (m_Cur.Kind == TokColon || m_Cur.Kind == TokDotDot) {   // bit slice e[a:b] / e[a..b]
                bool Incl = (m_Cur.Kind == TokColon); Advance ();
                Expr *B = ParseExpr (0);
                Expect (TokRBracket, "to close the bit slice");
                Expr *S = new Expr (ExprBitSlice); S->Loc = Loc; S->RangeInclusive = Incl;
                S->Args.push_back (pBase); S->Args.push_back (A); S->Args.push_back (B);
                pBase = S;
            } else {                                                   // new-syntax index e[i]
                Expect (TokRBracket, "to close the index");
                Expr *I = new Expr (ExprIndex); I->Loc = Loc; I->Args.push_back (pBase); I->Args.push_back (A);
                pBase = I;
            }
        } else if (AtKeyword ("is")) {
            SRC_LOC Loc = m_Cur.Loc; Advance ();
            Expr *E = new Expr (ExprIs); E->Loc = Loc; E->Args.push_back (pBase);
            if (m_Cur.Kind == TokType) { E->VType = ParseType (); }
            else { std::string Msg = "expected a type after 'is'"; m_pDiag->Report (SevError, Loc, Msg); }
            pBase = E;
        } else {
            break;
        }
    }
    return pBase;
}

Expr *
Parser::ParsePrefix ()
{
    TOKEN_KIND K = m_Cur.Kind;
    if (K == TokMinus || K == TokTilde || K == TokNot || K == TokPlus) {
        SRC_LOC Loc = m_Cur.Loc;
        Advance ();
        if (K == TokPlus) { return ParsePrefix (); }    // unary '+' is a no-op
        Expr *E = new Expr (ExprUnary);
        E->Loc = Loc; E->Op = K;
        E->Args.push_back (ParseExpr (11));             // prefix binds tighter than any infix
        return E;
    }
    // A bare type prefix is either typed memory (#t %M[..]) or a cast of the following expression
    // (#t expr, e.g. #i32 r[rs] or a chained #i64 #i32 r[rs]). The cast binds like a prefix unary
    // operator -- tighter than any infix, so `#i32 a + b` is `(#i32 a) + b`, and chains right.
    if (K == TokType) {
        SRC_LOC Loc = m_Cur.Loc;
        Type   *T   = ParseType ();
        if (m_Cur.Kind == TokMeta && (m_Cur.Text == "M" || m_Cur.Text == "MEM")) {
            Advance ();
            return ParsePostfix (ParseMemRef (Loc, T));     // typed memory reference
        }
        if (m_Cur.Kind == TokMeta && m_Cur.Text == "LL") {  // typed load-linked: #t %LL[addr]
            Advance ();
            Expr *E = ParseMemRef (Loc, T);
            E->Linked = true;
            return ParsePostfix (E);
        }
        if (m_Cur.Kind == TokMeta && m_Cur.Text == "SC") {  // typed store-conditional: #t %SC[addr] <- v
            Advance ();
            return ParseStoreCond (Loc, T);
        }
        Expr *E = new Expr (ExprCast);
        E->Loc = Loc; E->VType = T;
        E->Args.push_back (ParsePrefix ());
        return E;
    }
    return ParsePostfix (ParsePrimary ());
}

Expr *
Parser::ParseExpr (UINT32 MinBp)
{
    Expr *Lhs = ParsePrefix ();
    for (;;) {
        UINT32 Bp = InfixBp (m_Cur.Kind);
        if (Bp == 0 || Bp < MinBp) {
            break;
        }
        TOKEN_KIND Op = m_Cur.Kind;
        SRC_LOC Loc = m_Cur.Loc;
        Advance ();
        Expr *Rhs = ParseExpr (Bp + 1);                 // left-associative
        Expr *Bin = new Expr (ExprBinary);
        Bin->Loc = Loc; Bin->Op = Op;
        Bin->Args.push_back (Lhs);
        Bin->Args.push_back (Rhs);
        Lhs = Bin;
    }
    // ternary select (lowest precedence; right-associative): cond ? then : else
    if (MinBp == 0 && m_Cur.Kind == TokQuestion) {
        Advance ();
        Expr *Then = ParseExpr (0);
        Expect (TokColon, "in a select expression");
        Expr *Else = ParseExpr (0);
        Expr *Sel = new Expr (ExprSelect); Sel->Loc = Lhs->Loc;
        Sel->Args.push_back (Lhs); Sel->Args.push_back (Then); Sel->Args.push_back (Else);
        return Sel;
    }
    return Lhs;
}

// ---- statements -----------------------------------------------------------

// A basic statement WITHOUT its trailing ';': an assignment (`=`, `+=`, `<<=`, ... with an
// optional lhs type) or a bare expression (e.g. %CC(..) / @macro(..)). The caller decides
// whether a ';' follows (statement context) or not (inline context).
Stmt *
Parser::ParseBasicStmt ()
{
    SRC_LOC Loc = m_Cur.Loc;
    Type   *Lt  = nullptr;
    Expr   *Lhs = nullptr;
    if (m_Cur.Kind == TokType) {
        Lt = ParseType ();
        if (m_Cur.Kind == TokMeta && (m_Cur.Text == "M" || m_Cur.Text == "MEM")) {
            Advance ();                              // typed memory lhs: #t %M[expr]
            Lhs = ParsePostfix (ParseMemRef (Loc, Lt));
            Lt = nullptr;
        }
    }
    if (Lhs == nullptr) { Lhs = ParseExpr (0); }    // a typed (Lt set) or plain lhs / expression
    TOKEN_KIND Aop = AssignOpOf (m_Cur.Kind);
    if (Aop != TokUnknown) {
        Advance ();
        return MakeAssign (Loc, Lhs, Aop, ParseExpr (0), Lt);
    }
    delete Lt;
    Stmt *S = new Stmt (StmtExpr);
    S->Loc = Loc; S->Rhs = Lhs;
    return S;
}

// if / for / while. Their bodies are insn_stmts (ParseStmt), so a basic body statement keeps
// its ';'.
Stmt *
Parser::ParseFlow ()
{
    SRC_LOC Loc = m_Cur.Loc;
    if (AtKeyword ("if")) {
        Advance ();
        Stmt *S = new Stmt (StmtIf); S->Loc = Loc;
        Expect (TokLParen, "after 'if'");
        S->Cond = ParseExpr (0);
        Expect (TokRParen, "after the if condition");
        S->Then.push_back (ParseStmt ());
        if (AtKeyword ("else")) { Advance (); S->Else.push_back (ParseStmt ()); }
        return S;
    }
    if (AtKeyword ("while")) {
        Advance ();
        Stmt *S = new Stmt (StmtWhile); S->Loc = Loc;
        Expect (TokLParen, "after 'while'");
        S->Cond = ParseExpr (0);
        Expect (TokRParen, "after the while condition");
        S->Body.push_back (ParseStmt ());
        return S;
    }
    // for
    Advance ();
    Stmt *S = new Stmt (StmtFor); S->Loc = Loc;
    Expect (TokLParen, "after 'for'");
    if (m_Cur.Kind != TokSemi) { do { S->Init.push_back (ParseBasicStmt ()); } while (Accept (TokComma)); }
    Expect (TokSemi, "after the for-loop init");
    if (m_Cur.Kind != TokSemi) { S->Cond = ParseExpr (0); }
    Expect (TokSemi, "after the for-loop condition");
    if (m_Cur.Kind != TokRParen) { do { S->Step.push_back (ParseBasicStmt ()); } while (Accept (TokComma)); }
    Expect (TokRParen, "after the for-loop step");
    S->Body.push_back (ParseStmt ());
    return S;
}

void
Parser::ParseBlock (std::vector<Stmt *> *pOut)
{
    if (!Expect (TokLBrace, "to open the block")) { return; }
    while (m_Cur.Kind != TokRBrace && m_Cur.Kind != TokEof) {
        pOut->push_back (ParseStmt ());
    }
    Expect (TokRBrace, "to close the block");
}

// insn_stmts: a block, a flow statement, or a basic statement terminated by ';'.
Stmt *
Parser::ParseStmt ()
{
    if (m_Cur.Kind == TokLBrace) {
        Stmt *S = new Stmt (StmtBlock); S->Loc = m_Cur.Loc;
        ParseBlock (&S->Body);
        return S;
    }
    if (AtKeyword ("if") || AtKeyword ("while") || AtKeyword ("for")) { return ParseFlow (); }
    Stmt *S = ParseBasicStmt ();
    Expect (TokSemi, "after the statement");
    return S;
}

// inline_insn_stmts: a flow statement, or a basic statement with NO trailing ';' (the inline
// `insn id : <stmt>`, `macro id() : <stmt>`, and `pre <stmt>` contexts).
Stmt *
Parser::ParseInlineStmt ()
{
    if (AtKeyword ("if") || AtKeyword ("while") || AtKeyword ("for")) { return ParseFlow (); }
    return ParseBasicStmt ();
}

// ---- declarations ---------------------------------------------------------

Directive *
Parser::ParseGenericDirective ()
{
    Directive *D = new Directive ();
    D->Keyword = m_Cur.Text;
    D->Loc = m_Cur.Loc;
    Advance ();
    // Slurp a comma-separated list of expressions / a string until ';'.
    while (m_Cur.Kind != TokSemi && m_Cur.Kind != TokEof && m_Cur.Kind != TokRBrace) {
        if (m_Cur.Kind == TokString) {
            D->Str = m_Cur.Text;
            Advance ();
        } else {
            D->Args.push_back (ParseExpr (0));
        }
        if (!Accept (TokComma)) { break; }
    }
    Accept (TokSemi);
    return D;
}

void
Parser::ParseRegisters (Arch *pArch)
{
    Advance ();                                     // 'registers'
    if (!Expect (TokLBrace, "to open the register list")) { return; }
    while (m_Cur.Kind != TokRBrace && m_Cur.Kind != TokEof) {
        if (m_Cur.Kind == TokIdent) {
            Reg R;
            R.Name = m_Cur.Text;
            R.Loc = m_Cur.Loc;
            pArch->Registers.push_back (R);
            Advance ();
        } else {
            std::string Msg = std::string ("expected a register name, found ") + TokenName (m_Cur.Kind);
            m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), Msg);
            Advance ();
        }
        if (!Accept (TokComma)) { break; }
    }
    Expect (TokRBrace, "to close the register list");
}

// features { <name> ("doc")? ; ... }   -- the ISA features a CPU model may include.
// An instruction opts into one with the feature(<name>) attribute; ungated instructions
// are the always-present base ISA.
void
Parser::ParseFeatures (Arch *pArch)
{
    Advance ();                                     // 'features'
    if (!Expect (TokLBrace, "to open the features block")) { return; }
    while (m_Cur.Kind != TokRBrace && m_Cur.Kind != TokEof) {
        if (m_Cur.Kind == TokIdent) {
            Feature F;
            F.Name = m_Cur.Text;
            F.Loc = m_Cur.Loc;
            Advance ();
            if (m_Cur.Kind == TokString) { F.Doc = m_Cur.Text; Advance (); }
            pArch->Features.push_back (F);
        } else {
            std::string M = std::string ("expected a feature name, found ") + TokenName (m_Cur.Kind);
            m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), M);
            Advance ();
        }
        AcceptListSep ();                                   // ';' or ',' between features
    }
    Expect (TokRBrace, "to close the features block");
}

// cpu "<name>" ("doc")? { <feature> ; ... }   -- a named bundle of features. Selecting it
// enables exactly those features.
void
Parser::ParseCpu (Arch *pArch)
{
    Advance ();                                     // 'cpu'
    Cpu *C = new Cpu ();
    C->Loc = m_Cur.Loc;
    if (m_Cur.Kind == TokString) { C->Name = m_Cur.Text; Advance (); }
    else { std::string M = std::string ("expected the CPU model name as a string, found ") + TokenName (m_Cur.Kind);
           m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), M); }
    if (m_Cur.Kind == TokString) { C->Doc = m_Cur.Text; Advance (); }
    if (Expect (TokLBrace, "to open the CPU feature list")) {
        while (m_Cur.Kind != TokRBrace && m_Cur.Kind != TokEof) {
            if (m_Cur.Kind == TokIdent) { C->Features.push_back (m_Cur.Text); Advance (); }
            else { std::string M = std::string ("expected a feature name, found ") + TokenName (m_Cur.Kind);
                   m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), M); Advance (); }
            AcceptListSep ();
        }
        Expect (TokRBrace, "to close the CPU feature list");
    }
    pArch->Cpus.push_back (C);
}

// ---- old .def register_file -----------------------------------------------

// `id` or `id.field` -- the source name of a `<-` / `<->` register-field binding.
std::string
Parser::ParseQualifiedName ()
{
    std::string Name;
    if (m_Cur.Kind == TokIdent) { Name = m_Cur.Text; Advance (); }
    else { std::string M = std::string ("expected a name, found ") + TokenName (m_Cur.Kind);
           m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), M); return Name; }
    if (Accept (TokDot) && m_Cur.Kind == TokIdent) { Name += "."; Name += m_Cur.Text; Advance (); }
    return Name;
}

// A repeatable identifier's tail: an optional `?`, then an optional `: <number>` or
// `: ( <expr> )` start index (e.g. `st?`, `r?:1`, `sp?:( sr.state )`). Returns the literal start
// index (the `: <number>` form, e.g. `r?:1` -> r1..rN) so the register-file builder can number the
// copies from it; the `: ( <expr> )` form is a dynamic window selector and yields a start of 0.
UINT64
Parser::ConsumeRepeatTail ()
{
    if (!Accept (TokQuestion)) { return 0; }
    if (Accept (TokColon)) {
        if (Accept (TokLParen)) { delete ParseExpr (0); Expect (TokRParen, "to close the repeat count"); }
        else { UINT64 V = 0; ExpectInt (&V, "as the start index"); return V; }
    }
    return 0;
}

// Would the current token begin a register splitter (vs an alias)?  A splitter starts with
// a type, `explicit`, `evaluate`, or a union `[`; a plain identifier after `->` is an alias.
bool
Parser::StartsSplitter () CONST
{
    return m_Cur.Kind == TokType || m_Cur.Kind == TokLBracket
           || AtKeyword ("explicit") || AtKeyword ("evaluate");
}

// One field of a splitter: `id` optionally bound (`-> %meta`, `<- src`, `<-> src`, `<- (e)`),
// or a bare expression (a hardwired constant such as the 0s in the flags layout).
BitBind *
Parser::ParseValueBind ()
{
    BitBind *B = new BitBind ();
    B->Loc = m_Cur.Loc;
    // An optional per-field type gives the bind its own width (e.g. `#i3 0 : #i1 C->%C : #i28 0`),
    // so a colon splitter can describe a wide register's fields without enumerating every bit.
    if (m_Cur.Kind == TokType) { B->SubType = ParseType (); }
    if (m_Cur.Kind == TokIdent) {
        B->Name = m_Cur.Text;
        Advance ();
        if (Accept (TokArrow)) {                            // id -> %meta
            if (m_Cur.Kind == TokMeta) { B->MetaMap = m_Cur.Text; Advance (); }
            else { std::string M = "expected a %meta target after '->'"; m_pDiag->Report (SevError, B->Loc, M); }
        } else if (Accept (TokBindLeft)) {                  // id <- src   |   id <- ( expr )
            if (Accept (TokLParen)) { B->HardExpr = ParseExpr (0); Expect (TokRParen, "to close the hardwired value"); }
            else { B->SrcBind = ParseQualifiedName (); ConsumeRepeatTail (); }
        } else if (Accept (TokBindBidi)) {                  // id <-> src   (src may be repeatable)
            B->Bidi = true; B->SrcBind = ParseQualifiedName (); ConsumeRepeatTail ();
        }
        return B;
    }
    Expr *E = ParseExpr (0);                                 // a bare constant / expression
    if (E->Kind == ExprInt) { B->IsConst = true; B->Const = E->Int; delete E; }
    else { B->HardExpr = E; }
    return B;
}

// A union entry: `<type> ( <value-bind> | [ <value-bind>, ... ] )`.
BitBind *
Parser::ParseTypedValueBind ()
{
    BitBind *B = new BitBind ();
    B->Loc = m_Cur.Loc;
    if (m_Cur.Kind == TokType) { B->SubType = ParseType (); }
    if (Accept (TokLBracket)) {                              // type [ vb, vb, ... ]
        if (m_Cur.Kind != TokRBracket) { do { B->Sub.push_back (ParseValueBind ()); } while (Accept (TokComma)); }
        Expect (TokRBracket, "to close the typed value-bind list");
    } else {
        B->Sub.push_back (ParseValueBind ());               // type <single value-bind>
    }
    return B;
}

// [type]? [explicit]? [evaluate(e)]? ( '[' union ']' | '(' field-bind ')' )?
Splitter *
Parser::ParseSplitter ()
{
    Splitter *S = new Splitter ();
    S->Loc = m_Cur.Loc;
    if (m_Cur.Kind == TokType) { S->FieldType = ParseType (); }
    if (AtKeyword ("explicit")) { S->Explicit = true; Advance (); }
    if (AtKeyword ("evaluate")) {
        Advance ();
        Expect (TokLParen, "after 'evaluate'");
        S->Evaluate = ParseExpr (0);
        Expect (TokRParen, "to close 'evaluate(...)'");
    }
    if (Accept (TokLBracket)) {                              // union [ typed-bind, ... ]
        S->Union = true;
        if (m_Cur.Kind != TokRBracket) { do { S->Binds.push_back (ParseTypedValueBind ()); } while (Accept (TokComma)); }
        Expect (TokRBracket, "to close the union");
    } else if (Accept (TokLParen)) {                        // field-bind ( a : b : c )
        if (m_Cur.Kind != TokRParen) { do { S->Binds.push_back (ParseValueBind ()); } while (Accept (TokColon)); }
        Expect (TokRParen, "to close the field bind");
    }
    return S;
}

// The binding after `->` (a meta-register and/or splitter, or a simple `-> id` alias) or
// the `<- expr` alias.
RegBinding *
Parser::ParseRegBinding ()
{
    RegBinding *B = new RegBinding ();
    B->Loc = m_Cur.Loc;
    if (Accept (TokArrow)) {                                 // ->
        if (m_Cur.Kind == TokMeta) {
            B->Meta = m_Cur.Text; Advance ();
            if (StartsSplitter ()) { B->Split = ParseSplitter (); }
        } else if (StartsSplitter ()) {
            B->Split = ParseSplitter ();
        } else if (m_Cur.Kind == TokIdent) {
            B->AliasId = m_Cur.Text; Advance ();
            ConsumeRepeatTail ();                            // a repeatable alias id (sp?:( count ))
        }
    } else if (Accept (TokBindLeft)) {                       // <- expr   (e.g. `zero <- 0`)
        B->AliasExpr = ParseExpr (0);
    }
    return B;
}

// `[ (<expr> **)? <type> <name>(?)? ( -> <binding> | <- <alias> )? ]`
RegDecl *
Parser::ParseRegDecl ()
{
    Expect (TokLBracket, "to open a register declaration");
    RegDecl *R = new RegDecl ();
    R->Loc = m_Cur.Loc;
    if (m_Cur.Kind != TokType) {                            // `<count> ** <type> ...` repetition
        R->RepeatCount = ParseExpr (0);
        Expect (TokStarStar, "after the repetition count");
    }
    if (m_Cur.Kind == TokType) { R->VType = ParseType (); }
    else { std::string M = std::string ("expected a register type, found ") + TokenName (m_Cur.Kind);
           m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), M); }
    if (m_Cur.Kind == TokIdent) {
        R->Name = m_Cur.Text; Advance ();
        if (m_Cur.Kind == TokQuestion) { R->Repeatable = true; R->RepeatStart = ConsumeRepeatTail (); }
    } else { std::string M = std::string ("expected a register name, found ") + TokenName (m_Cur.Kind);
             m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), M); }
    if (m_Cur.Kind == TokArrow || m_Cur.Kind == TokBindLeft) { R->Binding = ParseRegBinding (); }
    Expect (TokRBracket, "to close the register declaration");
    return R;
}

// `group <id> : <reg_decl> ;`  |  `group <id> { <reg_decl> , ... }`
Group *
Parser::ParseGroup ()
{
    Advance ();                                             // 'group'
    Group *G = new Group ();
    if (m_Cur.Kind == TokIdent) { G->Name = m_Cur.Text; G->Name_Loc = m_Cur.Loc; Advance (); }
    else { std::string M = std::string ("expected a group name, found ") + TokenName (m_Cur.Kind);
           m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), M); }
    if (Accept (TokColon)) {                                // simple single-register group
        G->Regs.push_back (ParseRegDecl ());
        Expect (TokSemi, "after a simple group");
    } else if (Accept (TokLBrace)) {                        // brace-delimited register list
        while (m_Cur.Kind != TokRBrace && m_Cur.Kind != TokEof) {
            G->Regs.push_back (ParseRegDecl ());
            if (!Accept (TokComma)) { break; }
        }
        Expect (TokRBrace, "to close the group");
        Accept (TokSemi);                                  // optional trailing ';'
    } else {
        std::string M = std::string ("expected ':' or '{' after the group name, found ") + TokenName (m_Cur.Kind);
        m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), M);
        SyncTo (TokSemi);
    }
    return G;
}

void
Parser::ParseRegisterFile (Arch *pArch)
{
    Advance ();                                             // 'register_file'
    if (!Expect (TokLBrace, "to open register_file")) { return; }
    RegisterFile *RF = new RegisterFile ();
    while (m_Cur.Kind != TokRBrace && m_Cur.Kind != TokEof) {
        if (AtKeyword ("group")) {
            RF->Groups.push_back (ParseGroup ());
            Accept (TokSemi);
        } else {
            std::string M = std::string ("expected 'group' in register_file, found ") + TokenName (m_Cur.Kind);
            m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), M);
            SyncTo (TokSemi);
        }
    }
    Expect (TokRBrace, "to close register_file");
    delete pArch->RegFile;
    pArch->RegFile = RF;
}

// formats { <name> [ <field>:<width> (, ...) ] ; ... }   -- bit layouts, declared once
// and shared by the instructions that reference them.
void
Parser::ParseFormats (Arch *pArch)
{
    Advance ();                                     // 'formats'
    if (!Expect (TokLBrace, "to open the formats block")) { return; }
    while (m_Cur.Kind != TokRBrace && m_Cur.Kind != TokEof) {
        Format *F = new Format ();
        F->Loc = m_Cur.Loc;
        if (m_Cur.Kind == TokIdent) { F->Name = m_Cur.Text; Advance (); }
        else { std::string M = std::string ("expected a format name, found ") + TokenName (m_Cur.Kind);
               m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), M); delete F; SyncTo (TokSemi); continue; }
        Expect (TokLBracket, "to open the field list");
        while (m_Cur.Kind != TokRBracket && m_Cur.Kind != TokEof) {
            FormatField FF;
            if (m_Cur.Kind == TokIdent) { FF.Name = m_Cur.Text; FF.Loc = m_Cur.Loc; Advance (); }
            else { std::string M = std::string ("expected a field name, found ") + TokenName (m_Cur.Kind);
                   m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), M); SyncTo (TokRBracket); break; }
            Expect (TokColon, "between a field name and its width");
            UINT64 W = 0;
            ExpectInt (&W, "as the field width in bits");
            FF.Width = (UINT32) W;
            F->Fields.push_back (FF);
            if (!Accept (TokComma)) { break; }
        }
        Expect (TokRBracket, "to close the field list");
        Expect (TokSemi, "after a format definition");
        pArch->Formats.push_back (F);
    }
    Expect (TokRBrace, "to close the formats block");
}

// [ format(<fmt> :: <field>=<value>, ...), disasm("..."), <other>(...) ]
//
// Attributes carry an instruction's encoding (which format, and the opcode-field
// bindings) and its disassembly, separately from the body. `=` here is a named
// binding inside the attribute -- distinct from assignment in the body.
void
Parser::ParseAttributes (Insn *pInsn)
{
    Advance ();                                     // '['
    while (m_Cur.Kind != TokRBracket && m_Cur.Kind != TokEof) {
        if (m_Cur.Kind != TokIdent) {
            std::string M = std::string ("expected an attribute name, found ") + TokenName (m_Cur.Kind);
            m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), M);
            break;
        }
        std::string Attr = m_Cur.Text;
        SRC_LOC AttrLoc = m_Cur.Loc;
        Advance ();

        if (Attr == "format") {
            Expect (TokLParen, "after 'format'");
            if (m_Cur.Kind == TokIdent) { pInsn->Format = m_Cur.Text; pInsn->FormatLoc = m_Cur.Loc; Advance (); }
            else { std::string M = "expected a format name"; m_pDiag->Report (SevError, m_Cur.Loc, M); }
            if (Accept (TokColonColon)) {            // field bindings: op=val, reg=val, ...
                do {
                    Field *B = new Field ();
                    if (m_Cur.Kind == TokIdent) { B->Name = m_Cur.Text; B->Loc = m_Cur.Loc; Advance (); }
                    else { std::string M = "expected a field name in the binding"; m_pDiag->Report (SevError, m_Cur.Loc, M); }
                    Expect (TokAssign, "in a field binding");
                    B->Value = ParseExpr (0);
                    pInsn->Bindings.push_back (B);
                } while (Accept (TokComma));
            }
            Expect (TokRParen, "to close 'format(...)'");
        } else if (Attr == "disasm") {
            Expect (TokLParen, "after 'disasm'");
            if (m_Cur.Kind == TokString) { pInsn->HasDisasm = true; pInsn->Disasm = m_Cur.Text; Advance (); }
            else { std::string M = std::string ("expected a format string, found ") + TokenName (m_Cur.Kind);
                   m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), M); }
            Expect (TokRParen, "to close 'disasm(...)'");
        } else if (Attr == "feature") {
            Expect (TokLParen, "after 'feature'");
            if (m_Cur.Kind == TokIdent) { pInsn->Feature = m_Cur.Text; pInsn->FeatureLoc = m_Cur.Loc; Advance (); }
            else { std::string M = std::string ("expected a feature name, found ") + TokenName (m_Cur.Kind);
                   m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), M); }
            Expect (TokRParen, "to close 'feature(...)'");
        } else {
            // Unknown attribute -> capture generically (the escape hatch).
            Directive *D = new Directive ();
            D->Keyword = Attr;
            D->Loc = AttrLoc;
            if (Accept (TokLParen)) {
                while (m_Cur.Kind != TokRParen && m_Cur.Kind != TokEof) {
                    if (m_Cur.Kind == TokString) { D->Str = m_Cur.Text; Advance (); }
                    else { D->Args.push_back (ParseExpr (0)); }
                    if (!Accept (TokComma)) { break; }
                }
                Expect (TokRParen, "to close the attribute arguments");
            }
            pInsn->Directives.push_back (D);
        }
        if (!Accept (TokComma)) { break; }
    }
    Expect (TokRBracket, "to close the attribute list");
}

// [attrs]? insn <name> (: <super-insn>)? { <statement>* }
Insn *
Parser::ParseInsnDecl ()
{
    Insn *I = new Insn ();
    if (m_Cur.Kind == TokLBracket) {
        ParseAttributes (I);
    }
    if (!AtKeyword ("insn")) {
        std::string M = std::string ("expected 'insn' after the attribute list, found ") + TokenName (m_Cur.Kind);
        m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), M);
        return I;
    }
    I->Loc = m_Cur.Loc;
    Advance ();                                     // 'insn'
    if (m_Cur.Kind == TokIdent) { I->Name = m_Cur.Text; Advance (); }
    else { std::string M = std::string ("expected an instruction name, found ") + TokenName (m_Cur.Kind);
           m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), M); }
    if (Accept (TokColon)) {                        // `: <super-insn>` -- inheritance
        if (m_Cur.Kind == TokIdent) { I->Super = m_Cur.Text; I->SuperLoc = m_Cur.Loc; Advance (); }
        else { std::string M = std::string ("expected a super-instruction name after ':', found ") + TokenName (m_Cur.Kind);
               m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), M); }
    }
    if (Expect (TokLBrace, "to open the instruction body")) {
        while (m_Cur.Kind != TokRBrace && m_Cur.Kind != TokEof) {
            I->Semantics.push_back (ParseStmt ());
        }
        Expect (TokRBrace, "to close the instruction body");
    }
    return I;
}

void
Parser::ParseArchItem (Arch *pArch)
{
    if (m_Cur.Kind == TokSemi) {                    // a stray separator (e.g. `register_file { } ;`)
        Advance ();
    } else if (AtKeyword ("name")) {
        Advance ();
        if (m_Cur.Kind == TokString) { pArch->FullName = m_Cur.Text; Advance (); }
        else { std::string M = "expected a string after 'name'"; m_pDiag->Report (SevError, m_Cur.Loc, M); }
        Expect (TokSemi, "after name");
    } else if (AtKeyword ("endian") || AtKeyword ("default_endian")) {
        Advance ();
        if (AtKeyword ("little") || AtKeyword ("both")) { pArch->Little = true; Advance (); }
        else if (AtKeyword ("big")) { pArch->Little = false; Advance (); }
        else { std::string M = "expected 'little', 'big' or 'both'"; m_pDiag->Report (SevError, m_Cur.Loc, M); }
        Expect (TokSemi, "after endian");
    } else if (AtKeyword ("word_size")) {
        Advance (); UINT64 V = 0; ExpectInt (&V, "as the word size"); pArch->WordSize = (UINT32) V;
        Expect (TokSemi, "after word_size");
    } else if (AtKeyword ("address_size")) {
        Advance (); UINT64 V = 0; ExpectInt (&V, "as the address size"); pArch->AddressSize = (UINT32) V;
        Expect (TokSemi, "after address_size");
    } else if (AtKeyword ("byte_size")) {
        Advance (); UINT64 V = 0; ExpectInt (&V, "as the byte size"); pArch->ByteSize = (UINT32) V;
        Expect (TokSemi, "after byte_size");
    } else if (AtKeyword ("float_size")) {
        Advance (); UINT64 V = 0; ExpectInt (&V, "as the float size"); pArch->FloatSize = (UINT32) V;
        Expect (TokSemi, "after float_size");
    } else if (AtKeyword ("psr_size")) {
        Advance (); UINT64 V = 0; ExpectInt (&V, "as the psr size"); pArch->PsrSize = (UINT32) V;
        Expect (TokSemi, "after psr_size");
    } else if (AtKeyword ("register_file")) {
        ParseRegisterFile (pArch);
    } else if (AtKeyword ("registers")) {
        ParseRegisters (pArch);
    } else if (AtKeyword ("features")) {
        ParseFeatures (pArch);
    } else if (AtKeyword ("cpu")) {
        ParseCpu (pArch);
    } else if (AtKeyword ("formats")) {
        ParseFormats (pArch);
    } else if (AtKeyword ("insn") || m_Cur.Kind == TokLBracket) {
        pArch->Insns.push_back (ParseInsnDecl ());                  // [attrs]? insn ...
    } else if (m_Cur.Kind == TokIdent) {
        pArch->Directives.push_back (ParseGenericDirective ());     // escape hatch
    } else {
        std::string Msg = std::string ("expected an architecture directive, found ") + TokenName (m_Cur.Kind);
        m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), Msg);
        SyncTo (TokSemi);
    }
}

Arch *
Parser::ParseArch ()
{
    Arch *A = new Arch ();
    A->Loc = m_Cur.Loc;
    Advance ();                                     // 'arch'
    if (m_Cur.Kind == TokString) { A->Name = m_Cur.Text; Advance (); }
    else { std::string M = std::string ("expected the architecture name as a string, found ") + TokenName (m_Cur.Kind);
           m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), M); }
    if (Expect (TokLBrace, "to open the architecture body")) {
        while (m_Cur.Kind != TokRBrace && m_Cur.Kind != TokEof) {
            ParseArchItem (A);
        }
        Expect (TokRBrace, "to close the architecture body");
    }
    return A;
}

// ---- old .def top-level declarations --------------------------------------

// `insn <id> : ;`  |  `insn <id> : <stmt> ;`  |  `insn <id> { <stmt>* }`. ParseStmt already
// consumes the trailing ';' of the inline body, so the inline and block forms share it.
Insn *
Parser::ParseOldInsn ()
{
    Advance ();                                     // 'insn'
    Insn *I = new Insn ();
    I->Loc = m_Cur.Loc;
    if (m_Cur.Kind == TokIdent) { I->Name = m_Cur.Text; Advance (); }
    else { std::string M = std::string ("expected an instruction name, found ") + TokenName (m_Cur.Kind);
           m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), M); }
    if (Accept (TokColon)) {
        // Two shapes after the colon, both supported:
        //   body-first    `insn add : dst = %CC(dst+src) encode ... ;`
        //   clauses-first `insn cld : encode ..., disasm ... => flags.D = 0 ;`  (or { block })
        if (m_Cur.Kind != TokSemi && !AtKeyword ("encode") && !AtKeyword ("disasm")) {
            I->Semantics.push_back (ParseInlineStmt ());
        }
        ParseInsnTail (I);
        if (Accept (TokAssign)) {                   // `=> <inline body>`
            Accept (TokGt);
            if (m_Cur.Kind != TokSemi) { I->Semantics.push_back (ParseInlineStmt ()); }
            Expect (TokSemi, "after the instruction body");
        } else if (m_Cur.Kind == TokLBrace) {       // `{ <block body> }`
            ParseBlock (&I->Semantics);
            Accept (TokSemi);
        } else {
            Expect (TokSemi, "after the instruction");
        }
    } else if (m_Cur.Kind == TokLBrace) {
        ParseBlock (&I->Semantics);
        ParseInsnTail (I);
        Accept (TokSemi);
    }
    return I;
}

// Post-body instruction clauses, in any order with an optional comma between them:
//   `encode <alt> (| <alt>)*`  and  `disasm "<format>"`.
void
Parser::ParseInsnTail (Insn *pInsn)
{
    for (;;) {
        Accept (TokComma);
        if (AtKeyword ("encode")) {
            ParseEncodeClause (pInsn);
        } else if (AtKeyword ("disasm")) {
            Advance ();
            if (m_Cur.Kind == TokLParen) {           // declarative: disasm ( mnemonic:.., .. )
                delete pInsn->DisasmDecl;
                pInsn->DisasmDecl = ParseDisasmDecl ();
            } else if (m_Cur.Kind == TokString) {    // override string: disasm "..."
                pInsn->Disasm = m_Cur.Text; pInsn->HasDisasm = true; Advance ();
            } else {
                m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), "expected a disassembly format string or ( ... )");
            }
        } else {
            break;
        }
    }
}

// `encode <alt> ( | <alt> )*` -- one or more bit-field word patterns for the same insn.
void
Parser::ParseEncodeClause (Insn *pInsn)
{
    Advance ();                                     // 'encode'
    do {
        EncAlt *A = ParseEncAlt ();
        if (A != nullptr) { pInsn->Encodings.push_back (A); }
    } while (Accept (TokPipe));
}

// `#iN ( <field> (, <field>)* )` -- the instruction word and its MSB-first field layout.
EncAlt *
Parser::ParseEncAlt ()
{
    EncAlt *A = new EncAlt ();
    A->Loc = m_Cur.Loc;
    Type *pWord = ParseType ();                     // the #iN word width
    if (pWord != nullptr) { A->WordBits = pWord->Width; delete pWord; }
    else { m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), "an encoding needs a word type, e.g. #i32"); }

    Expect (TokLParen, "to open the encoding field list");
    if (m_Cur.Kind != TokRParen) {
        do {
            EncField F;
            if (ParseEncField (&F)) { A->Fields.push_back (F); }
        } while (Accept (TokComma));
    }
    Expect (TokRParen, "to close the encoding field list");

    // A field that follows an `@addrmode` operand is positioned in the byte tail, after that
    // mode's variable-length displacement -- so it is NOT part of the fixed `#iN` opcode word.
    bool PastAddrMode = false;
    for (EncField &F : A->Fields) {
        if (PastAddrMode) { F.Tail = true; }
        if (!F.AddrMode.empty ()) { PastAddrMode = true; }
    }

    if (A->WordBits != 0 && A->WordFieldBits () != A->WordBits) {
        std::string Msg = "the opcode word's fields total " + std::to_string (A->WordFieldBits ())
                        + " bits but the word is " + std::to_string (A->WordBits);
        m_pDiag->Report (SevError, A->Loc, m_Cur.Range (), Msg);
    }
    return A;
}

// `<name> : <width> ( = <const> | -> <operand> )?` -- one field: a matched constant, an
// operand binding, or a free (reserved) field. A name with no `: width` is an implicit operand
// that occupies no encoding bits: `name = <const>` binds it to a fixed immediate, `name <- <reg>`
// to a fixed register (an x86 shift's by-1 / by-CL count, say).
bool
Parser::ParseEncField (EncField *pField)
{
    if (m_Cur.Kind != TokIdent) {
        m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), "expected a field name");
        return false;
    }
    pField->Name = m_Cur.Text;
    pField->Loc  = m_Cur.Loc;
    Advance ();
    if (m_Cur.Kind != TokColon) {                    // implicit operand: no encoding bits
        pField->Width   = 0;
        pField->Operand = pField->Name;
        if (Accept (TokAssign)) {                    // name = <const>  : a fixed immediate
            UINT64 V = 0;
            ExpectInt (&V, "as the implicit operand value");
            pField->HasImplicitImm = true;
            pField->ImplicitImm = V;
        } else if (Accept (TokBindLeft)) {           // name <- <reg>   : a fixed register
            if (m_Cur.Kind == TokIdent) { pField->RegMap.push_back (m_Cur.Text); Advance (); }
            else { m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), "expected a register name after '<-'"); }
        } else {
            m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (),
                             "expected ': width', '= value', or '<- reg' after a field name");
        }
        return true;
    }
    if (!Expect (TokColon, "after the field name")) { return false; }
    UINT64 W = 0;
    if (!ExpectInt (&W, "as the field width")) { return false; }
    pField->Width = (UINT32) W;

    if (Accept (TokAssign)) {                        // = <const>  : the opcode match
        UINT64 V = 0;
        ExpectInt (&V, "as the field's matched value");
        pField->HasConst = true;
        pField->Const = V;
    } else if (Accept (TokArrow)) {                  // -> <operand>  : bind the field's bits
        if (m_Cur.Kind == TokIdent) { pField->Operand = m_Cur.Text; Advance (); }
        else { m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), "expected an operand name after '->'"); }
        // optional register map: -> op[ r0, r1, ... ]  -- the field value selects a register,
        // so the operand is that register location (else it is the immediate field value).
        if (Accept (TokLBracket)) {
            if (m_Cur.Kind != TokRBracket) {
                do {
                    if (m_Cur.Kind == TokIdent) { pField->RegMap.push_back (m_Cur.Text); Advance (); }
                    else { m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), "expected a register name in the map"); break; }
                } while (Accept (TokComma));
            }
            Expect (TokRBracket, "to close the register map");
        }
        // -> op rel : the field is a PC-relative displacement (a branch target).
        if (AtKeyword ("rel")) { pField->Relative = true; Advance (); }
        // -> op sx : sign-extend the field value to the machine word (a short signed immediate).
        if (AtKeyword ("sx")) { pField->SignExt = true; Advance (); }
        // -> op @<addrmode> : the field selects through an addressing mode.
        if (m_Cur.Kind == TokMacroIdent) { pField->AddrMode = m_Cur.Text; Advance (); }
        else if (Accept (TokAt) && m_Cur.Kind == TokIdent) { pField->AddrMode = m_Cur.Text; Advance (); }
    }
    return true;
}

// `addrmode <name> ( <params> ) [ disp ( <bits-expr> ) ] { <rule>* }`.
void
Parser::ParseAddrMode (Arch *pArch)
{
    Advance ();                                     // 'addrmode'
    AddrMode *A = new AddrMode ();
    A->Loc = m_Cur.Loc;
    if (m_Cur.Kind == TokIdent) { A->Name = m_Cur.Text; Advance (); }
    Expect (TokLParen, "after the addrmode name");
    if (m_Cur.Kind != TokRParen) {
        do { if (m_Cur.Kind == TokIdent) { A->Params.push_back (m_Cur.Text); Advance (); } } while (Accept (TokComma));
    }
    Expect (TokRParen, "to close the addrmode parameters");
    if (AtKeyword ("disp")) {                        // the default displacement width
        Advance ();
        Expect (TokLParen, "after 'disp'");
        A->DispSize = ParseExpr (0);
        Expect (TokRParen, "to close disp(...)");
    }
    Expect (TokLBrace, "to open the addrmode body");
    while (m_Cur.Kind != TokRBrace && m_Cur.Kind != TokEof) {
        AddrRule *R = ParseAddrRule ();
        if (R != nullptr) { A->Rules.push_back (R); }
    }
    Expect (TokRBrace, "to close the addrmode body");
    pArch->AddrModes.push_back (A);
}

// `<cond> => reg [ <regs> ] ;`  or  `<cond> => mem [ <term> (+ <term>)* ] ;`  (`default` = always).
AddrRule *
Parser::ParseAddrRule ()
{
    AddrRule *R = new AddrRule ();
    R->Loc = m_Cur.Loc;
    if (AtKeyword ("default")) { Advance (); }
    else { R->Cond = ParseExpr (0); }
    if (Accept (TokAssign)) { Accept (TokGt); }     // the '=>' arrow
    if (AtKeyword ("reg")) {
        Advance ();
        R->IsReg = true;
        Expect (TokLBracket, "to open the register list");
        if (m_Cur.Kind != TokRBracket) {
            do { if (m_Cur.Kind == TokIdent) { R->RegMap.push_back (m_Cur.Text); Advance (); } } while (Accept (TokComma));
        }
        Expect (TokRBracket, "to close the register list");
    } else if (AtKeyword ("mem")) {
        Advance ();
        Expect (TokLBracket, "to open the address");
        do {
            AddrTerm T;
            if (m_Cur.Kind == TokIdent) {
                std::string Id = m_Cur.Text;
                Advance ();
                if (Id == "disp")        { T.Disp = true; T.DispBits = 0; }
                else if (Id == "disp8")  { T.Disp = true; T.DispBits = 8; }
                else if (Id == "disp16") { T.Disp = true; T.DispBits = 16; }
                else                     { T.Reg = Id; }
                R->Mem.push_back (T);
            }
        } while (Accept (TokPlus));
        Expect (TokRBracket, "to close the address");
    } else {
        m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), "expected 'reg' or 'mem' in an addrmode rule");
    }
    Expect (TokSemi, "after an addrmode rule");
    return R;
}

// `macro <id> ( <params> ) : <stmt> ;`  |  `macro <id> ( <params> ) { body }`.
void
Parser::ParseMacro (Arch *pArch)
{
    Advance ();                                     // 'macro'
    if (AtKeyword ("disasm")) {                     // `macro disasm <id> ( params ) => <fmt> ;`
        Advance ();
        DisasmMacro *D = ParseDisasmMacro ();
        if (D != nullptr) { pArch->DisasmMacros.push_back (D); }
        return;
    }
    Macro *M = new Macro ();
    M->Loc = m_Cur.Loc;
    if (m_Cur.Kind == TokIdent) { M->Name = m_Cur.Text; Advance (); }
    else { std::string Msg = std::string ("expected a macro name, found ") + TokenName (m_Cur.Kind);
           m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), Msg); }
    Expect (TokLParen, "after the macro name");
    if (m_Cur.Kind != TokRParen) {
        do { if (m_Cur.Kind == TokIdent) { M->Params.push_back (m_Cur.Text); Advance (); } } while (Accept (TokComma));
    }
    Expect (TokRParen, "to close the macro parameters");
    if (Accept (TokColon)) {                        // inline body: : <stmt> ;
        if (!Accept (TokSemi)) {
            M->Body.push_back (ParseInlineStmt ());
            Expect (TokSemi, "after the inline macro body");
        }
    } else if (m_Cur.Kind == TokLBrace) {
        ParseBlock (&M->Body);
        Accept (TokSemi);
    }
    pArch->Macros.push_back (M);
}

// `macro disasm <id> ( params ) => <format> ;` ('macro disasm' already consumed).
DisasmMacro *
Parser::ParseDisasmMacro ()
{
    DisasmMacro *D = new DisasmMacro ();
    D->Loc = m_Cur.Loc;
    if (m_Cur.Kind == TokIdent) { D->Name = m_Cur.Text; Advance (); }
    Expect (TokLParen, "after the disasm macro name");
    if (m_Cur.Kind != TokRParen) {
        do { if (m_Cur.Kind == TokIdent) { D->Params.push_back (m_Cur.Text); Advance (); } } while (Accept (TokComma));
    }
    Expect (TokRParen, "to close the parameters");
    if (Accept (TokAssign)) { Accept (TokGt); }     // the '=>' arrow
    D->Body = ParseDisasmFmt ();
    Expect (TokSemi, "after the disasm macro body");
    return D;
}

// A format expression: a conditional `( <expr> ) ? <fmt> : <fmt>`, or one or more atoms
// concatenated with '+'.
DisasmFmt *
Parser::ParseDisasmFmt ()
{
    if (m_Cur.Kind == TokLParen) {                   // ( <cond> ) ? <then> : <else>
        Advance ();
        Expr *Cond = ParseExpr (0);
        Expect (TokRParen, "to close the condition");
        Expect (TokQuestion, "after the condition");
        DisasmFmt *F = new DisasmFmt ();
        F->Kind = DFmtCond;
        F->Cond = Cond;
        F->Kids.push_back (ParseDisasmFmt ());
        Expect (TokColon, "in the conditional format");
        F->Kids.push_back (ParseDisasmFmt ());
        return F;
    }
    DisasmFmt *First = ParseDisasmAtom ();
    if (m_Cur.Kind != TokPlus) { return First; }
    DisasmFmt *C = new DisasmFmt ();
    C->Kind = DFmtConcat;
    C->Kids.push_back (First);
    while (Accept (TokPlus)) { C->Kids.push_back (ParseDisasmAtom ()); }
    return C;
}

// "literal" | $param ( : hex(<width>?) | dec | sdec | upper | lower )? | @macro( args )
DisasmFmt *
Parser::ParseDisasmAtom ()
{
    DisasmFmt *F = new DisasmFmt ();
    if (m_Cur.Kind == TokString) {
        F->Kind = DFmtLit; F->Text = m_Cur.Text; Advance ();
        return F;
    }
    if (m_Cur.Kind == TokMacroIdent) {              // @macro( args )
        F->Kind = DFmtCall; F->Text = m_Cur.Text; Advance ();
        Expect (TokLParen, "after a disasm macro call");
        if (m_Cur.Kind != TokRParen) {
            do { if (m_Cur.Kind == TokIdent) { F->Args.push_back (m_Cur.Text); Advance (); } } while (Accept (TokComma));
        }
        Expect (TokRParen, "to close a disasm macro call");
        return F;
    }
    Accept (TokDollar);                             // optional '$' before a parameter
    std::string Param;
    if (m_Cur.Kind == TokIdent) { Param = m_Cur.Text; Advance (); }
    F->Text = Param;
    F->Kind = DFmtParam;
    if (Accept (TokColon)) {
        std::string Dir;
        if (m_Cur.Kind == TokIdent) { Dir = m_Cur.Text; Advance (); }
        if (Dir == "hex") {
            F->Kind = DFmtHex;
            if (Accept (TokLParen)) {
                if (m_Cur.Kind == TokIdent) { F->WidthParam = m_Cur.Text; Advance (); }
                Expect (TokRParen, "to close hex(...)");
            }
        } else if (Dir == "dec")  { F->Kind = DFmtDec; F->Signed = false; }
        else if (Dir == "sdec")   { F->Kind = DFmtDec; F->Signed = true; }
        else if (Dir == "upper" || Dir == "lower") { F->Kind = DFmtCase; F->Casing = Dir; }
    }
    return F;
}

// `jump insn <id> : <clause> (, <clause>)* { action }` where a clause is, in any order,
//   `type <t>` | `encode <alt> (| <alt>)*` | `condition <e>` | `delay <e>` | `pre {...}`.
JumpInsn *
Parser::ParseJumpInsn ()
{
    Advance ();                                     // 'jump'
    if (AtKeyword ("insn")) { Advance (); }
    else { std::string M = "expected 'insn' after 'jump'"; m_pDiag->Report (SevError, m_Cur.Loc, M); }
    JumpInsn *J = new JumpInsn ();
    J->Loc = m_Cur.Loc;
    if (m_Cur.Kind == TokIdent) { J->Name = m_Cur.Text; Advance (); }
    Expect (TokColon, "after the jump-insn name");
    // The header clauses, in any order, ending at the action block. A comma between clauses
    // is optional (so both `type branch, encode ...` and `type branch condition ...` parse).
    for (;;) {
        Accept (TokComma);
        if (AtKeyword ("type")) {
            Advance ();
            if (m_Cur.Kind == TokIdent) { J->JumpType = m_Cur.Text; Advance (); }
            else { m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), "expected a jump type"); }
        } else if (AtKeyword ("encode")) {
            Advance ();
            do {
                EncAlt *A = ParseEncAlt ();
                if (A != nullptr) { J->Encodings.push_back (A); }
            } while (Accept (TokPipe));
        } else if (AtKeyword ("disasm")) {
            Advance ();
            if (m_Cur.Kind == TokLParen) {
                delete J->DisasmDecl;
                J->DisasmDecl = ParseDisasmDecl ();
            } else if (m_Cur.Kind == TokString) {
                J->Disasm = m_Cur.Text; J->HasDisasm = true; Advance ();
            } else {
                m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), "expected a disassembly format string or ( ... )");
            }
        } else if (AtKeyword ("condition")) {
            Advance (); J->Condition = ParseExpr (0);
        } else if (AtKeyword ("delay")) {
            Advance (); J->Delay = ParseExpr (0);
        } else if (AtKeyword ("pre")) {
            Advance ();
            if (m_Cur.Kind == TokLBrace) { ParseBlock (&J->Pre); } else { J->Pre.push_back (ParseInlineStmt ()); }
        } else {
            break;                                  // the action block (or end)
        }
    }
    ParseBlock (&J->Action);
    Accept (TokSemi);
    return J;
}

// `decoder_operands [ (const|ccflags)? <type> <name>, ... ];`
void
Parser::ParseDecoderOperands (Arch *pArch)
{
    Advance ();                                     // 'decoder_operands'
    if (!Expect (TokLBracket, "after decoder_operands")) { return; }
    while (m_Cur.Kind != TokRBracket && m_Cur.Kind != TokEof) {
        DecoderOperand *D = new DecoderOperand ();
        D->Loc = m_Cur.Loc;
        if (AtKeyword ("const"))        { D->Kind = DecopConst;   Advance (); }
        else if (AtKeyword ("ccflags")) { D->Kind = DecopCcflags; Advance (); }
        if (m_Cur.Kind == TokType) { D->VType = ParseType (); }
        if (m_Cur.Kind == TokIdent) { D->Name = m_Cur.Text; Advance (); }
        pArch->DecoderOps.push_back (D);
        if (!Accept (TokComma)) { break; }
    }
    Expect (TokRBracket, "to close decoder_operands");
    Expect (TokSemi, "after decoder_operands");
}

// `address_display flat;` (the default) or `address_display segmented shift <N> offset <M>;` --
// how a code address is shown. Segmented renders it as seg:off (e.g. x86 real-mode CS:IP): off is
// the low <M> bits, seg is the rest divided by 2^<N>, so seg*2^N + off recovers the linear address.
void
Parser::ParseAddressDisplay (Arch *pArch)
{
    Advance ();                                     // 'address_display'
    if (AtKeyword ("flat")) {
        Advance ();
        pArch->AddrSegShift = 0;
    } else if (AtKeyword ("segmented")) {
        Advance ();
        UINT64 Shift = 0, Off = 0;
        // `shift <N>` and `offset <M>` in either order.
        for (int I = 0; I < 2; I++) {
            if (AtKeyword ("shift"))       { Advance (); ExpectInt (&Shift, "as the segment shift"); }
            else if (AtKeyword ("offset")) { Advance (); ExpectInt (&Off, "as the offset width in bits"); }
            else { break; }
        }
        if (Shift == 0 || Off == 0) {
            m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (),
                             "a segmented address_display needs a non-zero shift and offset width");
        }
        pArch->AddrSegShift = (UINT32) Shift;
        pArch->AddrOffBits  = (UINT32) Off;
    } else {
        m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), "expected 'flat' or 'segmented' after address_display");
    }
    Expect (TokSemi, "after address_display");
}

// `regset <name> [ <reg> (, <reg>)* ];` -- a named register list reused by encodings.
void
Parser::ParseRegSet (Arch *pArch)
{
    Advance ();                                     // 'regset'
    RegSet *R = new RegSet ();
    R->Loc = m_Cur.Loc;
    if (m_Cur.Kind == TokIdent) { R->Name = m_Cur.Text; Advance (); }
    else { m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), "expected a register-set name"); }
    Expect (TokLBracket, "to open the register set");
    if (m_Cur.Kind != TokRBracket) {
        do {
            if (m_Cur.Kind == TokIdent) { R->Regs.push_back (m_Cur.Text); Advance (); }
            else { m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), "expected a register name"); break; }
        } while (Accept (TokComma));
    }
    Expect (TokRBracket, "to close the register set");
    Expect (TokSemi, "after the register set");
    pArch->RegSets.push_back (R);
}

// Skip forward to the next top-level declaration keyword (or EOF), so a stray token does not
// cascade into a diagnostic per following token -- the parser resynchronises at a boundary.
void
Parser::SyncToTopLevel ()
{
    while (m_Cur.Kind != TokEof) {
        if (AtKeyword ("arch") || AtKeyword ("insn") || AtKeyword ("jump") || AtKeyword ("macro")
            || AtKeyword ("regset") || AtKeyword ("decoder_operands") || AtKeyword ("group")
            || AtKeyword ("features") || AtKeyword ("cpu") || AtKeyword ("formats")
            || AtKeyword ("include") || AtKeyword ("disasm") || AtKeyword ("addrmode")
            || AtKeyword ("address_display")) {
            return;
        }
        Advance ();
    }
}

// Apply one property of a disasm style block to the style, by category.
static void
ApplyDisasmProp (DisasmStyle *pSt, std::string CONST &Cat, std::string CONST &Key, std::string CONST &Val)
{
    if (Cat == "mnemonic") {
        if (Key == "casing") { pSt->MnemCasing = Val; }
        else if (Key == "suffix" && Val == "size") { pSt->MnemSizeSuffix = true; }
    } else if (Cat == "register") {
        if (Key == "prefix") { pSt->RegPrefix = Val; }
        else if (Key == "casing") { pSt->RegCasing = Val; }
        else if (Key == "call") { pSt->RegMacro = Val; }
    } else if (Cat == "integer") {
        if (Key == "prefix") { pSt->IntPrefix = Val; }
        else if (Key == "suffix") { pSt->IntSuffix = Val; }
        else if (Key == "call") { pSt->IntMacro = Val; }
    } else if (Cat == "displacement") {
        if (Key == "prefix") { pSt->DispPrefix = Val; }
        else if (Key == "suffix") { pSt->DispSuffix = Val; }
        else if (Key == "call") { pSt->DispMacro = Val; }
    } else if (Cat == "ordering") {
        if (Key == "reverse") { pSt->ReverseOperands = true; }
    }
}

// `{ style <name> { <key> : <val>, ... } ... }` -- the per-style rules for one category
// (mnemonic / register / integer / ordering); each property is applied to its style.
void
Parser::ParseDisasmStyleBlocks (DisasmFeatures *pF, std::string CONST &Cat)
{
    if (!Expect (TokLBrace, "to open a disasm category")) { return; }
    while (AtKeyword ("style")) {
        Advance ();
        std::string SName;
        if (m_Cur.Kind == TokIdent) { SName = m_Cur.Text; Advance (); }
        DisasmStyle *pSt = pF->Find (SName);
        Expect (TokLBrace, "to open a style block");
        while (m_Cur.Kind == TokIdent) {
            std::string Key = m_Cur.Text;
            Advance ();
            std::string Val;
            if (Accept (TokColon) && (m_Cur.Kind == TokString || m_Cur.Kind == TokIdent)) {
                Val = m_Cur.Text;
                Advance ();
            }
            if (pSt != nullptr) { ApplyDisasmProp (pSt, Cat, Key, Val); }
            Accept (TokComma);
        }
        Expect (TokRBrace, "to close a style block");
        Accept (TokComma);
    }
    Expect (TokRBrace, "to close a disasm category");
}

// `disasm features { styles {..} size {..} mnemonic {..} operands [ordering{..}] {register{..}
// integer{..}} }` -- the syntax styles and their rendering rules (see Ast.h).
void
Parser::ParseDisasmFeatures (Arch *pArch)
{
    Advance ();                                     // 'disasm'
    if (AtKeyword ("features")) { Advance (); }
    DisasmFeatures *pF = new DisasmFeatures ();
    Expect (TokLBrace, "to open the disasm features");
    while (m_Cur.Kind != TokRBrace && m_Cur.Kind != TokEof) {
        if (AtKeyword ("styles")) {
            Advance ();
            Expect (TokLBrace, "to open styles");
            while (m_Cur.Kind == TokIdent) {
                DisasmStyle S; S.Name = m_Cur.Text;
                pF->Styles.push_back (m_Cur.Text);
                pF->Rules.push_back (S);
                Advance ();
                if (!Accept (TokComma)) { Accept (TokSemi); }
            }
            Expect (TokRBrace, "to close styles");
        } else if (AtKeyword ("size")) {
            Advance ();
            Expect (TokLBrace, "to open size");
            while (m_Cur.Kind == TokIdent) {
                std::string Name = m_Cur.Text;
                Advance ();
                Expect (TokColon, "in a size entry");
                std::string Code;
                if (m_Cur.Kind == TokString) { Code = m_Cur.Text; Advance (); }
                pF->Sizes[Name] = Code;
                Accept (TokComma);
            }
            Expect (TokRBrace, "to close size");
        } else if (AtKeyword ("mnemonic")) {
            Advance ();
            ParseDisasmStyleBlocks (pF, std::string ("mnemonic"));
        } else if (AtKeyword ("operands")) {
            Advance ();
            if (Accept (TokLBracket)) {              // [ ordering { ... } ]
                while (m_Cur.Kind != TokRBracket && m_Cur.Kind != TokEof) {
                    if (AtKeyword ("ordering")) { Advance (); ParseDisasmStyleBlocks (pF, std::string ("ordering")); }
                    else { Advance (); }
                }
                Expect (TokRBracket, "to close the operand attributes");
            }
            Expect (TokLBrace, "to open operands");
            while (m_Cur.Kind != TokRBrace && m_Cur.Kind != TokEof) {
                if (AtKeyword ("register")) { Advance (); ParseDisasmStyleBlocks (pF, std::string ("register")); }
                else if (AtKeyword ("integer")) { Advance (); ParseDisasmStyleBlocks (pF, std::string ("integer")); }
                else if (AtKeyword ("displacement")) { Advance (); ParseDisasmStyleBlocks (pF, std::string ("displacement")); }
                else { Advance (); }
            }
            Expect (TokRBrace, "to close operands");
        } else {
            Advance ();
        }
        Accept (TokComma);
    }
    Expect (TokRBrace, "to close the disasm features");
    delete pArch->Disasm;
    pArch->Disasm = pF;
}

// `( mnemonic : "mov", size : word, operands : dst, src )` -- the declarative per-insn
// disassembly. `operands` is the last field (it consumes the remaining names).
DisasmSpec *
Parser::ParseDisasmDecl ()
{
    DisasmSpec *D = new DisasmSpec ();
    Expect (TokLParen, "to open a disasm declaration");
    while (m_Cur.Kind != TokRParen && m_Cur.Kind != TokEof) {
        if (m_Cur.Kind != TokIdent) { Advance (); continue; }
        std::string Key = m_Cur.Text;
        Advance ();
        Expect (TokColon, "in a disasm field");
        if (Key == "mnemonic") {
            if (m_Cur.Kind == TokString) { D->Mnemonic = m_Cur.Text; Advance (); }
        } else if (Key == "size") {
            if (m_Cur.Kind == TokIdent) { D->Size = m_Cur.Text; Advance (); }
        } else if (Key == "operands") {
            while (m_Cur.Kind == TokIdent) {
                D->Operands.push_back (m_Cur.Text);
                Advance ();
                if (!Accept (TokComma)) { break; }
            }
        }
        Accept (TokComma);
    }
    Expect (TokRParen, "to close a disasm declaration");
    return D;
}

// Strip the file name from a path, leaving the directory (or "." if none) for resolving a
// relative include against the file that issued it.
static std::string
DirOf (std::string CONST &Path)
{
    std::string::size_type Slash = Path.find_last_of ("/\\");
    return (Slash == std::string::npos) ? std::string (".") : Path.substr (0, Slash);
}

// `include "<file>";` -- load the file and splice its top-level declarations into the same
// module (so they attach to the current arch). The path is relative to the including file.
void
Parser::ParseInclude (Module *pModule)
{
    SRC_LOC Loc = m_Cur.Loc;
    Advance ();                                     // 'include'
    std::string Rel;
    if (m_Cur.Kind == TokString) { Rel = m_Cur.Text; Advance (); }
    else { m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), "expected a quoted file name after 'include'"); }
    Accept (TokSemi);
    if (Rel.empty ()) { return; }

    bool Absolute = (!Rel.empty () && (Rel[0] == '/' || Rel[0] == '\\'));
    std::string Path = Absolute ? Rel : (DirOf (m_CurFilePath) + "/" + Rel);

    std::string Err;
    FILE_ID Fid = m_pSm->LoadFile (Path, &Err);
    if (Fid == InvalidFile) {
        m_pDiag->Report (SevError, Loc, std::string ("cannot include '") + Rel + "': " + Err);
        return;
    }

    // Swap in a lexer over the included file (a deque keeps existing texts stable), parse its
    // declarations into the same module, then restore the outer file.
    Lexer       SavedLexer = m_Lexer;
    Token       SavedCur   = m_Cur;
    std::string SavedPath  = m_CurFilePath;
    m_Lexer       = Lexer (m_pSm, Fid, m_pDiag);
    m_CurFilePath = m_pSm->Name (Fid);
    Advance ();
    ParseToplevel (pModule);
    m_Lexer       = SavedLexer;
    m_Cur         = SavedCur;
    m_CurFilePath = SavedPath;
}

// `feature <name> { <insn> | <jump insn> | <macro> ... }` -- every instruction declared in
// the block is gated on <name>, so a whole ISA extension lives in one block (or one file).
void
Parser::ParseFeatureBlock (Module *M)
{
    Advance ();                                     // 'feature'
    std::string Name;
    if (m_Cur.Kind == TokIdent || m_Cur.Kind == TokString) { Name = m_Cur.Text; Advance (); }
    else { m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), "expected a feature name"); }
    if (!Expect (TokLBrace, "to open the feature block")) { return; }
    Arch *pArch = M->Archs.empty () ? nullptr : M->Archs.back ();
    while (m_Cur.Kind != TokRBrace && m_Cur.Kind != TokEof) {
        if (m_pDiag->Overflowed ()) { break; }
        if (m_Cur.Kind == TokSemi) {
            Advance ();
        } else if (AtKeyword ("insn")) {
            Insn *I = ParseOldInsn ();
            I->Feature = Name;
            if (pArch != nullptr) { pArch->Insns.push_back (I); } else { delete I; }
        } else if (AtKeyword ("jump")) {
            JumpInsn *J = ParseJumpInsn ();
            J->Feature = Name;
            if (pArch != nullptr) { pArch->Jumps.push_back (J); } else { delete J; }
        } else if (AtKeyword ("macro")) {
            if (pArch != nullptr) { ParseMacro (pArch); }
        } else {
            std::string Msg = std::string ("expected an instruction in a feature block, found ") + TokenName (m_Cur.Kind);
            m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), Msg);
            Advance ();
        }
    }
    Expect (TokRBrace, "to close the feature block");
}

Module *
Parser::ParseModule ()
{
    Module *M = new Module ();
    ParseToplevel (M);
    return M;
}

void
Parser::ParseToplevel (Module *M)
{
    while (m_Cur.Kind != TokEof) {
        if (m_pDiag->Overflowed ()) { break; }      // too many errors: stop churning
        // The arch block, then the top-level declarations that attach to it (decoder_operands,
        // macros, instructions, jump instructions, instruction groups -- the old .def order).
        if (m_Cur.Kind == TokSemi) {                // a stray separator (e.g. `arch { } ;`)
            Advance ();
        } else if (AtKeyword ("include")) {
            ParseInclude (M);
        } else if (AtKeyword ("arch")) {
            M->Archs.push_back (ParseArch ());
        } else if (!M->Archs.empty () && AtKeyword ("features")) {
            ParseFeatures (M->Archs.back ());            // declare ISA features
        } else if (!M->Archs.empty () && AtKeyword ("cpu")) {
            ParseCpu (M->Archs.back ());                 // a CPU model bundling features
        } else if (!M->Archs.empty () && AtKeyword ("feature")) {
            ParseFeatureBlock (M);                       // gate a group of instructions
        } else if (M->Archs.empty ()) {
            std::string Msg = std::string ("expected 'arch', found ") + TokenName (m_Cur.Kind);
            m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), Msg);
            Advance ();
        } else if (AtKeyword ("decoder_operands")) {
            ParseDecoderOperands (M->Archs.back ());
        } else if (AtKeyword ("disasm")) {
            ParseDisasmFeatures (M->Archs.back ());      // `disasm features { ... }`
        } else if (AtKeyword ("regset")) {
            ParseRegSet (M->Archs.back ());
        } else if (AtKeyword ("addrmode")) {
            ParseAddrMode (M->Archs.back ());
        } else if (AtKeyword ("address_display")) {
            ParseAddressDisplay (M->Archs.back ());
        } else if (AtKeyword ("macro")) {
            ParseMacro (M->Archs.back ());
        } else if (AtKeyword ("jump")) {
            M->Archs.back ()->Jumps.push_back (ParseJumpInsn ());
        } else if (AtKeyword ("insn")) {
            M->Archs.back ()->Insns.push_back (ParseOldInsn ());
        } else if (AtKeyword ("group")) {
            // `group insn <id> [ ... ] condition <expr> ;` -- an instruction group; the old
            // compiler discards it, so skip to the terminator.
            SyncTo (TokSemi);
        } else {
            std::string Msg = std::string ("expected a top-level declaration, found ") + TokenName (m_Cur.Kind);
            m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), Msg);
            SyncToTopLevel ();                       // resync at a declaration boundary
        }
    }
}

} // namespace Upcl
} // namespace LibCPU
