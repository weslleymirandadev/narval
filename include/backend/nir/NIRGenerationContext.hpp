#pragma once

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/IR/Value.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"

#include "backend/nir/NarvalDialect.h"
#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalTypes.h"
#include "frontend/checker/type.hpp"
#include "frontend/ast/types.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nv {

struct NIRSymbolInfo {
    mlir::Value        value;    // MLIR SSA value for this symbol
    std::shared_ptr<nv::Type> nv_type; // Narval type (from checker)
    bool               is_comptime = false;
};

class NIRSymbolTable {
public:
    void push_scope();
    void pop_scope();
    void define(const std::string& name, mlir::Value val,
                std::shared_ptr<nv::Type> nv_type = nullptr,
                bool is_comptime = false);
    // Returns nullptr-value optional if not found
    std::optional<NIRSymbolInfo> lookup(const std::string& name) const;
    bool exists(const std::string& name) const;

private:
    std::vector<std::unordered_map<std::string, NIRSymbolInfo>> scopes_ = {{}};
};

// Replaces IRGenerationContext for the MLIR-based NIR pipeline.
// Wraps an mlir::OpBuilder + mlir::ModuleOp and provides helpers matching
// the API style of IRGenerationContext so codegen files can be migrated
// incrementally.

class NIRGenerationContext {
public:
    // MLIRContext and module are owned externally; the context is passed by ref.
    explicit NIRGenerationContext(mlir::MLIRContext& ctx,
                                  const std::string& source_file = "<input>");

    //  Core MLIR accessors 
    mlir::MLIRContext&            get_mlir_context()  { return ctx_; }
    mlir::OpBuilder&              get_builder()        { return builder_; }
    mlir::ModuleOp                get_module()         { return *module_; }
    mlir::OwningOpRef<mlir::ModuleOp>& get_module_ref() { return module_; }

    //  Scope management 
    void push_scope() { symbols_.push_scope(); }
    void pop_scope()  { symbols_.pop_scope(); }

    void define(const std::string& name, mlir::Value val,
                std::shared_ptr<nv::Type> nv_type = nullptr,
                bool is_comptime = false);

    mlir::Value lookup(const std::string& name);
    std::optional<NIRSymbolInfo> lookup_info(const std::string& name);

    //  Function context 
    void set_current_func(mlir::func::FuncOp fn);
    mlir::func::FuncOp get_current_func();
    bool has_current_func() const { return current_func_.has_value(); }

    void set_current_function_fallible(bool v) { current_fallible_ = v; }
    bool is_current_function_fallible() const   { return current_fallible_; }

    void set_current_function_async(bool v) { current_async_ = v; }
    bool is_current_function_async() const   { return current_async_; }

    //  Type helpers

    // The universal boxed Narval value type: !narval.value
    mlir::narval::ValueType get_narval_value_type();

    // Map a Narval checker type to an MLIR type.
    // Most types map to !narval.value; LOW_LEVEL types map to concrete LLVM types.
    mlir::Type nv_type_to_mlir(std::shared_ptr<nv::Type> nv_type);

    //  Location helper 
    mlir::Location loc(const PositionData* pos = nullptr);

    //  Checker pointer (for type resolution) 
    void  set_type_checker(void* checker) { checker_ = checker; }
    void* get_type_checker()              { return checker_; }

    //  Extra linker items (bridges, .so paths) 
    void add_extra_link_item(const std::string& item) {
        extra_link_items_.push_back(item);
    }
    const std::vector<std::string>& get_extra_link_items() const {
        return extra_link_items_;
    }

    //  Python namespace tracking (mirrors IRGenerationContext) 
    std::unordered_set<std::string> py_namespaces;
    void register_py_namespace(const std::string& alias) {
        py_namespaces.insert(alias);
    }
    bool is_py_namespace(const std::string& name) const {
        return py_namespaces.count(name) > 0;
    }

    //  Runtime function declaration 
    // Ensures a runtime C function is declared in the module (like
    // IRGenerationContext::ensure_runtime_func). Returns the FuncOp.
    mlir::func::FuncOp ensure_runtime_func(const std::string& name,
                                            mlir::FunctionType fn_type);

    //  Control-flow emit helpers (Phase 2) 

    // Emit narval.if. Caller fills then_region / else_region on the returned op.
    mlir::narval::IfOp emit_if(mlir::Location loc, mlir::Value condition,
                                mlir::TypeRange result_types = {});

    // Emit narval.yield (terminator for if / for_range / while regions).
    mlir::narval::YieldOp emit_yield(mlir::Location loc,
                                      mlir::ValueRange values = {});

    // Emit narval.for_range. Returns the op; caller populates body region.
    mlir::narval::ForRangeOp emit_for_range(mlir::Location loc,
                                             mlir::Value lb, mlir::Value ub,
                                             mlir::Value step,
                                             llvm::StringRef var_name,
                                             mlir::ValueRange init_args = {});

    // Emit narval.while. Caller fills condition_region and body_region.
    mlir::narval::WhileOp emit_while(mlir::Location loc,
                                      mlir::TypeRange result_types,
                                      mlir::ValueRange init_args);

    //  NIR dump 
    void dump_nir();
    void print_nir(llvm::raw_ostream& os);

    //  Pipeline: lower NIR → LLVM IR 
    // Runs the full pass pipeline (narval → std → llvm dialect → LLVM IR).
    // Returns ownership of the resulting llvm::Module.
    llvm::Expected<std::unique_ptr<llvm::Module>>
    lower_to_llvm_ir(llvm::LLVMContext& llvm_ctx);

private:
    mlir::MLIRContext&                   ctx_;
    mlir::OpBuilder                      builder_;
    mlir::OwningOpRef<mlir::ModuleOp>    module_;
    NIRSymbolTable                       symbols_;
    std::optional<mlir::func::FuncOp>   current_func_;
    bool                                 current_fallible_ = false;
    bool                                 current_async_    = false;
    void*                                checker_          = nullptr;
    std::string                          source_file_;
    std::vector<std::string>             extra_link_items_;
};

} // namespace nv
