#include "frontend/ast/expressions/post_decrement_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/access_expr_node.hpp"
#include "backend/codegen/ir_utils.hpp"

void PostDecrementExprNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    auto& b = ctx.get_builder();

    llvm::Value* addr = nullptr;
    llvm::Type* elemTy = nullptr;

    if (auto* id = dynamic_cast<IdentifierNode*>(operand.get())) {
        auto info_opt = ctx.get_symbol_table().lookup_symbol(id->symbol);
        if (!info_opt.has_value()) { ctx.push_value(nullptr); return; }
        auto info = info_opt.value();
        addr = info.value; elemTy = info.llvm_type;
    } else if (auto* acc = dynamic_cast<AccessExprNode*>(operand.get())) {
        if (acc->expr) acc->expr->codegen(ctx);
        llvm::Value* base = ctx.pop_value();
        if (!base || !base->getType()->isStructTy()) { ctx.push_value(nullptr); return; }
        auto* viewTy = llvm::cast<llvm::StructType>(base->getType());
        if (!viewTy->hasName() || viewTy->getName() != "nv.array.view") { ctx.push_value(nullptr); return; }
        if (acc->index) acc->index->codegen(ctx);
        llvm::Value* idx_v = ctx.pop_value();
        auto* i32 = llvm::Type::getInt32Ty(ctx.get_context());
        if (!idx_v || idx_v->getType() != i32) idx_v = nv::ir_utils::promote_type(ctx, idx_v, i32);
        auto* allocaView = ctx.create_alloca(viewTy, "pdec.view");
        b.CreateStore(base, allocaView);
        auto* dataPtr = b.CreateStructGEP(viewTy, allocaView, 1);
        auto* data = b.CreateLoad(llvm::PointerType::getUnqual(i32), dataPtr);
        addr = b.CreateInBoundsGEP(i32, data, idx_v);
        elemTy = i32;
    } else {
        ctx.push_value(nullptr); return;
    }

    auto* oldv = b.CreateLoad(elemTy, addr);
    auto* one = elemTy->isDoubleTy() ? (llvm::Value*)llvm::ConstantFP::get(elemTy, 1.0)
                                     : (llvm::Value*)llvm::ConstantInt::get(elemTy, 1);
    llvm::Value* next = elemTy->isDoubleTy() ? (llvm::Value*)b.CreateFSub(oldv, one)
                                             : (llvm::Value*)b.CreateSub(oldv, one);
    b.CreateStore(next, addr);
    ctx.push_value(oldv);
}
