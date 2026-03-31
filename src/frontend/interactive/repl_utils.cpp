#include "frontend/interactive/repl_utils.hpp"
#include "frontend/checker/type.hpp"

namespace nv {
namespace repl_utils {

bool is_brace_balanced(const std::string& s) {
    int depth = 0;
    for (char c : s) {
        if (c == '{') depth++;
        else if (c == '}') depth--;
        if (depth < 0) return false;
    }
    return depth == 0;
}

bool ends_with_operator(const std::string& s) {
    if (s.empty()) return false;
    char last = s.back();
    return last == '+' || last == '-' || last == '*' || last == '/' || last == '%'
        || last == '<' || last == '>' || last == '&' || last == '|' || last == '^';
}

std::string sanitize_input(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\n\r");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\n\r");
    return s.substr(a, b - a + 1);
}

bool needs_continuation(const std::string& input) {
    auto t = sanitize_input(input);
    if (t.empty()) return false;
    if (!is_brace_balanced(t)) return true;
    if (ends_with_operator(t)) return true;
    // trailing open paren or unclosed string could be added here
    return false;
}

// Simple formatters (placeholders) — keep minimal to avoid depending on runtime
std::string format_value(llvm::JITTargetAddress addr) {
    // Try to call runtime nv_write if available — otherwise show raw addr
    (void)addr;
    return "<value>";
}

std::string format_type(std::shared_ptr<Type> type) {
    if (!type) return "unknown";
    return type->toString();
}

bool is_valid_identifier(const std::string& s) {
    if (s.empty()) return false;
    if (!std::isalpha(s[0]) && s[0] != '_') return false;
    for (char c : s) {
        if (!std::isalnum(c) && c != '_') return false;
    }
    return true;

}

} // namespace repl_utils
} // namespace nv
