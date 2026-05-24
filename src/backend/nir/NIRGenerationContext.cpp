#include "backend/nir/NIRGenerationContext.hpp"
#include "backend/nir/NarvalPasses.h"

#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVM.h"
#include "mlir/Conversion/MemRefToLLVM/MemRefToLLVM.h"
#include "mlir/Conversion/Passes.h"
#include "mlir/Conversion/ReconcileUnrealizedCasts/ReconcileUnrealizedCasts.h"
#include "mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h"
#include "mlir/Conversion/LinalgToStandard/LinalgToStandard.h"
#include "mlir/Conversion/VectorToLLVM/ConvertVectorToLLVMPass.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Linalg/Passes.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/Vector/IR/VectorOps.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Target/LLVMIR/ModuleTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Transforms/Passes.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/Support/TargetSelect.h"

// MLIR 17+ uses free-function isa<>/cast<>/dyn_cast<> instead of method form.
using mlir::isa;
using mlir::cast;
using mlir::dyn_cast;

namespace nv {

//===----------------------------------------------------------------------===//
// NIRSymbolTable
//===----------------------------------------------------------------------===//

void NIRSymbolTable::push_scope() { scopes_.emplace_back(); }
void NIRSymbolTable::pop_scope() {
    if (scopes_.size() > 1) scopes_.pop_back();
}
void NIRSymbolTable::define(const std::string& name, mlir::Value val,
                             std::shared_ptr<Type> nv_type, bool is_comptime) {
    scopes_.back()[name] = {val, std::move(nv_type), is_comptime};
}
std::optional<NIRSymbolInfo> NIRSymbolTable::lookup(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) return found->second;
    }
    return std::nullopt;
}
bool NIRSymbolTable::exists(const std::string& name) const {
    return lookup(name).has_value();
}

//===----------------------------------------------------------------------===//
// NIRGenerationContext
//===----------------------------------------------------------------------===//

NIRGenerationContext::NIRGenerationContext(mlir::MLIRContext& ctx,
                                           const std::string& source_file)
    : ctx_(ctx), builder_(&ctx), source_file_(source_file) {

    // Keep multithreading as configured by the caller.
    // Threading::DISABLED can cause hangs in MLIR 22 when TypeID lookups
    // are first resolved — letting MLIR's thread pool handle init is safer.

    // Register all dialects we may emit or lower into.
    ctx_.loadDialect<mlir::narval::NarvalDialect>();
    ctx_.loadDialect<mlir::func::FuncDialect>();
    ctx_.loadDialect<mlir::arith::ArithDialect>();
    ctx_.loadDialect<mlir::cf::ControlFlowDialect>();
    ctx_.loadDialect<mlir::scf::SCFDialect>();
    ctx_.loadDialect<mlir::memref::MemRefDialect>();
    ctx_.loadDialect<mlir::vector::VectorDialect>();
    ctx_.loadDialect<mlir::LLVM::LLVMDialect>();

    // Create the top-level module.
    module_ = mlir::ModuleOp::create(builder_.getUnknownLoc());
    builder_.setInsertionPointToEnd(module_->getBody());
}

//  Scope / symbol management 

void NIRGenerationContext::define(const std::string& name, mlir::Value val,
                                   std::shared_ptr<Type> nv_type, bool is_comptime) {
    symbols_.define(name, val, std::move(nv_type), is_comptime);
}

mlir::Value NIRGenerationContext::lookup(const std::string& name) {
    auto info = symbols_.lookup(name);
    if (info) return info->value;
    return mlir::Value{};
}

std::optional<NIRSymbolInfo> NIRGenerationContext::lookup_info(const std::string& name) {
    return symbols_.lookup(name);
}

//  Function context 

void NIRGenerationContext::set_current_func(mlir::func::FuncOp fn) {
    current_func_ = fn;
    current_fallible_ = false;
    current_async_    = false;
}

mlir::func::FuncOp NIRGenerationContext::get_current_func() {
    return *current_func_;
}

//  Type helpers 

mlir::narval::ValueType NIRGenerationContext::get_narval_value_type() {
    return mlir::narval::ValueType::get(&ctx_);
}

