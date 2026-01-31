#include "frontend/checker/namespace.hpp"
#include "frontend/checker/type.hpp"
#include <stdexcept>

bool nv::Namespace::has_key(std::string k) {
    return names.find(k) != names.end();
}

bool nv::Namespace::is_const(std::string k) {
    return consts.find(k) != consts.end();
}

std::shared_ptr<nv::Type>& nv::Namespace::get_key(const std::string& k) {
    if (!has_key(k)) {
        if (parent) return parent->get_key(k);
        throw std::runtime_error(std::string("Key '") + k + "' not found.");
    }
    return names[k];
}

void nv::Namespace::put_key(const std::string& k, const std::shared_ptr<nv::Type>& v, bool islocked) {
    if (is_const(k)) {
        throw std::runtime_error(std::string("'") + k + "' can not be changed.");
    }

    names[k] = v;
    if (islocked)
        consts[k] = true;
}

void nv::Namespace::put_key(const std::string& k, const std::shared_ptr<nv::Type>& v) {
    put_key(k, v, false);
};

void nv::Namespace::set_key(const std::string& k, const std::shared_ptr<nv::Type>& v) {
    if (!is_const(k)) {
        throw std::runtime_error(std::string("'") + k + "' can not be changed.");
    }

    names[k] = v;
}

void nv::Namespace::collect_free_vars(std::unordered_set<int>& free_vars) const {
    // Coletar variáveis livres de todas as variáveis neste namespace
    for (const auto& pair : names) {
        const auto& type = pair.second;
        if (type) {
            // Se for tipo polimórfico, coletar apenas variáveis livres (não quantificadas)
            if (type->kind == Kind::POLY_TYPE) {
                auto poly = std::static_pointer_cast<const PolyType>(type);
                poly->collect_free_vars(free_vars);
            } else {
                // Para tipos não polimórficos, coletar todas as variáveis livres
                type->collect_free_vars(free_vars);
            }
        }
    }
    
    // Coletar também do namespace pai (escopo superior)
    if (parent) {
        parent->collect_free_vars(free_vars);
    }
}