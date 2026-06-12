/** @file
  A small self-contained line editor for the LibCPU debugger (and, later, lcx).

  When stdin is a terminal it runs in raw mode and offers, as you type:
    * real-time syntax colouring  -- the command word is coloured by validity
      (known / ambiguous-prefix / unknown) and numeric arguments are highlighted;
    * ghost completion            -- the unique remaining text of the command is
      shown dimmed past the cursor; Tab accepts it (or the common prefix);
    * DCL-style reduced commands   -- any unambiguous prefix is accepted, so "s"
      runs "step" and "di" runs "disassemble";
    * history                      -- Up/Down recall previous lines.
  When stdin is NOT a terminal (a piped script) it falls back to plain line input,
  so the same debugger is scriptable for tests and batch use.

  No external dependency: raw mode is POSIX termios, guarded so non-POSIX builds get
  the plain-input path.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_LINEEDITOR_H
#define LIBCPU_LINEEDITOR_H

#include "LibCPU/Base.h"
#include <string>
#include <vector>

namespace LibCPU {

class LcLineEditor {
public:
    // pCommands: the command keywords used for colouring + completion (canonical
    // spellings, e.g. "step", "continue", "disassemble").
    explicit LcLineEditor (std::vector<std::string> Commands);

    // Read one line into Out. Returns false at end of input (EOF / Ctrl-D). The
    // prompt is printed by the editor. Interactive when stdin is a TTY, else plain.
    bool ReadLine (CHAR8 CONST *pPrompt, std::string &Out);

private:
    bool ReadLineRaw (CHAR8 CONST *pPrompt, std::string &Out);
    bool ReadLinePlain (std::string &Out);

    // Editor-state helpers (raw mode only).
    std::string FirstToken (std::string CONST &Line) CONST;
    std::string Ghost (std::string CONST &Line) CONST;          // unique completion remainder
    void        Render (CHAR8 CONST *pPrompt, std::string CONST &Line, size_t Cursor) CONST;

    std::vector<std::string> m_Commands;
    std::vector<std::string> m_History;
    bool                     m_IsTty;
};

} // namespace LibCPU

#endif // LIBCPU_LINEEDITOR_H
