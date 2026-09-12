#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/for_stmt_node.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "frontend/ast/expressions/access_expr_node.hpp"
#include "frontend/ast/expressions/binary_expr_node.hpp"
#include "frontend/ast/expressions/call_expr_node.hpp"
#include "frontend/ast/expressions/numeric_literal_node.hpp"
#include <algorithm>
#include <map>
#include <vector>

static std::string get_binding_name(const ForStmtNode& node) {
    if (!node.bindings.empty())
        if (node.bindings[0] && node.bindings[0]->kind == NodeType::Identifier)
            return static_cast<IdentifierNode*>(node.bindings[0].get())->symbol;
    return "";
}

// ── @[vectorize]: the loop lowered to raw f64 loads, arithmetic and stores ────
//
// The checker has already accepted the loop (range, independent iterations, no I/O) and,
// when the body has the element-wise shape over float tensors, set vectorize_ok. What is
// left here is the reason the annotation exists at all: the body has to reach the numbers
// without a runtime call per element. A boxed `a[i]` IS such a call, and the vectorizer
// cannot see through an opaque call — no metadata makes that vectorizable. So the loop is
// lowered with the elements never becoming objects:
//
//     %pa = nv_tensor_data_ptr_bridge(a), ...               (once, outside the loop)
//     for i: store(load(%pa + 8*i) <op> load(%pb + 8*i), %pc + 8*i)
//
// i.e. llvm.getelementptr/llvm.load/llvm.store on f64 and arith on f64 SSA values: real
// memory operations the vectorizer can widen. Anything that does not match keeps the
// general lowering (the annotation is still honoured, it just cannot become SIMD), so the
// attribute never changes what a program computes — only how the work is done.

// A term of the postfix program: a tensor element, a number, or the operator between them.
struct VecTok {
    bool        is_op = false;
    bool        is_index = false;   // the loop variable: a numeric operand like a literal
    char        op = 0;
    std::string name;
    double      literal = 0.0;
};

// Postfix order, so the tree shape survives (`(a+b)*(c+d)` is not `a+b*c+d`).
static bool vec_postfix(const Node* e, std::vector<VecTok>& out, const std::string& index_name) {
    if (!e) return false;
    if (e->kind == NodeType::Identifier) {
        auto* id = static_cast<const IdentifierNode*>(e);
        if (id->symbol != index_name) return false;
        VecTok t; t.is_index = true;
        out.push_back(t);
        return true;
    }
    if (e->kind == NodeType::CallExpression) {
        // A numeric conversion of a numeric operand (`float(i)`): the cast is what the raw
        // path computes for the induction variable anyway.
        auto* call = static_cast<const CallExprNode*>(e);
        if (!call->caller || call->caller->kind != NodeType::Identifier) return false;
        const std::string& fn = static_cast<const IdentifierNode*>(call->caller.get())->symbol;
        if (fn != "float" && fn != "float32" && fn != "float64" &&
            fn != "int32" && fn != "int64")
            return false;
        if (call->args.size() != 1 || !call->args[0]) return false;
        return vec_postfix(call->args[0]->value.get(), out, index_name);
    }
    if (e->kind == NodeType::BinaryExpression) {
        auto* b = static_cast<const BinaryExprNode*>(e);
        if (b->op.size() != 1 || std::string("+-*/").find(b->op[0]) == std::string::npos)
            return false;
        if (!vec_postfix(b->left.get(), out, index_name) ||
            !vec_postfix(b->right.get(), out, index_name))
            return false;
        VecTok t; t.is_op = true; t.op = b->op[0];
        out.push_back(t);
        return true;
    }
    if (e->kind == NodeType::NumericLiteral) {
        double v = 0.0;
        try { v = std::stod(static_cast<const NumericLiteralNode*>(e)->value); }
        catch (...) { return false; }
        VecTok t; t.literal = v;
        out.push_back(t);
        return true;
    }
    if (e->kind == NodeType::AccessExpression) {
        auto* a = static_cast<const AccessExprNode*>(e);
        if (!a->expr || a->expr->kind != NodeType::Identifier) return false;
        VecTok t; t.name = static_cast<const IdentifierNode*>(a->expr.get())->symbol;
        if (t.name.empty()) return false;
        out.push_back(t);
        return true;
    }
    return false;
}

