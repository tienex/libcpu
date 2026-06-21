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
    void ParseToplevel (Module *pModule);        // the top-level declaration loop (reused by include)
    void ParseInclude (Module *pModule);         // `include "<file>";` -- splice another file's decls
    void ParseFeatureBlock (Module *pModule);    // `feature <name> { <insns> }` -- gate a group

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
    void   SyncToTopLevel ();                    // skip to the next top-level declaration keyword

    // grammar
    Arch      *ParseArch ();
    void       ParseArchItem (Arch *pArch);
    void       ParseRegisters (Arch *pArch);
    void       ParseFeatures (Arch *pArch);      // the `features { ... }` block
    void       ParseCpu (Arch *pArch);           // a `cpu "..." { ... }` model

    // old .def register_file: register_file { group <id> { [ <reg_decl> ], ... } ... }
    void        ParseRegisterFile (Arch *pArch);
    Group      *ParseGroup ();
    RegDecl    *ParseRegDecl ();                  // `[ (e **)? <type> <name> ( -> bind | <- alias )? ]`
    RegBinding *ParseRegBinding ();               // after `->` / `<-`
    Splitter   *ParseSplitter ();                 // `[type]? [explicit]? [evaluate(e)]? ( [..] | (..) )`
    BitBind    *ParseTypedValueBind ();           // a union entry: `<type> ( vb | [ vb, ... ] )`
    BitBind    *ParseValueBind ();                // `id ( -> %m | <- src | <-> src | <- (e) )?` | <expr>
    std::string ParseQualifiedName ();            // `id` or `id.field`
    void        ConsumeRepeatTail ();             // a repeatable-id tail: `? ( : <count> )?`
    bool        StartsSplitter () CONST;
    void       ParseFormats (Arch *pArch);      // the `formats { ... }` block
    Insn      *ParseInsnDecl ();                 // optional [attrs] then `insn ...` (new syntax)
    void       ParseAttributes (Insn *pInsn);    // `[ format(..), disasm(..), .. ]`

    // old .def top-level declarations (after the arch block)
    Insn      *ParseOldInsn ();                   // `insn <id> : <stmt> ;` | `insn <id> { body }`
    void       ParseEncodeClause (Insn *pInsn);   // `encode <alt> ( | <alt> )*`
    void       ParseInsnTail (Insn *pInsn);        // post-body clauses: encode / disasm
    void       ParseDisasmFeatures (Arch *pArch);  // `disasm features { ... }`
    void       ParseDisasmStyleBlocks (DisasmFeatures *pF, std::string CONST &Cat); // `style <n> { props }`*
    DisasmSpec *ParseDisasmDecl ();                // `( mnemonic:"..", size:.., operands: a, b )`
    EncAlt    *ParseEncAlt ();                     // `#iN ( <field> (, <field>)* )`
    bool       ParseEncField (EncField *pField);   // `<name> : <width> ( = <const> | -> <operand> )?`
    void       ParseMacro (Arch *pArch);          // `macro <id> ( params ) ...` | `macro disasm ...`
    DisasmMacro *ParseDisasmMacro ();             // `macro disasm <id> ( params ) => <fmt> ;`
    DisasmFmt  *ParseDisasmFmt ();                // a format expression: atom (+ atom)*
    DisasmFmt  *ParseDisasmAtom ();               // literal / $param:directive / @macro(args)
    JumpInsn  *ParseJumpInsn ();                  // `jump insn <id> : type <t> ... { action }`
    void       ParseDecoderOperands (Arch *pArch);// `decoder_operands [ ... ];`
    void       ParseRegSet (Arch *pArch);         // `regset <name> [ <reg>, ... ];`
    void       ParseAddrMode (Arch *pArch);       // `addrmode <name> ( p ) disp(e) { rules }`
    void       ParseAddressDisplay (Arch *pArch); // `address_display segmented shift N offset M;`
    AddrRule  *ParseAddrRule ();                   // `<cond> => reg [ .. ] | mem [ .. ] ;`
    Directive *ParseGenericDirective ();         // the escape hatch
    Stmt      *ParseStmt ();          // insn_stmts:  { } | flow | <basic> ';'
    Stmt      *ParseInlineStmt ();     // inline_insn_stmts:  flow | <basic>   (no trailing ';')
    Stmt      *ParseBasicStmt ();      // an assignment or bare-expression statement (no ';')
    Stmt      *ParseFlow ();           // if / for / while (their bodies are insn_stmts)
    void       ParseBlock (std::vector<Stmt *> *pOut);   // `{ <stmt>* }`

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
    std::string       m_CurFilePath;             // the file being lexed (for relative includes)
};

} // namespace Upcl
} // namespace LibCPU

#endif // LIBCPU_UPCL_PARSER_H
