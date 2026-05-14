#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace narval::lsp {

using json = nlohmann::json;

std::optional<std::string> json_string_at(const json& value, const std::string& key);

} // namespace narval::lsp
