#pragma once
namespace rph {
    struct Type;
}
#include <memory>
#include <string>
#include <unordered_map>

namespace rph {
    class Namespace {
        private:
            std::unordered_map<std::string, std::shared_ptr<struct Type>> names;
            std::unordered_map<std::string, bool> consts;
            std::shared_ptr<Namespace> parent;
        public:
            Namespace(std::shared_ptr<Namespace> prnt) : parent(prnt) {};
            Namespace() : parent(nullptr) {};
            std::shared_ptr<struct Type> get_key(std::string k);
            bool has_key(std::string k);
            bool is_const(std::string k);
            void put_key(std::string k, std::shared_ptr<struct Type> v, bool islocked);
            void put_key(std::string k, std::shared_ptr<struct Type> v);
            void set_key(std::string k, std::shared_ptr<struct Type> v);
    };
}