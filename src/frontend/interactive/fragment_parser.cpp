#include "frontend/interactive/fragment_parser.hpp"

#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/ast/ast.hpp"

namespace narval::frontend::interactive {

std::unique_ptr<::Program> FragmentParser::parse(const std::string& source, const std::string& virtual_filename) const {
    Lexer lexer(source, virtual_filename);
    auto tokens = lexer.tokenize();

    Parser parser;
    auto ast = parser.produce_ast(tokens, lexer.get_import_infos());
    if (!ast) return nullptr;

    auto program = std::unique_ptr<::Program>(dynamic_cast<::Program*>(ast.release()));
    if (!program) return nullptr;

    return program;
}

} // namespace narval::frontend::interactive
