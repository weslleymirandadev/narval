#pragma once

#include <string>
#include <unordered_set>

class Program;

namespace narval::frontend::interactive {

struct SymbolUsage {
    std::unordered_set<std::string> defined;
    std::unordered_set<std::string> used;
};

SymbolUsage collect_symbol_usage(const Program& program);

} // namespace narval::frontend::interactive
