#include "backend/codegen/method_handler.hpp"
#include <unordered_map>
#include <memory>
#include <vector>

namespace nv {

static std::vector<std::unique_ptr<MethodHandler>> g_method_handlers;

void register_method_handler(std::unique_ptr<MethodHandler> handler) {
    // Registrar handler para todos os métodos que ele pode handle
    if (handler) {
        g_method_handlers.push_back(std::move(handler));
    }
}

MethodHandler* get_method_handler(const std::string& method) {
    // Procurar um handler que pode handle o método especificado
    for (auto& handler : g_method_handlers) {
        if (handler && handler->can_handle(method)) {
            return handler.get();
        }
    }
    return nullptr;
}

}
