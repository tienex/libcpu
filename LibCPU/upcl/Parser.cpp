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
    : m_pSm (pSm), m_pDiag (pDiag), m_Lexer (pSm, File, pDiag)
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
        if (Name == "S" || Name == "U" || Name == "OFTRAP" || Name == "ORD" || Name == "UNO") {
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

    // typed memory: #t %M[expr]
    if (m_Cur.Kind == TokType) {
        Type *T = ParseType ();
        if (m_Cur.Kind == TokMeta && (m_Cur.Text == "M" || m_Cur.Text == "MEM")) {
            Advance ();
            return ParseMemRef (Loc, T);
        }
        delete T;
        std::string M = "a type in an expression must introduce a memory reference (#t %M[..])";
        m_pDiag->Report (SevError, Loc, M);
        Expr *E = new Expr (ExprInt); E->Loc = Loc; return E;
    }

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

// An assignment / bare-expression statement: `=`, `+=`, `<<=`, ... make it an assignment;
// otherwise the parsed expression is a statement on its own (e.g. %CC(..) / @macro(..)).
Stmt *
Parser::FinishAssignOrExpr (SRC_LOC Loc, Expr *pLhs, Type *pLhsType)
{
    TOKEN_KIND Aop = AssignOpOf (m_Cur.Kind);
    if (Aop != TokUnknown) {
        Advance ();
        Stmt *S = MakeAssign (Loc, pLhs, Aop, ParseExpr (0), pLhsType);
        Expect (TokSemi, "after the assignment");
        return S;
    }
    delete pLhsType;
    Stmt *S = new Stmt (StmtExpr);
    S->Loc = Loc; S->Rhs = pLhs;
    Expect (TokSemi, "after the statement");
    return S;
}

// A for-loop init / step entry: `lhs <op>= rhs` with no trailing ';'.
Stmt *
Parser::ParseSimpleAssign ()
{
    SRC_LOC Loc = m_Cur.Loc;
    Expr *Lhs = ParseExpr (0);
    TOKEN_KIND Aop = AssignOpOf (m_Cur.Kind);
    if (Aop != TokUnknown) { Advance (); }
    else { Expect (TokAssign, "in the for-loop assignment"); Aop = TokAssign; }
    return MakeAssign (Loc, Lhs, Aop, ParseExpr (0), nullptr);
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

Stmt *
Parser::ParseStmt ()
{
    SRC_LOC Loc = m_Cur.Loc;

    if (m_Cur.Kind == TokLBrace) {                  // { block }
        Stmt *S = new Stmt (StmtBlock); S->Loc = Loc;
        ParseBlock (&S->Body);
        return S;
    }
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
    if (AtKeyword ("for")) {
        Advance ();
        Stmt *S = new Stmt (StmtFor); S->Loc = Loc;
        Expect (TokLParen, "after 'for'");
        if (m_Cur.Kind != TokSemi) { do { S->Init.push_back (ParseSimpleAssign ()); } while (Accept (TokComma)); }
        Expect (TokSemi, "after the for-loop init");
        if (m_Cur.Kind != TokSemi) { S->Cond = ParseExpr (0); }
        Expect (TokSemi, "after the for-loop condition");
        if (m_Cur.Kind != TokRParen) { do { S->Step.push_back (ParseSimpleAssign ()); } while (Accept (TokComma)); }
        Expect (TokRParen, "after the for-loop step");
        S->Body.push_back (ParseStmt ());
        return S;
    }

    // basic statement: an optional lhs type, then an assignment or a bare expression.
    if (m_Cur.Kind == TokType) {
        Type *Lt = ParseType ();
        if (m_Cur.Kind == TokMeta && (m_Cur.Text == "M" || m_Cur.Text == "MEM")) {
            Advance ();                              // typed memory lhs: #t %M[expr] = ...
            return FinishAssignOrExpr (Loc, ParsePostfix (ParseMemRef (Loc, Lt)), nullptr);
        }
        Expr *Lhs = ParseExpr (0);                  // typed assignment: #t <lhs> = ...
        return FinishAssignOrExpr (Loc, Lhs, Lt);
    }
    Expr *Lhs = ParseExpr (0);
    return FinishAssignOrExpr (Loc, Lhs, nullptr);
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
    if (AtKeyword ("name")) {
        Advance ();
        if (m_Cur.Kind == TokString) { pArch->FullName = m_Cur.Text; Advance (); }
        else { std::string M = "expected a string after 'name'"; m_pDiag->Report (SevError, m_Cur.Loc, M); }
        Expect (TokSemi, "after name");
    } else if (AtKeyword ("endian")) {
        Advance ();
        if (AtKeyword ("little")) { pArch->Little = true; Advance (); }
        else if (AtKeyword ("big")) { pArch->Little = false; Advance (); }
        else { std::string M = "expected 'little' or 'big'"; m_pDiag->Report (SevError, m_Cur.Loc, M); }
        Expect (TokSemi, "after endian");
    } else if (AtKeyword ("word_size")) {
        Advance (); UINT64 V = 0; ExpectInt (&V, "as the word size"); pArch->WordSize = (UINT32) V;
        Expect (TokSemi, "after word_size");
    } else if (AtKeyword ("address_size")) {
        Advance (); UINT64 V = 0; ExpectInt (&V, "as the address size"); pArch->AddressSize = (UINT32) V;
        Expect (TokSemi, "after address_size");
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

Module *
Parser::ParseModule ()
{
    Module *M = new Module ();
    while (m_Cur.Kind != TokEof) {
        if (AtKeyword ("arch")) {
            M->Archs.push_back (ParseArch ());
        } else {
            std::string Msg = std::string ("expected 'arch', found ") + TokenName (m_Cur.Kind);
            m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), Msg);
            Advance ();
        }
    }
    return M;
}

} // namespace Upcl
} // namespace LibCPU
