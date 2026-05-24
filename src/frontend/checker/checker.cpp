#include "frontend/checker/checker.hpp"
#include <sstream>
#include "frontend/checker/type.hpp"
#include "frontend/checker/unification.hpp"
#include "frontend/checker/builtins.hpp"
#include "frontend/attributes/attribute_mapper.hpp"
#include "frontend/checker/expressions/check_call_expr.hpp"
#include "frontend/checker/expressions/check_primary_expr.hpp"
#include "frontend/checker/expressions/check_binary_expr.hpp"
#include "frontend/checker/expressions/check_assignment_expr.hpp"
#include "frontend/checker/expressions/check_array_expr.hpp"
#include "frontend/checker/expressions/check_tuple_expr.hpp"
#include "frontend/checker/expressions/check_vector_expr.hpp"
#include "frontend/checker/checker_meth.hpp"
#include "frontend/ast/ast.hpp"
#include "frontend/syntax_highlighter.hpp"
#include <memory>
#include <unordered_set>
#include <algorithm>
#include <fstream>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <sstream>

constexpr size_t MAX_LINE_LENGTH = 1024;
constexpr const char* ANSI_BOLD  = "\x1b[1m";
constexpr const char* ANSI_RESET = "\x1b[0m";
constexpr const char* ANSI_RED   = "\x1b[31m";
constexpr const char* ANSI_BLUE  = "\x1b[34m";

// Conjunto estático para rastrear erros de identificador já reportados (evitar duplicação entre checkers/ASTs clonados)
// Usa chave composta: filename:line:col:symbol
static std::unordered_set<std::string> reported_identifier_errors;

namespace {
    // Converte um caminho relativo em absoluto
    std::string to_absolute_path(const std::string& path) {
        if (path.empty()) {
            return path;
        }
        if (path.rfind("repl[", 0) == 0 || path.rfind("notebook[", 0) == 0 || path.rfind("repl_line_", 0) == 0 || path.rfind("cell_", 0) == 0) {
            return path;
        }
        
        try {
            std::filesystem::path file_path(path);
            
            // Se já é absoluto, tentar normalizar
            if (file_path.is_absolute()) {
                try {
                    return std::filesystem::canonical(file_path).string();
                } catch (const std::filesystem::filesystem_error&) {
                    return std::filesystem::absolute(file_path).string();
                }
            }
            
            // Se é relativo, converter para absoluto
            try {
                return std::filesystem::canonical(std::filesystem::absolute(file_path)).string();
            } catch (const std::filesystem::filesystem_error&) {
                return std::filesystem::absolute(file_path).string();
            }
        } catch (const std::exception&) {
            // Se falhar, retornar o caminho original
            return path;
        }
    }
}

nv::Checker::Checker() {
    err = false;  // Inicializar flag de erro
    reported_errors.clear();  // Inicializar conjunto de erros reportados
    auto globalnamespace = std::make_shared<Namespace>();
    namespaces.push_back(globalnamespace);
    scope = globalnamespace;
    types["int"] = std::make_shared<nv::Int>();
    types["str"] = std::make_shared<nv::String>();
    types["char"] = std::make_shared<nv::Char>();
    types["float"] = std::make_shared<nv::Float>();
    types["bool"] = std::make_shared<nv::Boolean>();
    types["None"] = std::make_shared<nv::None>();

    // Tipos low-level para funções @[abi(...)] e blocos asm
    auto make_ll = [](const std::string& n, bool s, int b) {
        return std::make_shared<nv::LowLevelType>(n, s, b);
    };
    types["i8"]    = make_ll("i8",    true,  8);
    types["i16"]   = make_ll("i16",   true,  16);
    types["i32"]   = make_ll("i32",   true,  32);
    types["i64"]   = make_ll("i64",   true,  64);
    types["i128"]  = make_ll("i128",  true,  128);
    types["u8"]    = make_ll("u8",    false, 8);
    types["u16"]   = make_ll("u16",   false, 16);
    types["u32"]   = make_ll("u32",   false, 32);
    types["u64"]   = make_ll("u64",   false, 64);
    types["u128"]  = make_ll("u128",  false, 128);
    types["f32"]   = make_ll("f32",   true,  32);
    types["f64"]   = make_ll("f64",   true,  64);
    types["usize"] = make_ll("usize", false, 64);
    types["isize"] = make_ll("isize", true,  64);
    types["ptr"]   = make_ll("ptr",   false, 0);
    // Tipo function genérico (params desconhecidos, retorno desconhecido)
    types["function"] = std::make_shared<nv::Function>(
        std::vector<std::shared_ptr<nv::Type>>{}, 
        types["None"]
    );

    // Agora que os objetos Type estão dentro de shared_ptr, inicializar seus prototypes
    types["int"]->init_prototype();
    types["str"]->init_prototype();
    types["char"]->init_prototype();
    types["float"]->init_prototype();
    types["bool"]->init_prototype();
    types["None"]->init_prototype();
    
    // Registrar classe Error builtin
    auto error_class = std::make_shared<nv::Class>("Error");
    error_class->add_field("message", types["str"], false);
    error_class->init_prototype();
    types["Error"] = error_class;
    scope->put_key("Error", error_class, true);

    // Registrar funções builtin do runtime
    register_builtins(*this);
}

