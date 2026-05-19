#include "lsp/analyzer.hpp"

#include "lsp/path_utils.hpp"

#include "frontend/checker/checker.hpp"
#include "frontend/ast/program.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/ast/statements/function_stmt_node.hpp"
#include "frontend/ast/statements/class_stmt_node.hpp"
#include "frontend/ast/statements/enum_stmt_node.hpp"
#include "frontend/ast/statements/interface_stmt_node.hpp"
#include "frontend/ast/statements/import_stmt_node.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/lexer/lexer_error.hpp"
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"

#include <exception>
#include <filesystem>
#include <fstream>
#include <unordered_set>

namespace narval::lsp {
namespace {

bool is_exported_stmt_for_lsp(const std::unique_ptr<Stmt>& stmt) {
    if (!stmt) {
        return false;
    }
    if (stmt->kind == NodeType::DeclarationStatement ||
        stmt->kind == NodeType::FunctionStatement ||
        stmt->kind == NodeType::ClassStatement ||
        stmt->kind == NodeType::EnumStatement ||
        stmt->kind == NodeType::InterfaceStatement) {
        return true;
    }
    if (stmt->kind == NodeType::AssignmentExpression) {
        auto* assign = static_cast<AssignmentExprNode*>(stmt.get());
        return assign->target && assign->target->kind == NodeType::Identifier;
    }
    return false;
}

void append_imported_exports(
    Program* combined,
    const std::vector<ImportInfo>& import_infos,
    const std::filesystem::path& base_dir,
    std::unordered_set<std::string>& visiting,
    std::vector<nv::Diagnostic>& diagnostics
) {
    for (const auto& import_info : import_infos) {
        std::filesystem::path import_path(import_info.module_path);
        if (import_path.is_relative()) {
            import_path = base_dir / import_path;
        }
        import_path = import_path.lexically_normal();

        std::string full_path;
        try {
            full_path = std::filesystem::canonical(import_path).string();
        } catch (const std::filesystem::filesystem_error&) {
            full_path = import_path.string();
        }

        if (visiting.count(full_path)) {
            continue;
        }
        visiting.insert(full_path);

        std::ifstream file(full_path);
        if (!file.is_open()) {
            diagnostics.push_back({full_path, 1, 1, 1, 1, "Module not found: " + import_info.module_path});
            visiting.erase(full_path);
            continue;
        }

        std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        try {
            Lexer lexer(source, full_path);
            lexer.set_emit_diagnostics(false);
            auto tokens = lexer.tokenize();

            Parser parser;
            parser.set_emit_diagnostics(false);
            auto ast = parser.produce_ast(tokens, lexer.get_import_infos());
            if (parser.has_error()) {
                diagnostics.insert(diagnostics.end(), parser.diagnostics.begin(), parser.diagnostics.end());
                visiting.erase(full_path);
                continue;
            }

            append_imported_exports(
                combined,
                lexer.get_import_infos(),
                std::filesystem::path(full_path).parent_path(),
                visiting,
                diagnostics
            );

            auto* program = dynamic_cast<Program*>(ast.get());
            if (program) {
                for (const auto& stmt : program->get_statements()) {
                    if (is_exported_stmt_for_lsp(stmt)) {
                        combined->add_statement(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
                    }
                }
            }
        } catch (const LexicalError& e) {
            diagnostics.push_back(e.diagnostic());
        } catch (const std::exception& e) {
            diagnostics.push_back({full_path, 1, 1, 1, 1, e.what()});
        }

        visiting.erase(full_path);
    }
}

std::unique_ptr<Node> build_lsp_check_ast(
    std::unique_ptr<Node> ast,
    const std::vector<ImportInfo>& import_infos,
    const std::string& current_path,
    std::vector<nv::Diagnostic>& diagnostics
) {
    auto combined = std::make_unique<Program>();
    std::unordered_set<std::string> visiting;

    append_imported_exports(
        combined.get(),
        import_infos,
        std::filesystem::path(current_path).parent_path(),
        visiting,
        diagnostics
    );

    auto* current_program = dynamic_cast<Program*>(ast.get());
    if (!current_program) {
        return ast;
    }

    for (const auto& stmt : current_program->get_statements()) {
        if (stmt->kind != NodeType::ImportStatement) {
            combined->add_statement(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
        }
    }

    return combined;
}

} // namespace

DocumentState NarvalAnalyzer::analyze(const std::string& uri, const std::string& text) {
    DocumentState state;
    state.uri = uri;
    state.path = uri_to_path(uri);
    state.text = text;

    try {
        Lexer lexer(text, state.path);
        lexer.set_emit_diagnostics(false);
        state.lexer_tokens = lexer.tokenize();

        Parser parser;
        parser.set_emit_diagnostics(false);
        auto ast = parser.produce_ast(state.lexer_tokens, lexer.get_import_infos());
        state.diagnostics = parser.diagnostics;
        if (parser.has_error()) {
            return state;
        }

        auto check_ast = build_lsp_check_ast(
            std::move(ast),
            lexer.get_import_infos(),
            state.path,
            state.diagnostics
        );

        nv::Checker checker;
        checker.set_source_file(state.path);
        checker.set_emit_diagnostics(false);
        checker.check_node(check_ast.get());
        state.diagnostics.insert(
            state.diagnostics.end(),
            checker.diagnostics.begin(),
            checker.diagnostics.end()
        );
    } catch (const LexicalError& e) {
        state.diagnostics.push_back(e.diagnostic());
    } catch (const std::exception& e) {
        state.diagnostics.push_back({state.path, 1, 1, 1, 1, e.what()});
    }

    return state;
}

} // namespace narval::lsp
