/** @file  Line editor implementation. See LineEditor.h. */

#include "LineEditor.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

#if defined(__unix__) || defined(__APPLE__)
#  include <unistd.h>
#  include <termios.h>
#  define LCED_POSIX 1
#else
#  define LCED_POSIX 0
#endif

namespace LibCPU {

// ANSI colours.
static CHAR8 CONST *const C_RESET = "\x1b[0m";
static CHAR8 CONST *const C_CMD   = "\x1b[1;32m";   // known command: bold green
static CHAR8 CONST *const C_AMBIG = "\x1b[1;33m";   // ambiguous prefix: bold yellow
static CHAR8 CONST *const C_BAD   = "\x1b[1;31m";   // unknown: bold red
static CHAR8 CONST *const C_NUM   = "\x1b[36m";     // numeric argument: cyan
static CHAR8 CONST *const C_GHOST = "\x1b[2m";      // completion ghost: dim

LcLineEditor::LcLineEditor (std::vector<std::string> Commands)
    : m_Commands (std::move (Commands))
{
    std::sort (m_Commands.begin (), m_Commands.end ());
#if LCED_POSIX
    m_IsTty = isatty (STDIN_FILENO) != 0;
#else
    m_IsTty = false;
#endif
}

std::string
LcLineEditor::FirstToken (std::string CONST &Line) CONST
{
    size_t I = 0;
    while (I < Line.size () && Line[I] == ' ') {
        I++;
    }
    size_t J = I;
    while (J < Line.size () && Line[J] != ' ') {
        J++;
    }
    return Line.substr (I, J - I);
}

// The dimmed completion shown past the cursor: when the command word typed so far is
// a prefix of one or more commands, the remaining text of their longest common
// prefix (so it only ever offers unambiguous characters).
std::string
LcLineEditor::Ghost (std::string CONST &Line) CONST
{
    // Only complete the command word (no space typed yet).
    if (Line.find (' ') != std::string::npos || Line.empty ()) {
        return std::string ();
    }
    std::vector<std::string CONST *> Cand;
    for (std::string CONST &C : m_Commands) {
        if (C.size () > Line.size () && C.compare (0, Line.size (), Line) == 0) {
            Cand.push_back (&C);
        }
    }
    if (Cand.empty ()) {
        return std::string ();
    }
    std::string Common = *Cand[0];
    for (size_t I = 1; I < Cand.size (); I++) {
        size_t K = 0;
        while (K < Common.size () && K < Cand[I]->size () && Common[K] == (*Cand[I])[K]) {
            K++;
        }
        Common.resize (K);
    }
    return Common.size () > Line.size () ? Common.substr (Line.size ()) : std::string ();
}

// Redraw the prompt + buffer (coloured) + ghost, leaving the cursor at Cursor.
void
LcLineEditor::Render (CHAR8 CONST *pPrompt, std::string CONST &Line, size_t Cursor) CONST
{
    std::string Token = FirstToken (Line);
    int Matches = 0;
    bool Exact = false;
    for (std::string CONST &C : m_Commands) {
        if (!Token.empty () && C.size () >= Token.size () && C.compare (0, Token.size (), Token) == 0) {
            Matches++;
            if (C == Token) { Exact = true; }
        }
    }
    CHAR8 CONST *pCmdColour = C_BAD;
    if (Matches == 1 || Exact) { pCmdColour = C_CMD; }
    else if (Matches > 1)      { pCmdColour = C_AMBIG; }

    std::string Out = "\r\x1b[K";                  // carriage return + clear line
    Out += pPrompt;

    // Colour the command word, then the rest (numbers highlighted).
    size_t Lead = 0;
    while (Lead < Line.size () && Line[Lead] == ' ') { Out.push_back (Line[Lead++]); }
    size_t TokEnd = Lead;
    while (TokEnd < Line.size () && Line[TokEnd] != ' ') { TokEnd++; }
    if (TokEnd > Lead) {
        Out += pCmdColour;
        Out += Line.substr (Lead, TokEnd - Lead);
        Out += C_RESET;
    }
    // Arguments: highlight tokens that look numeric.
    size_t I = TokEnd;
    while (I < Line.size ()) {
        if (Line[I] == ' ') { Out.push_back (Line[I++]); continue; }
        size_t J = I;
        while (J < Line.size () && Line[J] != ' ') { J++; }
        std::string Arg = Line.substr (I, J - I);
        bool Numeric = !Arg.empty () && (Arg[0] == '$' || Arg[0] == '*' ||
                       (Arg[0] >= '0' && Arg[0] <= '9'));
        if (Numeric) { Out += C_NUM; Out += Arg; Out += C_RESET; }
        else         { Out += Arg; }
        I = J;
    }

    // Ghost completion (dimmed), shown only at end of line.
    std::string G = Ghost (Line);
    if (!G.empty () && Cursor == Line.size ()) {
        Out += C_GHOST;
        Out += G;
        Out += C_RESET;
    }

    // Reposition the cursor: prompt width + Cursor columns from the line start.
    Out += "\r";
    size_t Col = std::strlen (pPrompt) + Cursor;
    if (Col > 0) {
        char Move[24];
        std::snprintf (Move, sizeof (Move), "\x1b[%zuC", Col);
        Out += Move;
    }
    std::fwrite (Out.data (), 1, Out.size (), stdout);
    std::fflush (stdout);
}

bool
LcLineEditor::ReadLinePlain (std::string &Out)
{
    Out.clear ();
    int c;
    bool Any = false;
    while ((c = std::getchar ()) != EOF) {
        Any = true;
        if (c == '\n') {
            return true;
        }
        Out.push_back ((char) c);
    }
    return Any;   // last line without newline still counts; only EOF-with-nothing ends
}

#if LCED_POSIX

bool
LcLineEditor::ReadLineRaw (CHAR8 CONST *pPrompt, std::string &Out)
{
    struct termios Old;
    if (tcgetattr (STDIN_FILENO, &Old) != 0) {
        std::fputs (pPrompt, stdout);
        std::fflush (stdout);
        return ReadLinePlain (Out);
    }
    struct termios Raw = Old;
    Raw.c_lflag &= ~(ICANON | ECHO);
    Raw.c_cc[VMIN]  = 1;
    Raw.c_cc[VTIME] = 0;
    tcsetattr (STDIN_FILENO, TCSANOW, &Raw);

    std::string Line;
    size_t Cursor = 0;
    int    HistPos = (int) m_History.size ();
    bool   Eof = false;
    Render (pPrompt, Line, Cursor);

    for (;;) {
        int c = std::getchar ();
        if (c == EOF || c == 4) {                       // EOF / Ctrl-D
            if (Line.empty ()) { Eof = true; }
            break;
        }
        if (c == '\r' || c == '\n') {
            break;
        }
        if (c == 3) {                                   // Ctrl-C: cancel the line
            Line.clear ();
            Cursor = 0;
            break;
        }
        if (c == 127 || c == 8) {                       // Backspace
            if (Cursor > 0) {
                Line.erase (Cursor - 1, 1);
                Cursor--;
            }
        } else if (c == '\t') {                          // Tab: accept ghost completion
            std::string G = Ghost (Line);
            if (!G.empty ()) {
                Line.insert (Cursor, G);
                Cursor += G.size ();
            }
        } else if (c == 27) {                            // escape sequence
            int a = std::getchar ();
            int b = std::getchar ();
            if (a == '[') {
                if (b == 'C' && Cursor < Line.size ()) { Cursor++; }            // right
                else if (b == 'D' && Cursor > 0) { Cursor--; }                  // left
                else if (b == 'A') {                                           // up: history
                    if (HistPos > 0) { HistPos--; Line = m_History[HistPos]; Cursor = Line.size (); }
                } else if (b == 'B') {                                         // down: history
                    if (HistPos < (int) m_History.size () - 1) { HistPos++; Line = m_History[HistPos]; Cursor = Line.size (); }
                    else { HistPos = (int) m_History.size (); Line.clear (); Cursor = 0; }
                } else if (b == 'H') { Cursor = 0; }                            // Home
                else if (b == 'F') { Cursor = Line.size (); }                   // End
            }
        } else if (c >= 32 && c < 127) {                 // printable
            Line.insert (Cursor, 1, (char) c);
            Cursor++;
        }
        Render (pPrompt, Line, Cursor);
    }

    tcsetattr (STDIN_FILENO, TCSANOW, &Old);
    std::fputc ('\n', stdout);
    std::fflush (stdout);

    Out = Line;
    if (!Line.empty ()) {
        m_History.push_back (Line);
    }
    return !Eof;
}

#else

bool
LcLineEditor::ReadLineRaw (CHAR8 CONST *pPrompt, std::string &Out)
{
    std::fputs (pPrompt, stdout);
    std::fflush (stdout);
    return ReadLinePlain (Out);
}

#endif

bool
LcLineEditor::ReadLine (CHAR8 CONST *pPrompt, std::string &Out)
{
    if (m_IsTty) {
        return ReadLineRaw (pPrompt, Out);
    }
    return ReadLinePlain (Out);
}

} // namespace LibCPU