mlir::Type NIRGenerationContext::nv_type_to_mlir(std::shared_ptr<Type> nv_type) {
    if (!nv_type) return get_narval_value_type();
    auto nv_value = get_narval_value_type();
    switch (nv_type->kind) {
    case Kind::INT: case Kind::FLOAT: case Kind::BOOL:
    case Kind::STRING: case Kind::CHAR: case Kind::ARRAY:
    case Kind::VECTOR: case Kind::MAP: case Kind::TUPLE:
    case Kind::CLASS: case Kind::INTERFACE: case Kind::ENUM:
    case Kind::OPTION: case Kind::RESULT: case Kind::FUTURE:
        return nv_value;
    case Kind::NONE:
        // None is the void type in Narval — functions with no return value use this.
        return mlir::NoneType::get(&ctx_);
    case Kind::LOW_LEVEL: {
        const std::string& name = nv_type->toString();
        if (name == "i8"  || name == "u8")  return builder_.getIntegerType(8);
        if (name == "i16" || name == "u16") return builder_.getIntegerType(16);
        if (name == "i32" || name == "u32") return builder_.getIntegerType(32);
        if (name == "i64" || name == "u64") return builder_.getIntegerType(64);
        if (name == "i128"|| name == "u128")return builder_.getIntegerType(128);
        if (name == "f32") return builder_.getF32Type();
        if (name == "f64") return builder_.getF64Type();
        if (name == "usize" || name == "isize") return builder_.getIndexType();
        if (name == "ptr")  return mlir::LLVM::LLVMPointerType::get(&ctx_);
        return nv_value;
    }
    case Kind::TYPE_VAR: case Kind::POLY_TYPE: case Kind::ERROR:
        return nv_value;
    case Kind::FUNCTION: {
        std::vector<mlir::Type> param_types;
        for (auto& p : nv_type->params)
            param_types.push_back(nv_type_to_mlir(p));
        mlir::Type ret_type = nv_type->ret
            ? nv_type_to_mlir(nv_type->ret)
            : mlir::NoneType::get(&ctx_);
        return mlir::FunctionType::get(&ctx_, param_types,
                                       isa<mlir::NoneType>(ret_type)
                                           ? mlir::TypeRange{}
                                           : mlir::TypeRange{ret_type});
    }
    default: return nv_value;
    }
}

//  Location 

mlir::Location NIRGenerationContext::loc(const PositionData* pos) {
    if (!pos) return builder_.getUnknownLoc();
    return mlir::FileLineColLoc::get(&ctx_,
        llvm::StringRef(source_file_),
        static_cast<unsigned>(pos->line),
        static_cast<unsigned>(pos->col[0] + 1));
}

//  Runtime function declaration 

mlir::func::FuncOp NIRGenerationContext::ensure_runtime_func(
    const std::string& name, mlir::FunctionType fn_type) {
    if (auto existing = module_->lookupSymbol<mlir::func::FuncOp>(name))
        return existing;
    mlir::OpBuilder::InsertionGuard guard(builder_);
    builder_.setInsertionPointToStart(module_->getBody());
    auto fn = mlir::func::FuncOp::create(builder_, builder_.getUnknownLoc(),
                                          name, fn_type);
    fn.setPrivate();
    return fn;
}

//  Control-flow emit helpers 

mlir::narval::IfOp NIRGenerationContext::emit_if(mlir::Location loc,
                                                   mlir::Value condition,
                                                   mlir::TypeRange result_types) {
    auto op = mlir::narval::IfOp::create(builder_, loc, result_types, condition);
    // Auto-generated builder leaves regions empty; populate with entry blocks.
    if (op.getThenRegion().empty()) op.getThenRegion().emplaceBlock();
    if (op.getElseRegion().empty()) op.getElseRegion().emplaceBlock();
    return op;
}

mlir::narval::YieldOp NIRGenerationContext::emit_yield(mlir::Location loc,
                                                         mlir::ValueRange values) {
    return mlir::narval::YieldOp::create(builder_, loc, values);
}

mlir::narval::ForRangeOp NIRGenerationContext::emit_for_range(
    mlir::Location loc, mlir::Value lb, mlir::Value ub, mlir::Value step,
    llvm::StringRef var_name, mlir::ValueRange init_args) {
    return mlir::narval::ForRangeOp::create(
        builder_, loc,
        mlir::TypeRange(init_args.getTypes()),
        lb, ub, step, init_args,
        builder_.getStringAttr(var_name));
}

