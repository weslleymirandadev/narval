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
        case TokenType::OVERRIDE:
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

bool is_boundary(TokenType type) {
    switch (type) {
        case TokenType::ASSIGNMENT:
        case TokenType::RETURN:
        case TokenType::SEMICOLON:
        case TokenType::OPAREN:
        case TokenType::OBRACE:
        case TokenType::CBRACE:
            return true;
        default:
            return false;
    }
}

bool has_param_named(const std::vector<Token>& tokens, size_t begin, size_t end, const std::string& name) {
    for (size_t i = begin; i + 1 < end; ++i) {
        if (tokens[i].type == TokenType::IDENTIFIER &&
            tokens[i].lexeme == name &&
            tokens[i + 1].type == TokenType::COLON) {
            return true;
        }
    }
    return false;
}

std::optional<size_t> matching_close_paren(const std::vector<Token>& tokens, size_t open_paren) {
    int depth = 0;
    for (size_t i = open_paren; i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::OPAREN) {
            ++depth;
        } else if (tokens[i].type == TokenType::CPAREN) {
            --depth;
            if (depth == 0) {
                return i;
            }
        } else if (depth == 1 && (tokens[i].type == TokenType::OBRACE ||
                                  tokens[i].type == TokenType::SEMICOLON)) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

bool is_function_like_parameter_list(const std::vector<Token>& tokens, size_t open_paren) {
    if (open_paren == 0 ||
        (tokens[open_paren - 1].type != TokenType::IDENTIFIER &&
         tokens[open_paren - 1].type != TokenType::NEW)) {
        return false;
    }

    auto close_paren = matching_close_paren(tokens, open_paren);
    if (!close_paren || *close_paren + 1 >= tokens.size()) {
        return false;
    }

    return tokens[*close_paren + 1].type == TokenType::COLON;
}

bool is_closure_parameter_declaration(const std::vector<Token>& tokens, size_t index) {
    if (index + 1 >= tokens.size() || tokens[index + 1].type != TokenType::COLON) {
        return false;
    }

    size_t boundary = 0;
    for (size_t i = index; i > 0; --i) {
        if (is_boundary(tokens[i - 1].type)) {
            boundary = i;
            break;
        }
    }

    size_t open_pipe = tokens.size();
    for (size_t i = boundary; i < index; ++i) {
        if (tokens[i].type == TokenType::BITWISE_OR) {
            open_pipe = i;
            break;
        }
    }
    if (open_pipe == tokens.size()) {
        return false;
    }

    for (size_t i = index + 1; i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::BITWISE_OR) {
            return true;
        }
        if (tokens[i].type == TokenType::OBRACE || tokens[i].type == TokenType::SEMICOLON ||
            tokens[i].type == TokenType::ASSIGNMENT) {
            return false;
        }
    }

    return false;
}

bool is_function_parameter_declaration(const std::vector<Token>& tokens, size_t index) {
    if (index + 1 >= tokens.size() || tokens[index + 1].type != TokenType::COLON) {
        return false;
    }

    size_t open_paren = tokens.size();
    int depth = 0;
    for (size_t i = index; i > 0; --i) {
        const TokenType type = tokens[i - 1].type;
        if (type == TokenType::CPAREN) {
            ++depth;
        } else if (type == TokenType::OPAREN) {
            if (depth == 0) {
                open_paren = i - 1;
                break;
            }
            --depth;
        } else if (depth == 0 && (type == TokenType::OBRACE || type == TokenType::SEMICOLON)) {
            return false;
        }
    }

    if (open_paren == tokens.size() || open_paren == 0) {
        return false;
    }

    if (is_function_like_parameter_list(tokens, open_paren)) {
        return true;
    }

    for (size_t i = open_paren; i > 0; --i) {
        if (tokens[i - 1].type == TokenType::DEF) {
            return true;
        }
        if (tokens[i - 1].type == TokenType::SEMICOLON || tokens[i - 1].type == TokenType::OBRACE ||
            tokens[i - 1].type == TokenType::CBRACE) {
            break;
        }
    }

    return false;
}

