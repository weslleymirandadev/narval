#pragma once
#include "frontend/checker/type.hpp"
#include "frontend/checker/namespace.hpp"
#include "frontend/checker/unification.hpp"
#include "frontend/ast/ast.hpp"
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace nv {
    class Checker {
        private:
            std::vector<std::string> lines;
            size_t line_count = 0;
            std::string current_filename;
            
            void read_lines(const std::string& filename);
            void print_error_context(const PositionData* pos);
            
        public:
            std::vector<std::shared_ptr<nv::Namespace>> namespaces;
            std::shared_ptr<nv::Namespace> scope;
            std::unordered_map<std::string, std::shared_ptr<Type>> types;
            UnificationContext unify_ctx;
            bool err;
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