#pragma once

#include "backend/codegen/ir_context.hpp"
#include "llvm/IR/Value.h"
#include <vector>
#include <memory>

namespace nv {

class Node;

// Interface para handlers de métodos específicos
class MethodHandler {
public:
    virtual ~MethodHandler() = default;
    virtual llvm::Value* handle(nv::IRGenerationContext& ctx, const std::string& method, const std::vector<std::unique_ptr<Node>>& args) = 0;
    virtual bool can_handle(const std::string& method) const = 0;
};

// Funções para registrar e obter handlers
void register_method_handler(std::unique_ptr<MethodHandler> handler);
MethodHandler* get_method_handler(const std::string& method);

}
