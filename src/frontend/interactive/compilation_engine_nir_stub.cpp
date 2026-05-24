#include "frontend/interactive/compilation_engine.hpp"
#include "frontend/interactive/repl.hpp"
#include "frontend/diagnostic.hpp"
#include "frontend/lexer/lexer.hpp"
#include "frontend/lexer/lexer_error.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/attributes/attribute_mapper.hpp"
#include "backend/nir/NIRGenerationContext.hpp"

#include "mlir/IR/MLIRContext.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Arith/IR/Arith.h"

#include "llvm/Support/raw_ostream.h"

#include <iostream>
#include <sstream>

namespace nv {

CompilationEngine::CompilationEngine(REPLState* s, ModuleManager& mm)
    : state(s), module_manager(mm) {}


bool CompilationEngine::compile_and_execute(const std::string& input,
                                             const std::string& source_name) {
    try {
        Lexer lexer(input, source_name);
        auto tokens = lexer.tokenize();

        Parser parser;
        auto ast = parser.produce_ast(tokens);
        if (!ast) return false;

        nv::Checker checker;
        checker.set_source_file(source_name);
        if (state && state->checker) {
            checker.scope = state->checker->scope;
        }
        checker.set_emit_diagnostics(true);
        checker.check_node(ast.get());

        if (checker.err) {
            if (state && state->config && state->config->show_errors) {
                for (const auto& d : checker.diagnostics)
                    print_diagnostic(d, input);
            }
            return false;
        }

        if (state && state->checker)
            state->checker->scope = checker.scope;

        mlir::MLIRContext mlir_ctx;
        nv::NIRGenerationContext nir_ctx(mlir_ctx, source_name);
        nir_ctx.set_type_checker(&checker);

        auto& b  = nir_ctx.get_builder();
        auto  ul = b.getUnknownLoc();
        auto  void_fn_ty = mlir::FunctionType::get(&mlir_ctx, {}, {});

        auto main_fn = mlir::func::FuncOp::create(b, ul, "main.start", void_fn_ty);
        main_fn.setPublic();
        auto* entry_blk = main_fn.addEntryBlock();
        b.setInsertionPointToStart(entry_blk);
        nir_ctx.set_current_func(main_fn);

        nv::generate_ir_nir(std::move(ast), nir_ctx);

        if (entry_blk->empty() ||
            !entry_blk->back().hasTrait<mlir::OpTrait::IsTerminator>()) {
            mlir::func::ReturnOp::create(b, ul);
        }

        auto result = nir_ctx.jit_execute();
        if (!result) {
            llvm::errs() << "NIR JIT error: "
                         << llvm::toString(result.takeError()) << "\n";
            return false;
        }
        return true;

    } catch (const LexicalError& e) {
        if (state && state->config && state->config->show_errors)
            print_diagnostic(e.diagnostic(), input);
        return false;
    } catch (const std::exception& e) {
        if (state && state->config && state->config->show_errors)
            std::cerr << "Error: " << e.what() << "\n";
        return false;
    }
}

void CompilationEngine::print_value(llvm::JITTargetAddress /*addr*/) {}

bool CompilationEngine::process_imports(const std::string& /*input*/,
                                         std::unique_ptr<Node>& /*ast*/) {
    return true;
}

void CompilationEngine::collect_repl_names(Node* /*node*/,
    std::unordered_set<std::string>& /*defined*/,
    std::unordered_set<std::string>& /*used*/) {}

bool CompilationEngine::handle_special_loop_cases(
    std::unique_ptr<Node>& /*ast*/,
    const std::unordered_set<std::string>& /*defined*/,
    const std::unordered_set<std::string>& /*used*/,
    const std::vector<std::string>& /*slots*/) {
    return false;
}

bool CompilationEngine::compile_expression(
    std::unique_ptr<Node>& /*ast*/,
    const std::unordered_set<std::string>& /*defined*/,
    const std::unordered_set<std::string>& /*used*/,
    const std::vector<std::string>& /*slots*/,
    bool /*single_write*/, bool /*contains_write*/) {
    return false;
}

bool CompilationEngine::should_print_result(
    const std::unordered_set<std::string>& /*defined*/,
    const std::unordered_set<std::string>& /*used*/,
    bool /*single_write*/, bool /*single_decl*/, bool /*single_id*/) {
    return false;
}

void CompilationEngine::box_and_store_result(
    llvm::Value* /*result*/,
    llvm::Value* /*out_param*/,
    llvm::IRBuilder<>& /*builder*/,
    llvm::Module* /*module*/) {}

void CompilationEngine::print_diagnostic(const nv::Diagnostic& d,
                                          const std::string& /*input*/) {
    const char* label = (d.severity == 3) ? "note" : "error";
    std::cerr << d.filename << ":" << d.line << ":" << d.col_start
              << ": " << label << ": " << d.message << "\n";
}

} // namespace nv
