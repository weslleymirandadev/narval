#include "frontend/checker/checker.hpp"
#include "frontend/checker/type.hpp"
#include <memory>

rph::Checker::Checker() {
    auto globalnamespace = std::make_shared<Namespace>();
    namespaces.push_back(globalnamespace);
    scope = globalnamespace;
    types["int"] = std::make_shared<rph::Int>();
    types["string"] = std::make_shared<rph::String>();
    types["float"] = std::make_shared<rph::Float>();
    types["bool"] = std::make_shared<rph::Boolean>();
    types["void"] = std::make_shared<rph::Void>();
}

std::shared_ptr<rph::Type>& rph::Checker::getty(std::string k) {
    return types.at(k);
}

void rph::Checker::push_scope() {
    auto ns = std::make_shared<Namespace>(scope);
    namespaces.push_back(ns);
    scope = ns;
}

void rph::Checker::pop_scope() {
    namespaces.pop_back();
    scope = namespaces[namespaces.size() - 1];
}