void nv::Checker::apply_compilation_attributes(const CompilationAttributes& attrs) {
    if (!attrs.no_std) {
        return;
    }

    no_std_attr_node = attrs.no_std_node;
    static const std::vector<std::string> runtime_symbols = {
        "write", "read", "exit", "str", "int", "char", "float", "bool",
        "Some", "Ok", "Err", "json", "Error"
    };
    for (const auto& name : runtime_symbols) {
        scope->erase_key(name);
    }
}

nv::Type& nv::Checker::getty(std::string ty) {
    return *types.at(ty);
}

std::shared_ptr<nv::Type>& nv::Checker::gettyptr(std::string ty, Node* error_node){
        // Verificar se já existe no cache
    auto it = types.find(ty);
    if (it != types.end()) {
        return it->second;
    }
    
    // Parsear tipos compostos: vector, int[10], str[5], etc.
    
    // Tipo vector
    if (ty == "vector") {
        auto vec_type = std::make_shared<nv::Vector>();
        vec_type->init_prototype();
        types[ty] = vec_type;
        return types[ty];
    }
    
    // Tensor<elem_type, [d0, d1, ...]> — must be before the array bracket check
    // because "Tensor<float, [2, 3]>" contains '[' which would fool the array check.
    if (ty.size() > 8 && ty.substr(0, 7) == "Tensor<" && ty.back() == '>') {
        std::string inner = ty.substr(7, ty.size() - 8);
        auto bracket = inner.find(", [");
        if (bracket != std::string::npos) {
            std::string elem_str  = inner.substr(0, bracket);
            std::string shape_str = inner.substr(bracket + 3);
            if (!shape_str.empty() && shape_str.back() == ']')
                shape_str.pop_back();
            std::vector<int64_t> dims;
            std::istringstream iss(shape_str);
            std::string tok;
            while (std::getline(iss, tok, ',')) {
                size_t s = tok.find_first_not_of(" \t");
                size_t e = tok.find_last_not_of(" \t");
                if (s == std::string::npos) continue;
                tok = tok.substr(s, e - s + 1);
                if (tok == "?" || tok == "-1")
                    dims.push_back(-1);
                else {
                    try { dims.push_back(std::stoll(tok)); }
                    catch (...) { dims.push_back(-1); }
                }
            }
            auto& elem_type = gettyptr(elem_str, error_node);
            auto tensor = std::make_shared<nv::TensorType>(elem_type, dims);
            types[ty] = tensor;
            return types[ty];
        }
    }

    // Tipo array: int[10], str[5], etc.
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
                    auto& base_type = gettyptr(base_type_str, error_node);
                    auto arr_type = std::make_shared<nv::Array>(base_type, size);
                    arr_type->init_prototype();
                    types[ty] = arr_type;
                    return types[ty];
                }
            } catch (...) {
                // Não é um número válido
            }
        }
    }
    
    // Option<T>
    if (ty.size() > 8 && ty.substr(0, 7) == "Option<" && ty.back() == '>') {
        std::string inner = ty.substr(7, ty.size() - 8);
        auto& elem = gettyptr(inner, error_node);
        auto opt = std::make_shared<nv::Option>(elem);
        types[ty] = opt;
        return types[ty];
    }

    // Result<T, E>
    if (ty.size() > 8 && ty.substr(0, 7) == "Result<" && ty.back() == '>') {
        std::string inner = ty.substr(7, ty.size() - 8);
        auto comma = inner.find(", ");
        if (comma != std::string::npos) {
            auto ok_s  = inner.substr(0, comma);
            auto err_s = inner.substr(comma + 2);
            auto& ok_t  = gettyptr(ok_s, error_node);
            auto& err_t = gettyptr(err_s, error_node);
            auto res = std::make_shared<nv::Result>(ok_t, err_t);
            types[ty] = res;
            return types[ty];
        }
    }
    
    // Future<T>
    if (ty.size() > 8 && ty.substr(0, 7) == "Future<" && ty.back() == '>') {
        std::string inner = ty.substr(7, ty.size() - 8);
        auto& elem = gettyptr(inner, error_node);
        auto fut = std::make_shared<nv::Future>(elem);
        types[ty] = fut;
        return types[ty];
    }

    // Classe genérica instanciada: Box<int>, Container<str, int>, etc.
    // Formato: Name<T1, T2, ...>  — Name não é nenhum tipo builtin acima
    {
        auto lt = ty.find('<');
        if (lt != std::string::npos && ty.back() == '>') {
            std::string base_name = ty.substr(0, lt);
            // Extrair argumentos de tipo (respeitando aninhamento)
            std::string args_str = ty.substr(lt + 1, ty.size() - lt - 2);
            std::vector<std::string> arg_strs;
            int depth = 0;
            std::string cur;
            for (char c : args_str) {
                if (c == '<') { depth++; cur += c; }
                else if (c == '>') { depth--; cur += c; }
                else if (c == ',' && depth == 0) {
                    // trim
                    size_t s = cur.find_first_not_of(' ');
                    size_t e = cur.find_last_not_of(' ');
                    if (s != std::string::npos) arg_strs.push_back(cur.substr(s, e - s + 1));
                    cur.clear();
                } else {
                    cur += c;
                }
            }
            if (!cur.empty()) {
                size_t s = cur.find_first_not_of(' ');
                size_t e = cur.find_last_not_of(' ');
                if (s != std::string::npos) arg_strs.push_back(cur.substr(s, e - s + 1));
            }

            auto def_it = generic_class_defs.find(base_name);
            if (def_it != generic_class_defs.end()) {
                const auto& def = def_it->second;
                if (arg_strs.size() == def.type_param_ids.size()) {
                    // Construir substituição: TypeVar id → tipo concreto
                    std::unordered_map<int, std::shared_ptr<nv::Type>> subst;
                    for (size_t i = 0; i < def.type_param_ids.size(); i++) {
                        subst[def.type_param_ids[i]] = gettyptr(arg_strs[i], error_node);
                    }
                    // Criar classe concreta com campos substituídos
                    auto inst = std::make_shared<nv::Class>(ty);
                    inst->parent_class   = def.class_type->parent_class;
                    inst->is_abstract    = def.class_type->is_abstract;
                    inst->is_builtin_derived = def.class_type->is_builtin_derived;
                    for (auto& [fname, ftype] : def.class_type->fields) {
                        inst->fields[fname] = ftype->substitute(subst);
                    }
                    for (auto& [mname, mtype] : def.class_type->methods) {
                        inst->methods[mname] = mtype->substitute(subst);
                    }
                    inst->method_access  = def.class_type->method_access;
                    inst->init_prototype();
                    types[ty] = inst;
                    return types[ty];
                }
            }
        }
    }

    // Tipo função: |param1: type1, param2: type2|: return_type
    if (nv::is_function_type(ty)) {
        try {
            auto func_type = nv::parse_function_type(ty, [this](const std::string& type_str) -> std::shared_ptr<nv::Type> {
                return gettyptr(type_str);
            });
            types[ty] = func_type;
            return types[ty];
        } catch (const std::exception& e) {
            std::string abs_filename = to_absolute_path(current_filename);
            std::cerr << ANSI_BOLD << abs_filename << ": "
                      << ANSI_RED << "ERROR" << ANSI_RESET << ANSI_BOLD << ": "
                      << "Invalid function type: " << ty << " - " << e.what() << ANSI_RESET << "\n\n";
            err = true;
            return types["None"];
        }
    }

    // Tipo não encontrado — usar nó explícito, senão o nó corrente como fallback
    Node* report_node = error_node ? error_node : current_node;
    if (report_node) {
        error(report_node, "Unknown type: '" + ty + "'");
    } else {
        std::string abs_filename = to_absolute_path(current_filename);
        std::cerr << ANSI_BOLD << abs_filename << ": "
                  << ANSI_RED << "ERROR" << ANSI_RESET << ANSI_BOLD << ": "
                  << "Unknown type: '" << ty << "'" << ANSI_RESET << "\n\n";
        err = true;
    }
    return types["None"];
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
    if (!pos || lines.empty() || pos->line == 0 || pos->line - 1 >= line_count) {
        return;
    }

    std::string line_content = lines[pos->line - 1];
    std::replace(line_content.begin(), line_content.end(), '\n', ' ');

    std::cerr << " " << pos->line << " |   "
              << nv::syntax_highlighter::highlight_line(line_content) << "\n";

    int line_width = pos->line > 0 ? static_cast<int>(std::log10(pos->line) + 1) : 1;
    std::cerr << std::string(line_width, ' ') << "  |";
    std::cerr << std::string(pos->col[0] - 1 + 3, ' ');

    std::cerr << ANSI_RED;
    for (size_t i = pos->col[0]; i < pos->col[1]; ++i) {
        std::cerr << "^";
    }
    std::cerr << ANSI_RESET << "\n\n";
}

