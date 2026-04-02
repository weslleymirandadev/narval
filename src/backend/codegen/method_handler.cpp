#include "backend/codegen/method_handler.hpp"
#include <unordered_map>
#include <memory>

namespace nv {

static std::unordered_map<std::string, std::unique_ptr<MethodHandler>> g_method_handlers;

void register_method_handler(std::unique_ptr<MethodHandler> handler) {
    // Registrar handler para todos os métodos que ele pode handle
    // Isso precisa ser implementado quando tivermos os handlers específicos
}

MethodHandler* get_method_handler(const std::string& method) {
    auto it = g_method_handlers.find(method);
    return (it != g_method_handlers.end()) ? it->second.get() : nullptr;
}

}
