#pragma once
namespace nv {
    struct Type;
}
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace nv {
    class Namespace {
        private:
            std::unordered_map<std::string, std::shared_ptr<Type>> names;
            std::unordered_map<std::string, bool> consts;
            std::shared_ptr<Namespace> parent;
        public:
            Namespace(std::shared_ptr<Namespace> prnt) : parent(prnt) {};
            Namespace() : parent(nullptr) {};
            std::shared_ptr<nv::Type>& get_key(const std::string& k);
            bool has_key(std::string k);
            bool is_const(std::string k);
<<<<<<< HEAD
            void put_key(const std::string& k, const std::shared_ptr<nv::Type>& v, bool ismutable);
=======
            void put_key(const std::string& k, const std::shared_ptr<nv::Type>& v, bool islocked);
>>>>>>> 7d7b28c04a119a9c000597cd586b6688408f92d1
            void put_key(const std::string& k, const std::shared_ptr<nv::Type>& v);
            void set_key(const std::string& k, const std::shared_ptr<nv::Type>& v);
            
            // Coletar variáveis de tipo livres de todas as variáveis neste namespace e nos pais
            void collect_free_vars(std::unordered_set<int>& free_vars) const;
    };
}