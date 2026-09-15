#pragma once

#include <string>
#include <vector>

namespace nv {
    struct Diagnostic {
        std::string filename;
        size_t line = 1;
        size_t col_start = 1;
        size_t col_end = 1;
        int severity = 1;
        std::string message;
        // Extra lines that explain the message ("= note:" in the terminal). The compiler's
        // error printer writes them itself, but they travel with the diagnostic so a client
        // that only sees the struct — the language server — is not left with a bare title.
        std::vector<std::string> notes;
    };
}
