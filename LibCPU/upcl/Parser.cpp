/** @file  UPCL recursive-descent parser. See Parser.h. */

#include "Parser.h"

namespace LibCPU {
namespace Upcl {

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

// ---- expressions (Pratt) --------------------------------------------------

UINT32
Parser::InfixBp (TOKEN_KIND Kind)
{
    // Higher binds tighter. Add an operator: one row here (and its TOKEN_KIND).
    switch (Kind) {
    case TokOrOr:                                            return 1;
    case TokAndAnd:                                          return 2;
    case TokPipe:                                            return 3;
    case TokCaret:                                           return 4;
    case TokAmp:                                             return 5;
    case TokEqEq: case TokNotEq:                             return 6;
    case TokLt: case TokLtEq: case TokGt: case TokGtEq:      return 7;
    case TokShl: case TokShr:                                return 8;
    case TokPlus: case TokMinus:                             return 9;
    case TokStar: case TokSlash: case TokPercent:            return 10;
    default:                                                 return 0;   // not an infix operator
    }
}

Expr *
Parser::ParsePrimary ()
{
    SRC_LOC Loc = m_Cur.Loc;
    if (m_Cur.Kind == TokInt) {
        Expr *E = new Expr (ExprInt);
        E->Loc = Loc;
        E->Int = m_Cur.Int;
        Advance ();
        return E;
    }
    if (m_Cur.Kind == TokIdent) {
        std::string Name = m_Cur.Text;
        Advance ();
        if (m_Cur.Kind == TokLParen) {              // call: name(args...)
            Advance ();
            Expr *E = new Expr (ExprCall);
            E->Loc = Loc;
            E->Name = Name;
            if (m_Cur.Kind != TokRParen) {
                do {
                    E->Args.push_back (ParseExpr (0));
                } while (Accept (TokComma));
            }
            Expect (TokRParen, "to close the argument list");
            return E;
        }
        Expr *E = new Expr (ExprName);              // bare name (possibly indexed below)
        E->Loc = Loc;
        E->Name = Name;
        if (m_Cur.Kind == TokLBracket) {            // index: name[expr]
            Advance ();
            Expr *Idx = new Expr (ExprIndex);
            Idx->Loc = Loc;
            Idx->Args.push_back (E);
            Idx->Args.push_back (ParseExpr (0));
            Expect (TokRBracket, "to close the index");
            return Idx;
        }
        return E;
    }
    if (m_Cur.Kind == TokLParen) {
        Advance ();
        Expr *E = ParseExpr (0);
        Expect (TokRParen, "to close the parenthesised expression");
        return E;
    }
    std::string Msg = std::string ("expected an expression, found ") + TokenName (m_Cur.Kind);
    m_pDiag->Report (SevError, m_Cur.Loc, m_Cur.Range (), Msg);
    Expr *E = new Expr (ExprInt);                   // a placeholder so callers stay simple
    E->Loc = Loc;
    return E;
}

Expr *
Parser::ParsePrefix ()
{
    TOKEN_KIND K = m_Cur.Kind;
    if (K == TokMinus || K == TokTilde || K == TokNot) {
        SRC_LOC Loc = m_Cur.Loc;
        Advance ();
        Expr *E = new Expr (ExprUnary);
        E->Loc = Loc;
        E->Op = K;
        E->Args.push_back (ParseExpr (11));         // prefix binds tighter than any infix
        return E;
    }
    return ParsePrimary ();
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
        Expr *Rhs = ParseExpr (Bp + 1);             // left-associative
        Expr *Bin = new Expr (ExprBinary);
        Bin->Loc = Loc;
        Bin->Op = Op;
        Bin->Args.push_back (Lhs);
        Bin->Args.push_back (Rhs);
        Lhs = Bin;
    }
    return Lhs;
}

// ---- statements -----------------------------------------------------------

Stmt *
Parser::ParseStmt ()
{
    SRC_LOC Loc = m_Cur.Loc;
    Expr *Lhs = ParseExpr (0);
    Stmt *S = new Stmt (StmtAssign);
    S->Loc = Loc;
    S->Lhs = Lhs;
    if (Expect (TokAssign, "in assignment")) {
        S->Rhs = ParseExpr (0);
    }
    Expect (TokSemi, "after statement");
    return S;
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
        if (!Accept (TokSemi)) { Accept (TokComma); }   // tolerate ';' or ',' between features
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
            if (!Accept (TokSemi)) { Accept (TokComma); }
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
