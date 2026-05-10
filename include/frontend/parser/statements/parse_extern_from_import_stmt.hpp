#pragma once
#include "frontend/parser/parser.hpp"
#include <memory>

std::unique_ptr<Node> parse_extern_from_import_stmt(Parser* parser);
