#include "frontend/checker/type.hpp"
#include "frontend/checker/namespace.hpp"
#include "frontend/ast/ast.hpp"
#include <vector>
#include <unordered_map>
namespace rph {
    class Checker {
        private:
            std::vector<std::shared_ptr<rph::Namespace>> namespaces;
            std::shared_ptr<rph::Namespace> scope;
            std::unordered_map<std::string, std::unique_ptr<rph::Type>> types;
        public:
            Checker();
            void getty(std::string ty);
            void push_scope();
            void pop_scope();
            std::unique_ptr<rph::Type> check_node(std::unique_ptr<Node> node);
    };
} 