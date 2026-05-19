#include "frontend/interactive/compilation_engine.hpp"
#include "frontend/interactive/import_processor.hpp"
#include "frontend/interactive/repl.hpp"
#include "frontend/interactive/syntax_highlighter.hpp"
#include "frontend/lexer/lexer.hpp"
#include "frontend/lexer/lexer_error.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/ast/expressions/call_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/ast/statements/function_stmt_node.hpp"
#include "frontend/ast/statements/for_stmt_node.hpp"
#include "frontend/ast/statements/while_stmt_node.hpp"
#include "frontend/ast/statements/forever_stmt_node.hpp"
#include "frontend/ast/statements/match_stmt_node.hpp"
#include "frontend/ast/expressions/numeric_literal_node.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "backend/codegen/generate_ir.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <sstream>

// Incluir funções do runtime
#include "backend/runtime/nv_runtime.h"
#include "backend/runtime/prototypes.h"

extern "C" {
    void nv_write(Value* v);
    void create_str(Value* out, const char* s);
    void create_int(Value* out, int32_t v);
    void create_float(Value* out, double v);
    void create_bool(Value* out, int32_t b);
}

namespace nv {

namespace {

constexpr const char* ANSI_BOLD = "\x1b[1m";
constexpr const char* ANSI_RESET = "\x1b[0m";
constexpr const char* ANSI_RED = "\x1b[31m";
constexpr const char* ANSI_BLUE = "\x1b[34m";

bool contains_write_call(Node* node) {
    if (!node) return false;
    if (auto* prog = dynamic_cast<Program*>(node)) {
        for (const auto& stmt : prog->get_statements()) {
            if (contains_write_call(stmt.get())) return true;
        }
        return false;
    }
    if (auto* call = dynamic_cast<CallExprNode*>(node)) {
        if (auto* id = dynamic_cast<IdentifierNode*>(call->caller.get())) {
            if (id->symbol == "write") return true;
        }
        for (const auto& arg : call->args) {
            if (arg && contains_write_call(arg->value.get())) return true;
        }
        return false;
    }
    if (auto* if_stmt = dynamic_cast<IfStatementNode*>(node)) {
        if (contains_write_call(if_stmt->condition.get())) return true;
        for (const auto& stmt : if_stmt->consequent) {
            if (contains_write_call(stmt.get())) return true;
        }
        for (const auto& stmt : if_stmt->alternate) {
            if (contains_write_call(stmt.get())) return true;
        }
        return false;
    }
    if (auto* bin = dynamic_cast<BinaryExprNode*>(node)) {
        return contains_write_call(bin->left.get()) || contains_write_call(bin->right.get());
    }
    return false;
}

} // namespace

CompilationEngine::CompilationEngine(REPLState* state, ModuleManager& module_manager)
    : state(state), module_manager(module_manager) {}

bool CompilationEngine::compile_and_execute(const std::string& input, const std::string& source_name) {
    if (state) {
        state->source_cache[source_name] = input;
    }

    try {
        Lexer lexer(input, source_name);
        lexer.set_emit_diagnostics(false);
        auto tokens = lexer.tokenize();
        if (tokens.empty()) return true;

        // Coletar informações de importação para o parser
        std::vector<ImportInfo> import_infos;
        
        Parser parser;
        parser.set_emit_diagnostics(false);
        std::unique_ptr<Node> ast;
        try {
            ast = parser.produce_ast(tokens, import_infos);
        } catch (const std::exception&) {
            if (state->config && state->config->show_errors) {
                for (const auto& diagnostic : parser.diagnostics) {
                    print_diagnostic(diagnostic, input);
                }
            }
            return false; 
        }
        
        // Processa importações usando a API comum
        if (!process_imports(input, ast)) {
            return false;
        }

        bool single_write_call = false;
        bool last_stmt_no_result = false;
        bool single_declaration_no_print = false;
        bool single_identifier_expr = false;
        
        if (auto* prog = dynamic_cast<Program*>(ast.get())) {
            const auto& stmts = prog->get_statements();
            if (stmts.size() == 1) {
                Node* first = stmts[0].get();
                if (dynamic_cast<FunctionStmtNode*>(first)) single_declaration_no_print = true;
                else if (dynamic_cast<DeclarationStmtNode*>(first)) single_declaration_no_print = true;
                else if (auto* assign = dynamic_cast<AssignmentExprNode*>(first)) {
                    if (assign->target && dynamic_cast<IdentifierNode*>(assign->target.get()))
                        single_declaration_no_print = true;
                }
                if (!single_declaration_no_print) {
                    if (auto* call = dynamic_cast<CallExprNode*>(first)) {
                        if (auto* id = dynamic_cast<IdentifierNode*>(call->caller.get())) {
                            if (id->symbol == "write") single_write_call = true;
                        }
                    }
                    if (dynamic_cast<IdentifierNode*>(first)) single_identifier_expr = true;
                }
            }
            if (!stmts.empty()) {
                Node* last = stmts.back().get();
                if (dynamic_cast<IfStatementNode*>(last) ||
                    dynamic_cast<ForStmtNode*>(last) || dynamic_cast<WhileStmtNode*>(last) ||
                    dynamic_cast<ForeverStmtNode*>(last) || dynamic_cast<MatchStmtNode*>(last)) {
                    last_stmt_no_result = true;
                }
            }
        }

        state->checker->set_source_file(source_name);
        state->checker->set_emit_diagnostics(false);
        auto type = state->checker->check_node(ast.get());
        if (state->checker->err) { 
            if (state->config && state->config->show_errors) {
                for (const auto& diagnostic : state->checker->diagnostics) {
                    print_diagnostic(diagnostic, input);
                }
            }
            state->checker->set_emit_diagnostics(true);
            return false; 
        }
        state->checker->set_emit_diagnostics(true);

        std::unordered_set<std::string> defined_this_line, used_this_line;
        collect_repl_names(ast.get(), defined_this_line, used_this_line);

        auto temp_module = std::make_unique<llvm::Module>("repl_expr", *state->llvm_context);
        auto temp_builder = std::make_unique<llvm::IRBuilder<llvm::NoFolder>>(*state->llvm_context);

        IRGenerationContext context(*state->llvm_context, *temp_module, *temp_builder, state->checker.get());

        auto& C = *state->llvm_context;
        auto* ValueTy = nv::ir_utils::get_value_struct(context);
        auto* VoidTy  = llvm::Type::getVoidTy(C);
        auto* ValuePtr= nv::ir_utils::get_value_ptr(context);

        temp_module->getOrInsertFunction("nv_write", llvm::FunctionType::get(VoidTy, {ValuePtr}, false));
        temp_module->getOrInsertFunction("create_str", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::PointerType::getUnqual(C)}, false));
        temp_module->getOrInsertFunction("create_int", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::Type::getInt32Ty(C)}, false));
        temp_module->getOrInsertFunction("create_float", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::Type::getDoubleTy(C)}, false));
        temp_module->getOrInsertFunction("create_bool", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::Type::getInt32Ty(C)}, false));

        std::vector<std::string> slot_names;
        for (const std::string& name : defined_this_line) {
            std::shared_ptr<nv::Type> nv_type;
            try { nv_type = state->checker->scope->get_key(name); } catch (...) { continue; }
            if (!nv_type) continue;
            nv_type = context.resolve_type(nv_type);
            if (nv_type->kind == nv::Kind::FUNCTION) continue;
            slot_names.push_back(name);
            if (state->repl_var_values.find(name) == state->repl_var_values.end()) {
                state->repl_var_values[name] = Value{};
            }
        }
        for (const std::string& name : used_this_line) {
            if (defined_this_line.count(name)) continue;
            if (!state->repl_global_names.count(name)) continue;
            std::shared_ptr<nv::Type> nv_type;
            try { nv_type = state->checker->scope->get_key(name); } catch (...) { continue; }
            if (!nv_type) continue;
            nv_type = context.resolve_type(nv_type);
            if (nv_type->kind == nv::Kind::FUNCTION) {
                auto* function = std::static_pointer_cast<nv::Function>(nv_type).get();
                if (!function) continue;
                std::vector<llvm::Type*> param_tys;
                for (const auto& p : function->paramstype)
                    param_tys.push_back(context.nv_type_to_llvm(p));
                llvm::Type* ret_ty = context.nv_type_to_llvm(function->returntype);
                auto* ft = llvm::FunctionType::get(ret_ty, param_tys, false);
                auto ext_fn = temp_module->getOrInsertFunction(name, ft);
                llvm::Function* fn = llvm::cast<llvm::Function>(ext_fn.getCallee());
                nv::SymbolInfo info(fn, fn->getType(), nv_type, false, true);
                context.get_symbol_table().define_symbol(name, info);
            } else {
                if (std::find(slot_names.begin(), slot_names.end(), name) == slot_names.end())
                    slot_names.push_back(name);
                if (state->repl_var_values.find(name) == state->repl_var_values.end())
                    state->repl_var_values[name] = Value{};
            }
        }

        // Handle special loop cases
        if (handle_special_loop_cases(ast, defined_this_line, used_this_line, slot_names)) {
            return true;
        }

        bool has_write_call = contains_write_call(ast.get());
        return compile_expression(ast, defined_this_line, used_this_line, slot_names, single_write_call, has_write_call || last_stmt_no_result);
    } catch (const LexicalError& e) {
        if (state->config && state->config->show_errors) {
            print_diagnostic(e.diagnostic(), input);
        }
        return false;
    } catch (const std::exception& e) {
        if (state->config && state->config->show_errors) {
            std::cerr << "Compilation error: " << e.what() << std::endl;
        }
        return false;
    }
}

