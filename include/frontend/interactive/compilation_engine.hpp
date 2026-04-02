#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_set>
#include <llvm/ExecutionEngine/JITSymbol.h>

#include "frontend/ast/ast.hpp"
#include "frontend/interactive/repl_state.hpp"
#include "frontend/module_manager.hpp"

namespace nv {

struct REPLConfig;

class CompilationEngine {
public:
    explicit CompilationEngine(REPLState* state, ModuleManager& module_manager);
    ~CompilationEngine() = default;

    bool compile_and_execute(const std::string& input);
    void print_value(llvm::JITTargetAddress addr);

private:
    REPLState* state;
    ModuleManager& module_manager;
    
    bool process_imports(const std::string& input, std::unique_ptr<Node>& ast);
    void collect_repl_names(Node* node, std::unordered_set<std::string>& defined, std::unordered_set<std::string>& used);
    bool handle_special_loop_cases(std::unique_ptr<Node>& ast, const std::unordered_set<std::string>& defined_this_line, const std::unordered_set<std::string>& used_this_line, const std::vector<std::string>& slot_names);
    bool compile_expression(std::unique_ptr<Node>& ast, const std::unordered_set<std::string>& defined_this_line, const std::unordered_set<std::string>& used_this_line, const std::vector<std::string>& slot_names, bool single_write_call);
    bool should_print_result(const std::unordered_set<std::string>& defined_this_line, const std::unordered_set<std::string>& used_this_line, bool single_write_call, bool single_declaration_no_print, bool single_identifier_expr);
    void box_and_store_result(llvm::Value* result, llvm::Value* out_param, llvm::IRBuilder<>& builder, llvm::Module* module);
};

} // namespace nv
