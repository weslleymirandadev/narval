#include "frontend/interactive/repl.hpp"
#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#ifdef RETURN
#undef RETURN
#endif
#endif
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/ast/program.hpp"
#include "frontend/ast/expressions/call_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/binary_expr_node.hpp"
#include "frontend/ast/expressions/access_expr_node.hpp"
#include "frontend/ast/expressions/member_expr_node.hpp"
#include "frontend/ast/expressions/conditional_expr_node.hpp"
#include "frontend/ast/expressions/unary_minus_expr_node.hpp"
#include "frontend/ast/expressions/logical_not_expr_node.hpp"
#include "frontend/ast/expressions/array_expr_node.hpp"
#include "frontend/ast/expressions/tuple_expr_node.hpp"
#include "frontend/ast/expressions/vector_expr_node.hpp"
#include "frontend/ast/expressions/range_expr_node.hpp"
#include "frontend/ast/expressions/increment_expr_node.hpp"
#include "frontend/ast/expressions/decrement_expr_node.hpp"
#include "frontend/ast/expressions/post_increment_expr_node.hpp"
#include "frontend/ast/expressions/post_decrement_expr_node.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/ast/statements/def_stmt_node.hpp"
#include "frontend/ast/statements/for_stmt_node.hpp"
#include "frontend/ast/statements/while_stmt_node.hpp"
#include "frontend/ast/expressions/numeric_literal_node.hpp"
#include "frontend/ast/statements/loop_stmt_node.hpp"
#include "frontend/ast/statements/match_stmt_node.hpp"
#include "frontend/checker/type.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "backend/codegen/generate_ir.hpp"
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <llvm/TargetParser/Host.h>
#include <dlfcn.h>

// Incluir funções do runtime
#include "backend/runtime/nv_runtime.h"
#include "backend/runtime/prototypes.h"

extern "C" {
    void nv_write(void* v);
    void create_str(void* out, const char* s);
    void create_int(void* out, int32_t v);
    void create_float(void* out, double v);
    void create_bool(void* out, int b);
}

#ifdef HAVE_READLINE
extern "C" {
    char* rl_completer(const char*, int);
    char** rl_completion(const char*, int, int);
}
#endif

