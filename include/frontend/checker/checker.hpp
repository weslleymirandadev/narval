#pragma once
#include "frontend/checker/type.hpp"
#include "frontend/checker/namespace.hpp"
#include "frontend/checker/unification.hpp"
#include "frontend/ast/ast.hpp"
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace rph {
    class Checker {
        public:
            std::vector<std::shared_ptr<rph::Namespace>> namespaces;
            std::shared_ptr<rph::Namespace> scope;
            std::unordered_map<std::string, std::shared_ptr<Type>> types;
            UnificationContext unify_ctx;
            bool err;
            Checker();
            rph::Type& getty(std::string ty);
            std::shared_ptr<rph::Type>& gettyptr(std::string ty);
            void push_scope();
            void pop_scope();
            std::shared_ptr<rph::Type>& check_node(Node* node);
            
            // Inferência de tipos
            std::shared_ptr<Type> infer_type(Node* node);
            std::shared_ptr<Type> infer_expr(Node* node);
            std::unordered_set<int> get_free_vars_in_env();
    };
} 