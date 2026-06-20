#include "backend/nir/NarvalPasses.h"
#include "backend/nir/NarvalTypes.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#define GEN_PASS_DEF_FIXSCFIFTYPES
#include "NarvalPasses.h.inc"

using namespace mlir;
using namespace mlir::narval;

namespace nv {
namespace {

//===----------------------------------------------------------------------===//
// FixSCFIfTypesPass — Fix scf.if result types that still have !narval.value
// Also updates scf.yield operands to match the new result types.
//===----------------------------------------------------------------------===//

struct FixSCFIfTypesPassImpl
    : public mlir::PassWrapper<FixSCFIfTypesPassImpl,
                                mlir::OperationPass<mlir::ModuleOp>> {
    MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FixSCFIfTypesPassImpl)

    StringRef getName() const override { return "fix-scf-if-types"; }

    void runOnOperation() override {
        mlir::ModuleOp module = getOperation();
        llvm::SmallVector<mlir::scf::IfOp> if_ops;
        module.walk([&](mlir::scf::IfOp op) {
            bool has_narval = false;
            for (auto t : op.getResultTypes())
                if (mlir::isa<narval::ValueType, narval::RefType,
                              narval::MutRefType>(t))
                    has_narval = true;
            if (has_narval)
                if_ops.push_back(op);
        });

        for (auto op : if_ops) {
            mlir::OpBuilder b(op);
            auto loc = op.getLoc();

            auto ptr_ty = LLVM::LLVMPointerType::get(b.getContext());
            llvm::SmallVector<mlir::Type> res_types;
            llvm::SmallVector<unsigned> converted_indices;
            for (unsigned i = 0; i < op.getNumResults(); ++i) {
                auto t = op.getResultTypes()[i];
                if (mlir::isa<narval::ValueType, narval::RefType,
                              narval::MutRefType>(t)) {
                    res_types.push_back(ptr_ty);
                    converted_indices.push_back(i);
                } else {
                    res_types.push_back(t);
                }
            }

            bool has_else = !op.getElseRegion().empty() &&
                            !op.getElseRegion().front().empty();

            auto new_if = mlir::scf::IfOp::create(b, loc, res_types,
                                                   op.getCondition(),
                                                   has_else);

            // Fix then region: update yield operands and move region
            {
                auto* old_then = &op.getThenRegion().front();
                auto* new_then_blk = &new_if.getThenRegion().front();
                // Fix yield operands in then region: for converted indices,
                // strip unrealized_conversion_cast if present
                auto& then_ops = old_then->getOperations();
                if (!then_ops.empty()) {
                    auto* term = &then_ops.back();
                    if (auto yield = mlir::dyn_cast<mlir::scf::YieldOp>(term)) {
                        bool changed = false;
                        llvm::SmallVector<mlir::Value> new_operands;
                        for (unsigned j = 0; j < yield.getNumOperands(); ++j) {
                            auto val = yield.getOperand(j);
                            // If this operand is at a converted index and is
                            // produced by an unrealized_conversion_cast, strip it
                            if (std::find(converted_indices.begin(),
                                          converted_indices.end(), j)
                                    != converted_indices.end()) {
                                if (auto def_op = val.getDefiningOp()) {
                                    if (mlir::isa<mlir::UnrealizedConversionCastOp>(def_op)) {
                                        new_operands.push_back(def_op->getOperand(0));
                                        def_op->erase();
                                        changed = true;
                                        continue;
                                    }
                                }
                            }
                            new_operands.push_back(val);
                        }
                        if (changed) {
                            yield->setOperands(new_operands);
                        }
                    }
                }
                // Move the fixed block into the new if
                old_then->moveBefore(new_then_blk);
                new_then_blk->erase();
            }

            // Fix else region: same yield operand fix
            if (has_else) {
                auto* old_else = &op.getElseRegion().front();
                auto* new_else_blk = &new_if.getElseRegion().front();
                auto& else_ops = old_else->getOperations();
                if (!else_ops.empty()) {
                    auto* term = &else_ops.back();
                    if (auto yield = mlir::dyn_cast<mlir::scf::YieldOp>(term)) {
                        bool changed = false;
                        llvm::SmallVector<mlir::Value> new_operands;
                        for (unsigned j = 0; j < yield.getNumOperands(); ++j) {
                            auto val = yield.getOperand(j);
                            if (std::find(converted_indices.begin(),
                                          converted_indices.end(), j)
                                    != converted_indices.end()) {
                                if (auto def_op = val.getDefiningOp()) {
                                    if (mlir::isa<mlir::UnrealizedConversionCastOp>(def_op)) {
                                        new_operands.push_back(def_op->getOperand(0));
                                        def_op->erase();
                                        changed = true;
                                        continue;
                                    }
                                }
                            }
                            new_operands.push_back(val);
                        }
                        if (changed) {
                            yield->setOperands(new_operands);
                        }
                    }
                }
                old_else->moveBefore(new_else_blk);
                new_else_blk->erase();
            }

            op.replaceAllUsesWith(new_if.getResults());
            op.erase();
        }
    }
};

} // namespace

std::unique_ptr<mlir::Pass> createFixSCFIfTypesPass() {
    return std::make_unique<FixSCFIfTypesPassImpl>();
}

} // namespace nv
