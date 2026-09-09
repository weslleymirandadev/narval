// nir_tensor_codegen.cpp — All tensor codegen lives here.
// Called from nir_call_expr.cpp and nir_member_expr.cpp via simple dispatch.

#include "backend/nir/nir_tensor_codegen.hpp"
#include "frontend/ast/ast.hpp"
#include "frontend/ast/expressions/call_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/member_expr_node.hpp"
#include "frontend/ast/expressions/numeric_literal_node.hpp"
#include "backend/nir/NarvalOps.h"
#include "../nir_codegen_utils.hpp"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Linalg/IR/Linalg.h"
#include "mlir/Dialect/Tensor/IR/Tensor.h"
#include <functional>

namespace nv_tensor_codegen {

using namespace nv;
namespace narval = mlir::narval;

// ── Internal helpers ──────────────────────────────────────────────────
static mlir::Value i64_const(nv::NIRGenerationContext& ctx, int64_t val) {
    auto loc = ctx.get_builder().getUnknownLoc();
    return ctx.get_builder().create<mlir::arith::ConstantOp>(
        loc, ctx.get_builder().getI64IntegerAttr(val));
}

static std::string bridge_name(const std::string& method) {
    if (method == "zeros" || method == "ones" || method == "") return "nv_tensor_fill_nd";
    if (method == "item")    return "nv_tensor_item_bridge";
    if (method == "tolist")  return "nv_tensor_tolist_bridge";
    if (method == "T" || method == "transpose") return "nv_tensor_transpose_bridge";
    if (method == "shape")   return "nv_tensor_get_shape_bridge";
    if (method == "ndim")    return "nv_tensor_get_ndim_bridge";
    if (method == "dtype")   return "nv_tensor_get_dtype_bridge";
    if (method == "size" || method == "nelem") return "nv_tensor_nelem_bridge";
    if (method == "reshape") return "nv_tensor_reshape_bridge";
    return "";
}

static bool is_tensor_attr(const std::string& prop) {
    return prop == "shape" || prop == "ndim" || prop == "dtype"
        || prop == "T" || prop == "size" || prop == "nelem";
}

// Extract integer dimension values from argument nodes.
static std::vector<int64_t> extract_dims(const std::vector<std::unique_ptr<ArgNode>>& args) {
    std::vector<int64_t> dims;
    for (const auto& arg : args) {
        if (arg && arg->value && arg->value->kind == NodeType::NumericLiteral) {
            auto* num = static_cast<NumericLiteralNode*>(arg->value.get());
            if (num->value.find('.') == std::string::npos) {
                dims.push_back(std::stoll(num->value));
                continue;
            }
        }
        dims.push_back(0); // fallback
    }
    return dims;
}

// ── emit_tensor_fill: Tensor.zeros/ones/Tensor(dims) ────────────────
// The tensor is built in the MLIR tensor world (tensor.empty + linalg.fill)
// and boxed into a runtime Value with narval.tensor_to_value — the codegen
// producer of the tensor boxing path. Falls back to the runtime bridge for
// 0-D (no dims) or >8-D shapes (nv_box_tensor caps at 8 dims).
static void emit_tensor_fill(nv::NIRGenerationContext& ctx,
                              const std::string& method,
                              const std::vector<int64_t>& dims) {
    auto loc = ctx.get_builder().getUnknownLoc();
    auto vt  = ctx.get_narval_value_type();

    if (dims.empty() || dims.size() > 8) {
        // 0-D / oversized — keep the runtime bridge (parity path).
        llvm::SmallVector<mlir::Type> arg_types(10, ctx.get_builder().getIntegerType(64));
        llvm::SmallVector<mlir::Value> rt_args;
        rt_args.push_back(i64_const(ctx, (method == "ones") ? 1 : 0));
        rt_args.push_back(i64_const(ctx, (int64_t)dims.size()));
        for (int j = 0; j < 8; j++)
            rt_args.push_back(i64_const(ctx, (j < (int)dims.size()) ? dims[j] : 0));

        auto fn_type = mlir::FunctionType::get(&ctx.get_mlir_context(), arg_types, vt);
        ctx.ensure_runtime_func("nv_tensor_fill_nd", fn_type);
        auto call = narval::CallRuntimeOp::create(
            ctx.get_builder(), loc, mlir::TypeRange{vt},
            mlir::SymbolRefAttr::get(&ctx.get_mlir_context(), "nv_tensor_fill_nd"),
            rt_args);
        ctx.push_value(call.getResults()[0]);
        return;
    }

    auto& b = ctx.get_builder();
    auto f32 = b.getF32Type();

    // %empty = tensor.empty() : tensor<dims x f32>
    auto empty = mlir::tensor::EmptyOp::create(b, loc, dims, f32);

    // %fill = arith.constant <0.0|1.0> : f32
    double fill_d = (method == "ones") ? 1.0 : 0.0;
    auto fill_c = mlir::arith::ConstantOp::create(
        b, loc, mlir::FloatAttr::get(f32, fill_d));

    // %filled = linalg.fill ins(%fill) outs(%empty) — the result tensor type
    // matches the outs operand (tensor.empty result).
    auto emptyRes = empty.getOperation()->getResult(0);
    auto filled = mlir::linalg::FillOp::create(
        b, loc, mlir::TypeRange{emptyRes.getType()},
        mlir::ValueRange{fill_c.getOperation()->getResult(0)},
        mlir::ValueRange{emptyRes});

    // %v = narval.tensor_to_value %filled : tensor<...xf32> -> !narval.value
    auto t2v = narval::TensorToValueOp::create(
        b, loc, filled.getOperation()->getResult(0));
    ctx.push_value(t2v.getOperation()->getResult(0));
}

// ── emit_simple_bridge: obj.item(), obj.shape, etc. ─────────────────
static void emit_simple_bridge(nv::NIRGenerationContext& ctx,
                                mlir::Value tensor_val,
                                const std::string& fn) {
    auto loc = ctx.get_builder().getUnknownLoc();
    auto vt = ctx.get_narval_value_type();
    ctx.push_value(nir_call_runtime(ctx, loc, fn, {tensor_val}, {vt}));
}

// ── emit_reshape: obj.reshape(dims) ────────────────────────────────
static void emit_reshape(nv::NIRGenerationContext& ctx,
                          mlir::Value tensor_val,
                          const std::vector<int64_t>& dims) {
    auto loc = ctx.get_builder().getUnknownLoc();
    auto vt = ctx.get_narval_value_type();
    std::string fn = "nv_tensor_reshape_bridge";

    llvm::SmallVector<mlir::Type> arg_types;
    arg_types.push_back(vt);
    arg_types.push_back(ctx.get_builder().getIntegerType(64));
    for (int j = 0; j < 8; j++)
        arg_types.push_back(ctx.get_builder().getIntegerType(64));

    llvm::SmallVector<mlir::Value> rt_args;
    rt_args.push_back(tensor_val);
    rt_args.push_back(i64_const(ctx, (int64_t)dims.size()));
    for (int j = 0; j < 8; j++)
        rt_args.push_back(i64_const(ctx, (j < (int)dims.size()) ? dims[j] : 0));

    auto fn_type = mlir::FunctionType::get(&ctx.get_mlir_context(), arg_types, vt);
    ctx.ensure_runtime_func(fn, fn_type);
    auto call = narval::CallRuntimeOp::create(
        ctx.get_builder(), loc, mlir::TypeRange{vt},
        mlir::SymbolRefAttr::get(&ctx.get_mlir_context(), fn),
        rt_args);
    ctx.push_value(call.getResults()[0]);
}

// ── Public API ────────────────────────────────────────────────────────

bool try_handle_call(nv::NIRGenerationContext& ctx, Node* node) {
    if (!node || node->kind != NodeType::CallExpression) return false;
    auto* call = static_cast<CallExprNode*>(node);
    auto* callee = call->caller.get();
    if (!callee) return false;

    // ── Pattern 1: Tensor(args...) — constructor from values ─────────
    //   Tensor(3)       → 0-D, value=3, shape=[]
    //   Tensor(1, 2, 3) → 1-D, [1,2,3],       shape=[3]
    //   Tensor({1,2,3}) → 1-D, [1,2,3],       shape=[3]
    if (callee->kind == NodeType::Identifier) {
        auto* id = static_cast<IdentifierNode*>(callee);
        if (id->symbol == "Tensor") {
            if (call->args.empty()) {
                ctx.push_value({}); return true;
            }

            // Extract ALL values recursively (flatten nested arrays) + compute shape
            std::vector<int64_t> dims;
            std::vector<mlir::Value> flat_values;

            // Recursive extraction of scalar values
            std::function<void(Node*, std::vector<int64_t>&, int)> extract_values;
            extract_values = [&](Node* node, std::vector<int64_t>& shape, int depth) {
                if (!node) return;
                if (node->kind == NodeType::ArrayExpression) {
                    auto* arr = static_cast<ArrayExprNode*>(node);
                    if ((int)shape.size() <= depth)
                        shape.push_back((int64_t)arr->elements.size());
                    for (auto& el : arr->elements) {
                        extract_values(el.get(), shape, depth + 1);
                    }
                } else if (node->kind == NodeType::NumericLiteral) {
                    // Codegen the numeric literal and push to flat list
                    node->nir_codegen(ctx);
                    if (ctx.has_value())
                        flat_values.push_back(ctx.pop_value());
                }
            };

            if (call->args.size() == 1 && call->args[0] && call->args[0]->value &&
                call->args[0]->value->kind == NodeType::ArrayExpression) {
                // Tensor({1,2,3}) or Tensor({{1,2},{3,4}})
                extract_values(call->args[0]->value.get(), dims, 0);
            } else {
                // Tensor(3) or Tensor(1, 2, 3) 
                for (auto& arg : call->args) {
                    if (arg && arg->value) {
                        arg->value->nir_codegen(ctx);
                        if (ctx.has_value())
                            flat_values.push_back(ctx.pop_value());
                    }
                }
                // For single numeric arg, shape = [] (0-D)
                if (call->args.size() > 1)
                    dims.push_back((int64_t)call->args.size());
                // else dims stays empty = 0-D
            }

            if (flat_values.empty()) { ctx.push_value({}); return true; }

            auto loc = ctx.get_builder().getUnknownLoc();
            auto vt = ctx.get_narval_value_type();
            auto& b = ctx.get_builder();

            // Create flat array from extracted values
            auto N = (int64_t)flat_values.size();
            auto sz = nir_emit_const(ctx, loc, b.getI64IntegerAttr(N));
            mlir::Value flat_arr = nir_call_runtime(ctx, loc, "nv_create_array", {sz}, {vt});
            for (int i = 0; i < (int)flat_values.size(); i++) {
                auto idx = nir_emit_const(ctx, loc, b.getI64IntegerAttr(i));
                nir_call_runtime(ctx, loc, "nv_array_set", {flat_arr, idx, flat_values[i]}, {});
            }

            // Call nv_tensor_from_flat_array(flat_arr, ndim, d0..d7)
            llvm::SmallVector<mlir::Type> fn_arg_types;
            fn_arg_types.push_back(vt);
            fn_arg_types.push_back(b.getIntegerType(64));
            for (int j = 0; j < 8; j++)
                fn_arg_types.push_back(b.getIntegerType(64));

            llvm::SmallVector<mlir::Value> rt_args;
            rt_args.push_back(flat_arr);
            rt_args.push_back(i64_const(ctx, (int64_t)dims.size()));
            for (int j = 0; j < 8; j++)
                rt_args.push_back(i64_const(ctx, (j < (int)dims.size()) ? dims[j] : 0));

            auto fn_type = mlir::FunctionType::get(&ctx.get_mlir_context(), fn_arg_types, vt);
            ctx.ensure_runtime_func("nv_tensor_from_flat_array", fn_type);
            auto call_op = mlir::narval::CallRuntimeOp::create(
                b, loc, mlir::TypeRange{vt},
                mlir::SymbolRefAttr::get(&ctx.get_mlir_context(), "nv_tensor_from_flat_array"),
                rt_args);
            ctx.push_value(call_op.getResults()[0]);
            return true;
        }
    }

    // ── Pattern 2: Tensor.zeros(3, 4) / Tensor.ones(3, 4) —─────────
    if (callee->kind == NodeType::MemberExpression) {
        auto* member = static_cast<MemberExprNode*>(callee);
        if (member->object && member->object->kind == NodeType::Identifier &&
            member->property && member->property->kind == NodeType::Identifier) {
            auto* obj  = static_cast<IdentifierNode*>(member->object.get());
            auto* prop = static_cast<IdentifierNode*>(member->property.get());
            if (obj->symbol == "Tensor" &&
                (prop->symbol == "zeros" || prop->symbol == "ones")) {
                emit_tensor_fill(ctx, prop->symbol, extract_dims(call->args));
                return true;
            }
        }

        // ── Pattern 3: obj.item() / obj.tolist() / obj.reshape(dims) ─
        if (member->property && member->property->kind == NodeType::Identifier) {
            auto* prop = static_cast<IdentifierNode*>(member->property.get());
            const std::string& m = prop->symbol;

            // Evaluate the object
            if (member->object) member->object->nir_codegen(ctx);
            mlir::Value obj = ctx.has_value() ? ctx.pop_value() : mlir::Value();
            if (!obj) { ctx.push_value({}); return true; }

            if (m == "item" || m == "tolist") {
                emit_simple_bridge(ctx, obj, bridge_name(m));
                return true;
            }
            if (m == "reshape") {
                emit_reshape(ctx, obj, extract_dims(call->args));
                return true;
            }
        }
    }

    return false; // not a tensor call
}

bool try_handle_member(nv::NIRGenerationContext& ctx,
                        mlir::Value object_val,
                        const std::string& property) {
    if (!is_tensor_attr(property)) return false;
    emit_simple_bridge(ctx, object_val, bridge_name(property));
    return true;
}

} // namespace nv_tensor_codegen
