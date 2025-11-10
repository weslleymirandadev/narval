#include "frontend/checker/namespace.hpp"
#include <stdexcept>

bool rph::Namespace::has_key(std::string k) {
    return names.find(k) != names.end();
}

bool rph::Namespace::is_const(std::string k) {
    return consts.find(k) != consts.end();
}

std::shared_ptr<struct rph::Type> rph::Namespace::get_key(std::string k) {
    if (!has_key(k)) {
        throw std::runtime_error(std::string("Key '") + k + "' not found.");
    }
    return names[k];
}

void rph::Namespace::put_key(std::string k, std::shared_ptr<struct rph::Type> v, bool islocked) {
    if (!is_const(k)) {
        throw std::runtime_error(std::string("'") + k + "' can not be changed.");
    }

    names[k] = v;
    if (islocked)
        consts[k] = true;
}

void rph::Namespace::put_key(std::string k, std::shared_ptr<struct Type> v) {
    put_key(k, v, false);
};

void rph::Namespace::set_key(std::string k, std::shared_ptr<struct rph::Type> v) {
    if (!is_const(k)) {
        throw std::runtime_error(std::string("'") + k + "' can not be changed.");
    }

    names[k] = v;
}