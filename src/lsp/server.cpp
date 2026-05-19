#include "lsp/server.hpp"

#include "lsp/diagnostics.hpp"
#include "lsp/json_utils.hpp"
#include "lsp/path_utils.hpp"
#include "lsp/semantic_tokens.hpp"

#include <map>
#include <vector>

namespace narval::lsp {

void LSPServer::run() {
    while (true) {
        auto message = transport_.read_message();
        if (!message) {
            break;
        }

        handle_message(*message);
        if (should_exit_) {
            break;
        }
    }
}

void LSPServer::handle_message(const json& message) {
    auto method = json_string_at(message, "method");
    if (!method) {
        return;
    }

    const json* id = message.contains("id") ? &message.at("id") : nullptr;

    if (*method == "initialize") {
        handle_initialize(id);
    } else if (*method == "initialized") {
        return;
    } else if (*method == "shutdown") {
        shutdown_requested_ = true;
        send_response(id, nullptr);
    } else if (*method == "exit") {
        should_exit_ = true;
    } else if (*method == "textDocument/didOpen") {
        handle_did_open(message);
    } else if (*method == "textDocument/didChange") {
        handle_did_change(message);
    } else if (*method == "textDocument/semanticTokens/full") {
        handle_semantic_tokens(id, message);
    } else if (id) {
        send_response(id, nullptr);
    }
}

void LSPServer::handle_initialize(const json* id) {
    send_response(id, {
        {"capabilities", {
            {"textDocumentSync", 1},
            {"semanticTokensProvider", {
                {"legend", {
                    {"tokenTypes", {"keyword", "variable", "parameter", "function", "type", "string", "number", "operator"}},
                    {"tokenModifiers", {"declaration", "readonly", "defaultLibrary"}},
                }},
                {"full", true},
            }},
        }},
    });
}

void LSPServer::handle_did_open(const json& message) {
    const json& text_document = message.value("params", json::object()).value("textDocument", json::object());
    auto uri = json_string_at(text_document, "uri");
    auto text = json_string_at(text_document, "text");
    if (!uri || !text) {
        return;
    }

    documents_[*uri] = analyzer_.analyze(*uri, *text);
    publish_diagnostics(documents_[*uri]);
}

void LSPServer::handle_did_change(const json& message) {
    const json& params = message.value("params", json::object());
    const json& text_document = params.value("textDocument", json::object());
    auto uri = json_string_at(text_document, "uri");
    if (!uri || !params.contains("contentChanges") || !params["contentChanges"].is_array() ||
        params["contentChanges"].empty()) {
        return;
    }

    auto text = json_string_at(params["contentChanges"].back(), "text");
    if (!text) {
        return;
    }

    documents_[*uri] = analyzer_.analyze(*uri, *text);
    publish_diagnostics(documents_[*uri]);
}

void LSPServer::handle_semantic_tokens(const json* id, const json& message) {
    const json& text_document = message.value("params", json::object()).value("textDocument", json::object());
    auto uri = json_string_at(text_document, "uri");
    if (!uri || documents_.find(*uri) == documents_.end()) {
        send_response(id, {{"data", json::array()}});
        return;
    }

    std::vector<SemanticToken> semantic_tokens;
    const auto& lexer_tokens = documents_[*uri].lexer_tokens;
    for (size_t i = 0; i < lexer_tokens.size(); ++i) {
        auto token = classify_token(lexer_tokens, i);
        if (token) {
            semantic_tokens.push_back(*token);
        }
    }

    send_response(id, {{"data", encode_semantic_tokens(std::move(semantic_tokens))}});
}

void LSPServer::publish_diagnostics(const DocumentState& state) {
    std::map<std::string, json> diagnostics_by_uri;

    for (const auto& diagnostic : state.diagnostics) {
        const std::string uri = diagnostic.filename.empty() || diagnostic.filename == state.path
            ? (state.uri.empty() ? path_to_uri(state.path) : state.uri)
            : path_to_uri(diagnostic.filename);
        diagnostics_by_uri[uri].push_back(diagnostic_to_json(diagnostic));
    }

    const std::string current_uri = state.uri.empty() ? path_to_uri(state.path) : state.uri;
    if (diagnostics_by_uri.find(current_uri) == diagnostics_by_uri.end()) {
        diagnostics_by_uri[current_uri] = json::array();
    }

    for (const auto& [uri, diagnostics] : diagnostics_by_uri) {
        send_notification("textDocument/publishDiagnostics", {
            {"uri", uri},
            {"diagnostics", diagnostics},
        });
    }
}

void LSPServer::send_response(const json* id, const json& result) {
    transport_.send_message({
        {"jsonrpc", "2.0"},
        {"id", id ? *id : json(nullptr)},
        {"result", result},
    });
}

void LSPServer::send_notification(const std::string& method, const json& params) {
    transport_.send_message({
        {"jsonrpc", "2.0"},
        {"method", method},
        {"params", params},
    });
}

} // namespace narval::lsp