namespace nv {

REPL::REPL(const REPLConfig& config) : config(config), state(std::make_unique<REPLState>()) {
#ifdef HAVE_READLINE
    if (config.enable_readline) {
        rl_bind_key('\t', rl_complete);
    }
#endif
}

REPL::~REPL() = default;

bool REPL::initialize() {
    if (!state->initialize()) {
        std::cerr << "Failed to initialize REPL" << std::endl;
        return false;
    }
    std::cout << "Narval REPL - Interactive Compiler" << std::endl;
    std::cout << "Type :help for available commands or :quit to exit" << std::endl;
    return true;
}

std::string REPL::read_input() {
    std::string line;
#ifdef HAVE_READLINE
    if (config.enable_readline) {
        char* raw_line = readline(in_multiline ? config.multiline_prompt.c_str() : config.prompt.c_str());
        if (!raw_line) return "";
        line = raw_line;
        free(raw_line);
        if (!line.empty()) add_history(line.c_str());
    } else {
#endif
        if (config.show_prompt) {
            std::cout << (in_multiline ? config.multiline_prompt : config.prompt);
            std::cout.flush();
        }
        std::getline(std::cin, line);
#ifdef HAVE_READLINE
    }
#endif
    return line;
}

bool REPL::is_complete_expression(const std::string& input) {
    return repl_utils::is_brace_balanced(input) && 
           !repl_utils::ends_with_operator(input) &&
           !repl_utils::needs_continuation(input);
}

std::string REPL::preprocess_input(const std::string& input) {
    std::string result = repl_utils::sanitize_input(input);
    if (!result.empty()) {
        size_t last = result.find_last_not_of(" \t\n\r");
        if (last != std::string::npos)
            result.erase(last + 1);
        else
            result.clear();
    }

    bool is_function_def = result.find("def ") == 0 || result.find("\ndef ") != std::string::npos;
    bool has_braces = result.find('{') != std::string::npos;
    bool will_auto_print = should_auto_print(result);

    bool looks_like_assignment = false;
    size_t eq_pos = result.find('=');
    if (eq_pos != std::string::npos) {
        if (result.find("==") == std::string::npos && result.find("!=") == std::string::npos
            && result.find("<=") == std::string::npos && result.find(">=") == std::string::npos
            && result.find("=>") == std::string::npos && result.find("->") == std::string::npos) {
            looks_like_assignment = true;
        }
    }

    if (!result.empty() && result.back() != ';' && !is_function_def && !has_braces && (!will_auto_print || looks_like_assignment)) {
        result += ";";
    }

    return result;
}

bool REPL::should_auto_print(const std::string& input) {
    if (!config.auto_print) return false;
    std::string trimmed = input;
    size_t start = trimmed.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return false;
    trimmed = trimmed.substr(start);
    size_t last = trimmed.find_last_not_of(" \t\n\r");
    if (last != std::string::npos) trimmed.erase(last + 1);
    if (trimmed.empty()) return false;
    static const char* stmt_prefixes[] = {
        "def ", "let ", "if ", "for ", "while ", "loop ", "match ", "return ", "break ", "continue ", "import "
    };
    for (const char* p : stmt_prefixes) {
        if (trimmed.size() >= strlen(p) && trimmed.compare(0, strlen(p), p) == 0)
            return false;
    }
    size_t semi = trimmed.find(';');
    if (semi != std::string::npos && semi < trimmed.size() - 1) {
        std::string after = trimmed.substr(semi + 1);
        after.erase(0, after.find_first_not_of(" \t\n\r"));
        if (!after.empty()) return false;
    }
    return true;
}

std::string REPL::generate_result_var() {
    return state->last_result_var + std::to_string(state->result_counter++);
}

// collect_repl_names helper
namespace {
void collect_repl_names(Node* node,
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
    if (auto* def = dynamic_cast<DefStmtNode*>(node)) {
        defined.insert(def->name);
        for (auto& p : def->parameters)
            for (auto& kv : p.parameter)
                collect_repl_names(nullptr, defined, used);
        for (auto& s : def->body)
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
            if (a) collect_repl_names(a.get(), defined, used);
        return;
    }
    if (auto* bin = dynamic_cast<BinaryExprNode*>(node)) {
        if (bin->left) collect_repl_names(bin->left.get(), defined, used);
        if (bin->right) collect_repl_names(bin->right.get(), defined, used);
        return;
    }
    if (auto* acc = dynamic_cast<AccessExprNode*>(node)) {
        if (acc->expr) collect_repl_names(acc->expr.get(), defined, used);
        if (acc->index) collect_repl_names(acc->index.get(), defined, used);
        return;
    }
    if (auto* mem = dynamic_cast<MemberExprNode*>(node)) {
        if (mem->object) collect_repl_names(mem->object.get(), defined, used);
        if (mem->property) collect_repl_names(mem->property.get(), defined, used);
        return;
    }
    if (auto* cond = dynamic_cast<ConditionalExprNode*>(node)) {
        if (cond->condition) collect_repl_names(cond->condition.get(), defined, used);
        if (cond->true_expr) collect_repl_names(cond->true_expr.get(), defined, used);
        if (cond->false_expr) collect_repl_names(cond->false_expr.get(), defined, used);
        return;
    }
    if (auto* un = dynamic_cast<UnaryMinusExprNode*>(node)) {
        if (un->operand) collect_repl_names(un->operand.get(), defined, used);
        return;
    }
    if (auto* notn = dynamic_cast<LogicalNotExprNode*>(node)) {
        if (notn->operand) collect_repl_names(notn->operand.get(), defined, used);
        return;
    }
    if (auto* arr = dynamic_cast<ArrayExprNode*>(node)) {
        for (auto& e : arr->elements)
            if (e) collect_repl_names(e.get(), defined, used);
        return;
    }
    if (auto* tup = dynamic_cast<TupleExprNode*>(node)) {
        for (auto& e : tup->elements)
            if (e) collect_repl_names(e.get(), defined, used);
        return;
    }
    if (auto* vec = dynamic_cast<VectorExprNode*>(node)) {
        for (auto& e : vec->elements)
            if (e) collect_repl_names(e.get(), defined, used);
        return;
    }
    if (auto* rng = dynamic_cast<RangeExprNode*>(node)) {
        if (rng->start) collect_repl_names(rng->start.get(), defined, used);
        if (rng->end) collect_repl_names(rng->end.get(), defined, used);
        return;
    }
    if (auto* inc = dynamic_cast<IncrementExprNode*>(node)) {
        if (inc->operand) collect_repl_names(inc->operand.get(), defined, used);
        return;
    }
    if (auto* dec = dynamic_cast<DecrementExprNode*>(node)) {
        if (dec->operand) collect_repl_names(dec->operand.get(), defined, used);
        return;
    }
    if (auto* posti = dynamic_cast<PostIncrementExprNode*>(node)) {
        if (posti->operand) collect_repl_names(posti->operand.get(), defined, used);
        return;
    }
    if (auto* postd = dynamic_cast<PostDecrementExprNode*>(node)) {
        if (postd->operand) collect_repl_names(postd->operand.get(), defined, used);
        return;
    }
}

} // anonymous namespace

bool REPL::compile_and_execute(const std::string& input) {
    try {
        Lexer lexer(input, "repl_line_001");
        auto tokens = lexer.tokenize();
        if (tokens.empty()) return true;

        Parser parser;
        auto ast = parser.produce_ast(tokens);
        if (parser.has_error()) { handle_error("Syntax error in input"); return false; }

        bool single_write_call = false;
        bool last_stmt_no_result = false;
        bool single_declaration_no_print = false;
        bool single_identifier_expr = false;
        if (auto* prog = dynamic_cast<Program*>(ast.get())) {
            const auto& stmts = prog->get_statements();
            if (stmts.size() == 1) {
                Node* first = stmts[0].get();
                if (dynamic_cast<DefStmtNode*>(first)) single_declaration_no_print = true;
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
                if (dynamic_cast<ForStmtNode*>(last) || dynamic_cast<WhileStmtNode*>(last) ||
                    dynamic_cast<LoopStmtNode*>(last) || dynamic_cast<MatchStmtNode*>(last)) {
                    last_stmt_no_result = true;
                }
            }
        }

        state->checker->set_source_file("repl_line_001");
        auto& type = state->checker->check_node(ast.get());
        if (state->checker->err) { handle_error("Type error in input"); return false; }

        std::unordered_set<std::string> defined_this_line, used_this_line;
        collect_repl_names(ast.get(), defined_this_line, used_this_line);

        auto temp_module = std::make_unique<llvm::Module>("repl_expr", *state->llvm_context);
        auto temp_builder = std::make_unique<llvm::IRBuilder<llvm::NoFolder>>(*state->llvm_context);

        IRGenerationContext context(*state->llvm_context, *temp_module, *temp_builder, state->checker.get());

        auto& C = *state->llvm_context;
        auto* ValueTy = llvm::StructType::getTypeByName(C, "nv.rt.Value");
        if (!ValueTy) {
            auto* i32 = llvm::Type::getInt32Ty(C);
            auto* i64 = llvm::Type::getInt64Ty(C);
            auto* i8  = llvm::Type::getInt8Ty(C);
            auto* i8p = llvm::PointerType::getUnqual(i8);
            ValueTy = llvm::StructType::create(C, {i32, i64, i8p, i8p, i32}, "nv.rt.Value");
        }
        auto* VoidTy  = llvm::Type::getVoidTy(C);
        auto* ValuePtr= llvm::PointerType::getUnqual(ValueTy);

        temp_module->getOrInsertFunction("nv_write", llvm::FunctionType::get(VoidTy, {ValuePtr}, false));
        temp_module->getOrInsertFunction("create_str", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(C))}, false));
        temp_module->getOrInsertFunction("create_int", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::Type::getInt32Ty(C)}, false));
        temp_module->getOrInsertFunction("create_float", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::Type::getDoubleTy(C)}, false));
        temp_module->getOrInsertFunction("create_bool", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::Type::getInt32Ty(C)}, false));

        std::vector<std::string> slot_names;
        for (const std::string& name : defined_this_line) {
            std::shared_ptr<nv::Type> nv_type;
            try { nv_type = state->checker->scope->get_key(name); } catch (...) { continue; }
            if (!nv_type) continue;
            nv_type = context.resolve_type(nv_type);
            if (nv_type->kind == nv::Kind::DEF) continue;
            slot_names.push_back(name);
            state->repl_var_values[name] = Value{};
        }
        for (const std::string& name : used_this_line) {
            if (defined_this_line.count(name)) continue;
            if (!state->repl_global_names.count(name)) continue;
            std::shared_ptr<nv::Type> nv_type;
            try { nv_type = state->checker->scope->get_key(name); } catch (...) { continue; }
            if (!nv_type) continue;
            nv_type = context.resolve_type(nv_type);
            if (nv_type->kind == nv::Kind::DEF) {
                auto* def = std::static_pointer_cast<nv::Def>(nv_type).get();
                if (!def) continue;
                std::vector<llvm::Type*> param_tys;
                for (const auto& p : def->paramstype)
                    param_tys.push_back(context.nv_type_to_llvm(p));
                llvm::Type* ret_ty = context.nv_type_to_llvm(def->returntype);
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
        for (const std::string& name : state->repl_global_names) {
            if (std::find(slot_names.begin(), slot_names.end(), name) != slot_names.end()) continue;
            std::shared_ptr<nv::Type> nv_type;
            try { nv_type = state->checker->scope->get_key(name); } catch (...) { continue; }
            if (!nv_type) continue;
            nv_type = context.resolve_type(nv_type);
            if (nv_type->kind == nv::Kind::DEF) continue;
            slot_names.push_back(name);
            if (state->repl_var_values.find(name) == state->repl_var_values.end())
                state->repl_var_values[name] = Value{};
        }

        for (const std::string& name : state->repl_global_names) {
            std::shared_ptr<nv::Type> nv_type;
            try { nv_type = state->checker->scope->get_key(name); } catch (...) { continue; }
            if (!nv_type) continue;
            nv_type = context.resolve_type(nv_type);
            if (nv_type->kind != nv::Kind::DEF) continue;
            auto* def = std::static_pointer_cast<nv::Def>(nv_type).get();
            if (!def) continue;
            std::vector<llvm::Type*> param_tys;
            for (const auto& p : def->paramstype)
                param_tys.push_back(context.nv_type_to_llvm(p));
            llvm::Type* ret_ty = context.nv_type_to_llvm(def->returntype);
            auto* ft = llvm::FunctionType::get(ret_ty, param_tys, false);
            auto ext_fn = temp_module->getOrInsertFunction(name, ft);
            llvm::Function* fn = llvm::cast<llvm::Function>(ext_fn.getCallee());
            nv::SymbolInfo info(fn, fn->getType(), nv_type, false, true);
            context.get_symbol_table().define_symbol(name, info);
        }

        auto* I32 = llvm::Type::getInt32Ty(C);
        auto* F64 = llvm::Type::getDoubleTy(C);
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

        Program* prog_ptr = dynamic_cast<Program*>(ast.get());
        if (prog_ptr && prog_ptr->get_statements().size() == 1) {
            ForStmtNode* for_node = dynamic_cast<ForStmtNode*>(prog_ptr->get_statements()[0].get());
            if (for_node && for_node->iterable && !for_node->range_start && !for_node->range_end
                && for_node->body.size() == 1 && for_node->else_block.empty()
                && for_node->bindings.size() == 1) {
                NumericLiteralNode* lit = dynamic_cast<NumericLiteralNode*>(for_node->iterable.get());
                IdentifierNode* bid = dynamic_cast<IdentifierNode*>(for_node->bindings[0].get());
                if (lit && bid) {
                    int N = 0;
                    try { N = std::stoi(lit->value); } catch (...) {}
                    if (N > 0 && N <= 100000) {
                        std::string loop_var = bid->symbol;
                        std::unique_ptr<Program> body_prog = std::make_unique<Program>();
                        body_prog->add_statement(std::unique_ptr<Stmt>(static_cast<Stmt*>(for_node->body[0]->clone())));
                        static int loop_counter = 0;
                        std::string loop_func_name = "__repl_loop_" + std::to_string(loop_counter++);
                        llvm::Type* slots_ty = llvm::PointerType::getUnqual(ValuePtr);
                        std::vector<llvm::Type*> loop_param_tys = { ValuePtr };
                        if (!slot_names.empty()) loop_param_tys.push_back(slots_ty);
                        loop_param_tys.push_back(llvm::Type::getInt32Ty(C));
                        auto* loop_func_type = llvm::FunctionType::get(VoidTy, loop_param_tys, false);
                        llvm::Function* loop_func = llvm::Function::Create(loop_func_type, llvm::Function::ExternalLinkage, loop_func_name, temp_module.get());
                        loop_func->getArg(0)->setName("out_result");
                        llvm::Value* loop_out = loop_func->getArg(0);
                        llvm::Value* loop_slots = slot_names.empty() ? nullptr : loop_func->getArg(1);
                        llvm::Value* loop_i_param = loop_func->getArg(slot_names.empty() ? 1 : 2);
                        auto* loop_entry = llvm::BasicBlock::Create(C, "entry", loop_func);
                        temp_builder->SetInsertPoint(loop_entry);
                        context.set_current_function(loop_func);
                        context.set_repl_loop_mode(true);
                        if (loop_slots) {
                            for (size_t i = 0; i < slot_names.size(); ++i) {
                                const std::string& name = slot_names[i];
                                llvm::Value* slot_addr = temp_builder->CreateGEP(ValuePtr, loop_slots, llvm::ConstantInt::get(I32, i));
                                llvm::Value* slot = temp_builder->CreateLoad(ValuePtr, slot_addr);
                                llvm::GlobalVariable* gv = llvm::cast<llvm::GlobalVariable>(temp_module->getOrInsertGlobal("repl_global_" + name, ValueTy));
                                if (gv->isDeclaration()) gv->setLinkage(llvm::GlobalValue::ExternalLinkage);
                                temp_builder->CreateStore(temp_builder->CreateLoad(ValueTy, slot), gv);
                                std::shared_ptr<nv::Type> nv_type;
                                try { nv_type = state->checker->scope->get_key(name); } catch (...) { continue; }
                                if (nv_type) nv_type = context.resolve_type(nv_type);
                                context.get_symbol_table().define_symbol(name, nv::SymbolInfo(temp_builder->CreatePointerCast(gv, ValuePtr), ValueTy, nv_type, false, false));
                            }
                        }
                        llvm::Value* i_alloca = context.create_and_register_variable(loop_var, llvm::Type::getInt32Ty(C), nullptr, false);
                        temp_builder->CreateStore(loop_i_param, i_alloca);
                        context.set_repl_out_result_ptr(loop_out);
                        context.enter_scope();
                        if (body_prog) body_prog->print();
                        nv::generate_ir(std::move(body_prog), context);
                        bool loop_have_result = false;
                        if (context.has_value()) {
                            if (last_stmt_no_result) {
                                (void)context.pop_value();
                            } else {
                                llvm::Value* result = context.pop_value();
                                if (result) {
                                    if (result->getType() == ValueTy) {
                                        llvm::Value* to_store = result;
                                        if (llvm::isa<llvm::AllocaInst>(result)) {
                                            to_store = temp_builder->CreateLoad(ValueTy, result, "loop_result_load");
                                        }
                                        temp_builder->CreateStore(to_store, loop_out);
                                        loop_have_result = true;
                                    } else {
                                        auto* box = temp_builder->CreateAlloca(ValueTy, nullptr, "loop_box");
                                        auto* create_int_fn = llvm::cast<llvm::Function>(temp_module->getOrInsertFunction("create_int", llvm::FunctionType::get(VoidTy, {ValuePtr, I32}, false)).getCallee());
                                        auto* create_float_fn = llvm::cast<llvm::Function>(temp_module->getOrInsertFunction("create_float", llvm::FunctionType::get(VoidTy, {ValuePtr, F64}, false)).getCallee());
                                        auto* create_bool_fn = llvm::cast<llvm::Function>(temp_module->getOrInsertFunction("create_bool", llvm::FunctionType::get(VoidTy, {ValuePtr, I32}, false)).getCallee());
                                        bool boxed_ok = false;
                                        if (result->getType()->isIntegerTy(1)) {
                                            temp_builder->CreateCall(create_bool_fn, {box, temp_builder->CreateZExt(result, I32)});
                                            boxed_ok = true;
                                        } else if (result->getType()->isIntegerTy(32)) {
                                            temp_builder->CreateCall(create_int_fn, {box, result});
                                            boxed_ok = true;
                                        } else if (result->getType()->isIntegerTy(64)) {
                                            temp_builder->CreateCall(create_int_fn, {box, temp_builder->CreateTrunc(result, I32)});
                                            boxed_ok = true;
                                        } else if (result->getType()->isFloatingPointTy()) {
                                            llvm::Value* f = result->getType() == F64 ? result : temp_builder->CreateFPExt(result, F64);
                                            temp_builder->CreateCall(create_float_fn, {box, f});
                                            boxed_ok = true;
                                        }
                                        if (boxed_ok) {
                                            llvm::Value* boxed = temp_builder->CreateLoad(ValueTy, box, "loop_boxed");
                                            temp_builder->CreateStore(boxed, loop_out);
                                            loop_have_result = true;
                                        }
                                    }
                                }
                            }
                        }
                        context.exit_scope();
                        context.set_repl_loop_mode(false);
                        context.set_repl_out_result_ptr(nullptr);
                        if (loop_slots) {
                            for (size_t i = 0; i < slot_names.size(); ++i) {
                                const std::string& name = slot_names[i];
                                llvm::GlobalVariable* gv = temp_module->getGlobalVariable("repl_global_" + name);
                                if (!gv) continue;
                                llvm::Value* slot_addr = temp_builder->CreateGEP(ValuePtr, loop_slots, llvm::ConstantInt::get(I32, i));
                                temp_builder->CreateStore(temp_builder->CreateLoad(ValueTy, gv), temp_builder->CreateLoad(ValuePtr, slot_addr));
                            }
                        }
                        temp_builder->CreateRetVoid();
                        temp_module->print(llvm::errs(), nullptr);
                        auto tsm_loop = llvm::orc::ThreadSafeModule(std::move(temp_module), std::make_unique<llvm::LLVMContext>());
                        if (auto err = state->jit->addIRModule(std::move(tsm_loop))) {
                            llvm::consumeError(std::move(err));
                            handle_error("JIT compilation failed (host-loop)");
                            return false;
                        }
                        if (auto sym = state->jit->lookup(loop_func_name)) {
                            void* addr = (void*)sym->getValue();
                            Value result_buffer = {};
                            (void)N;
                            for (int32_t i = 0; i < N; ++i) {
                                result_buffer = {};
                                if (slot_names.empty())
                                    ((void(*)(Value*, int32_t))addr)(&result_buffer, i);
                                else {
                                    std::vector<Value*> slot_ptrs;
                                    for (const auto& n : slot_names) slot_ptrs.push_back(&state->repl_var_values[n]);
                                    ((void(*)(Value*, Value**, int32_t))addr)(&result_buffer, slot_ptrs.data(), i);
                                }
                                if (result_buffer.type != 0) {
                                    print_value(llvm::JITTargetAddress(&result_buffer));
                                }
                                std::cout.flush();
                                std::fflush(stdout);
                            }
                            std::cout.flush();
                            std::cerr.flush();
                            std::fflush(nullptr);
                            for (const std::string& n : defined_this_line) state->repl_global_names.insert(n);
                            return true;
                        }
                        handle_error("Failed to find compiled loop function");
                        return false;
                    }
                }
            }
        }

        static int expr_counter = 0;
        std::string func_name = "__repl_expr_" + std::to_string(expr_counter++);

        llvm::Type* slots_ty = llvm::PointerType::getUnqual(ValuePtr);
        std::vector<llvm::Type*> param_tys = { ValuePtr };
        if (!slot_names.empty()) param_tys.push_back(slots_ty);
        auto* func_type_w = llvm::FunctionType::get(VoidTy, param_tys, false);
        auto* func = llvm::Function::Create(func_type_w, llvm::Function::ExternalLinkage, func_name, temp_module.get());
        func->getArg(0)->setName("out_result");
        llvm::Value* out_param = func->getArg(0);
        llvm::Value* slots_param = slot_names.empty() ? nullptr : func->getArg(1);

        auto* entry_bb = llvm::BasicBlock::Create(C, "entry", func);
        temp_builder->SetInsertPoint(entry_bb);
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
            if (last_stmt_no_result) {
                (void)context.pop_value();
            } else {
                llvm::Value* result = context.pop_value();
                if (!result) { }
                else if (result->getType() == ValueTy) {
                    llvm::Value* to_store = result;
                    if (llvm::isa<llvm::AllocaInst>(result)) {
                        to_store = temp_builder->CreateLoad(ValueTy, result, "repl_result_load");
                    }
                    temp_builder->CreateStore(to_store, out_param);
                    have_result = true;
                } else {
                    auto* I32 = llvm::Type::getInt32Ty(C);
                    auto* F64 = llvm::Type::getDoubleTy(C);
                    auto* box = temp_builder->CreateAlloca(ValueTy, nullptr, "repl_box");
                    auto* create_int_fn = llvm::cast<llvm::Function>(temp_module->getOrInsertFunction("create_int", llvm::FunctionType::get(VoidTy, {ValuePtr, I32}, false)).getCallee());
                    auto* create_float_fn = llvm::cast<llvm::Function>(temp_module->getOrInsertFunction("create_float", llvm::FunctionType::get(VoidTy, {ValuePtr, F64}, false)).getCallee());
                    auto* create_bool_fn = llvm::cast<llvm::Function>(temp_module->getOrInsertFunction("create_bool", llvm::FunctionType::get(VoidTy, {ValuePtr, I32}, false)).getCallee());
                    bool boxed_ok = false;
                    if (result->getType()->isIntegerTy(1)) {
                        temp_builder->CreateCall(create_bool_fn, {box, temp_builder->CreateZExt(result, I32)});
                        boxed_ok = true;
                    } else if (result->getType()->isIntegerTy(32)) {
                        temp_builder->CreateCall(create_int_fn, {box, result});
                        boxed_ok = true;
                    } else if (result->getType()->isIntegerTy(64)) {
                        temp_builder->CreateCall(create_int_fn, {box, temp_builder->CreateTrunc(result, I32)});
                        boxed_ok = true;
                    } else if (result->getType()->isFloatingPointTy()) {
                        llvm::Value* f = result->getType() == F64 ? result : temp_builder->CreateFPExt(result, F64);
                        temp_builder->CreateCall(create_float_fn, {box, f});
                        boxed_ok = true;
                    }
                    if (boxed_ok) {
                        llvm::Value* boxed = temp_builder->CreateLoad(ValueTy, box, "repl_boxed");
                        temp_builder->CreateStore(boxed, out_param);
                        have_result = true;
                    }
                }
            }
        }
        temp_builder->CreateRetVoid();

        auto tsm = llvm::orc::ThreadSafeModule(std::move(temp_module), std::make_unique<llvm::LLVMContext>());
        if (auto err = state->jit->addIRModule(std::move(tsm))) {
            llvm::errs() << "JIT compilation failed: " << err << "\n";
            llvm::consumeError(std::move(err));
            handle_error("JIT compilation failed");
            return false;
        }

        Value result_buffer = {};
        if (auto func_symbol = state->jit->lookup(func_name)) {
            void* addr = (void*)func_symbol->getValue();
            if (slot_names.empty()) {
                ((void(*)(Value*))addr)(&result_buffer);
            } else {
                std::vector<Value*> slot_ptrs;
                for (const auto& n : slot_names) slot_ptrs.push_back(&state->repl_var_values[n]);
                ((void(*)(Value*, Value**))addr)(&result_buffer, slot_ptrs.data());
            }
        } else {
            handle_error("Failed to find compiled function");
            return false;
        }

        std::cout.flush();
        std::cerr.flush();

        if (have_result && !single_write_call && !single_declaration_no_print) {
            auto sym = state->get_symbol("nv_write");
            if (sym) {
                if (defined_this_line.size() == 1) {
                    const std::string& n = *defined_this_line.begin();
                    if (std::find(slot_names.begin(), slot_names.end(), n) != slot_names.end()) {
                        auto it = state->repl_var_values.find(n);
                        if (it != state->repl_var_values.end()) result_buffer = it->second;
                    }
                } else if (single_identifier_expr && defined_this_line.empty() && used_this_line.size() == 1) {
                    const std::string& n = *used_this_line.begin();
                    if (std::find(slot_names.begin(), slot_names.end(), n) != slot_names.end()) {
                        auto it = state->repl_var_values.find(n);
                        if (it != state->repl_var_values.end()) result_buffer = it->second;
                    }
                }
                print_value(llvm::JITTargetAddress(&result_buffer));
                if (defined_this_line.size() == 1) {
                    const std::string& n = *defined_this_line.begin();
                    if (std::find(slot_names.begin(), slot_names.end(), n) != slot_names.end())
                        state->repl_var_values[n] = result_buffer;
                }
            } else {
                std::cout << "(value captured, nv_write not available)" << std::endl;
            }
        }

        for (const std::string& n : defined_this_line) state->repl_global_names.insert(n);
        return true;
    } catch (const std::exception& e) {
        handle_error("Compilation error: " + std::string(e.what()));
        return false;
    }
}

