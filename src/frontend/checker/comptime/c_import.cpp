#include "frontend/comptime/c_import.hpp"
#include <cstdlib>
#include "frontend/checker/checker.hpp"
#include "frontend/checker/type.hpp"

#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace nv {
namespace {

std::string trim(std::string s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// Strip // and /* */ comments plus preprocessor lines (handling \-continuations).
std::string clean_source(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    bool line_comment = false, block_comment = false, in_preproc = false, at_line_start = true;
    for (size_t i = 0; i < src.size(); ++i) {
        char c = src[i];
        char n = (i + 1 < src.size()) ? src[i + 1] : '\0';
        if (line_comment) { if (c == '\n') { line_comment = false; out += '\n'; at_line_start = true; } continue; }
        if (block_comment) { if (c == '*' && n == '/') { block_comment = false; ++i; } if (c == '\n') out += '\n'; continue; }
        if (in_preproc) {
            if (c == '\\' && n == '\n') { ++i; continue; }
            if (c == '\n') { in_preproc = false; out += '\n'; at_line_start = true; }
            continue;
        }
        if (at_line_start && c == '#') { in_preproc = true; continue; }
        at_line_start = (c == '\n');
        if (c == '/' && n == '/') { line_comment = true; ++i; continue; }
        if (c == '/' && n == '*') { block_comment = true; ++i; continue; }
        out += c;
    }
    return out;
}

std::vector<std::string> words_of(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') cur += c;
        else { if (!cur.empty()) out.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool has_word(const std::vector<std::string>& ws, std::initializer_list<const char*> names) {
    for (const auto& w : ws)
        for (const char* n : names)
            if (w == n) return true;
    return false;
}

// C type spelling -> shape code: 'i' int, 'd' double, 's' string, 'v' void.
bool map_c_type(const std::string& spelling, char& code) {
    std::string t = trim(spelling);
    if (t.empty()) { code = 'i'; return true; }          // C implicit int
    auto ws = words_of(t);
    bool is_ptr = t.find('*') != std::string::npos;

    if (has_word(ws, {"double", "float", "longdouble"})) { code = 'd'; return true; }
    if (has_word(ws, {"void"})) { code = 'v'; return true; }
    if (has_word(ws, {"int", "char", "short", "long", "size_t", "ssize_t", "ptrdiff_t",
                      "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t",
                      "uint32_t", "uint64_t", "uintptr_t", "intptr_t", "unsigned", "signed",
                      "intmax_t", "uintmax_t", "wchar_t", "bool", "_Bool"})) {
        if (is_ptr) {
            // Only `char *` strings can be marshalled for now.
            if (has_word(ws, {"char"})) { code = 's'; return true; }
            return false;
        }
        code = 'i';
        return true;
    }
    return false;  // structs, enums, function pointers, unknown typedefs
}

// Split a parameter list on top-level commas.
std::vector<std::string> split_params(const std::string& inside) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (char c : inside) {
        if (c == '(') ++depth;
        else if (c == ')') --depth;
        if (c == ',' && depth == 0) { out.push_back(cur); cur.clear(); continue; }
        cur += c;
    }
    if (!trim(cur).empty()) out.push_back(cur);
    return out;
}

const std::unordered_set<std::string>& c_keywords() {
    static const std::unordered_set<std::string> kw = {
        "if", "else", "while", "for", "return", "switch", "case", "do", "sizeof",
        "typedef", "struct", "union", "enum", "static", "extern", "inline", "const",
        "volatile", "register", "void", "int", "char", "float", "double", "long",
        "short", "signed", "unsigned", "void", "goto", "break", "continue",
    };
    return kw;
}

} // anonymous namespace

bool import_c_header(Checker* checker, const std::string& path,
                     const std::string& link, std::string& error) {
    // Resolve the header: as given, next to the current source file, then /usr/include.
    std::string resolved;
    auto exists = [](const std::string& p) {
        std::ifstream f(p);
        return f.good();
    };
    if (exists(path)) {
        resolved = path;
    } else {
        std::string src = checker ? checker->current_filename : "";
        size_t slash = src.find_last_of('/');
        if (slash != std::string::npos) {
            std::string cand = src.substr(0, slash + 1) + path;
            if (exists(cand)) resolved = cand;
        }
        if (resolved.empty()) {
            std::string cand = "/usr/include/" + path;
            if (exists(cand)) resolved = cand;
        }
    }
    if (resolved.empty()) {
        error = "import_c: header not found: '" + path + "'";
        return false;
    }

    std::ifstream in(resolved);
    if (!in.is_open()) {
        error = "import_c: cannot open '" + resolved + "'";
        return false;
    }
    std::stringstream buf;
    buf << in.rdbuf();
    std::string cleaned = clean_source(buf.str());

    size_t imported = 0;
    size_t start = 0;
    while (start < cleaned.size()) {
        size_t semi = cleaned.find(';', start);
        if (semi == std::string::npos) break;
        std::string chunk = cleaned.substr(start, semi - start);
        start = semi + 1;

        if (chunk.find('{') != std::string::npos) continue;        // definition
        if (has_word(words_of(chunk), {"typedef"})) continue;
        size_t open = chunk.find('(');
        if (open == std::string::npos) continue;                   // not a function
        size_t close = chunk.rfind(')');
        if (close == std::string::npos || close < open) continue;

        std::string head = trim(chunk.substr(0, open));
        std::string params = chunk.substr(open + 1, close - open - 1);
        std::string tail = trim(chunk.substr(close + 1));
        if (!tail.empty()) continue;                               // function pointers / arrays

        auto hw = words_of(head);
        if (hw.empty()) continue;
        std::string name = hw.back();
        if (c_keywords().count(name)) continue;

        std::string ret_spelling = head.substr(0, head.size() - name.size());
        char ret_code = 0;
        if (!map_c_type(ret_spelling, ret_code)) continue;

        std::string shape(1, ret_code);
        bool ok = true;
        for (const auto& raw : split_params(params)) {
            std::string p = trim(raw);
            if (p.empty() || p == "void") continue;
            if (p.find("...") != std::string::npos) { ok = false; break; }  // varargs
            char code = 0;
            if (!map_c_type(p, code)) { ok = false; break; }
            if (code == 'v') { ok = false; break; }
            shape += code;
        }
        if (!ok) continue;

        // Do not clobber an existing symbol (user function, extern, builtin).
        bool exists_in_scope = true;
        try { checker->scope->get_key(name); } catch (...) { exists_in_scope = false; }
        if (exists_in_scope) continue;

        // Register the prototype so calls type-check like an extern declaration.
        auto ret_type = (ret_code == 'd') ? checker->gettyptr("float")
                      : (ret_code == 's') ? checker->gettyptr("str")
                      : (ret_code == 'v') ? checker->gettyptr("None")
                                          : checker->gettyptr("int");
        std::vector<std::shared_ptr<nv::Type>> param_types;
        for (size_t i = 1; i < shape.size(); ++i) {
            char c = shape[i];
            param_types.push_back((c == 'd') ? checker->gettyptr("float")
                                  : (c == 's') ? checker->gettyptr("str")
                                               : checker->gettyptr("int"));
        }
        checker->scope->put_key(name, std::make_shared<nv::Function>(param_types, ret_type), true);
        checker->c_import_sigs[name] = shape;
        ++imported;
    }

    if (std::getenv("NARVAL_VERBOSE")) {
        if (link.empty())
            std::cerr << "NIR: import_c '" << resolved << "' -> " << imported << " function(s)\n";
        else
            std::cerr << "NIR: import_c '" << resolved << "' -> " << imported
                      << " function(s), link -l" << link << "\n";
    }

    if (!link.empty())
        checker->extra_link_items.push_back("-l" + link);

    if (imported == 0) {
        error = "import_c: no usable declarations in '" + resolved + "'";
        return false;
    }
    return true;
}

} // namespace nv
