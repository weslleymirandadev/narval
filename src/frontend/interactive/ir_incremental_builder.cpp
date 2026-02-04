#include "frontend/interactive/ir_incremental_builder.hpp"

#include <queue>

#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>

#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>

class Program;

namespace nv {
class Checker;
}

namespace llvm {
class Module;
}

namespace llvm::orc {
class ThreadSafeContext;
}

namespace narval::frontend::interactive {

// IR Incremental Builder
//
// Model:
// - Each interactive execution produces an IR "fragment" (one LLVM module with
//   a single entry function).
// - All fragments are added to the same JITDylib, so LLVM symbols are resolved
//   incrementally through normal external linkage.
// - We track fragment dependencies at the *symbol interface* level (defined vs.
//   used symbols). This allows us to invalidate dependents transitively when a
//   fragment is rebuilt.
//
// Design constraints:
// - This component is independent from SessionManager.
// - It exposes clear integration points with the JIT by returning a module
//   + entry function name, and by providing invalidation sets that the JIT layer
//   can unload.

IrIncrementalBuilder::IrIncrementalBuilder() = default;

void IrIncrementalBuilder::reset() {
    fragments_.clear();
    deps_.clear();
    rdeps_.clear();
    symbol_producer_fragment_.clear();
}

bool IrIncrementalBuilder::last_stmt_is_write_call(const Program& program) {
    if (program.body.empty()) return false;
    const auto* last = program.body.back().get();
    if (!last || last->kind != NodeType::CallExpression) return false;
    const auto* call = dynamic_cast<const CallExprNode*>(last);
    if (!call) return false;
    const auto* id = dynamic_cast<const IdentifierNode*>(call->caller.get());
    return id && id->symbol == "write";
}

bool IrIncrementalBuilder::last_stmt_can_autoprint(const Program& program) {
    if (program.body.empty()) return false;
    const auto* last = program.body.back().get();
    if (!last) return false;
    if (last->kind == NodeType::CallExpression) {
        return !last_stmt_is_write_call(program);
    }

    switch (last->kind) {
        case NodeType::Identifier:
        case NodeType::NumericLiteral:
        case NodeType::StringLiteral:
        case NodeType::BooleanLiteral:
        case NodeType::BinaryExpression:
        case NodeType::AssignmentExpression:
        case NodeType::DeclarationStatement:
        case NodeType::AccessExpression:
        case NodeType::MemberExpression:
        case NodeType::ConditionalExpression:
        case NodeType::TupleExpression:
        case NodeType::ArrayExpression:
        case NodeType::VectorExpression:
        case NodeType::Map:
        case NodeType::RangeExpression:
            return true;
        default:
            return false;
    }
}

IrBuildResult IrIncrementalBuilder::build_fragment(
    Program& program,
    nv::Checker& checker,
    const std::string& fragment_id,
    const std::string& unit_name,
    const IrBuildOptions& options
) {
    IrBuildResult out;

    const bool do_autoprint = options.auto_print_last_expr && last_stmt_can_autoprint(program);

    auto tsc = std::make_unique<llvm::orc::ThreadSafeContext>(std::make_unique<llvm::LLVMContext>());
    auto& llvm_ctx = *tsc->getContext();

    auto module = std::make_unique<llvm::Module>(unit_name, llvm_ctx);
    llvm::IRBuilder<llvm::NoFolder> builder(llvm_ctx);

    nv::IRGenerationContext ir_ctx(llvm_ctx, *module, builder, &checker);

    // 0) For now, skip registering external symbols to avoid cross-module issues
    // TODO: Implement proper cross-module symbol resolution in a future iteration
    // This approach avoids segfaults but limits cross-fragment variable usage

    // 1) Module-scope codegen for globals.
    ir_ctx.set_current_function(nullptr);
    for (auto& stmt : program.body) {
        if (!stmt) continue;
        if (stmt->kind == NodeType::DeclarationStatement || 
            stmt->kind == NodeType::DefStatement || 
            stmt->kind == NodeType::ImportStatement ||
            stmt->kind == NodeType::AssignmentExpression) {
            stmt->codegen(ir_ctx);
        }
    }

    // 2) Executable part inside entry function.
    auto* fn_ty = llvm::FunctionType::get(llvm::Type::getVoidTy(llvm_ctx), false);
    const std::string fn_name = "nv.interactive.unit." + unit_name;
    auto* fn = llvm::Function::Create(fn_ty, llvm::Function::ExternalLinkage, fn_name, *module);
    auto* bb = llvm::BasicBlock::Create(llvm_ctx, "entry", fn);

    builder.SetInsertPoint(bb);
    ir_ctx.set_current_function(fn);

    // IMPORTANT: Copy global variables from module scope to function scope
    // This ensures that variables declared in module scope are available in function scope
    for (auto& stmt : program.body) {
        if (!stmt) continue;
        if (stmt->kind == NodeType::DeclarationStatement || 
            stmt->kind == NodeType::AssignmentExpression) {
            // Re-process declarations in function scope to ensure they're available
            stmt->codegen(ir_ctx);
        }
    }

    for (auto& stmt : program.body) {
        if (!stmt) continue;
        if (stmt->kind == NodeType::DefStatement || 
            stmt->kind == NodeType::ImportStatement) {
            continue;
        }
        stmt->codegen(ir_ctx);
    }

    if (do_autoprint && ir_ctx.has_value()) {
        auto* v = ir_ctx.pop_value();
        if (v) {
            auto* ValueTy = nv::ir_utils::get_value_struct(ir_ctx);
            auto* ValuePtr = nv::ir_utils::get_value_ptr(ir_ctx);
            
            // Para auto-print, queremos sempre mostrar o valor de forma simples
            auto* value_alloca = ir_ctx.create_alloca(ValueTy, "autoprint_value");
            auto& B = ir_ctx.get_builder();
            auto& C = ir_ctx.get_context();
            auto* I32 = llvm::Type::getInt32Ty(C);
            auto* F64 = llvm::Type::getDoubleTy(C);
            auto* I8Ptr = llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(C));
            
            // Inicializar com zero para garantir valor limpo
            B.CreateStore(llvm::Constant::getNullValue(ValueTy), value_alloca);
            
            if (v->getType() == ValueTy) {
                // Já é um Value struct - usar diretamente
                B.CreateStore(v, value_alloca);
            } else if (v->getType()->isIntegerTy(1)) {
                // Boolean
                auto* create_fn = ir_ctx.ensure_runtime_func("create_bool", {ValuePtr, I32});
                B.CreateCall(create_fn, {value_alloca, B.CreateZExt(v, I32)});
            } else if (v->getType()->isIntegerTy()) {
                // Inteiro
                auto* create_fn = ir_ctx.ensure_runtime_func("create_int", {ValuePtr, I32});
                llvm::Value* iv = v->getType()->isIntegerTy(32) ? v : B.CreateSExtOrTrunc(v, I32);
                B.CreateCall(create_fn, {value_alloca, iv});
            } else if (v->getType()->isFloatingPointTy()) {
                // Float
                auto* create_fn = ir_ctx.ensure_runtime_func("create_float", {ValuePtr, F64});
                llvm::Value* fp = v->getType() == F64 ? v : B.CreateFPExt(v, F64);
                B.CreateCall(create_fn, {value_alloca, fp});
            } else if (v->getType()->isPointerTy()) {
                // Possível string
                std::vector<llvm::Type*> str_params = {ValuePtr, I8Ptr};
                auto* create_fn = ir_ctx.ensure_runtime_func("create_str", str_params);
                B.CreateCall(create_fn, {value_alloca, v});
            } else {
                // Tipo desconhecido - tentar converter para int como fallback
                auto* create_fn = ir_ctx.ensure_runtime_func("create_int", {ValuePtr, I32});
                llvm::Value* iv = v->getType()->isIntegerTy(32) ? v : B.CreateSExtOrTrunc(v, I32);
                B.CreateCall(create_fn, {value_alloca, iv});
            }

            // Chamar nv_write com o Value*
            auto* write_fn = ir_ctx.ensure_runtime_func("nv_write", {ValuePtr});
            builder.CreateCall(write_fn, {value_alloca});
        }
    }

