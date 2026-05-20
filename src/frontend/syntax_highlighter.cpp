#include "frontend/syntax_highlighter.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>

namespace nv::syntax_highlighter {
namespace {

constexpr const char* RESET = "\x1b[0m";
constexpr const char* DIM = "\x1b[2m";
constexpr const char* TAB_DISPLAY = "    ";
constexpr const char* COMMENT = "\x1b[38;2;106;153;85m";
constexpr const char* STRING = "\x1b[38;2;206;145;120m";
constexpr const char* ESCAPE = "\x1b[38;2;215;186;125m";
constexpr const char* NUMBER = "\x1b[38;2;181;206;168m";
constexpr const char* KEYWORD = "\x1b[38;2;86;156;214m";
constexpr const char* STORAGE = "\x1b[38;2;86;156;214m";
constexpr const char* TYPE = "\x1b[38;2;78;201;176m";
constexpr const char* FUNCTION = "\x1b[38;2;220;220;170m";
constexpr const char* PARAMETER = "\x1b[38;2;156;220;254m";
constexpr const char* CONSTANT = "\x1b[38;2;86;156;214m";
constexpr const char* OPERATOR = "\x1b[38;2;212;212;212m";
constexpr const char* PUNCTUATION = "\x1b[38;2;212;212;212m";

const std::unordered_set<std::string> KEYWORDS = {
    "if", "else", "elif", "match", "for", "forever", "while", "in",
    "new", "extends", "implements", "instanceof", "return", "break",
    "continue", "yield", "or", "defer", "propagate", "throw", "try",
    "catch", "finally", "err", "from", "import", "as"
};

const std::unordered_set<std::string> STORAGE_WORDS = {
    "async", "await", "public", "private", "protected", "abstract",
    "extern", "mut", "override", "class", "interface", "enum", "def", "prop"
};

const std::unordered_set<std::string> BUILTIN_TYPES = {
    "int", "str", "char", "float", "bool"
};

const std::unordered_set<std::string> BUILTIN_FUNCTIONS = {
    "write", "read", "int", "str", "char", "float", "bool"
};

const std::unordered_set<std::string> LANGUAGE_VALUES = {
    "true", "false", "None", "self", "super"
};

bool is_ident_start(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
}

bool is_ident_char(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

bool is_word_or_dot(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '.';
}

bool starts_with(const std::string& source, size_t pos, const std::string& needle) {
    return pos + needle.size() <= source.size() &&
           source.compare(pos, needle.size(), needle) == 0;
}

size_t skip_spaces(const std::string& source, size_t pos) {
    while (pos < source.size() && (source[pos] == ' ' || source[pos] == '\t')) {
        ++pos;
    }
    return pos;
}

size_t previous_non_space(const std::string& source, size_t pos) {
    while (pos > 0) {
        --pos;
        if (!std::isspace(static_cast<unsigned char>(source[pos]))) {
            return pos;
        }
    }
    return std::string::npos;
}

std::string previous_identifier(const std::string& source, size_t pos) {
    while (pos > 0 && std::isspace(static_cast<unsigned char>(source[pos - 1]))) {
        --pos;
    }
    size_t end = pos;
    while (pos > 0 && is_ident_char(source[pos - 1])) {
        --pos;
    }
    return end > pos ? source.substr(pos, end - pos) : "";
}

std::string color(const char* ansi, const std::string& text) {
    return std::string(ansi) + text + RESET;
}

std::string color_span(const char* ansi, const std::string& source, size_t start, size_t end) {
    return color(ansi, source.substr(start, end - start));
}

size_t consume_number(const std::string& source, size_t pos) {
    const size_t size = source.size();
    if (source[pos] == '-') {
        ++pos;
    }

    if (pos + 1 < size && source[pos] == '0' &&
        (source[pos + 1] == 'x' || source[pos + 1] == 'X' ||
         source[pos + 1] == 'b' || source[pos + 1] == 'B' ||
         source[pos + 1] == 'o' || source[pos + 1] == 'O')) {
        pos += 2;
        while (pos < size && (std::isxdigit(static_cast<unsigned char>(source[pos])) || source[pos] == '_')) {
            ++pos;
        }
        return pos;
    }

    while (pos < size && (std::isdigit(static_cast<unsigned char>(source[pos])) || source[pos] == '_')) {
        ++pos;
    }
    if (pos < size && source[pos] == '.' && pos + 1 < size &&
        std::isdigit(static_cast<unsigned char>(source[pos + 1]))) {
        ++pos;
        while (pos < size && (std::isdigit(static_cast<unsigned char>(source[pos])) || source[pos] == '_')) {
            ++pos;
        }
    }
    if (pos < size && (source[pos] == 'e' || source[pos] == 'E')) {
        size_t exp = pos + 1;
        if (exp < size && (source[exp] == '+' || source[exp] == '-')) {
            ++exp;
        }
        if (exp < size && std::isdigit(static_cast<unsigned char>(source[exp]))) {
            pos = exp + 1;
            while (pos < size && (std::isdigit(static_cast<unsigned char>(source[pos])) || source[pos] == '_')) {
                ++pos;
            }
        }
    }
    return pos;
}

size_t consume_operator(const std::string& source, size_t pos) {
    static const std::string ops[] = {
        "<<=", ">>=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
        "==", "!=", "<=", ">=", "&&", "||", "**", "//", "++", "--",
        "..=", "...", "..", "=>", "->"
    };
    for (const auto& op : ops) {
        if (starts_with(source, pos, op)) {
            return pos + op.size();
        }
    }
    return pos + 1;
}

bool is_operator_char(char ch) {
    const std::string chars = "+-*/%=&|^!<>.~@";
    return chars.find(ch) != std::string::npos;
}

bool is_punctuation(char ch) {
    const std::string chars = "();:,[]{}";
    return chars.find(ch) != std::string::npos;
}

std::string highlight_string(const std::string& source, size_t& pos) {
    const char quote = source[pos];
    const size_t start = pos++;
    std::string out;
    out += STRING;
    out += quote;

    bool escaped = false;
    while (pos < source.size()) {
        char ch = source[pos++];
        if (escaped) {
            out += RESET;
            out += ESCAPE;
            out += '\\';
            out += ch;
            out += RESET;
            out += STRING;
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        out += ch;
        if (ch == quote || (ch == '\n' && quote != '`')) {
            break;
        }
    }
    if (escaped) {
        out += '\\';
    }
    out += RESET;

    if (pos == start + 1) {
        return color_span(STRING, source, start, pos);
    }
    return out;
}

bool looks_like_parameter(const std::string& source, size_t start, size_t end) {
    size_t next = skip_spaces(source, end);
    if (next >= source.size() || source[next] != ':') {
        return false;
    }

    size_t prev = previous_non_space(source, start);
    if (prev == std::string::npos) {
        return false;
    }
    return source[prev] == '(' || source[prev] == '|' || source[prev] == ',';
}

bool looks_like_declared_variable(const std::string& source, size_t end) {
    size_t next = skip_spaces(source, end);
    if (next >= source.size() || source[next] != ':') {
        return false;
    }
    next = skip_spaces(source, next + 1);
    while (next < source.size() && is_ident_char(source[next])) {
        ++next;
    }
    next = skip_spaces(source, next);
    while (next < source.size() && source[next] == '[') {
        ++next;
        while (next < source.size() && std::isdigit(static_cast<unsigned char>(source[next]))) {
            ++next;
        }
        if (next < source.size() && source[next] == ']') {
            ++next;
        }
        next = skip_spaces(source, next);
    }
    return next < source.size() && source[next] == '=';
}

bool follows_inheritance_keyword(const std::string& source, size_t start) {
    std::string prev = previous_identifier(source, start);
    if (prev == "extends" || prev == "implements") {
        return true;
    }

    size_t cursor = start;
    while (cursor > 0 && std::isspace(static_cast<unsigned char>(source[cursor - 1]))) {
        --cursor;
    }
    if (cursor == 0 || source[cursor - 1] != ',') {
        return false;
    }

    --cursor;
    while (cursor > 0) {
        std::string word = previous_identifier(source, cursor);
        if (word.empty()) {
            --cursor;
            continue;
        }
        if (word == "implements" || word == "extends") {
            return true;
        }
        if (word == "class" || word == "interface" || word == "enum" || word == "def") {
            return false;
        }
        cursor = source.rfind(word, cursor);
        if (cursor == std::string::npos || cursor == 0) {
            return false;
        }
    }

    return false;
}

std::string highlight_identifier(const std::string& source, size_t& pos) {
    const size_t start = pos;
    while (pos < source.size() && is_ident_char(source[pos])) {
        ++pos;
    }

    const std::string word = source.substr(start, pos - start);
    const size_t next = skip_spaces(source, pos);
    const size_t prev = previous_non_space(source, start);

    if (LANGUAGE_VALUES.count(word)) {
        return color(CONSTANT, word);
    }
    if (KEYWORDS.count(word)) {
        return color(KEYWORD, word);
    }
    if (STORAGE_WORDS.count(word)) {
        return color(STORAGE, word);
    }
    if (prev != std::string::npos && source[prev] == ':' && !looks_like_declared_variable(source, pos)) {
        return color(TYPE, word);
    }
    if (follows_inheritance_keyword(source, start)) {
        return color(TYPE, word);
    }
    if (!word.empty() && std::isupper(static_cast<unsigned char>(word[0]))) {
        return color(TYPE, word);
    }
    if (looks_like_parameter(source, start, pos)) {
        return color(PARAMETER, word);
    }
    if (next < source.size() && source[next] == '(') {
        return color(FUNCTION, word);
    }
    if (BUILTIN_TYPES.count(word)) {
        return color(TYPE, word);
    }
    if (prev != std::string::npos && source[prev] == '.') {
        return word;
    }
    return word;
}

} // namespace

std::string highlight_source(const std::string& source) {
    std::string out;
    out.reserve(source.size() + source.size() / 4);

    for (size_t pos = 0; pos < source.size();) {
        const char ch = source[pos];

        if (ch == '#') {
            const size_t start = pos;
            if (starts_with(source, pos, "#\"")) {
                pos += 2;
                while (pos < source.size() && !starts_with(source, pos, "\"#")) {
                    ++pos;
                }
                if (pos < source.size()) {
                    pos += 2;
                }
            } else {
                while (pos < source.size() && source[pos] != '\n') {
                    ++pos;
                }
            }
            out += color_span(COMMENT, source, start, pos);
            continue;
        }

        if (ch == '"' || ch == '\'' || ch == '`') {
            out += highlight_string(source, pos);
            continue;
        }

        const bool signed_number = ch == '-' && pos + 1 < source.size() &&
            std::isdigit(static_cast<unsigned char>(source[pos + 1])) &&
            (pos == 0 || !is_word_or_dot(source[pos - 1]));
        if (std::isdigit(static_cast<unsigned char>(ch)) || signed_number) {
            const size_t start = pos;
            pos = consume_number(source, pos);
            out += color_span(NUMBER, source, start, pos);
            continue;
        }

        if (is_ident_start(ch)) {
            out += highlight_identifier(source, pos);
            continue;
        }

        if (is_operator_char(ch)) {
            const size_t start = pos;
            pos = consume_operator(source, pos);
            out += color_span(OPERATOR, source, start, pos);
            continue;
        }

        if (is_punctuation(ch)) {
            out += color_span(PUNCTUATION, source, pos, pos + 1);
            ++pos;
            continue;
        }

        if (ch == '\r') {
            ++pos;
            continue;
        }

        if (ch == '\t') {
            out += std::string(DIM) + TAB_DISPLAY + RESET;
            ++pos;
            continue;
        }

        out += ch;
        ++pos;
    }

    return out;
}

std::string highlight_line(const std::string& line) {
    return highlight_source(line);
}

} // namespace nv::syntax_highlighter
