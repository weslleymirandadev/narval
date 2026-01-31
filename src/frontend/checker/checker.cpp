#include "frontend/checker/checker.hpp"
#include "frontend/checker/type.hpp"
#include "frontend/checker/unification.hpp"
#include "frontend/checker/builtins.hpp"
#include "frontend/ast/ast.hpp"
#include <memory>
#include <unordered_set>
#include <algorithm>
#include <fstream>
#include <cmath>
#include <iostream>
#include <algorithm>

constexpr size_t MAX_LINE_LENGTH = 1024;
constexpr const char* ANSI_BOLD = "\x1b[1m";
constexpr const char* ANSI_RESET = "\x1b[0m";
constexpr const char* ANSI_RED = "\x1b[31m";

nv::Checker::Checker() {
    auto globalnamespace = std::make_shared<Namespace>();
    namespaces.push_back(globalnamespace);
    scope = globalnamespace;
    types["int"] = std::make_shared<nv::Int>();
    types["string"] = std::make_shared<nv::String>();
    types["float"] = std::make_shared<nv::Float>();
    types["bool"] = std::make_shared<nv::Boolean>();
    types["void"] = std::make_shared<nv::Void>();
    
    // Registrar funções builtin do runtime
    register_builtins(*this);
}

nv::Type& nv::Checker::getty(std::string ty) {
    return *types.at(ty);
}

std::shared_ptr<nv::Type>& nv::Checker::gettyptr(std::string ty){
    // Verificar se já existe no cache
    auto it = types.find(ty);
    if (it != types.end()) {
        return it->second;
    }
    
    // Parsear tipos compostos: vector, int[10], string[5], etc.
    
    // Tipo vector
    if (ty == "vector") {
        auto vec_type = std::make_shared<nv::Vector>();
        types[ty] = vec_type;
        return types[ty];
    }
    
    // Tipo array: int[10], string[5], etc.
    // Formato: base_type[size]
    size_t bracket_pos = ty.find('[');
    if (bracket_pos != std::string::npos && bracket_pos > 0) {
        std::string base_type_str = ty.substr(0, bracket_pos);
        size_t close_bracket = ty.find(']', bracket_pos);
        if (close_bracket != std::string::npos) {
            std::string size_str = ty.substr(bracket_pos + 1, close_bracket - bracket_pos - 1);
            try {
                int size = std::stoi(size_str);
                if (size > 0) {
                    // Obter tipo base
                    auto& base_type = gettyptr(base_type_str);
                    auto arr_type = std::make_shared<nv::Array>(base_type, size);
                    types[ty] = arr_type;
                    return types[ty];
                }
            } catch (...) {
                // Não é um número válido
            }
        }
    }
    
    // Tipo não encontrado
    // Não temos Node aqui, então usar erro genérico
    std::cerr << ANSI_BOLD << current_filename << ": "
              << ANSI_RED << "ERROR" << ANSI_RESET << ANSI_BOLD << ": "
              << "Unknown type: " << ty << ANSI_RESET << "\n\n";
    err = true;
    // Retornar void como fallback
    return types["void"];
}
void nv::Checker::push_scope() {
    auto ns = std::make_shared<Namespace>(scope);
    namespaces.push_back(ns);
    scope = ns;
}

void nv::Checker::pop_scope() {
    namespaces.pop_back();
    scope = namespaces[namespaces.size() - 1];
}

std::unordered_set<int> nv::Checker::get_free_vars_in_env() {
    std::unordered_set<int> free_vars;
    
    // Coletar variáveis livres de todas as variáveis no ambiente atual
    // O método collect_free_vars do Namespace já coleta recursivamente
    // de todos os escopos pais através da cadeia de parent, então
    // precisamos apenas chamar no escopo atual
    if (scope) {
        scope->collect_free_vars(free_vars);
    }
    
    return free_vars;
}

void nv::Checker::read_lines(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        // Se não conseguir abrir, apenas limpar linhas (erro será reportado sem contexto)
        lines.clear();
        line_count = 0;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.size() > MAX_LINE_LENGTH) {
            // Linha muito longa, truncar
            line = line.substr(0, MAX_LINE_LENGTH);
        }
        lines.push_back(line);
    }
    line_count = lines.size();
}

