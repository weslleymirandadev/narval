#include "../nir_codegen_utils.hpp"
#include "backend/nir/NarvalOps.h"
#include "frontend/ast/statements/while_stmt_node.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "mlir/Dialect/Arith/IR/Arith.h"

void WhileStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto& b   = ctx.get_builder();
    auto  loc = ctx.loc(position.get());
    auto  vt  = ctx.get_narval_value_type();

    // Loop-carried variables: names assigned directly in the body (either a
    // `x = expr` assignment or a `x: T = expr` declaration) that already exist
    // in the enclosing scope. Their live values flow through the while's init
    // args / results.
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

    auto while_op = ctx.emit_while(loc, res_types, init_args);

    // Each region block receives the loop-carried values as block arguments.
    if (!init_args.empty()) {
        llvm::SmallVector<mlir::Location> arg_locs(init_args.size(), loc);
        while_op.getConditionRegion().front().addArguments(res_types, arg_locs);
        while_op.getBodyRegion().front().addArguments(res_types, arg_locs);
    }

    // Condition region — terminates with yield {i1, carried...}.
    {
        mlir::OpBuilder::InsertionGuard g(b);
        b.setInsertionPointToStart(&while_op.getConditionRegion().front());
        ctx.push_scope();
        for (size_t i = 0; i < carried.size(); ++i)
            ctx.define(carried[i].first,
                       while_op.getConditionRegion().front().getArgument(i));

        mlir::Value i1_cond;
        if (condition) {
            condition->nir_codegen(ctx);
            mlir::Value cond_val = ctx.pop_value();
            if (cond_val) i1_cond = nir_to_i1(ctx, loc, cond_val);
        }
        if (!i1_cond)
            i1_cond = mlir::arith::ConstantIntOp::create(b, loc, 1, 1).getResult();

        llvm::SmallVector<mlir::Value> yv{i1_cond};
        for (mlir::Value a : while_op.getConditionRegion().front().getArguments())
            yv.push_back(a);
        mlir::narval::YieldOp::create(b, loc, yv);
        ctx.pop_scope();
    }

    // Body region — terminates with yield {updated carried...}.
    {
        mlir::OpBuilder::InsertionGuard g(b);
        b.setInsertionPointToStart(&while_op.getBodyRegion().front());
        ctx.push_scope();
        for (size_t i = 0; i < carried.size(); ++i)
            ctx.define(carried[i].first,
                       while_op.getBodyRegion().front().getArgument(i));

        nir_emit_body(body, ctx);

        mlir::Block& body_blk = while_op.getBodyRegion().front();
        if (body_blk.empty() ||
            !body_blk.back().hasTrait<mlir::OpTrait::IsTerminator>()) {
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

    b.setInsertionPointAfter(while_op);

    // Write the final carried values back into the enclosing scope.
    for (size_t i = 0; i < carried.size(); ++i)
        ctx.define(carried[i].first, while_op.getResults()[i]);
}
