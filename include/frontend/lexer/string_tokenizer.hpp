#pragma once
#include <string>
#include "frontend/lexer/token.hpp"

Token tokenize_string(const std::string& input, size_t& position, size_t line, size_t column, const std::string& filename);
