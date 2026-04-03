#include "backend/codegen/method_handler.hpp"
#include "backend/codegen/generate_ir.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "frontend/ast/ast.hpp"
#include "llvm/IR/IRBuilder.h"

namespace nv {

class JsonHandler : public MethodHandler {
public:
    llvm::Value* handle(nv::IRGenerationContext& ctx, const std::string& method, const std::vector<std::unique_ptr<Node>>& args) override {
        auto& b = ctx.get_builder();
        auto* ValueTy = nv::ir_utils::get_value_struct(ctx);
        auto* I8P = nv::ir_utils::get_i8_ptr(ctx);
        
        if (method == "parseString") {
            nv::register_feature("json");
            nv::register_feature("string");
            
            if (args.empty()) return nullptr;
            
            args[0]->codegen(ctx);
            if (!ctx.has_value()) return nullptr;
            
            llvm::Value* json_string = ctx.pop_value();
            
            // Converter para i8* se necessário
            if (json_string->getType() != I8P) {
                json_string = b.CreateBitCast(json_string, I8P);
            }
            
            // Criar variável global para o resultado
            auto* global_result = new llvm::GlobalVariable(
                ctx.get_module(),
                ValueTy,
                false,
                llvm::GlobalValue::ExternalLinkage,
                llvm::UndefValue::get(ValueTy),
                "json_parse_string_out"
            );
            
            // Chamar função JSON
            auto* fn = ctx.get_module().getFunction("json_parse_string");
            if (!fn) return nullptr;
            
            b.CreateCall(fn, {global_result, json_string});
            return global_result;
        }
        
        return nullptr;
    }
    
    bool can_handle(const std::string& method) const override {
        return method == "parseString";
    }
};

// Registrar o handler automaticamente
static struct JsonHandlerRegistrar {
    JsonHandlerRegistrar() {
        register_method_handler(std::make_unique<JsonHandler>());
    }
} g_json_handler_registrar;

}