    builder.CreateRetVoid();

    // Move module to result AFTER all processing
    if (llvm::verifyModule(*module, &llvm::errs())) {
        out.ok = false;
        out.error = "invalid llvm module";
        return out;
    }

    out.ok = true;
    out.entry_function = fn_name;
    out.module = std::move(module);
    out.tsc = std::move(tsc);

    // Register/overwrite fragment metadata (interface committed separately).
    IrFragment& frag = fragments_[fragment_id];
    frag.id = fragment_id;
    frag.active = true;
    frag.unit_name = unit_name;
    frag.entry_function = fn_name;
    return out;
}

IrRebuildResult IrIncrementalBuilder::rebuild_fragment(
    Program& program,
    nv::Checker& checker,
    const std::string& fragment_id,
    const std::string& unit_name,
    const IrBuildOptions& options
) {
    IrRebuildResult out;
    out.invalidation = invalidate_fragment(fragment_id);
    out.build = build_fragment(program, checker, fragment_id, unit_name, options);
    return out;
}

void IrIncrementalBuilder::set_fragment_dependencies(const std::string& fragment_id, const std::unordered_set<std::string>& deps) {
    // Remove old reverse edges.
    if (auto it = deps_.find(fragment_id); it != deps_.end()) {
        for (const auto& old : it->second) {
            auto rit = rdeps_.find(old);
            if (rit != rdeps_.end()) {
                rit->second.erase(fragment_id);
                if (rit->second.empty()) rdeps_.erase(rit);
            }
        }
    }

    deps_[fragment_id] = deps;
    for (const auto& d : deps) {
        rdeps_[d].insert(fragment_id);
    }
}