void nv::Checker::print_error_context(const PositionData* pos) {
    if (!pos || lines.empty() || pos->line == 0 || pos->line > line_count) {
        return;
    }

    std::string line_content = lines[pos->line - 1];
    std::replace(line_content.begin(), line_content.end(), '\n', ' ');

    std::cerr << " " << pos->line << " |   " << line_content << "\n";

    int line_width = pos->line > 0 ? static_cast<int>(std::log10(pos->line) + 1) : 1;
    std::cerr << std::string(line_width, ' ') << "  |";
    std::cerr << std::string(pos->col[0] - 1 + 3, ' ');

    std::cerr << ANSI_RED;
    for (size_t i = pos->col[0]; i < pos->col[1] && i <= line_content.length(); ++i) {
        std::cerr << "^";
    }
    std::cerr << ANSI_RESET << "\n\n";
}

void nv::Checker::set_source_file(const std::string& filename) {
    current_filename = filename;
    read_lines(filename);
}

void nv::Checker::error(Node* node, const std::string& message) {
    if (!node || !node->position) {
        std::cerr << ANSI_BOLD << current_filename << ": "
                  << ANSI_RED << "ERROR" << ANSI_RESET << ANSI_BOLD << ": "
                  << message << ANSI_RESET << "\n\n";
        err = true;
        return;
    }
    
    PositionData* pos = node->position.get();
    std::cerr << ANSI_BOLD
              << current_filename << ":" << pos->line << ":" << pos->col[0] << ": "
              << ANSI_RED << "ERROR" << ANSI_RESET << ANSI_BOLD << ": "
              << message << ANSI_RESET << "\n";

    print_error_context(pos);
    err = true;
}

std::shared_ptr<nv::Type> nv::Checker::infer_type(Node* node) {
    return infer_expr(node);
}