// Emits the vectorized loop. Returns false (leaving everything for the general path) when
// an operand is not a value in scope — the checker has already settled the shape and the
// types, so that is a safety net, not a decision.
static bool emit_vectorized_loop(nv::NIRGenerationContext& ctx, mlir::Location loc,
                                 mlir::Value lb, mlir::Value ub, mlir::Value step,
                                 const std::vector<VecTok>& code, const std::string& dest) {
    auto& b   = ctx.get_builder();
    auto& mc  = ctx.get_mlir_context();
    auto  vt  = ctx.get_narval_value_type();
    auto  f64 = b.getF64Type();
    auto  ptr = mlir::LLVM::LLVMPointerType::get(&mc);

    auto ptr_fn_t = mlir::FunctionType::get(&mc, {vt}, {ptr});
    ctx.ensure_runtime_func("nv_tensor_data_ptr_bridge", ptr_fn_t);
    auto ptr_fn = ctx.get_module().lookupSymbol<mlir::func::FuncOp>("nv_tensor_data_ptr_bridge");

    std::vector<std::string> names;
    names.push_back(dest);
    for (const auto& t : code)
        if (!t.is_op && !t.name.empty() &&
            std::find(names.begin(), names.end(), t.name) == names.end())
            names.push_back(t.name);

    std::map<std::string, mlir::Value> base;
    for (const auto& n : names) {
        mlir::Value v = ctx.lookup(n);
        if (!v) return false;
        base[n] = mlir::func::CallOp::create(b, loc, ptr_fn, {v}).getResult(0);
    }

    // scf.for, not the narval loop op: the tensor/MLIR path only accepts structured
    // control flow, and this loop is deliberately plain — an induction variable and
    // straight-line memory work, which is what the vectorizer wants to see.
    auto for_op = mlir::scf::ForOp::create(b, loc, lb, ub, step, mlir::ValueRange{});
    {
        mlir::OpBuilder::InsertionGuard g(b);
        auto* body_blk = &for_op.getRegion().front();
        b.setInsertionPointToStart(body_blk);
        // The index arrives as `index`; llvm.getelementptr wants a signless integer.
        mlir::Value iv = mlir::arith::IndexCastOp::create(
            b, loc, b.getI64Type(), body_blk->getArgument(0)).getResult();

        std::vector<mlir::Value> stack;
        for (const auto& t : code) {
            if (!t.is_op) {
                if (t.is_index) {
                    stack.push_back(mlir::arith::SIToFPOp::create(b, loc, f64, iv).getResult());
                    continue;
                }
                if (t.name.empty()) {
                    stack.push_back(mlir::arith::ConstantOp::create(
                        b, loc, mlir::FloatAttr::get(f64, t.literal)).getResult());
                    continue;
                }
                auto gep = mlir::LLVM::GEPOp::create(b, loc, ptr, f64, base[t.name],
                                                     mlir::ValueRange{iv});
                stack.push_back(mlir::LLVM::LoadOp::create(b, loc, f64, gep.getResult()).getRes());
                continue;
            }
            if (stack.size() < 2) return false;
            mlir::Value rhs = stack.back(); stack.pop_back();
            mlir::Value lhs = stack.back(); stack.pop_back();
            mlir::Value r;
            switch (t.op) {
                case '+': r = mlir::arith::AddFOp::create(b, loc, lhs, rhs).getResult(); break;
                case '-': r = mlir::arith::SubFOp::create(b, loc, lhs, rhs).getResult(); break;
                case '*': r = mlir::arith::MulFOp::create(b, loc, lhs, rhs).getResult(); break;
                default:  r = mlir::arith::DivFOp::create(b, loc, lhs, rhs).getResult(); break;
            }
            stack.push_back(r);
        }
        if (stack.empty()) return false;

        auto dest_gep = mlir::LLVM::GEPOp::create(b, loc, ptr, f64, base[dest],
                                                  mlir::ValueRange{iv});
        mlir::LLVM::StoreOp::create(b, loc, stack.back(), dest_gep.getResult());
        if (body_blk->empty() || !body_blk->back().hasTrait<mlir::OpTrait::IsTerminator>())
            mlir::scf::YieldOp::create(b, loc, mlir::ValueRange{});
    }
    b.setInsertionPointAfter(for_op);
    return true;
}

void ForStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto& b   = ctx.get_builder();
    auto  loc = ctx.loc(position.get());
    auto  vt  = ctx.get_narval_value_type();
    auto  idx = b.getIndexType();
    auto  i32 = b.getI32Type();

    std::string var = get_binding_name(*this);

    // @[vectorize] from the attribute right before this loop.
    const bool vectorize = ctx.pending_loop_vectorize;
    ctx.pending_loop_vectorize = false;

    // Loop-carried variables: names assigned directly in the body that already
    // exist in the enclosing scope (same rule as the while codegen).
    std::vector<std::pair<std::string, mlir::Value>> carried;
    for (const auto& stmt : body) {
        if (!stmt) continue;
        std::string name;
        if (stmt->kind == NodeType::DeclarationStatement) {
            auto* decl = static_cast<DeclarationStmtNode*>(stmt.get());
            if (decl->target && decl->target->kind == NodeType::Identifier)
                name = static_cast<IdentifierNode*>(decl->target.get())->symbol;
        } else if (stmt->kind == NodeType::AssignmentExpression) {
            auto* asg = static_cast<AssignmentExprNode*>(stmt.get());
            if (asg->target && asg->target->kind == NodeType::Identifier)
                name = static_cast<IdentifierNode*>(asg->target.get())->symbol;
        }
        if (name.empty()) continue;
        mlir::Value cur = ctx.lookup(name);
        if (cur) carried.emplace_back(name, cur);
    }

    llvm::SmallVector<mlir::Value> init_args;
    llvm::SmallVector<mlir::Type>  res_types;
    for (auto& [name, v] : carried) {
        (void)name;
        init_args.push_back(v);
        res_types.push_back(vt);
    }

    // ── Range-based: for x in lb..ub ──────────────────────────────────────
    if (range_start && range_end) {
        range_start->nir_codegen(ctx);
        mlir::Value lb_v = ctx.pop_value();
        range_end->nir_codegen(ctx);
        mlir::Value ub_v = ctx.pop_value();

        auto to_idx_fn = mlir::FunctionType::get(&ctx.get_mlir_context(), {vt}, {idx});
        ctx.ensure_runtime_func("nv_value_to_index", to_idx_fn);
        auto to_idx = ctx.get_module().lookupSymbol<mlir::func::FuncOp>("nv_value_to_index");

        auto lb = mlir::func::CallOp::create(b, loc, to_idx, {lb_v}).getResult(0);
        auto ub = mlir::func::CallOp::create(b, loc, to_idx, {ub_v}).getResult(0);
        if (range_inclusive) {
            auto one = mlir::arith::ConstantIndexOp::create(b, loc, 1).getResult();
            ub = mlir::arith::AddIOp::create(b, loc, ub, one).getResult();
        }
        auto step = mlir::arith::ConstantIndexOp::create(b, loc, 1).getResult();

        // The element-wise shape over float tensors becomes raw f64 memory work. Annotated or
        // not, this is the only lowering the tensor path accepts, so the gate is the verdict
        // (and no carried values, which is the independence the shape implies) — the
        // annotation itself only adds the SIMD marker after the loop.
        if (carried.empty() && vectorize_ok &&
            body.size() == 1 && body[0] &&
            body[0]->kind == NodeType::AssignmentExpression) {
            auto* asg = static_cast<AssignmentExprNode*>(body[0].get());
            std::vector<VecTok> code;
            if (asg->value && vec_postfix(asg->value.get(), code, var) && !code.empty() &&
                asg->target && asg->target->kind == NodeType::AccessExpression) {
                auto* acc = static_cast<const AccessExprNode*>(asg->target.get());
                if (acc->expr && acc->expr->kind == NodeType::Identifier) {
                    const std::string dest =
                        static_cast<const IdentifierNode*>(acc->expr.get())->symbol;
                    if (emit_vectorized_loop(ctx, loc, lb, ub, step, code, dest))
                        return;   // emitted and terminated: nothing left to do for this loop
                }
            }
        }

        auto for_op = ctx.emit_for_range(loc, lb, ub, step, var, init_args);
        if (vectorize) for_op->setAttr("narval.vectorize", b.getUnitAttr());
        {
            mlir::OpBuilder::InsertionGuard g(b);
            auto* body_blk = &for_op.getBody().front();
            b.setInsertionPointToStart(body_blk);
            ctx.push_scope();

            // Box index → narval.value for use in body.
            mlir::Value iv = body_blk->getArgument(0);
            auto box_fn_t = mlir::FunctionType::get(&ctx.get_mlir_context(), {idx}, {vt});
            ctx.ensure_runtime_func("nv_index_to_value", box_fn_t);
            auto box_fn = ctx.get_module().lookupSymbol<mlir::func::FuncOp>("nv_index_to_value");
            auto iv_boxed = mlir::func::CallOp::create(b, loc, box_fn, {iv}).getResult(0);
            if (!var.empty()) ctx.define(var, iv_boxed);

            // Bind loop-carried variables to the body block args.
            for (size_t i = 0; i < carried.size(); ++i)
                ctx.define(carried[i].first, body_blk->getArgument(1 + i));

            nir_emit_body(body, ctx);

            // Yield the updated carried values (before pop_scope — the
            // lookups must resolve inside the body scope).
            if (body_blk->empty() ||
                !body_blk->back().hasTrait<mlir::OpTrait::IsTerminator>()) {
                llvm::SmallVector<mlir::Value> yv;
                for (auto& [name, _] : carried) {
                    mlir::Value v = ctx.lookup(name);
                    if (!v) v = nir_emit_const(ctx, loc, b.getI64IntegerAttr(0));
                    yv.push_back(v);
                }
                mlir::narval::YieldOp::create(b, loc, yv);
            }
            ctx.pop_scope();
        }
        b.setInsertionPointAfter(for_op);
        for (size_t i = 0; i < carried.size(); ++i)
            ctx.define(carried[i].first, for_op.getResults()[i]);
        return;
    }

    // ── Collection-based: for x in iterable ───────────────────────────────
    if (iterable) {
        iterable->nir_codegen(ctx);
        mlir::Value iter = ctx.pop_value();

        auto len_fn_t = mlir::FunctionType::get(&ctx.get_mlir_context(), {vt}, {i32});
        ctx.ensure_runtime_func("nv_len", len_fn_t);
        auto len_fn = ctx.get_module().lookupSymbol<mlir::func::FuncOp>("nv_len");
        auto len_i32 = mlir::func::CallOp::create(b, loc, len_fn, {iter}).getResult(0);
        auto len_idx = mlir::arith::IndexCastOp::create(b, loc, idx, len_i32).getResult();

        auto zero = mlir::arith::ConstantIndexOp::create(b, loc, 0).getResult();
        auto one  = mlir::arith::ConstantIndexOp::create(b, loc, 1).getResult();

        auto for_op = ctx.emit_for_range(loc, zero, len_idx, one, var, init_args);
        {
            mlir::OpBuilder::InsertionGuard g(b);
            auto* body_blk = &for_op.getBody().front();
            b.setInsertionPointToStart(body_blk);
            ctx.push_scope();

            mlir::Value iv    = body_blk->getArgument(0);
            auto iv_i32 = mlir::arith::IndexCastOp::create(b, loc, i32, iv).getResult();

            auto get_fn_t = mlir::FunctionType::get(&ctx.get_mlir_context(), {vt, i32}, {vt});
            ctx.ensure_runtime_func("nv_get_at", get_fn_t);
            auto get_fn = ctx.get_module().lookupSymbol<mlir::func::FuncOp>("nv_get_at");
            auto elem = mlir::func::CallOp::create(b, loc, get_fn, {iter, iv_i32}).getResult(0);
            if (!var.empty()) ctx.define(var, elem);

            for (size_t i = 0; i < carried.size(); ++i)
                ctx.define(carried[i].first, body_blk->getArgument(1 + i));

            nir_emit_body(body, ctx);

            // Yield the updated carried values (before pop_scope — the
            // lookups must resolve inside the body scope).
            if (body_blk->empty() ||
                !body_blk->back().hasTrait<mlir::OpTrait::IsTerminator>()) {
                llvm::SmallVector<mlir::Value> yv;
                for (auto& [name, _] : carried) {
                    mlir::Value v = ctx.lookup(name);
                    if (!v) v = nir_emit_const(ctx, loc, b.getI64IntegerAttr(0));
                    yv.push_back(v);
                }
                mlir::narval::YieldOp::create(b, loc, yv);
            }
            ctx.pop_scope();
        }
        b.setInsertionPointAfter(for_op);
        for (size_t i = 0; i < carried.size(); ++i)
            ctx.define(carried[i].first, for_op.getResults()[i]);
    }
}