void REPL::print_value(llvm::JITTargetAddress addr) {
    auto sym = state->get_symbol("nv_write");
    if (sym) {
        using NvWriteFn = void(*)(Value*);
        (*reinterpret_cast<NvWriteFn>(*sym))(reinterpret_cast<Value*>(addr));
    }
}

bool REPL::execute_command(REPLCommand cmd, const std::vector<std::string>& args) {
    switch (cmd) {
        case REPLCommand::HELP:
            show_help();
            return true;
        case REPLCommand::QUIT:
        case REPLCommand::EXIT:
            return false;
        case REPLCommand::RESET:
            reset_state();
            return true;
        case REPLCommand::VARS:
            show_variables();
            return true;
        case REPLCommand::HISTORY:
            show_history();
            return true;
        case REPLCommand::LOAD:
            if (args.empty()) { handle_error("Usage: :load <filename>"); return true; }
            return load_file(args[0]);
        case REPLCommand::SAVE:
            if (args.empty()) { handle_error("Usage: :save <filename>"); return true; }
            return save_history(args[0]);
        default:
            return true;
    }
}

void REPL::show_help() {
    std::cout << "\nNarval REPL Commands:\n";
    std::cout << "  :help, :h          Show this help message\n";
    std::cout << "  :quit, :exit, :q  Exit REPL\n";
    std::cout << "  :reset, :clear     Reset REPL context\n";
    std::cout << "  :vars, :v          Show defined variables\n";
    std::cout << "  :history, :hist    Show command history\n";
    std::cout << "  :load <file>       Load and execute file\n";
    std::cout << "  :save <file>       Save command history to file\n";
    std::cout << "\nFeatures:\n";
    std::cout << "  - Auto-print of expression results\n";
    std::cout << "  - Persistent variable state\n";
    std::cout << "  - Multiline input support\n";
    std::cout << "  - JIT compilation with LLVM\n";
    std::cout << "  - Type checking and error reporting\n";
    std::cout << std::endl;
}

