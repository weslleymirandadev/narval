#include "frontend/interactive/command_parser.hpp"
#include "frontend/interactive/repl_utils.hpp"

namespace nv {

bool CommandParser::is_command(const std::string& line) {
    auto t = repl_utils::sanitize_input(line);
    return !t.empty() && t[0] == ':';
}

std::pair<REPLCommand, std::vector<std::string>> CommandParser::parse(const std::string& line) {
    std::string s = repl_utils::sanitize_input(line);
    if (s.empty()) return {REPLCommand::NONE, {}};
    if (s == ":q" || s == ":quit") return {REPLCommand::QUIT, {}};
    if (s == ":h" || s == ":help") return {REPLCommand::HELP, {}};
    if (s.rfind(":load ", 0) == 0) return {REPLCommand::LOAD, {s.substr(6)}};
    if (s == ":vars") return {REPLCommand::VARS, {}};
    if (s == ":reset") return {REPLCommand::RESET, {}};
    if (s == ":history") return {REPLCommand::HISTORY, {}};
    if (s == ":save") return {REPLCommand::SAVE, {}};
    return {REPLCommand::NONE, {}};
}

} // namespace nv