std::shared_ptr<nv::Type> nv::Checker::infer_expr(Node* node) {
    switch (node->kind) {
        case NodeType::NumericLiteral: {
            const auto* vl = static_cast<NumericLiteralNode*>(node);
            if (vl->value.find('.') != std::string::npos) {
                return gettyptr("float");
            }
            return gettyptr("int");
        }
        case NodeType::StringLiteral:
            return gettyptr("string");
        case NodeType::BooleanLiteral:
            return gettyptr("bool");
        case NodeType::Identifier: {
            const auto* id = static_cast<IdentifierNode*>(node);
            auto& var_type = scope->get_key(id->symbol);
            // Se for tipo polimórfico, instanciar
            if (var_type->kind == Kind::POLY_TYPE) {
                auto poly = std::static_pointer_cast<PolyType>(var_type);
                int next_id = unify_ctx.get_next_var_id();
                return poly->instantiate(next_id);
            }
            return var_type;
        }
        case NodeType::BinaryExpression: {
            const auto* bin = static_cast<BinaryExprNode*>(node);
            auto left_type = infer_expr(bin->left.get());
            auto right_type = infer_expr(bin->right.get());
            
            // Resolver tipos antes de unificar
            left_type = unify_ctx.resolve(left_type);
            right_type = unify_ctx.resolve(right_type);
            
            // Verificar coerção implícita int -> float antes de unificar
            bool left_is_int = left_type->kind == Kind::INT;
            bool left_is_float = left_type->kind == Kind::FLOAT;
            bool right_is_int = right_type->kind == Kind::INT;
            bool right_is_float = right_type->kind == Kind::FLOAT;
            
            // Se um é int e outro é float, promover int para float
            if (left_is_int && right_is_float) {
                left_type = gettyptr("float");
            } else if (left_is_float && right_is_int) {
                right_type = gettyptr("float");
            }
            
            // Unificar tipos dos operandos
            try {
                unify_ctx.unify(left_type, right_type);
            } catch (std::runtime_error& e) {
                throw std::runtime_error("Binary expression type error: " + std::string(e.what()));
            }
            
            // Resolver tipos após unificação
            left_type = unify_ctx.resolve(left_type);
            
            // Determinar tipo de retorno baseado no operador
            if (bin->op == "+" || bin->op == "-" || bin->op == "*" || bin->op == "/" || bin->op == "%") {
                // Operadores aritméticos retornam o tipo dos operandos (promovido se necessário)
                // Se havia int e float, o resultado já é float
                return left_type;
            } else if (bin->op == "==" || bin->op == "!=" || bin->op == "<" || 
                       bin->op == ">" || bin->op == "<=" || bin->op == ">=") {
                // Operadores de comparação retornam bool
                return gettyptr("bool");
            } else if (bin->op == "&&" || bin->op == "||") {
                // Operadores lógicos retornam bool
                unify_ctx.unify(left_type, gettyptr("bool"));
                return gettyptr("bool");
            }
            
            return left_type;
        }
        case NodeType::CallExpression: {
            const auto* call = static_cast<CallExprNode*>(node);
            auto func_type = infer_expr(call->caller.get());
            func_type = unify_ctx.resolve(func_type);
            
            // Se não for função, criar tipo de função com variáveis de tipo
            if (func_type->kind != Kind::LABEL) {
                // Tentar unificar com tipo de função
                auto ret_type = unify_ctx.new_type_var();
                std::vector<std::shared_ptr<Type>> param_types;
                for (const auto& arg : call->args) {
                    param_types.push_back(infer_expr(arg.get()));
                }
                auto expected_func = std::make_shared<Label>(param_types, ret_type);
                try {
                    unify_ctx.unify(func_type, expected_func);
                } catch (std::runtime_error& e) {
                    throw std::runtime_error("Call expression type error: " + std::string(e.what()));
                }
                func_type = unify_ctx.resolve(func_type);
                if (func_type->kind == Kind::LABEL) {
                    auto label = std::static_pointer_cast<Label>(func_type);
                    return label->returntype;
                }
            } else {
                auto label = std::static_pointer_cast<Label>(func_type);
                
                // Verificar se é uma função builtin com argumentos opcionais
                bool is_builtin_varargs = false;
                std::string func_name;
                if (call->caller->kind == NodeType::Identifier) {
                    auto* id = static_cast<IdentifierNode*>(call->caller.get());
                    func_name = id->symbol;
                    // Verificar se é builtin com varargs
                    auto it = std::find_if(BUILTIN_FUNCTIONS.begin(), BUILTIN_FUNCTIONS.end(),
                        [&func_name](const BuiltinFunction& b) { return b.name == func_name; });
                    if (it != BUILTIN_FUNCTIONS.end() && it->accepts_varargs) {
                        is_builtin_varargs = true;
                        if (!builtin_accepts_args(*it, call->args.size())) {
                            throw std::runtime_error("Function '" + func_name + "' argument count mismatch: " +
                                                    std::to_string(it->min_args) + " to " + 
                                                    (it->max_args == 0 ? "unlimited" : std::to_string(it->max_args)) +
                                                    " expected, got " + std::to_string(call->args.size()));
                        }
                    }
                }
                
                // Verificar número de argumentos
                if (!is_builtin_varargs && call->args.size() != label->paramstype.size()) {
                    throw std::runtime_error("Function call argument count mismatch: expected " + 
                                            std::to_string(label->paramstype.size()) + 
                                            ", got " + std::to_string(call->args.size()));
                }
                
                // Unificar tipos dos argumentos com tipos dos parâmetros
                // Para funções com varargs, unificar apenas os argumentos fornecidos
                if (call->args.size() > 0) {
                    size_t max_args = std::min(call->args.size(), label->paramstype.size());
                    for (size_t i = 0; i < max_args; i++) {
                        auto arg_type = infer_expr(call->args[i].get());
                        try {
                            unify_ctx.unify(arg_type, label->paramstype[i]);
                        } catch (std::runtime_error& e) {
                            throw std::runtime_error("Function call argument type error: " + std::string(e.what()));
                        }
                    }
                }
                return label->returntype;
            }
            
            return unify_ctx.new_type_var();
        }
        case NodeType::ArrayExpression: {
            const auto* arr = static_cast<ArrayExprNode*>(node);
            
            // Array vazio ou com tamanho variável → Vector
            if (arr->elements.empty()) {
                return std::make_shared<Vector>();
            }
            
            // Inferir tipo do primeiro elemento
            auto first_type = infer_expr(arr->elements[0].get());
            first_type = unify_ctx.resolve(first_type);
            
            // Tentar unificar todos os elementos com o primeiro
            bool all_same_type = true;
            for (size_t i = 1; i < arr->elements.size(); i++) {
                auto elem_type = infer_expr(arr->elements[i].get());
                elem_type = unify_ctx.resolve(elem_type);
                
                // Verificar se tipos são compatíveis (mesmo tipo ou coerção int->float)
                bool types_compatible = false;
                if (first_type->equals(elem_type)) {
                    types_compatible = true;
                } else {
                    // Verificar coerção int -> float
                    bool first_is_int = first_type->kind == Kind::INT;
                    bool first_is_float = first_type->kind == Kind::FLOAT;
                    bool elem_is_int = elem_type->kind == Kind::INT;
                    bool elem_is_float = elem_type->kind == Kind::FLOAT;
                    
                    if ((first_is_int && elem_is_float) || (first_is_float && elem_is_int)) {
                        types_compatible = true;
                        // Promover para float
                        if (first_is_int) {
                            first_type = gettyptr("float");
                        }
                    }
                }
                
                if (!types_compatible) {
                    // Tipos incompatíveis → Vector
                    all_same_type = false;
                    break;
                }
            }
            
            // Se todos têm mesmo tipo (ou coerção válida), criar Array com tamanho fixo
            if (all_same_type) {
                first_type = unify_ctx.resolve(first_type);
                // Verificar se não é variável de tipo não resolvida
                if (first_type->kind != Kind::TYPE_VAR) {
                    return std::make_shared<Array>(first_type, arr->elements.size());
                }
            }
            
            // Caso contrário, criar Vector (heterogêneo ou tamanho variável)
            return std::make_shared<Vector>();
        }
        case NodeType::VectorExpression: {
            // VectorExpression sempre cria Vector (tamanho variável, heterogêneo)
            const auto* vec = static_cast<VectorExprNode*>(node);
            return std::make_shared<Vector>();
        }
        case NodeType::TupleExpression: {
            const auto* tup = static_cast<TupleExprNode*>(node);
            std::vector<std::shared_ptr<Type>> elem_types;
            for (const auto& elem : tup->elements) {
                elem_types.push_back(infer_expr(elem.get()));
            }
            return std::make_shared<Tuple>(elem_types);
        }
        case NodeType::AssignmentExpression: {
            const auto* assign = static_cast<AssignmentExprNode*>(node);
            auto left_type = infer_expr(assign->target.get());
            auto right_type = infer_expr(assign->value.get());
            
            // Resolver tipos antes de unificar
            left_type = unify_ctx.resolve(left_type);
            right_type = unify_ctx.resolve(right_type);
            
            // Verificar coerção implícita int -> float
            bool left_is_int = left_type->kind == Kind::INT;
            bool left_is_float = left_type->kind == Kind::FLOAT;
            bool right_is_int = right_type->kind == Kind::INT;
            bool right_is_float = right_type->kind == Kind::FLOAT;
            
            // Se um é int e outro é float, promover int para float
            if (left_is_int && right_is_float) {
                left_type = gettyptr("float");
            } else if (left_is_float && right_is_int) {
                right_type = gettyptr("float");
            }
            
            try {
                unify_ctx.unify(left_type, right_type);
            } catch (std::runtime_error& e) {
                throw std::runtime_error("Assignment type error: " + std::string(e.what()));
            }
            
            // Resolver tipo após unificação
            right_type = unify_ctx.resolve(right_type);
            return right_type;
        }
        default:
            // Para outros tipos, usar verificação tradicional
            return check_node(node);
    }
}