bool is_parameter_declaration(const std::vector<Token>& tokens, size_t index) {
    return is_closure_parameter_declaration(tokens, index) ||
           is_function_parameter_declaration(tokens, index);
}

std::optional<size_t> enclosing_body_start(const std::vector<Token>& tokens, size_t index) {
    int depth = 0;
    for (size_t i = index; i > 0; --i) {
        const TokenType type = tokens[i - 1].type;
        if (type == TokenType::CBRACE) {
            ++depth;
        } else if (type == TokenType::OBRACE) {
            if (depth == 0) {
                return i - 1;
            }
            --depth;
        }
    }
    return std::nullopt;
}

bool header_has_function_param(const std::vector<Token>& tokens, size_t body_start, const std::string& name) {
    size_t close_paren = body_start;
    while (close_paren > 0 && tokens[close_paren - 1].type != TokenType::CPAREN) {
        --close_paren;
    }
    if (close_paren == 0) {
        return false;
    }
    --close_paren;

    int depth = 0;
    for (size_t i = close_paren; i > 0; --i) {
        const TokenType type = tokens[i - 1].type;
        if (type == TokenType::CPAREN) {
            ++depth;
        } else if (type == TokenType::OPAREN) {
            if (depth == 0) {
                return has_param_named(tokens, i, close_paren, name);
            }
            --depth;
        }
    }

    return false;
}

bool header_has_closure_param(const std::vector<Token>& tokens, size_t body_start, const std::string& name) {
    size_t boundary = 0;
    for (size_t i = body_start; i > 0; --i) {
        if (is_boundary(tokens[i - 1].type)) {
            boundary = i;
            break;
        }
    }

    size_t open_pipe = tokens.size();
    for (size_t i = boundary; i < body_start; ++i) {
        if (tokens[i].type == TokenType::BITWISE_OR) {
            open_pipe = i;
            break;
        }
    }
    if (open_pipe == tokens.size()) {
        return false;
    }

    for (size_t close_pipe = open_pipe + 1; close_pipe < body_start; ++close_pipe) {
        if (tokens[close_pipe].type == TokenType::BITWISE_OR) {
            return has_param_named(tokens, open_pipe + 1, close_pipe, name);
        }
    }

    return false;
}

bool is_parameter_reference(const std::vector<Token>& tokens, size_t index) {
    const std::string& name = tokens[index].lexeme;
    auto body_start = enclosing_body_start(tokens, index);
    while (body_start) {
        if (header_has_closure_param(tokens, *body_start, name) ||
            header_has_function_param(tokens, *body_start, name)) {
            return true;
        }
        body_start = enclosing_body_start(tokens, *body_start);
    }
    return false;
}

bool is_inheritance_type(const std::vector<Token>& tokens, size_t index) {
    if (index == 0) {
        return false;
    }

    if (tokens[index - 1].type == TokenType::EXTENDS ||
        tokens[index - 1].type == TokenType::IMPLEMENTS) {
        return true;
    }

    if (tokens[index - 1].type != TokenType::COMMA) {
        return false;
    }

    for (size_t i = index; i > 0; --i) {
        const TokenType type = tokens[i - 1].type;
        if (type == TokenType::IMPLEMENTS || type == TokenType::EXTENDS) {
            return true;
        }
        if (type == TokenType::CLASS || type == TokenType::INTERFACE ||
            type == TokenType::ENUM || type == TokenType::DEF ||
            type == TokenType::OBRACE || type == TokenType::SEMICOLON) {
            return false;
        }
    }

    return false;
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
        } else if (is_inheritance_type(tokens, index)) {
            type = Type;
        } else if (prev && (prev->type == TokenType::DEF || prev->type == TokenType::CLASS ||
                           prev->type == TokenType::INTERFACE || prev->type == TokenType::ENUM)) {
            type = prev->type == TokenType::DEF ? Function : Type;
            modifiers = 1;
        } else if (prev && prev->type == TokenType::COLON) {
            type = Type;
        } else if (is_parameter_declaration(tokens, index)) {
            type = Parameter;
            modifiers = 1;
        } else if ((!prev || prev->type != TokenType::DOT) && is_parameter_reference(tokens, index)) {
            type = Parameter;
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
