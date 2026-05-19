#pragma once

#include <string>

namespace nv::line_editor {

std::string read_line(const std::string& prompt, bool add_to_history);

} // namespace nv::line_editor
