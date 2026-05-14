#include "lsp/json_utils.hpp"

namespace narval::lsp {

std::optional<std::string> json_string_at(const json& value, const std::string& key) {
    if (!value.is_object() || !value.contains(key) || !value.at(key).is_string()) {
        return std::nullopt;
    }

    return value.at(key).get<std::string>();
}

} // namespace narval::lsp
