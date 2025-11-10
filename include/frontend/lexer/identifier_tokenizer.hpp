#pragma once
#include <string>
#include "frontend/lexer/token.hpp"

Token tokenize_identifier_or_keyword(const std::string& input, size_t& pos, size_t& line, size_t& column, const std::string& filename);
