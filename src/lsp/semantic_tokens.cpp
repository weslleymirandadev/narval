#include "lsp/semantic_tokens.hpp"

#include <algorithm>
#include <string>

namespace narval::lsp {

namespace {

bool is_keyword(TokenType type) {
    switch (type) {
        case TokenType::TRUE:
        case TokenType::FALSE:
        case TokenType::RETURN:
        case TokenType::IF:
        case TokenType::ELIF:
        case TokenType::ELSE:
        case TokenType::MATCH:
        case TokenType::FOR:
        case TokenType::WHILE:
        case TokenType::FOREVER:
        case TokenType::BREAK:
        case TokenType::CONTINUE:
        case TokenType::IMPORT:
        case TokenType::DEF:
        case TokenType::IN:
        case TokenType::CLASS:
        case TokenType::PUBLIC:
        case TokenType::PRIVATE:
        case TokenType::PROTECTED:
        case TokenType::ABSTRACT:
        case TokenType::EXTENDS:
        case TokenType::IMPLEMENTS:
        case TokenType::INTERFACE:
        case TokenType::NEW:
        case TokenType::SELF:
        case TokenType::SUPER:
        case TokenType::INSTANCEOF:
        case TokenType::MUT:
        case TokenType::TRY:
        case TokenType::CATCH:
        case TokenType::FINALLY:
        case TokenType::THROW:
        case TokenType::ENUM:
        case TokenType::OR_KW:
        case TokenType::PROPAGATE:
        case TokenType::ERR_KW:
        case TokenType::NONE_KW:
        case TokenType::DEFER:
        case TokenType::EXTERN:
        case TokenType::FROM:
        case TokenType::AS:
            return true;
        default:
            return false;
    }
}

bool is_operator(TokenType type) {
    switch (type) {
        case TokenType::ASSIGNMENT:
        case TokenType::PLUS:
        case TokenType::MINUS:
        case TokenType::MUL:
        case TokenType::DIV:
        case TokenType::POWER:
        case TokenType::EQUALS:
        case TokenType::DIFFERENT:
        case TokenType::LESS_THAN_EQUALS:
        case TokenType::GREATER_THAN_EQUALS:
        case TokenType::LT:
        case TokenType::GT:
        case TokenType::AND:
        case TokenType::OR:
        case TokenType::NOT:
        case TokenType::INCREMENT:
        case TokenType::DECREMENT:
        case TokenType::MOD:
        case TokenType::INTEGER_DIV:
        case TokenType::PLUS_ASSIGN:
        case TokenType::MUL_ASSIGN:
        case TokenType::MINUS_ASSIGN:
        case TokenType::DIV_ASSIGN:
        case TokenType::INTEGER_DIV_ASSIGN:
        case TokenType::POWER_ASSIGN:
        case TokenType::MOD_ASSIGN:
        case TokenType::BITWISE_AND:
        case TokenType::BITWISE_OR:
        case TokenType::BITWISE_XOR:
        case TokenType::BITWISE_NOT:
        case TokenType::LEFT_SHIFT:
        case TokenType::RIGHT_SHIFT:
        case TokenType::BITWISE_AND_ASSIGN:
        case TokenType::BITWISE_OR_ASSIGN:
        case TokenType::BITWISE_XOR_ASSIGN:
        case TokenType::LEFT_SHIFT_ASSIGN:
        case TokenType::RIGHT_SHIFT_ASSIGN:
        case TokenType::AT:
        case TokenType::AT_ASSIGN:
        case TokenType::ARROW:
        case TokenType::RANGE:
        case TokenType::INCLUSIVE_RANGE:
            return true;
        default:
            return false;
    }
}

bool is_builtin_type(const std::string& text) {
    return text == "int" || text == "float" || text == "str" || text == "bool" ||
           text == "void" || text == "vector" || text == "map" || text == "tuple" ||
           text == "array" || text == "Error";
}

} // namespace

std::optional<SemanticToken> classify_token(const std::vector<Token>& tokens, size_t index) {
    const Token& token = tokens[index];
    if (token.type == TokenType::EOF_TOKEN || token.type == TokenType::UNKNOWN) {
        return std::nullopt;
    }

    int type = Variable;
    int modifiers = 0;

    if (is_keyword(token.type)) {
        type = Keyword;
    } else if (token.type == TokenType::STRING) {
        type = String;
    } else if (token.type == TokenType::NUMBER) {
        type = Number;
    } else if (is_operator(token.type)) {
        type = Operator;
    } else if (token.type == TokenType::IDENTIFIER) {
        const Token* prev = index > 0 ? &tokens[index - 1] : nullptr;
        const Token* next = index + 1 < tokens.size() ? &tokens[index + 1] : nullptr;

        if (is_builtin_type(token.lexeme)) {
            type = Type;
        } else if (prev && (prev->type == TokenType::DEF || prev->type == TokenType::CLASS ||
                           prev->type == TokenType::INTERFACE || prev->type == TokenType::ENUM)) {
            type = prev->type == TokenType::DEF ? Function : Type;
            modifiers = 1;
        } else if (prev && prev->type == TokenType::COLON) {
            type = Type;
        } else if (next && next->type == TokenType::COLON) {
            type = Variable;
            modifiers = 1;
        } else if (next && next->type == TokenType::OPAREN) {
            type = Function;
        }
    } else {
        return std::nullopt;
    }

    int length = static_cast<int>(token.column_end > token.column_start
        ? token.column_end - token.column_start
        : token.lexeme.size());
    if (length <= 0) {
        length = 1;
    }

    return SemanticToken{
        static_cast<int>(token.line - 1),
        static_cast<int>(token.column_start - 1),
        length,
        type,
        modifiers,
    };
}

std::vector<int> encode_semantic_tokens(std::vector<SemanticToken> semantic_tokens) {
    std::sort(semantic_tokens.begin(), semantic_tokens.end(), [](const auto& a, const auto& b) {
        if (a.line != b.line) return a.line < b.line;
        return a.character < b.character;
    });

    std::vector<int> data;
    int last_line = 0;
    int last_character = 0;
    bool first = true;

    for (const auto& token : semantic_tokens) {
        int delta_line = first ? token.line : token.line - last_line;
        int delta_start = (first || delta_line != 0)
            ? token.character
            : token.character - last_character;

        data.push_back(delta_line);
        data.push_back(delta_start);
        data.push_back(token.length);
        data.push_back(token.type);
        data.push_back(token.modifiers);

        last_line = token.line;
        last_character = token.character;
        first = false;
    }

    return data;
}

} // namespace narval::lsp
