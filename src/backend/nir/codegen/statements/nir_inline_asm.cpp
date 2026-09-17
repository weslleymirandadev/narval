#include "frontend/ast/statements/inline_asm_stmt_node.hpp"
#include "../nir_codegen_utils.hpp"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

// Inline assembly reaches the back end here. The statement is
//
//     asm { "template" } input { reg x; } output { x -> r; }
//
// and it lowers to one llvm.inline_asm per output binding:
//
//   - every input contributes a general register ("r") holding the variable's integer
//     value (nv_unbox_int, the counterpart of nv_box_int),
//   - an output "x -> r" is an output register tied to the input x, which is the classic
//     in/out operand pair ("=r,0"): the template is expected to modify that register and
//     the result is bound to r, boxed back with nv_box_int,
//   - operand numbering inside the template follows the same order as the constraints:
//     $0 is the first output, then the inputs in declaration order.
//
// The op is created with side effects, otherwise a template whose result is unused
// ("nop") is dropped as dead code.
void InlineAsmStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    if (asm_template.empty()) return;              // the parser already reported it

    auto& b   = ctx.get_builder();
    auto  loc = ctx.loc(position.get());
    auto  vt  = ctx.get_narval_value_type();
    auto  i64 = b.getI64Type();

    // ── inputs: unbox the named variables ────────────────────────────────
    std::vector<mlir::Value> input_values;
    std::vector<std::string> input_names;          // parallel to input_values
    for (const auto& in : inputs) {
        mlir::Value boxed = ctx.lookup(in.var_name);
        if (!boxed) continue;                      // unknown name: the checker owns that error
        ctx.ensure_runtime_func("nv_unbox_int",
            mlir::FunctionType::get(&ctx.get_mlir_context(), {vt}, {i64}));
        auto raw = mlir::narval::CallRuntimeOp::create(
            b, loc, mlir::TypeRange{i64},
            mlir::SymbolRefAttr::get(&ctx.get_mlir_context(), "nv_unbox_int"),
            mlir::ValueRange{boxed});
        input_values.push_back(raw.getResults()[0]);
        input_names.push_back(in.var_name);
    }

    // ── constraints and the output positions the inputs tie to ───────────
    std::vector<std::string> constraints;
    for (size_t i = 0; i < outputs.size(); ++i)
        constraints.push_back("=r");

    std::unordered_map<std::string, size_t> output_of_input;
    for (size_t i = 0; i < outputs.size(); ++i) {
        const std::string& src = outputs[i].from_var;
        for (size_t j = 0; j < input_names.size(); ++j) {
            if (input_names[j] == src) {
                output_of_input[src] = i;
                break;
            }
        }
    }
    for (const auto& name : input_names) {
        auto it = output_of_input.find(name);
        // Tied to the output register when the input is the source of one, otherwise a
        // plain input register.
        constraints.push_back(it != output_of_input.end() ? std::to_string(it->second) : "r");
    }

    std::string constraint_str;
    for (size_t i = 0; i < constraints.size(); ++i) {
        if (i) constraint_str += ",";
        constraint_str += constraints[i];
    }

    // llvm.inline_asm takes a single result, so more than one output rides inside an LLVM
    // struct ("o result group starting at #0 requires 0 or 1 element, but found 2"
    // otherwise) and each field is extracted below.
    mlir::Type asm_result_type = i64;
    const bool multi_output = outputs.size() > 1;
    if (multi_output)
        asm_result_type = mlir::LLVM::LLVMStructType::getLiteral(
            &ctx.get_mlir_context(), std::vector<mlir::Type>(outputs.size(), i64));

    auto asm_op = mlir::LLVM::InlineAsmOp::create(
        b, loc,
        mlir::TypeRange{asm_result_type},
        mlir::ValueRange(input_values),
        asm_template,
        constraint_str,
        /*has_side_effects=*/true,
        /*is_align_stack=*/false,
        mlir::LLVM::tailcallkind::TailCallKind::None,
        mlir::LLVM::AsmDialectAttr{},
        mlir::ArrayAttr{});

    // ── bind every output name to its boxed result ───────────────────────
    for (size_t i = 0; i < outputs.size() && asm_op->getNumResults() == 1; ++i) {
        mlir::Value raw_result = asm_op->getResult(0);
        if (multi_output) {
            raw_result = mlir::LLVM::ExtractValueOp::create(
                b, loc, mlir::TypeRange{i64}, raw_result,
                std::vector<int64_t>{static_cast<int64_t>(i)}).getResult();
        }
        ctx.ensure_runtime_func("nv_box_int",
            mlir::FunctionType::get(&ctx.get_mlir_context(), {i64}, {vt}));
        auto boxed = mlir::narval::CallRuntimeOp::create(
            b, loc, mlir::TypeRange{vt},
            mlir::SymbolRefAttr::get(&ctx.get_mlir_context(), "nv_box_int"),
            mlir::ValueRange{raw_result});
        ctx.define(outputs[i].result_var, boxed.getResults()[0]);
        nir_store_module_global(ctx, loc, outputs[i].result_var, boxed.getResults()[0]);
    }
}