std::vector<std::string> IrIncrementalBuilder::bfs_collect_dependents(const std::string& fragment_id) const {
    std::vector<std::string> out;
    std::queue<std::string> q;
    std::unordered_set<std::string> visited;

    if (auto it = rdeps_.find(fragment_id); it != rdeps_.end()) {
        for (const auto& dep : it->second) {
            q.push(dep);
            visited.insert(dep);
        }
    }

    while (!q.empty()) {
        auto cur = q.front();
        q.pop();
        out.push_back(cur);

        if (auto it = rdeps_.find(cur); it != rdeps_.end()) {
            for (const auto& nxt : it->second) {
                if (visited.insert(nxt).second) {
                    q.push(nxt);
                }
            }
        }
    }

    return out;
}

void IrIncrementalBuilder::commit_fragment_interface(
    const std::string& fragment_id,
    const std::unordered_set<std::string>& defined_symbols,
    const std::unordered_set<std::string>& used_symbols
) {
    auto fit = fragments_.find(fragment_id);
    if (fit == fragments_.end()) return;

    auto& frag = fit->second;
    frag.defined_symbols = defined_symbols;
    frag.used_symbols = used_symbols;
    frag.active = true;

    // Dependencies derived from symbol producers.
    std::unordered_set<std::string> deps;
    for (const auto& used : used_symbols) {
        auto it = symbol_producer_fragment_.find(used);
        if (it != symbol_producer_fragment_.end()) {
            deps.insert(it->second);
        }
    }
    deps.erase(fragment_id);
    set_fragment_dependencies(fragment_id, deps);

    // Update symbol producers.
    for (const auto& def : defined_symbols) {
        symbol_producer_fragment_[def] = fragment_id;
    }
}

IrInvalidateResult IrIncrementalBuilder::invalidate_fragment(const std::string& fragment_id) {
    IrInvalidateResult out;

    auto base_it = fragments_.find(fragment_id);
    if (base_it == fragments_.end()) return out;

    // Invalidate base + dependents.
    out.invalidated_fragments.push_back(fragment_id);
    auto deps = bfs_collect_dependents(fragment_id);
    out.invalidated_fragments.insert(out.invalidated_fragments.end(), deps.begin(), deps.end());

    // Mark inactive and collect affected symbols.
    for (const auto& fid : out.invalidated_fragments) {
        auto it = fragments_.find(fid);
        if (it == fragments_.end()) continue;
        if (!it->second.active) continue;
        it->second.active = false;

        for (const auto& s : it->second.defined_symbols) {
            out.affected_symbols.insert(s);
        }

        // Remove producers pointing to this fragment.
        for (auto pit = symbol_producer_fragment_.begin(); pit != symbol_producer_fragment_.end();) {
            if (pit->second == fid) {
                pit = symbol_producer_fragment_.erase(pit);
            } else {
                ++pit;
            }
        }
    }

    return out;
}

bool IrIncrementalBuilder::is_fragment_active(const std::string& fragment_id) const {
    auto it = fragments_.find(fragment_id);
    if (it == fragments_.end()) return false;
    return it->second.active;
}

const IrFragment* IrIncrementalBuilder::get_fragment(const std::string& fragment_id) const {
    auto it = fragments_.find(fragment_id);
    if (it == fragments_.end()) return nullptr;
    return &it->second;
}

} // namespace narval::frontend::interactive
