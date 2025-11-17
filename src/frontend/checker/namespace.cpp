#include "frontend/checker/namespace.hpp"
#include <stdexcept>

bool rph::Namespace::has_key(std::string k) {
    return names.find(k) != names.end();
}

bool rph::Namespace::is_const(std::string k) {
    return consts.find(k) != consts.end();
}

std::shared_ptr<rph::Type>& rph::Namespace::get_key(const std::string& k) {
    if (!has_key(k)) {
        if (parent) return parent->get_key(k);
        throw std::runtime_error(std::string("Key '") + k + "' not found.");
    }
    return names[k];
}

void rph::Namespace::put_key(const std::string& k, const std::shared_ptr<rph::Type>& v, bool islocked) {
    if (!is_const(k)) {
        throw std::runtime_error(std::string("'") + k + "' can not be changed.");
    }

    names[k] = v;
    if (islocked)
        consts[k] = true;
}

void rph::Namespace::put_key(const std::string& k, const std::shared_ptr<rph::Type>& v) {
    put_key(k, v, false);
};

void rph::Namespace::set_key(const std::string& k, const std::shared_ptr<rph::Type>& v) {
    if (!is_const(k)) {
        throw std::runtime_error(std::string("'") + k + "' can not be changed.");
    }

    names[k] = v;
}