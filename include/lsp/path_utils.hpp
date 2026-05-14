#pragma once

#include <string>

namespace narval::lsp {

std::string uri_to_path(std::string uri);
std::string path_to_uri(std::string path);

} // namespace narval::lsp
