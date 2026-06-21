/** @file
  UPCL source management and a clang-style diagnostic engine.

  SourceManager owns every source buffer (files or in-memory strings) and lays them
  out in a single global location space, clang-style: a SRC_LOC is an opaque global
  offset that the manager decodes back to (file, line, column). One location therefore
  identifies a position unambiguously across many files -- the groundwork for `include`
  and multi-file descriptions, and it means diagnostics need only a SRC_LOC.

  DiagnosticEngine renders diagnostics in the familiar clang form, colourised on a TTY:

      cpu.upcl:12:9: error: expected ';' after register name
          [ #i8 A B ]
                  ^

  Colour is suppressed off a TTY or under NO_COLOR, so piped/batch output stays clean.

  Names follow the house style: behaviour classes are PascalCase; plain scalar/enum
  typedefs are UPPERCASE (and unprefixed -- the Upcl namespace already qualifies them).

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_UPCL_DIAGNOSTIC_H
#define LIBCPU_UPCL_DIAGNOSTIC_H

#include "LibCPU/Base.h"
#include <deque>
#include <string>
#include <vector>
#include <cstdio>

namespace LibCPU {
namespace Upcl {

typedef UINT32 SRC_LOC;                      // global offset across all managed buffers
typedef UINT32 FILE_ID;

inline constexpr FILE_ID InvalidFile = ~(FILE_ID) 0;

typedef struct _SRC_RANGE {
    SRC_LOC Begin;
    SRC_LOC End;                             // half-open
} SRC_RANGE;

typedef enum _SEVERITY {
    SevNote,
    SevWarning,
    SevError
} SEVERITY;

//
// Owns all source buffers in one global location space and maps locations back to
// (file, line, column). Buffers are added once and live for the manager's lifetime.
//
class SourceManager {
public:
    SourceManager () : m_Next (1) {}         // 0 is reserved as an "invalid" location

    // Register an in-memory buffer; returns its file id.
    FILE_ID AddBuffer (std::string Name, std::string Text);
    // Read a file from disk; returns InvalidFile (and fills *pError) on failure.
    FILE_ID LoadFile (std::string CONST &Path, std::string *pError);

    SRC_LOC FileBegin (FILE_ID Id) CONST { return m_Files[Id].Base; }
    SRC_LOC FileEnd (FILE_ID Id) CONST { return m_Files[Id].Base + (SRC_LOC) m_Files[Id].Text.size (); }
    std::string CONST &Name (FILE_ID Id) CONST { return m_Files[Id].Name; }
    std::string CONST &Text (FILE_ID Id) CONST { return m_Files[Id].Text; }
    UINT32 FileCount () CONST { return (UINT32) m_Files.size (); }

    // Decode a global location.
    FILE_ID FileOf (SRC_LOC Loc) CONST;
    void    LineCol (SRC_LOC Loc, UINT32 *pLine, UINT32 *pCol) CONST;
    // The text of the source line containing Loc; *pLineBegin gets the line's location.
    std::string LineText (SRC_LOC Loc, SRC_LOC *pLineBegin) CONST;

private:
    typedef struct _ENTRY {
        std::string         Name;
        std::string         Text;
        SRC_LOC             Base;            // global location of this buffer's first char
        std::vector<UINT32> LineStart;       // local offsets of each line's first char
    } ENTRY;

    // A deque, not a vector, so loading a file (during `include`) never reallocates the
    // existing entries -- an active Lexer holds a pointer into one entry's text.
    std::deque<ENTRY>  m_Files;
    SRC_LOC            m_Next;               // next free base in the global space
};

//
// Accumulates and renders diagnostics over a SourceManager; tracks an error count.
//
class DiagnosticEngine {
public:
    explicit DiagnosticEngine (SourceManager *pSm, std::FILE *pOut = stderr);

    void Report (SEVERITY Sev, SRC_LOC Loc, std::string CONST &Message);
    void Report (SEVERITY Sev, SRC_LOC Loc, SRC_RANGE Range, std::string CONST &Message);
    void Note (SRC_LOC Loc, std::string CONST &Message);

    UINT32 ErrorCount () CONST { return m_Errors; }
    UINT32 WarningCount () CONST { return m_Warnings; }
    bool   HadError () CONST { return m_Errors != 0; }
    void   SetColor (bool On) { m_Color = On; }
    // After this many errors, further diagnostics are suppressed (a clang-style stop, so one
    // mistake does not bury the real one under a cascade). 0 disables the limit.
    void   SetErrorLimit (UINT32 N) { m_ErrorLimit = N; }
    // True once the limit is hit -- the parser can stop early instead of churning on garbage.
    bool   Overflowed () CONST { return m_ErrorLimit != 0 && m_Errors > m_ErrorLimit; }

private:
    void Render (SEVERITY Sev, SRC_LOC Loc, SRC_RANGE Range, bool HasRange, std::string CONST &Message);

    SourceManager *m_pSm;
    std::FILE     *m_pOut;
    bool           m_Color;
    UINT32         m_Errors;
    UINT32         m_Warnings;
    UINT32         m_ErrorLimit = 20;        // clang's default -ferror-limit
};

} // namespace Upcl
} // namespace LibCPU

#endif // LIBCPU_UPCL_DIAGNOSTIC_H
