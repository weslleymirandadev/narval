#pragma once
#include "frontend/checker/type.hpp"
#include "frontend/checker/namespace.hpp"
#include "frontend/ast/ast.hpp"
#include <vector>
#include <unordered_map>
namespace rph {
    class Checker {
        public:
            std::vector<std::shared_ptr<rph::Namespace>> namespaces;
            std::shared_ptr<rph::Namespace> scope;
            std::unordered_map<std::string, std::shared_ptr<Type>> types;
            Checker();
            std::shared_ptr<rph::Type>& getty(std::string ty);
            void push_scope();
            void pop_scope();
            std::shared_ptr<rph::Type>& check_node(Node* node);
    };
} 