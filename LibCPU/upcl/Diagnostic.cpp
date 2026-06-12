/** @file  UPCL source manager + clang-style diagnostic engine. See Diagnostic.h. */

#include "Diagnostic.h"
#include <cstring>
#include <cstdlib>
#if defined(__unix__) || defined(__APPLE__)
#  include <unistd.h>
#endif

namespace LibCPU {
namespace Upcl {

// ANSI colours (matched to clang's defaults).
static CHAR8 CONST *const A_RESET = "\x1b[0m";
static CHAR8 CONST *const A_BOLD  = "\x1b[1m";
static CHAR8 CONST *const A_ERR   = "\x1b[1;31m";   // bold red
static CHAR8 CONST *const A_WARN  = "\x1b[1;35m";   // bold magenta
static CHAR8 CONST *const A_NOTE  = "\x1b[1;36m";   // bold cyan
static CHAR8 CONST *const A_CARET = "\x1b[1;32m";   // green caret / underline

// ---- SourceManager --------------------------------------------------------

FILE_ID
SourceManager::AddBuffer (std::string Name, std::string Text)
{
    ENTRY E;
    E.Name = std::move (Name);
    E.Text = std::move (Text);
    E.Base = m_Next;
    E.LineStart.push_back (0);
    for (UINT32 I = 0; I < E.Text.size (); I++) {
        if (E.Text[I] == '\n') {
            E.LineStart.push_back (I + 1);
        }
    }
    // Reserve [Base, Base + size]; +1 keeps the end-of-file location distinct from
    // the next buffer's start.
    m_Next += (SRC_LOC) E.Text.size () + 1;
    m_Files.push_back (std::move (E));
    return (FILE_ID) (m_Files.size () - 1);
}

FILE_ID
SourceManager::LoadFile (std::string CONST &Path, std::string *pError)
{
    std::FILE *pf = std::fopen (Path.c_str (), "rb");
    if (pf == nullptr) {
        if (pError) { *pError = "cannot open '" + Path + "'"; }
        return InvalidFile;
    }
    std::fseek (pf, 0, SEEK_END);
    long Size = std::ftell (pf);
    std::fseek (pf, 0, SEEK_SET);
    std::string Text;
    if (Size > 0) {
        Text.resize ((size_t) Size);
        size_t Got = std::fread (&Text[0], 1, (size_t) Size, pf);
        Text.resize (Got);
    }
    std::fclose (pf);
    return AddBuffer (Path, std::move (Text));
}

FILE_ID
SourceManager::FileOf (SRC_LOC Loc) CONST
{
    for (UINT32 I = 0; I < m_Files.size (); I++) {
        SRC_LOC Begin = m_Files[I].Base;
        SRC_LOC End   = Begin + (SRC_LOC) m_Files[I].Text.size ();
        if (Loc >= Begin && Loc <= End) {
            return (FILE_ID) I;
        }
    }
    return InvalidFile;
}

void
SourceManager::LineCol (SRC_LOC Loc, UINT32 *pLine, UINT32 *pCol) CONST
{
    FILE_ID Id = FileOf (Loc);
    if (Id == InvalidFile) { *pLine = 0; *pCol = 0; return; }
    ENTRY CONST &E = m_Files[Id];
    UINT32 Local = Loc - E.Base;
    if (Local > E.Text.size ()) { Local = (UINT32) E.Text.size (); }
    UINT32 Lo = 0, Hi = (UINT32) E.LineStart.size () - 1;
    while (Lo < Hi) {
        UINT32 Mid = (Lo + Hi + 1) / 2;
        if (E.LineStart[Mid] <= Local) { Lo = Mid; } else { Hi = Mid - 1; }
    }
    *pLine = Lo + 1;
    *pCol  = Local - E.LineStart[Lo] + 1;
}

std::string
SourceManager::LineText (SRC_LOC Loc, SRC_LOC *pLineBegin) CONST
{
    FILE_ID Id = FileOf (Loc);
    if (Id == InvalidFile) { if (pLineBegin) { *pLineBegin = Loc; } return std::string (); }
    ENTRY CONST &E = m_Files[Id];
    UINT32 Local = Loc - E.Base;
    if (Local > E.Text.size ()) { Local = (UINT32) E.Text.size (); }
    UINT32 Lo = 0, Hi = (UINT32) E.LineStart.size () - 1;
    while (Lo < Hi) {
        UINT32 Mid = (Lo + Hi + 1) / 2;
        if (E.LineStart[Mid] <= Local) { Lo = Mid; } else { Hi = Mid - 1; }
    }
    UINT32 Start = E.LineStart[Lo];
    UINT32 Stop  = Start;
    while (Stop < E.Text.size () && E.Text[Stop] != '\n') {
        Stop++;
    }
    if (pLineBegin) { *pLineBegin = E.Base + Start; }
    return E.Text.substr (Start, Stop - Start);
}

// ---- DiagnosticEngine -----------------------------------------------------

DiagnosticEngine::DiagnosticEngine (SourceManager *pSm, std::FILE *pOut)
    : m_pSm (pSm), m_pOut (pOut), m_Color (false), m_Errors (0), m_Warnings (0)
{
#if defined(__unix__) || defined(__APPLE__)
    bool Tty = isatty (fileno (pOut)) != 0;
    m_Color = Tty && std::getenv ("NO_COLOR") == nullptr;
#endif
}

void
DiagnosticEngine::Report (SEVERITY Sev, SRC_LOC Loc, std::string CONST &Message)
{
    SRC_RANGE R = { Loc, Loc };
    Render (Sev, Loc, R, false, Message);
}

void
DiagnosticEngine::Report (SEVERITY Sev, SRC_LOC Loc, SRC_RANGE Range, std::string CONST &Message)
{
    Render (Sev, Loc, Range, true, Message);
}

void
DiagnosticEngine::Note (SRC_LOC Loc, std::string CONST &Message)
{
    SRC_RANGE R = { Loc, Loc };
    Render (SevNote, Loc, R, false, Message);
}

void
DiagnosticEngine::Render (SEVERITY Sev, SRC_LOC Loc, SRC_RANGE Range, bool HasRange, std::string CONST &Message)
{
    if (Sev == SevError)        { m_Errors++; }
    else if (Sev == SevWarning) { m_Warnings++; }

    UINT32 Line = 0, Col = 0;
    m_pSm->LineCol (Loc, &Line, &Col);
    FILE_ID Id = m_pSm->FileOf (Loc);
    CHAR8 CONST *pName = (Id == InvalidFile) ? "<unknown>" : m_pSm->Name (Id).c_str ();

    CHAR8 CONST *pSevColor = (Sev == SevError) ? A_ERR : (Sev == SevWarning) ? A_WARN : A_NOTE;
    CHAR8 CONST *pBold  = m_Color ? A_BOLD : "";
    CHAR8 CONST *pReset = m_Color ? A_RESET : "";
    CHAR8 CONST *pSevC  = m_Color ? pSevColor : "";
    CHAR8 CONST *pSevText = (Sev == SevError) ? "error" : (Sev == SevWarning) ? "warning" : "note";

    std::fprintf (m_pOut, "%s%s:%u:%u:%s %s%s:%s %s%s%s\n",
                  pBold, pName, Line, Col, pReset,
                  pSevC, pSevText, pReset,
                  pBold, Message.c_str (), pReset);

    SRC_LOC LineBegin = 0;
    std::string Src = m_pSm->LineText (Loc, &LineBegin);
    std::fprintf (m_pOut, "%s\n", Src.c_str ());

    std::string Carets;
    UINT32 CaretCol = (Col > 0) ? Col - 1 : 0;
    for (UINT32 I = 0; I < CaretCol && I < Src.size (); I++) {
        Carets.push_back (Src[I] == '\t' ? '\t' : ' ');
    }
    Carets.push_back ('^');
    if (HasRange && Range.End > Range.Begin + 1) {
        SRC_LOC Stop = Range.End;
        SRC_LOC LineEnd = LineBegin + (SRC_LOC) Src.size ();
        if (Stop > LineEnd) { Stop = LineEnd; }
        for (SRC_LOC P = Range.Begin + 1; P < Stop; P++) {
            Carets.push_back ('~');
        }
    }
    std::fprintf (m_pOut, "%s%s%s\n", m_Color ? A_CARET : "", Carets.c_str (), pReset);
}

} // namespace Upcl
} // namespace LibCPU
