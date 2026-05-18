#include "lsp/analyzer.hpp"

#include "lsp/path_utils.hpp"

#include "frontend/checker/checker.hpp"
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"

#include <exception>

namespace narval::lsp {

DocumentState NarvalAnalyzer::analyze(const std::string& uri, const std::string& text) {
    DocumentState state;
    state.uri = uri;
    state.path = uri_to_path(uri);
    state.text = text;

    try {
        Lexer lexer(text, state.path);
        state.lexer_tokens = lexer.tokenize();

        Parser parser;
        parser.set_emit_diagnostics(false);
        auto ast = parser.produce_ast(state.lexer_tokens, lexer.get_import_infos());
        state.diagnostics = parser.diagnostics;
        if (parser.has_error()) {
            return state;
        }

        nv::Checker checker;
        checker.set_source_file(state.path);
        checker.set_emit_diagnostics(false);
        checker.check_node(ast.get());
        state.diagnostics.insert(
            state.diagnostics.end(),
            checker.diagnostics.begin(),
            checker.diagnostics.end()
        );
    } catch (const std::exception& e) {
        state.diagnostics.push_back({state.path, 1, 1, 1, 1, e.what()});
    }

    return state;
}

} // namespace narval::lsp