void nv::Checker::set_source_file(const std::string& filename) {
    current_filename = filename;
    read_lines(filename);
}

void nv::Checker::set_emit_diagnostics(bool enabled) {
    emit_diagnostics = enabled;
}

void nv::Checker::error(Node* node, const std::string& message) {
    // Evitar reportar o mesmo erro duas vezes usando o ponteiro do nó
    // O ponteiro do nó é único e não muda, então é a forma mais confiável de identificar o mesmo erro
    if (node && reported_errors.find(reinterpret_cast<const void*>(node)) != reported_errors.end()) {
        err = true;  // Manter flag de erro, mas não reportar novamente
        return;
    }
    
    std::string abs_filename = to_absolute_path(current_filename);
    
    // Marcar este nó como tendo tido erro reportado ANTES de reportar
    // para evitar que seja reportado novamente em chamadas recursivas
    if (node) {
        reported_errors.insert(reinterpret_cast<const void*>(node));
    }
    
    if (!node || !node->position) {
        diagnostics.push_back({abs_filename, 1, 1, 1, 1, message});
        if (emit_diagnostics) {
            std::cerr << ANSI_BOLD << abs_filename << ": "
                      << ANSI_RED << "ERROR" << ANSI_RESET << ANSI_BOLD << ": "
                      << message << ANSI_RESET << "\n\n";
        }
        err = true;
        if (emit_diagnostics) std::cerr.flush();
        return;
    }
    
    PositionData* pos = node->position.get();
    diagnostics.push_back({abs_filename, pos->line, pos->col[0], pos->col[1], 1, message});
    if (emit_diagnostics) {
        std::cerr << ANSI_BOLD
                  << abs_filename << ":" << pos->line << ":" << pos->col[0] << ": "
                  << ANSI_RED << "ERROR" << ANSI_RESET << ANSI_BOLD << ": "
                  << message << ANSI_RESET << "\n";

        print_error_context(pos);
    }
    err = true;
    if (emit_diagnostics) std::cerr.flush();  // Garantir que a mensagem foi exibida antes de continuar
}

