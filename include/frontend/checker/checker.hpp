#pragma once
#include "frontend/checker/type.hpp"
#include "frontend/checker/namespace.hpp"
#include "frontend/checker/unification.hpp"
#include "frontend/ast/ast.hpp"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>

namespace nv {
    class Checker {
        private:
            std::vector<std::string> lines;
            size_t line_count = 0;
            
            void read_lines(const std::string& filename);
            void print_error_context(const PositionData* pos);
            
        public:
            std::vector<std::shared_ptr<nv::Namespace>> namespaces;
            std::shared_ptr<nv::Namespace> scope;
            std::unordered_map<std::string, std::shared_ptr<Type>> types;
            // Map from function name to ordered list of parameter names (for keyword args)
            std::unordered_map<std::string, std::vector<std::string>> function_param_names;
            // Map from function name to ordered list of parameter default flags (for default values)
            std::unordered_map<std::string, std::vector<bool>> function_param_defaults;
            // Map from function name to ordered list of default value expressions (for codegen)
            std::unordered_map<std::string, std::vector<std::unique_ptr<Expr>>> function_default_values;
            UnificationContext unify_ctx;
            bool err;
            std::string current_filename;  // Nome do arquivo fonte atual (para erros e resolução de imports)
            // Usar ponteiro do nó como chave para evitar duplicação - o ponteiro é único e não muda
            std::unordered_set<const void*> reported_errors;  // Nós que já tiveram erros reportados (usando ponteiro como chave)
            // Rastrear tipo de retorno da função atual (para verificação de return statements)
            std::shared_ptr<Type> current_return_type = nullptr;
            // Rastrear classe atual sendo verificada (para controle de acesso)
            std::string current_class_name = "";
            // Rastrear se estamos dentro de um bloco `or { }` (return é permitido lá)
            int or_block_depth = 0;
            // Namespace aliases de wildcard imports: alias -> (nome_membro -> tipo)
            std::unordered_map<std::string, std::map<std::string, std::shared_ptr<Type>>> import_namespaces;
            Checker();
            nv::Type& getty(std::string ty);
            std::shared_ptr<nv::Type>& gettyptr(std::string ty);
            void push_scope();
            void pop_scope();
            std::shared_ptr<nv::Type>& check_node(Node* node);
            
            // Inferência de tipos
            std::shared_ptr<Type> infer_type(Node* node);
            std::shared_ptr<Type> infer_expr(Node* node);
            std::unordered_set<int> get_free_vars_in_env();
            
            // Gerenciamento de arquivo fonte para erros
            void set_source_file(const std::string& filename);
            void error(Node* node, const std::string& message);
    };
} 