void CompilationEngine::print_diagnostic(const nv::Diagnostic& diagnostic, const std::string& input) {
    const bool is_note = diagnostic.severity == 3;
    const char* label = is_note ? "NOTE" : "ERROR";
    const char* color = is_note ? ANSI_BLUE : ANSI_RED;
    const std::string* source = &input;
    if (state) {
        auto cached = state->source_cache.find(diagnostic.filename);
        if (cached != state->source_cache.end()) {
            source = &cached->second;
        }
    }

    std::cout.flush();
    std::cerr << ANSI_BOLD
              << diagnostic.filename << ":" << diagnostic.line << ":" << diagnostic.col_start << ": "
              << color << label << ANSI_RESET << ANSI_BOLD << ": "
              << diagnostic.message << ANSI_RESET << "\n";

    std::istringstream stream(*source);
    std::string line;
    size_t current_line = 1;
    while (std::getline(stream, line)) {
        if (current_line == diagnostic.line) {
            std::cerr << " " << diagnostic.line << " |   "
                      << syntax_highlighter::highlight_line(line) << "\n";
            int line_width = diagnostic.line > 0
                ? static_cast<int>(std::log10(static_cast<double>(diagnostic.line)) + 1)
                : 1;
            std::cerr << std::string(line_width, ' ') << "  |"
                      << std::string(diagnostic.col_start > 0 ? diagnostic.col_start + 2 : 3, ' ');
            std::cerr << color;
            size_t caret_end = std::max(diagnostic.col_end, diagnostic.col_start + 1);
            for (size_t col = diagnostic.col_start; col < caret_end; ++col) {
                std::cerr << "^";
            }
            std::cerr << ANSI_RESET << "\n\n";
            return;
        }
        current_line++;
    }

    std::cerr << "\n";
}