void nv::Checker::error_at(const std::string& filename, size_t line,
                           size_t col_start, size_t col_end,
                           const std::string& message) {
    std::string abs_filename = to_absolute_path(filename.empty() ? current_filename : filename);
    diagnostics.push_back({abs_filename, line, col_start, col_end, 1, message});
    if (emit_diagnostics) {
        std::cerr << ANSI_BOLD
                  << abs_filename << ":" << line << ":" << col_start << ": "
                  << ANSI_RED << "ERROR" << ANSI_RESET << ANSI_BOLD << ": "
                  << message << ANSI_RESET << "\n";

        PositionData pos(line, col_start, col_end, 0, 0, abs_filename);
        print_error_context(&pos);
    }
    err = true;
    if (emit_diagnostics) std::cerr.flush();
}

void nv::Checker::note_at(const std::string& filename, size_t line,
                           size_t col_start, size_t col_end,
                           const std::string& message) {
    std::string abs_filename = to_absolute_path(filename);
    diagnostics.push_back({abs_filename, line, col_start, col_end, 3, message});
    if (!emit_diagnostics) return;

    std::cerr << ANSI_BOLD
              << abs_filename << ":" << line << ":" << col_start << ": "
              << ANSI_BLUE << "NOTE" << ANSI_RESET << ANSI_BOLD << ": "
              << message << ANSI_RESET << "\n";

    // Obter linhas do arquivo-fonte (pode ser diferente do arquivo atual)
    std::vector<std::string>* src_lines = nullptr;
    std::vector<std::string> file_lines;
    if (filename == current_filename) {
        src_lines = &lines;
    } else {
        std::ifstream f(filename);
        if (f.is_open()) {
            std::string ln;
            while (std::getline(f, ln)) file_lines.push_back(ln);
            src_lines = &file_lines;
        }
    }

    if (src_lines && line > 0 && line - 1 < src_lines->size()) {
        std::string line_content = (*src_lines)[line - 1];
        std::replace(line_content.begin(), line_content.end(), '\n', ' ');
        std::cerr << " " << line << " |   "
                  << nv::syntax_highlighter::highlight_line(line_content) << "\n";
        int line_width = line > 0 ? static_cast<int>(std::log10(static_cast<double>(line)) + 1) : 1;
        std::cerr << std::string(line_width, ' ') << "  |";
        if (col_start > 0)
            std::cerr << std::string(col_start - 1 + 3, ' ');
        std::cerr << ANSI_BLUE;
        for (size_t i = col_start; i < col_end; ++i) std::cerr << "^";
        std::cerr << ANSI_RESET << "\n\n";
    }
    std::cerr.flush();
}

