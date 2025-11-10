#pragma once
#include <string>
#include <stdexcept>
#include "frontend/lexer/token.hpp"

Token tokenize_number(const std::string& input, size_t& pos, size_t& line, size_t& column, const std::string& filename);
