#pragma once

#include "lsp/json_utils.hpp"

#include <optional>

namespace narval::lsp {

class LSPTransport {
public:
    std::optional<json> read_message();
    void send_message(const json& message);
};

} // namespace narval::lsp
