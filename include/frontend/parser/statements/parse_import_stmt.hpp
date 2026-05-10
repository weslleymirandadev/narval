#pragma once
#include "frontend/parser/parser.hpp"
#include <memory>

// Consome o token IMPORT atual e o import_info correspondente,
// retornando um ImportStmtNode pronto.
//
// Pré-condição: parser->current_token().type == TokenType::IMPORT
//               e parser->has_pending_import() == true.
//
// Usado por produce_ast() e pelo ModuleManager.
std::unique_ptr<Node> parse_import_stmt(Parser* parser);