bool CompilationEngine::process_imports(const std::string& input, std::unique_ptr<Node>& ast) {
    return ImportProcessor::process_imports(input, ast, state, module_manager);
}

void CompilationEngine::collect_repl_names(Node* node,
    std::unordered_set<std::string>& defined,
    std::unordered_set<std::string>& used) {
    if (!node) return;
    if (auto* id = dynamic_cast<IdentifierNode*>(node)) {
        used.insert(id->symbol);
        return;
    }
    if (auto* assign = dynamic_cast<AssignmentExprNode*>(node)) {
        if (assign->target && assign->value) {
            if (auto* lid = dynamic_cast<IdentifierNode*>(assign->target.get()))
                defined.insert(lid->symbol);
            collect_repl_names(assign->target.get(), defined, used);
            collect_repl_names(assign->value.get(), defined, used);
        }
        return;
    }
    if (auto* decl = dynamic_cast<DeclarationStmtNode*>(node)) {
        if (decl->target) {
            if (auto* tid = dynamic_cast<IdentifierNode*>(decl->target.get()))
                defined.insert(tid->symbol);
            collect_repl_names(decl->target.get(), defined, used);
        }
        if (decl->value) collect_repl_names(decl->value.get(), defined, used);
        return;
    }
    if (auto* function = dynamic_cast<FunctionStmtNode*>(node)) {
        defined.insert(function->name);
        for (auto& p : function->parameters)
            for (auto& kv : p.parameter)
                collect_repl_names(nullptr, defined, used);
        for (auto& s : function->body)
            if (s) collect_repl_names(s.get(), defined, used);
        return;
    }
    if (auto* prog = dynamic_cast<Program*>(node)) {
        for (const auto& s : prog->get_statements())
            if (s) collect_repl_names(s.get(), defined, used);
        return;
    }
    if (auto* call = dynamic_cast<CallExprNode*>(node)) {
        if (call->caller) collect_repl_names(call->caller.get(), defined, used);
        for (auto& a : call->args)
            if (a && a->value) collect_repl_names(a->value.get(), defined, used);
        return;
    }
    if (auto* bin = dynamic_cast<BinaryExprNode*>(node)) {
        if (bin->left) collect_repl_names(bin->left.get(), defined, used);
        if (bin->right) collect_repl_names(bin->right.get(), defined, used);
        return;
    }
    // Add more node types as needed...
}

