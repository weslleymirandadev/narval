#include "backend/codegen/method_handler.hpp"
#include "backend/codegen/generate_ir.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "llvm/IR/IRBuilder.h"

namespace nv {

class BuiltinHandler : public MethodHandler {
public:
    llvm::Value* handle(nv::IRGenerationContext& ctx, const std::string& method, const std::vector<std::unique_ptr<Node>>& args) override {
        auto& b = ctx.get_builder();
        auto* ValueTy = nv::ir_utils::get_value_struct(ctx);
        
        if (method == "write") {
            nv::register_feature("write");
            
            if (args.empty()) return nullptr;
            args[0]->codegen(ctx);
            if (!ctx.has_value()) return nullptr;
            
            llvm::Value* arg = ctx.pop_value();
            auto* write_fn = ctx.get_module().getFunction("write");
            if (!write_fn) return nullptr;
            
            b.CreateCall(write_fn, {arg});
            return nullptr; // write retorna void
            
        } else if (method == "read") {
            nv::register_feature("read");
            
            auto* read_fn = ctx.get_module().getFunction("read");
            if (!read_fn) return nullptr;
            
            auto* result = b.CreateCall(read_fn, {});
            return result;
        }
        
        return nullptr;
    }
    
    bool can_handle(const std::string& method) const override {
        return method == "write" || method == "read";
    }
};

// Registrar o handler automaticamente
static struct BuiltinHandlerRegistrar {
    BuiltinHandlerRegistrar() {
        register_method_handler(std::make_unique<BuiltinHandler>());
    }
} g_builtin_handler_registrar;

}