mlir::narval::WhileOp NIRGenerationContext::emit_while(
    mlir::Location loc, mlir::TypeRange result_types,
    mlir::ValueRange init_args) {
    return mlir::narval::WhileOp::create(builder_, loc, result_types, init_args);
}

mlir::Value NIRGenerationContext::pop_value() {
    if (value_stack_.empty()) return mlir::Value{};
    mlir::Value v = value_stack_.back();
    value_stack_.pop_back();
    return v;
}

//  Dump / print

void NIRGenerationContext::dump_nir() {
    module_->print(llvm::errs());
}

void NIRGenerationContext::print_nir(llvm::raw_ostream& os) {
    module_->print(os);
}

//  Lower to LLVM IR 

llvm::Expected<std::unique_ptr<llvm::Module>>
NIRGenerationContext::lower_to_llvm_ir(llvm::LLVMContext& llvm_ctx) {
    auto run = [&](mlir::PassManager& p) -> bool {
        return mlir::succeeded(p.run(*module_));
    };

    // Phase A: narval-dialect cleanup + CF/std lowering
    {
        mlir::PassManager pm(&ctx_);
        pm.enableVerifier(false);
        pm.addPass(nv::createNarvalCanonicalizationPass());
        pm.addPass(mlir::createCSEPass());
        pm.addPass(mlir::createSymbolDCEPass());
        nv::apply_transform_annotations(*module_, pm);
        pm.addPass(nv::createNarvalOwnershipPass());
        pm.addPass(nv::createLowerNarvalTensorPass());
        pm.addPass(nv::createLowerNarvalControlFlowPass());
        pm.addPass(nv::createLowerNarvalToStandardPass());
        pm.addPass(nv::createLowerNarvalGPUPass());
        if (!run(pm))
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "NIR phase A failed");
    }

    // Phase B: linalg/scf/vector → LLVM dialect
    {
        mlir::PassManager pm(&ctx_);
        pm.enableVerifier(false);
        pm.addPass(nv::createNarvalLinalgVectorizePass());
        pm.addPass(mlir::createConvertLinalgToLoopsPass());
        pm.addPass(mlir::createSCFToControlFlowPass());
        pm.addPass(mlir::createConvertVectorToLLVMPass());
        pm.addPass(nv::createLowerNarvalToLLVMPass());
        pm.addPass(mlir::createReconcileUnrealizedCastsPass());
        if (!run(pm))
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "NIR phase B failed");
    }

    mlir::registerBuiltinDialectTranslation(ctx_);
    mlir::registerLLVMDialectTranslation(ctx_);

    auto llvm_module = mlir::translateModuleToLLVMIR(*module_, llvm_ctx,
                                                      source_file_);
    if (!llvm_module)
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "MLIR → LLVM IR translation failed");

    return std::move(llvm_module);
}

//  JIT execution

llvm::Expected<int> NIRGenerationContext::jit_execute() {
    // Lower NIR -> LLVM IR (same pipeline as batch mode)
    auto llvm_ctx = std::make_unique<llvm::LLVMContext>();
    auto mod_or_err = lower_to_llvm_ir(*llvm_ctx);
    if (!mod_or_err)
        return mod_or_err.takeError();

    // Initialize native JIT targets (idempotent after first call)
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

    // Build the JIT instance
    auto jit_or_err = llvm::orc::LLJITBuilder().create();
    if (!jit_or_err)
        return jit_or_err.takeError();
    auto& jit = *jit_or_err;

    // Expose all symbols already loaded in the host process so the JIT can
    // call Narval runtime functions (nv_write, create_int, …) directly.
    auto gen = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
        jit->getDataLayout().getGlobalPrefix());
    if (!gen)
        return gen.takeError();
    jit->getMainJITDylib().addGenerator(std::move(*gen));

    // Add the lowered LLVM module
    auto tsm = llvm::orc::ThreadSafeModule(std::move(*mod_or_err), std::move(llvm_ctx));
    if (auto err = jit->addIRModule(std::move(tsm)))
        return std::move(err);

    // Look up and invoke main.start
    auto sym = jit->lookup("main.start");
    if (!sym)
        return sym.takeError();

    auto fn = reinterpret_cast<void(*)()>(sym->getValue());
    fn();
    return 0;
}

} // namespace nv