bool CompilationEngine::handle_special_loop_cases(std::unique_ptr<Node>& ast, 
    const std::unordered_set<std::string>& defined_this_line, 
    const std::unordered_set<std::string>& used_this_line, 
    const std::vector<std::string>& slot_names) {
    
    Program* prog_ptr = dynamic_cast<Program*>(ast.get());
    if (!prog_ptr || prog_ptr->get_statements().size() != 1) return false;
    
    ForStmtNode* for_node = dynamic_cast<ForStmtNode*>(prog_ptr->get_statements()[0].get());
    if (!for_node || !for_node->iterable || for_node->range_start || for_node->range_end
        || for_node->body.size() != 1 || !for_node->else_block.empty()
        || for_node->bindings.size() != 1) {
        return false;
    }
    
    NumericLiteralNode* lit = dynamic_cast<NumericLiteralNode*>(for_node->iterable.get());
    IdentifierNode* bid = dynamic_cast<IdentifierNode*>(for_node->bindings[0].get());
    if (!lit || !bid) return false;
    
    int N = 0;
    try { N = std::stoi(lit->value); } catch (...) { return false; }
    if (N <= 0 || N > 100000) return false;
    
    // This is a simplified version - the full implementation would be quite long
    // For now, return false to indicate we didn't handle this special case
    return false;
}

