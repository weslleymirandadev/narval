#pragma once

#include <memory>
#include <string>

#include "frontend/ast/program.hpp"

namespace narval::frontend::interactive {

/**
 * FragmentParser
 *
 * Parses a code fragment (REPL line or notebook cell) into a Program AST.
 *
 * Parsing is kept separate from incremental semantic analysis so the semantic
 * analyzer can focus on session integration and diagnostics.
 */
class FragmentParser {
public:
    std::unique_ptr<::Program> parse(const std::string& source, const std::string& virtual_filename) const;
};

} // namespace narval::frontend::interactive
