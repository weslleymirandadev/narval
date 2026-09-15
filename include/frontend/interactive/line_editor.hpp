#pragma once

#include <string>

namespace nv::line_editor {

// Reads one line, re-rendering it with syntax colours as it is typed (readline on POSIX, a
// raw-mode console loop on Windows).
std::string read_line(const std::string& prompt, bool add_to_history);

// Asks the terminal to interpret the ANSI escapes the highlighter and the REPL output use.
// No-op where that is already the case (POSIX); on Windows the console has to be switched
// into VT mode once, otherwise every colour shows up as literal "←[38;2;..." text.
void enable_colored_output();

} // namespace nv::line_editor
