#pragma once

#include "frontend/lexer/token.hpp"

#include <optional>
#include <vector>

namespace narval::lsp {

enum TokenTypeIndex {
    Keyword = 0,
    Variable = 1,
    Parameter = 2,
    Function = 3,
    Type = 4,
    String = 5,
    Number = 6,
    Operator = 7,
};

struct SemanticToken {
    int line;
    int character;
    int length;
    int type;
    int modifiers;
};

std::optional<SemanticToken> classify_token(const std::vector<Token>& tokens, size_t index);
std::vector<int> encode_semantic_tokens(std::vector<SemanticToken> semantic_tokens);

} // namespace narval::lsp
