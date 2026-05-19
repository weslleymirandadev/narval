#pragma once

#include <string>

namespace nv::syntax_highlighter {

std::string highlight_source(const std::string& source);
std::string highlight_line(const std::string& line);

} // namespace nv::syntax_highlighter
