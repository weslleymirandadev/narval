#pragma once

#include "lsp/analyzer.hpp"
#include "lsp/document.hpp"
#include "lsp/transport.hpp"

#include <string>
#include <unordered_map>

namespace narval::lsp {

class LSPServer {
public:
    void run();

private:
    void handle_message(const json& message);
    void handle_initialize(const json* id);
    void handle_did_open(const json& message);
    void handle_did_change(const json& message);
    void handle_semantic_tokens(const json* id, const json& message);
    void publish_diagnostics(const DocumentState& state);
    void send_response(const json* id, const json& result);
    void send_notification(const std::string& method, const json& params);

    LSPTransport transport_;
    NarvalAnalyzer analyzer_;
    std::unordered_map<std::string, DocumentState> documents_;
    bool shutdown_requested_ = false;
    bool should_exit_ = false;
};

} // namespace narval::lsp
