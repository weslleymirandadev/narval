#pragma once

#include <string>

namespace nv {
    struct Diagnostic {
        std::string filename;
        size_t line = 1;
        size_t col_start = 1;
        size_t col_end = 1;
        int severity = 1;
        std::string message;
    };
}
