#include "backend/codegen/method_handler.hpp"
#include "backend/codegen/generate_ir.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "frontend/ast/ast.hpp"
#include "llvm/IR/IRBuilder.h"

namespace nv {

class BuiltinHandler : public MethodHandler {
public:
    llvm::Value* handle(nv::IRGenerationContext& ctx, const std::string& method, const std::vector<std::unique_ptr<Node>>& args) override {
        auto& b = ctx.get_builder();
        auto* ValueTy = nv::ir_utils::get_value_struct(ctx);
        auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);
        
        if (method == "write") {
            nv::register_feature("write");
            
            // TEMP: Testar printf com string literal fixa primeiro
            auto& ctx_module = ctx.get_context();
            auto* I8Ptr = llvm::PointerType::getUnqual(ctx_module);
            auto* printf_ty = llvm::FunctionType::get(llvm::Type::getInt32Ty(ctx_module), {I8Ptr}, true);
            auto* printf_fn = ctx.get_module().getFunction("printf");
            if (!printf_fn) {
                printf_fn = llvm::Function::Create(printf_ty, llvm::Function::ExternalLinkage, "printf", ctx.get_module());
            }
            
            // Testar com string literal fixa para confirmar que printf funciona
            auto* test_str = llvm::ConstantDataArray::getString(ctx_module, "WORKING!\n", true);
            auto* test_global = new llvm::GlobalVariable(ctx.get_module(), test_str->getType(), true, llvm::GlobalValue::PrivateLinkage, test_str, "test.str");
            std::vector<llvm::Value*> args = {test_global};
            ctx.get_builder().CreateCall(printf_fn, args);
            
            return nullptr; // write retorna void
            
        } else if (method == "read") {
            nv::register_feature("read");
            
            auto* read_fn = ctx.get_module().getFunction("read");
            if (!read_fn) return nullptr;
            
            auto* result = b.CreateCall(read_fn, {});
            return result;
            
        } else if (method == "str") {
            nv::register_feature("str");
            
            if (args.empty()) return nullptr;
            args[0]->codegen(ctx);
            if (!ctx.has_value()) return nullptr;
            
            llvm::Value* arg = ctx.pop_value();
            auto* str_fn = ctx.get_module().getFunction("nv_str_convert");
            if (!str_fn) return nullptr;

            auto* in_box = ctx.create_alloca(ValueTy, "str.in");
            b.CreateStore(arg, in_box);
            auto* out_box = ctx.create_alloca(ValueTy, "str.out");
            b.CreateCall(str_fn, {out_box, in_box});
            return b.CreateLoad(ValueTy, out_box, "str.result");
            
        } else if (method == "int") {
            nv::register_feature("int");
            
            if (args.empty()) return nullptr;
            args[0]->codegen(ctx);
            if (!ctx.has_value()) return nullptr;
            
            llvm::Value* arg = ctx.pop_value();
            auto* int_fn = ctx.get_module().getFunction("nv_int_convert");
            if (!int_fn) return nullptr;

            auto* in_box = ctx.create_alloca(ValueTy, "int.in");
            b.CreateStore(arg, in_box);
            auto* out_box = ctx.create_alloca(ValueTy, "int.out");
            b.CreateCall(int_fn, {out_box, in_box});
            return b.CreateLoad(ValueTy, out_box, "int.result");
            
        } else if (method == "float") {
            nv::register_feature("float");
            
            if (args.empty()) return nullptr;
            args[0]->codegen(ctx);
            if (!ctx.has_value()) return nullptr;
            
            llvm::Value* arg = ctx.pop_value();
            auto* float_fn = ctx.get_module().getFunction("nv_float_convert");
            if (!float_fn) return nullptr;

            auto* in_box = ctx.create_alloca(ValueTy, "float.in");
            b.CreateStore(arg, in_box);
            auto* out_box = ctx.create_alloca(ValueTy, "float.out");
            b.CreateCall(float_fn, {out_box, in_box});
            return b.CreateLoad(ValueTy, out_box, "float.result");
            
        } else if (method == "bool") {
            nv::register_feature("bool");
            
            if (args.empty()) return nullptr;
            args[0]->codegen(ctx);
            if (!ctx.has_value()) return nullptr;
            
            llvm::Value* arg = ctx.pop_value();
            auto* bool_fn = ctx.get_module().getFunction("nv_bool_convert");
            if (!bool_fn) return nullptr;

            auto* in_box = ctx.create_alloca(ValueTy, "bool.in");
            b.CreateStore(arg, in_box);
            auto* out_box = ctx.create_alloca(ValueTy, "bool.out");
            b.CreateCall(bool_fn, {out_box, in_box});
            return b.CreateLoad(ValueTy, out_box, "bool.result");

        } else if (method == "Some") {
            if (args.empty()) return nullptr;
            args[0]->codegen(ctx);
            if (!ctx.has_value()) return nullptr;
            auto* in = ctx.create_alloca(ValueTy, "some.in");
            b.CreateStore(ctx.pop_value(), in);
            auto* out = ctx.create_alloca(ValueTy, "some.out");
            auto* fn = ctx.ensure_runtime_func("create_option_some", {ValuePtr, ValuePtr});
            b.CreateCall(fn, {out, in});
            return b.CreateLoad(ValueTy, out, "some.result");

        } else if (method == "Ok") {
            if (args.empty()) return nullptr;
            args[0]->codegen(ctx);
            if (!ctx.has_value()) return nullptr;
            auto* in = ctx.create_alloca(ValueTy, "ok.in");
            b.CreateStore(ctx.pop_value(), in);
            auto* out = ctx.create_alloca(ValueTy, "ok.out");
            auto* fn = ctx.ensure_runtime_func("create_result_ok", {ValuePtr, ValuePtr});
            b.CreateCall(fn, {out, in});
            return b.CreateLoad(ValueTy, out, "ok.result");

        } else if (method == "Err") {
            if (args.empty()) return nullptr;
            args[0]->codegen(ctx);
            if (!ctx.has_value()) return nullptr;
            auto* in = ctx.create_alloca(ValueTy, "err.in");
            b.CreateStore(ctx.pop_value(), in);
            auto* out = ctx.create_alloca(ValueTy, "err.out");
            auto* fn = ctx.ensure_runtime_func("create_result_err", {ValuePtr, ValuePtr});
            b.CreateCall(fn, {out, in});
            return b.CreateLoad(ValueTy, out, "err.result");
        }

        return nullptr;
    }

    bool can_handle(const std::string& method) const override {
        return method == "write" || method == "read" ||
               method == "str"   || method == "int"  ||
               method == "float" || method == "bool" ||
               method == "Some"  ||
               method == "Ok"    || method == "Err";
    }
};

// Registrar o handler automaticamente
static struct BuiltinHandlerRegistrar {
    BuiltinHandlerRegistrar() {
        register_method_handler(std::make_unique<BuiltinHandler>());
    }
} g_builtin_handler_registrar;

}