bool CompilationEngine::compile_expression(std::unique_ptr<Node>& ast,
    const std::unordered_set<std::string>& defined_this_line,
    const std::unordered_set<std::string>& used_this_line,
    const std::vector<std::string>& slot_names,
    bool single_write_call,
    bool contains_write_call) {
    
    auto temp_module = std::make_unique<llvm::Module>("repl_expr", *state->llvm_context);
    auto temp_builder = std::make_unique<llvm::IRBuilder<llvm::NoFolder>>(*state->llvm_context);
    
    IRGenerationContext context(*state->llvm_context, *temp_module, *temp_builder, state->checker.get());
    auto& C = *state->llvm_context;
    auto* ValueTy = nv::ir_utils::get_value_struct(context);
    auto* VoidTy = llvm::Type::getVoidTy(C);
    auto* ValuePtr = nv::ir_utils::get_value_ptr(context);

    temp_module->getOrInsertFunction("nv_write", llvm::FunctionType::get(VoidTy, {ValuePtr}, false));
    temp_module->getOrInsertFunction("create_str", llvm::FunctionType::get(VoidTy, {ValuePtr, nv::ir_utils::get_i8_ptr(context)}, false));
    temp_module->getOrInsertFunction("create_int", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::Type::getInt32Ty(C)}, false));
    temp_module->getOrInsertFunction("create_float", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::Type::getDoubleTy(C)}, false));
    temp_module->getOrInsertFunction("create_bool", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::Type::getInt32Ty(C)}, false));

    // Setup global variables and functions (missing part)
    for (const std::string& name : state->repl_global_names) {
        std::shared_ptr<nv::Type> nv_type;
        try { nv_type = state->checker->scope->get_key(name); } catch (...) { continue; }
        if (!nv_type) continue;
        nv_type = context.resolve_type(nv_type);
        if (nv_type->kind != nv::Kind::FUNCTION) continue;
        auto* function = std::static_pointer_cast<nv::Function>(nv_type).get();
        if (!function) continue;
        std::vector<llvm::Type*> param_tys;
        for (const auto& p : function->paramstype)
            param_tys.push_back(context.nv_type_to_llvm(p));
        llvm::Type* ret_ty = context.nv_type_to_llvm(function->returntype);
        auto* ft = llvm::FunctionType::get(ret_ty, param_tys, false);
        auto ext_fn = temp_module->getOrInsertFunction(name, ft);
        llvm::Function* fn = llvm::cast<llvm::Function>(ext_fn.getCallee());
        nv::SymbolInfo info(fn, fn->getType(), nv_type, false, true);
        context.get_symbol_table().define_symbol(name, info);
    }

    // Add global variables for slots
    auto* I32 = llvm::Type::getInt32Ty(C);
    for (const std::string& name : slot_names) {
        if (state->repl_globals_added.count(name)) continue;
        auto glob_mod = std::make_unique<llvm::Module>("repl_globals_" + name, C);
        (void)new llvm::GlobalVariable(*glob_mod, ValueTy, false, llvm::GlobalValue::ExternalLinkage,
            llvm::Constant::getNullValue(ValueTy), "repl_global_" + name);
        auto tsm_glob = llvm::orc::ThreadSafeModule(std::move(glob_mod), std::make_unique<llvm::LLVMContext>());
        if (auto err = state->jit->addIRModule(std::move(tsm_glob))) {
            llvm::consumeError(std::move(err));
            continue;
        }
        state->repl_globals_added.insert(name);
    }
    
    static int expr_counter = 0;
    std::string func_name = "__repl_expr_" + std::to_string(expr_counter++);
    
    std::vector<llvm::Type*> param_tys = { ValuePtr };
    if (!slot_names.empty()) {
        param_tys.push_back(llvm::PointerType::getUnqual(C));
    }
    auto* func_type = llvm::FunctionType::get(VoidTy, param_tys, false);
    auto* func = llvm::Function::Create(func_type, llvm::Function::ExternalLinkage, func_name, temp_module.get());
    
    func->getArg(0)->setName("out_result");
    llvm::Value* out_param = func->getArg(0);
    llvm::Value* slots_param = slot_names.empty() ? nullptr : func->getArg(1);
    
    auto* entry_bb = llvm::BasicBlock::Create(C, "entry", func);
    temp_builder->SetInsertPoint(entry_bb);
    
    // Setup slot variables
    if (slots_param) {
        for (size_t i = 0; i < slot_names.size(); ++i) {
            const std::string& name = slot_names[i];
            llvm::Value* slot_addr = temp_builder->CreateGEP(ValuePtr, slots_param, llvm::ConstantInt::get(I32, i));
            llvm::Value* slot = temp_builder->CreateLoad(ValuePtr, slot_addr);
            llvm::GlobalVariable* global_var = llvm::cast<llvm::GlobalVariable>(
                temp_module->getOrInsertGlobal("repl_global_" + name, ValueTy));
            if (global_var->isDeclaration()) global_var->setLinkage(llvm::GlobalValue::ExternalLinkage);
            llvm::Value* global_ptr = temp_builder->CreatePointerCast(global_var, ValuePtr);
            llvm::Value* val_from_host = temp_builder->CreateLoad(ValueTy, slot, name + "_load");
            temp_builder->CreateStore(val_from_host, global_var);
            std::shared_ptr<nv::Type> nv_type;
            try { nv_type = state->checker->scope->get_key(name); } catch (...) { continue; }
            if (nv_type) nv_type = context.resolve_type(nv_type);
            nv::SymbolInfo info(global_ptr, ValueTy, nv_type, false, false);
            context.get_symbol_table().define_symbol(name, info);
        }
    }
    
    context.set_current_function(func);
    context.enter_scope();
    
    nv::generate_ir(std::move(ast), context, /*keep_result=*/ true);
    
    // Save slot variables back
    if (slots_param) {
        for (size_t i = 0; i < slot_names.size(); ++i) {
            const std::string& name = slot_names[i];
            llvm::GlobalVariable* global_var = temp_module->getGlobalVariable("repl_global_" + name);
            if (!global_var) continue;
            llvm::Value* slot_addr = temp_builder->CreateGEP(ValuePtr, slots_param, llvm::ConstantInt::get(I32, i));
            llvm::Value* slot = temp_builder->CreateLoad(ValuePtr, slot_addr);
            llvm::Value* val_to_host = temp_builder->CreateLoad(ValueTy, global_var, name + "_out");
            temp_builder->CreateStore(val_to_host, slot);
        }
    }
    
    bool have_result = false;
    if (context.has_value()) {
        llvm::Value* result = context.pop_value();
        if (result) {
            llvm::Value* to_store = nullptr;
            if (result->getType() == ValueTy) {
                to_store = result;
            } else if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(result);
                       alloca && alloca->getAllocatedType() == ValueTy) {
                to_store = temp_builder->CreateLoad(ValueTy, result, "repl_result_load");
            } else {
                auto* boxed = context.create_alloca(ValueTy, "repl_result_box");
                if (result->getType()->isIntegerTy(1)) {
                    auto* create_bool = context.ensure_runtime_func("create_bool", {ValuePtr, I32});
                    temp_builder->CreateCall(create_bool, {boxed, temp_builder->CreateZExt(result, I32)});
                    to_store = temp_builder->CreateLoad(ValueTy, boxed, "repl_bool_result");
                } else if (result->getType()->isIntegerTy()) {
                    auto* create_int = context.ensure_runtime_func("create_int", {ValuePtr, I32});
                    llvm::Value* iv = result->getType()->isIntegerTy(32)
                        ? result
                        : temp_builder->CreateSExtOrTrunc(result, I32);
                    temp_builder->CreateCall(create_int, {boxed, iv});
                    to_store = temp_builder->CreateLoad(ValueTy, boxed, "repl_int_result");
                } else if (result->getType()->isFloatingPointTy()) {
                    auto* F64 = llvm::Type::getDoubleTy(C);
                    auto* create_float = context.ensure_runtime_func("create_float", {ValuePtr, F64});
                    llvm::Value* fv = result->getType() == F64
                        ? result
                        : temp_builder->CreateFPExt(result, F64);
                    temp_builder->CreateCall(create_float, {boxed, fv});
                    to_store = temp_builder->CreateLoad(ValueTy, boxed, "repl_float_result");
                } else if (result->getType()->isPointerTy()) {
                    auto* I8P = nv::ir_utils::get_i8_ptr(context);
                    auto* create_str = context.ensure_runtime_func("create_str", {ValuePtr, I8P});
                    llvm::Value* sv = result->getType() == I8P
                        ? result
                        : temp_builder->CreateBitCast(result, I8P);
                    temp_builder->CreateCall(create_str, {boxed, sv});
                    to_store = temp_builder->CreateLoad(ValueTy, boxed, "repl_str_result");
                }
            }
            if (to_store) {
                temp_builder->CreateStore(to_store, out_param);
                have_result = true;
            }
        }
    }
    
    temp_builder->CreateRetVoid();
    
    auto tsm = llvm::orc::ThreadSafeModule(std::move(temp_module), std::make_unique<llvm::LLVMContext>());
    if (auto err = state->jit->addIRModule(std::move(tsm))) {
        llvm::errs() << "JIT compilation failed: " << err << "\n";
        llvm::consumeError(std::move(err));
        if (state->config && state->config->show_errors) {
            std::cerr << "JIT compilation failed" << std::endl;
        }
        return false;
    }
    
    Value result_buffer = {};
    if (auto func_symbol = state->jit->lookup(func_name)) {
        void* addr = (void*)func_symbol->getValue();
        if (contains_write_call && state->config && state->config->label_write_output && !state->config->output_prompt.empty()) {
            std::cout << state->config->output_prompt;
        }
        if (slot_names.empty()) {
            ((void(*)(Value*))addr)(&result_buffer);
        } else {
            std::vector<Value*> slot_ptrs;
            for (const auto& n : slot_names) slot_ptrs.push_back(&state->repl_var_values[n]);
            ((void(*)(Value*, Value**))addr)(&result_buffer, slot_ptrs.data());
        }
        
        if (have_result && !single_write_call && !contains_write_call) {
            if (state->config && !state->config->output_prompt.empty()) {
                std::cout << state->config->output_prompt;
            }
            print_value(llvm::JITTargetAddress(&result_buffer));
        }
    } else {
        if (state->config && state->config->show_errors) {
            std::cerr << "Failed to find compiled function" << std::endl;
        }
        return false;
    }
    
    for (const std::string& n : defined_this_line) {
        state->repl_global_names.insert(n);
    }
    
    return true;
}

void CompilationEngine::print_value(llvm::JITTargetAddress addr) {
    auto sym = state->get_symbol("nv_write");
    if (sym) {
        using NvWriteFn = void(*)(Value*);
        (*reinterpret_cast<NvWriteFn>(*sym))(reinterpret_cast<Value*>(addr));
    }
}

} // namespace nv
