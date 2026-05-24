#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/class_stmt_node.hpp"
#include "frontend/ast/statements/function_stmt_node.hpp"
#include "mlir/Dialect/Func/IR/FuncOps.h"

// Emit one class method as a func.func.
// Constructor ("new"): __ctor_ClassName(self, p0, ...) -> void
// Other methods:       __method_ClassName_name(self, p0, ...) -> narval.value
static void nir_emit_class_method(nv::NIRGenerationContext& ctx,
                                   const std::string& class_name,
                                   const std::string& method_name,
                                   FunctionStmtNode* fn_node)
{
    auto& b        = ctx.get_builder();
    auto& mlir_ctx = ctx.get_mlir_context();
    auto  vt       = ctx.get_narval_value_type();

    mlir::OpBuilder::InsertionGuard guard(b);

    bool is_ctor = (method_name == "new");
    std::string fn_name = is_ctor
        ? "__ctor_" + class_name
        : "__method_" + class_name + "_" + method_name;

    if (ctx.get_module().lookupSymbol(fn_name)) return; // already emitted

    auto loc = ctx.loc(fn_node->position.get());

    // self + declared params, all narval.value.
    size_t nparams = 1;
    for (const auto& p : fn_node->parameters)
        nparams += p.parameter.size();

    std::vector<mlir::Type> ptypes(nparams, vt);
    bool is_void_ret = is_ctor
        || (fn_node->return_type == "None"
            || fn_node->return_type == "void"
            || fn_node->return_type.empty());

    mlir::TypeRange ret_types = is_void_ret ? mlir::TypeRange{} : mlir::TypeRange{vt};
    auto fn_type = mlir::FunctionType::get(&mlir_ctx, ptypes, ret_types);

    auto fn = mlir::func::FuncOp::create(b, loc, fn_name, fn_type);
    fn.setPublic();
    auto* entry = fn.addEntryBlock();
    b.setInsertionPointToStart(entry);

    ctx.push_scope();

    // Bind self.
    ctx.define("__this", fn.getArgument(0));
    ctx.define("self",   fn.getArgument(0));

    // Bind declared parameters.
    size_t arg_idx = 1;
    for (const auto& param : fn_node->parameters) {
        for (const auto& [pname, _] : param.parameter) {
            if (arg_idx < fn.getNumArguments())
                ctx.define(pname, fn.getArgument(arg_idx));
            ++arg_idx;
        }
    }

    nir_emit_body(fn_node->body, ctx);

    auto* last_bb = &fn.getBody().back();
    if (last_bb->empty() || !last_bb->back().hasTrait<mlir::OpTrait::IsTerminator>()) {
        if (is_void_ret) {
            mlir::func::ReturnOp::create(b, loc);
        } else {
            mlir::Value ret = ctx.has_value()
                ? ctx.pop_value()
                : mlir::narval::ConstantOp::create(b, loc, vt, b.getI64IntegerAttr(0)).getResult();
            mlir::func::ReturnOp::create(b, loc, mlir::ValueRange{ret});
        }
    }

    ctx.pop_scope();
    ctx.get_module().push_back(fn);
}

void ClassStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    for (const auto& method : methods) {
        if (!method->method_def) continue;
        auto* fn_node = static_cast<FunctionStmtNode*>(method->method_def.get());
        nir_emit_class_method(ctx, name, method->name, fn_node);
    }
}