void REPL::show_variables() {
    std::cout << "\nDefined variables:\n";
    for (const auto& [name, addr] : state->symbols) {
        std::cout << "  " << name << " = " << repl_utils::format_value(addr) << std::endl;
    }
    std::cout << std::endl;
}

void REPL::show_history() {
    std::cout << "\nCommand history:\n";
    for (size_t i = 0; i < state->history.size(); ++i) {
        std::cout << "  " << (i + 1) << ": " << state->history[i] << std::endl;
    }
    std::cout << std::endl;
}

bool REPL::load_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) { handle_error("Cannot open file: " + filename); return true; }
    std::string line;
    int line_num = 0;
    while (std::getline(file, line)) {
        line_num++;
        if (!line.empty()) {
            std::cout << "Loading line " << line_num << ": " << line << std::endl;
            if (!compile_and_execute(line)) {
                handle_error("Error executing line " + std::to_string(line_num));
            }
        }
    }
    return true;
}

bool REPL::save_history(const std::string& filename) {
    std::ofstream file(filename);
    if (!file) { handle_error("Cannot create file: " + filename); return true; }
    for (const auto& cmd : state->history) file << cmd << std::endl;
    std::cout << "History saved to " << filename << std::endl;
    return true;
}

void REPL::reset_state() {
    state->reset();
    std::cout << "REPL state reset" << std::endl;
}

