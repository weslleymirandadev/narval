#include "lsp/diagnostics.hpp"

namespace narval::lsp {

json diagnostic_to_json(const nv::Diagnostic& diagnostic) {
    size_t line = diagnostic.line > 0 ? diagnostic.line - 1 : 0;
    size_t start = diagnostic.col_start > 0 ? diagnostic.col_start - 1 : 0;
    size_t end = diagnostic.col_end > 0 ? diagnostic.col_end - 1 : start;

    // The notes explain the message (for comptime errors they carry the reason: the missing
    // header, the unsupported rule) and the terminal printer writes them itself. In the editor
    // they have to travel inside the message: the LSP has a `relatedInformation` field, but
    // every client renders a multi-line message, none is guaranteed to show that one.
    std::string message = diagnostic.message;
    for (const auto& note : diagnostic.notes) {
        if (!message.empty())
            message += "\n";
        message += "note: " + note;
    }

    return {
        {"range", {
            {"start", {{"line", line}, {"character", start}}},
            {"end", {{"line", line}, {"character", end}}},
        }},
        {"severity", diagnostic.severity},
        {"source", "narval"},
        {"message", message},
    };
}

} // namespace narval::lsp
