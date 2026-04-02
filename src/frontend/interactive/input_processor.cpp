#include "frontend/interactive/input_processor.hpp"
#include "frontend/interactive/repl.hpp"
#include "frontend/interactive/repl_utils.hpp"
#include <iostream>
#include <algorithm>
#include <cstring>

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#ifdef RETURN
#undef RETURN
#endif
#endif

namespace nv {

InputProcessor::InputProcessor(const REPLConfig& config) : config(config) {}

std::string InputProcessor::read_input(bool in_multiline) {
    std::string line;
#ifdef HAVE_READLINE
    if (config.enable_readline) {
        char* raw_line = readline(in_multiline ? config.multiline_prompt.c_str() : config.prompt.c_str());
        if (!raw_line) return "";
        line = raw_line;
        free(raw_line);
        if (!line.empty()) add_history(line.c_str());
    } else {
#endif
        if (config.show_prompt) {
            std::cout << (in_multiline ? config.multiline_prompt : config.prompt);
            std::cout.flush();
        }
        std::getline(std::cin, line);
#ifdef HAVE_READLINE
    }
#endif
    return line;
}

bool InputProcessor::is_complete_expression(const std::string& input) {
    return repl_utils::is_brace_balanced(input) && 
           !repl_utils::ends_with_operator(input) &&
           !repl_utils::needs_continuation(input);
}

std::string InputProcessor::preprocess_input(const std::string& input) {
    std::string result = repl_utils::sanitize_input(input);
    if (!result.empty()) {
        size_t last = result.find_last_not_of(" \t\n\r");
        if (last != std::string::npos)
            result.erase(last + 1);
        else
            result.clear();
    }

    bool is_function_def = result.find("def ") == 0 || result.find("\ndef ") != std::string::npos;
    bool has_braces = result.find('{') != std::string::npos;
    bool will_auto_print = should_auto_print(result);

    bool looks_like_assignment = false;
    size_t eq_pos = result.find('=');
    if (eq_pos != std::string::npos) {
        if (result.find("==") == std::string::npos && result.find("!=") == std::string::npos
            && result.find("<=") == std::string::npos && result.find(">=") == std::string::npos
            && result.find("=>") == std::string::npos && result.find("->") == std::string::npos) {
            looks_like_assignment = true;
        }
    }

    if (!result.empty() && result.back() != ';' && !is_function_def && !has_braces && (!will_auto_print || looks_like_assignment)) {
        result += ";";
    }

    return result;
}

bool InputProcessor::should_auto_print(const std::string& input) {
    if (!config.auto_print) return false;
    std::string trimmed = input;
    size_t start = trimmed.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return false;
    trimmed = trimmed.substr(start);
    size_t last = trimmed.find_last_not_of(" \t\n\r");
    if (last != std::string::npos) trimmed.erase(last + 1);
    if (trimmed.empty()) return false;
    
    static const char* stmt_prefixes[] = {
        "def ", "let ", "if ", "for ", "while ", "forever ", "match ", "return ", "break ", "continue ", "import "
    };
    for (const char* p : stmt_prefixes) {
        if (trimmed.size() >= strlen(p) && trimmed.compare(0, strlen(p), p) == 0)
            return false;
    }
    size_t semi = trimmed.find(';');
    if (semi != std::string::npos && semi < trimmed.size() - 1) {
        std::string after = trimmed.substr(semi + 1);
        after.erase(0, after.find_first_not_of(" \t\n\r"));
        if (!after.empty()) return false;
    }
    return true;
}

} // namespace nv