void REPL::handle_error(const std::string& error) {
    if (config.show_errors) std::cerr << "Error: " << error << std::endl;
}

void REPL::handle_warning(const std::string& warning) {
    if (config.show_warnings) std::cout << "Warning: " << warning << std::endl;
}

void REPL::add_to_history(const std::string& command) {
    state->history.push_back(command);
    if (state->history.size() > static_cast<size_t>(config.max_history)) state->history.erase(state->history.begin());
}

void REPL::run() {
    while (true) {
        try {
            std::string line = read_input();
            if (line.empty() && std::cin.eof()) { std::cout << std::endl; break; }
            if (line.empty()) continue;
            if (line[0] == ':') {
                auto [cmd, args] = CommandParser::parse(line);
                if (cmd != REPLCommand::NONE) {
                    if (!execute_command(cmd, args)) break;
                    continue;
                }
            }
            if (in_multiline) current_input += "\n" + line; else current_input = line;
            if (!is_complete_expression(current_input)) { in_multiline = true; continue; }
            in_multiline = false;
            add_to_history(current_input);
            std::string processed = preprocess_input(current_input);
            compile_and_execute(processed);
            current_input.clear();
        } catch (const std::exception& e) {
            handle_error(std::string("REPL error: ") + e.what());
            in_multiline = false;
            current_input.clear();
        }
    }
}

void REPL::stop() { }

} // namespace nv