void nv::Checker::no_std_error(Node* use_node, const std::string& feature_name) {
    error(use_node, "'" + feature_name + "' not available with @[no_std].");
    if (no_std_attr_node && no_std_attr_node->position) {
        auto* pos = no_std_attr_node->position.get();
        const std::string& fn = pos->filename.empty() ? current_filename : pos->filename;
    note_at(fn, pos->line, pos->col[0], pos->col[1], "@[no_std] declared here.");
    }
}

std::shared_ptr<nv::Type> nv::Checker::infer_type(Node* node) {
    return infer_expr(node);
}

std::shared_ptr<nv::Type> nv::Checker::infer_expr(Node* node) {
    // Não parar mesmo se houver erros anteriores - continuar verificando
    // para reportar todos os erros possíveis
    // O método error() já previne duplicação usando reported_errors
    
    switch (node->kind) {
        case NodeType::NumericLiteral:
        case NodeType::StringLiteral:
        case NodeType::BooleanLiteral:
        case NodeType::Identifier: {
            auto& result = check_primary_expr(this, node);
            return result;
        }
        
        case NodeType::BinaryExpression:
            return check_binary_expr(this, node);
        
        case NodeType::CallExpression: {
            // Delegar para check_call_expr para evitar duplicação de lógica e erros duplicados
            // check_call_expr já faz toda a verificação necessária e reporta erros corretamente
            auto& result = check_call_expr(this, node);
            return result;
        }
        
        case NodeType::ArrayExpression:
            return check_array_expr(this, node);
        
        case NodeType::VectorExpression:
            return check_vector_expr(this, node);
        
        case NodeType::TupleExpression:
            return check_tuple_expr(this, node);
        
        case NodeType::AssignmentExpression:
            return check_assignment_expr(this, node);
        
        default:
            // Para outros tipos, usar verificação tradicional
            return check_node(node);
    }
}
