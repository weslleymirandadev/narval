#include "backend/codegen/ir_utils.hpp"
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <unordered_map>
#include <cctype>
#include <iostream>

namespace rph {
namespace ir_utils {

// === Cache e helpers ===
static std::unordered_map<std::string, llvm::Type*> type_cache;

static llvm::StructType* get_vec_struct(IRGenerationContext& ctx, llvm::Type* elem) {
    std::string name = "vec." + std::to_string((uintptr_t)elem);
    auto* s = llvm::StructType::getTypeByName(ctx.get_context(), name);
    if (!s) {
        s = llvm::StructType::create(ctx.get_context(), {
            llvm::PointerType::getUnqual(elem),
            get_i32(ctx), get_i32(ctx)
        }, name);
    }
    return s;
}

static llvm::StructType* get_map_struct(IRGenerationContext& ctx, llvm::Type* key, llvm::Type* val) {
    std::string name = "map." + std::to_string((uintptr_t)key) + "." + std::to_string((uintptr_t)val);
    auto* s = llvm::StructType::getTypeByName(ctx.get_context(), name);
    if (!s) {
        s = llvm::StructType::create(ctx.get_context(), { get_i8_ptr(ctx) }, name);
    }
    return s;
}

static llvm::Type* parse_array(const std::string& s, size_t& pos, IRGenerationContext& ctx);
static llvm::Type* parse_tuple(const std::string& s, size_t& pos, IRGenerationContext& ctx);

// === Implementações ===

llvm::Value* create_string_constant(IRGenerationContext& context, const std::string& value) {
    auto& module = context.get_module();
    auto& llvm_context = context.get_context();
    auto* str_type = llvm::ArrayType::get(llvm::Type::getInt8Ty(llvm_context), value.size() + 1);
    auto* global_str = new llvm::GlobalVariable(
        module, str_type, true,
        llvm::GlobalValue::PrivateLinkage,
        llvm::ConstantDataArray::getString(llvm_context, value, true), ".str"
    );
    auto* zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_context), 0);
    std::vector<llvm::Value*> indices = { zero, zero };
    return context.get_builder().CreateGEP(str_type, global_str, indices);
}

llvm::Value* create_int_constant(IRGenerationContext& context, int32_t value) {
    return llvm::ConstantInt::get(get_i32(context), value);
}

llvm::Value* create_float_constant(IRGenerationContext& context, double value) {
    return llvm::ConstantFP::get(get_f64(context), value);
}

llvm::Value* create_bool_constant(IRGenerationContext& context, bool value) {
    return llvm::ConstantInt::get(get_i1(context), value ? 1 : 0);
}

llvm::Value* promote_type(IRGenerationContext& context, llvm::Value* value, llvm::Type* target_type) {
    if (!value || !target_type) return nullptr;
    auto* source_type = value->getType();
    if (source_type == target_type) return value;
    auto& builder = context.get_builder();

    if (source_type->isIntegerTy() && target_type->isFloatingPointTy()) {
        return builder.CreateSIToFP(value, target_type, "inttofp");
    }
    if (source_type->isFloatingPointTy() && target_type->isIntegerTy()) {
        return builder.CreateFPToSI(value, target_type, "fptoint");
    }
    if (source_type->isIntegerTy() && target_type->isIntegerTy()) {
        auto src_bits = source_type->getIntegerBitWidth();
        auto tgt_bits = target_type->getIntegerBitWidth();
        if (src_bits > tgt_bits) return builder.CreateTrunc(value, target_type, "trunc");
        if (src_bits < tgt_bits) return builder.CreateZExt(value, target_type, "zext");
    }
    return value;
}

llvm::Value* promote_rph_type(
    IRGenerationContext& context,
    llvm::Value* value,
    std::shared_ptr<Type>& source_type,
    std::shared_ptr<Type>& target_type
) {
    if (!value || !source_type || !target_type) return nullptr;
    if (source_type->equals(*target_type)) return value;
    auto* target_llvm_type = context.rph_type_to_llvm(target_type);
    return promote_type(context, value, target_llvm_type);
}

llvm::Value* create_comparison(IRGenerationContext& context, llvm::Value* lhs, llvm::Value* rhs, const std::string& op) {
    if (!lhs || !rhs) return nullptr;
    auto& builder = context.get_builder();
    auto* lhs_type = lhs->getType();

    // String (i8*) content comparison via strcmp
    {
        auto* i8p = get_i8_ptr(context);
        if (lhs_type == i8p && rhs->getType() == i8p) {
            auto* i32 = get_i32(context);
            auto* strcmpTy = llvm::FunctionType::get(i32, { i8p, i8p }, false);
            auto* strcmpFn = llvm::cast<llvm::Function>(context.get_module().getOrInsertFunction("strcmp", strcmpTy).getCallee());
            auto* cmpRes = builder.CreateCall(strcmpFn, { lhs, rhs }, "strcmp");
            auto* zero = llvm::ConstantInt::get(i32, 0);
            if (op == "==") return builder.CreateICmpEQ(cmpRes, zero, "cmpeq");
            if (op == "!=") return builder.CreateICmpNE(cmpRes, zero, "cmpne");
            if (op == "<")  return builder.CreateICmpSLT(cmpRes, zero, "cmplt");
            if (op == ">")  return builder.CreateICmpSGT(cmpRes, zero, "cmpgt");
            if (op == "<=") return builder.CreateICmpSLE(cmpRes, zero, "cmple");
            if (op == ">=") return builder.CreateICmpSGE(cmpRes, zero, "cmpge");
        }
    }

    // Converter tipos diferentes (int <-> float)
    if (lhs_type != rhs->getType()) {
        if (lhs_type->isIntegerTy() && rhs->getType()->isFloatingPointTy()) {
            lhs = builder.CreateSIToFP(lhs, rhs->getType());
            lhs_type = lhs->getType(); // Atualizar tipo após conversão
        } else if (lhs_type->isFloatingPointTy() && rhs->getType()->isIntegerTy()) {
            rhs = builder.CreateSIToFP(rhs, lhs_type);
        }
    }
    
    // Após conversão, ambos devem ter o mesmo tipo - usar o tipo atualizado
    auto* final_type = lhs->getType();
    bool is_float = final_type->isFloatingPointTy();
    bool is_int = final_type->isIntegerTy() || final_type->isPointerTy();

    if (op == "==") return is_float
        ? builder.CreateFCmpOEQ(lhs, rhs, "cmpeq") : builder.CreateICmpEQ(lhs, rhs, "cmpeq");
    if (op == "!=") return is_float
        ? builder.CreateFCmpONE(lhs, rhs, "cmpne") : builder.CreateICmpNE(lhs, rhs, "cmpne");
    if (op == "<") return is_float
        ? builder.CreateFCmpOLT(lhs, rhs, "cmplt") : builder.CreateICmpSLT(lhs, rhs, "cmplt");
    if (op == ">") return is_float
        ? builder.CreateFCmpOGT(lhs, rhs, "cmpgt") : builder.CreateICmpSGT(lhs, rhs, "cmpgt");
    if (op == "<=") return is_float
        ? builder.CreateFCmpOLE(lhs, rhs, "cmple") : builder.CreateICmpSLE(lhs, rhs, "cmple");
    if (op == ">=") return is_float
        ? builder.CreateFCmpOGE(lhs, rhs, "cmpge") : builder.CreateICmpSGE(lhs, rhs, "cmpge");

    return nullptr;
}

llvm::Value* create_binary_op(IRGenerationContext& context, llvm::Value* lhs, llvm::Value* rhs, const std::string& op) {
    if (!lhs || !rhs) return nullptr;
    auto& builder = context.get_builder();
    auto* lhs_type = lhs->getType();

    auto* rhs_type = rhs->getType();

    if (op == "**") {
        llvm::Type* f64 = get_f64(context);
        if (!lhs_type->isFloatingPointTy()) lhs = builder.CreateSIToFP(lhs, f64);
        else if (lhs_type != f64) lhs = builder.CreateFPExt(lhs, f64);
        if (!rhs_type->isFloatingPointTy()) rhs = builder.CreateSIToFP(rhs, f64);
        else if (rhs_type != f64) rhs = builder.CreateFPExt(rhs, f64);
        auto* pow_decl = llvm::Intrinsic::getDeclaration(&context.get_module(), llvm::Intrinsic::pow, { f64 });
        return builder.CreateCall(pow_decl, { lhs, rhs }, "pow");
    }

    if (op == "+") {
        auto* i8p = llvm::PointerType::getUnqual(get_i8(context));
        bool lhs_is_str = (lhs_type == i8p);
        bool rhs_is_str = (rhs_type == i8p);
        if (lhs_is_str && rhs_is_str) {
            auto* catTy = llvm::FunctionType::get(i8p, { i8p, i8p }, false);
            auto* catFn = llvm::cast<llvm::Function>(context.get_module().getOrInsertFunction("string_concat", catTy).getCallee());
            return builder.CreateCall(catFn, { lhs, rhs }, "strcat");
        }
    }

    if ((op == "*") && ([&](){
        auto* i8ptr = llvm::PointerType::getUnqual(get_i8(context));
        bool lhs_is_str = (lhs_type == i8ptr);
        bool rhs_is_str = (rhs_type == i8ptr);
        return (lhs_is_str && rhs_type->isIntegerTy()) || (rhs_is_str && lhs_type->isIntegerTy());
    }())) {
        llvm::Value* str = lhs_type->isPointerTy() ? lhs : rhs;
        llvm::Value* n   = lhs_type->isIntegerTy() ? lhs : rhs;
        auto* i8p = llvm::PointerType::getUnqual(get_i8(context));
        auto* i32 = get_i32(context);
        if (!n->getType()->isIntegerTy(32)) n = builder.CreateIntCast(n, i32, true);
        auto* zero32 = llvm::ConstantInt::get(i32, 0);
        auto* is_le_zero = builder.CreateICmpSLE(n, zero32);
        auto* thenBB = llvm::BasicBlock::Create(context.get_context(), "strrep_then", context.get_current_function());
        auto* elseBB = llvm::BasicBlock::Create(context.get_context(), "strrep_else", context.get_current_function());
        auto* mergeBB = llvm::BasicBlock::Create(context.get_context(), "strrep_merge", context.get_current_function());
        builder.CreateCondBr(is_le_zero, thenBB, elseBB);
        builder.SetInsertPoint(thenBB);
        auto* empty = create_string_constant(context, "");
        builder.CreateBr(mergeBB);
        builder.SetInsertPoint(elseBB);
        auto* repTy = llvm::FunctionType::get(i8p, { i8p, i32 }, false);
        auto* repFn = llvm::cast<llvm::Function>(context.get_module().getOrInsertFunction("string_repeat", repTy).getCallee());
        auto* outp = builder.CreateCall(repFn, { str, n }, "strrep");
        builder.CreateBr(mergeBB);
        builder.SetInsertPoint(mergeBB);
        auto* phi = builder.CreatePHI(i8p, 2);
        phi->addIncoming(empty, thenBB);
        phi->addIncoming(outp, elseBB);
        return phi;
    }

    if (lhs_type != rhs_type) {
        if (lhs_type->isIntegerTy() && rhs_type->isFloatingPointTy()) {
            lhs = builder.CreateSIToFP(lhs, rhs_type);
            lhs_type = rhs_type;
        } else if (lhs_type->isFloatingPointTy() && rhs_type->isIntegerTy()) {
            rhs = builder.CreateSIToFP(rhs, lhs_type);
            rhs_type = lhs_type;
        }
    }

    if (op == "+") return lhs_type->isFloatingPointTy() ? builder.CreateFAdd(lhs, rhs, "add") : builder.CreateAdd(lhs, rhs, "add");
    if (op == "-") return lhs_type->isFloatingPointTy() ? builder.CreateFSub(lhs, rhs, "sub") : builder.CreateSub(lhs, rhs, "sub");
    if (op == "*") return lhs_type->isFloatingPointTy() ? builder.CreateFMul(lhs, rhs, "mul") : builder.CreateMul(lhs, rhs, "mul");
    if (op == "/") return lhs_type->isFloatingPointTy() ? builder.CreateFDiv(lhs, rhs, "div") : builder.CreateSDiv(lhs, rhs, "div");
    if (op == "//") {
        if (lhs_type->isFloatingPointTy()) {
            auto* f64 = get_f64(context);
            if (lhs_type != f64) lhs = builder.CreateFPExt(lhs, f64);
            if (rhs_type != f64) rhs = builder.CreateFPExt(rhs, f64);
            auto* floorTy = llvm::FunctionType::get(f64, { f64 }, false);
            auto* floorFn = llvm::cast<llvm::Function>(context.get_module().getOrInsertFunction("floor", floorTy).getCallee());
            auto* divv = builder.CreateFDiv(lhs, rhs);
            return builder.CreateCall(floorFn, { divv });
        } else {
            return builder.CreateSDiv(lhs, rhs, "idiv");
        }
    }
    if (op == "%") return lhs_type->isFloatingPointTy() ? builder.CreateFRem(lhs, rhs, "mod") : builder.CreateSRem(lhs, rhs, "mod");

    return nullptr;
}

llvm::Value* create_unary_op(IRGenerationContext& context, llvm::Value* operand, const std::string& op) {
    if (!operand) return nullptr;
    auto& builder = context.get_builder();
    if (op == "-" || op == "neg") return operand->getType()->isFloatingPointTy()
        ? builder.CreateFNeg(operand, "neg") : builder.CreateNeg(operand, "neg");
    if (op == "!" || op == "not") {
        if (operand->getType()->isIntegerTy(1)) {
            return builder.CreateXor(operand, llvm::ConstantInt::getTrue(context.get_context()), "not");
        }
    }
    return nullptr;
}

// === REMOVIDO: infer_llvm_type(Node*) ===
// O tipo do nó é obtido via checker ou context.rph_type_to_llvm()

llvm::Type* infer_llvm_type(IRGenerationContext& context, std::shared_ptr<Type>& type) {
    return type ? llvm_type_from_string(context, type->toString()) : get_void(context);
}

llvm::BasicBlock* create_and_set_block(IRGenerationContext& context, const std::string& name) {
    auto* block = context.create_block(name);
    auto* func = context.get_current_function();
    if (func && !block->getParent()) block->insertInto(func);
    context.get_builder().SetInsertPoint(block);
    return block;
}

llvm::Function* create_function(IRGenerationContext& context, const std::string& name, std::unique_ptr<Label>& label_type) {
    if (!label_type) return nullptr;
    std::vector<llvm::Type*> param_types;
    for (const auto& p : label_type->paramstype)
        param_types.push_back(context.rph_type_to_llvm(p));
    auto* ret_type = context.rph_type_to_llvm(label_type->returntype);
    auto* ft = llvm::FunctionType::get(ret_type, param_types, false);
    auto* func = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, context.get_module());
    context.set_current_function(func);
    return func;
}

llvm::Function* create_function(
    IRGenerationContext& context, const std::string& name,
    llvm::Type* return_type, const std::vector<llvm::Type*>& param_types, bool is_vararg
) {
    auto* ft = llvm::FunctionType::get(return_type, param_types, is_vararg);
    auto* func = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, context.get_module());
    context.set_current_function(func);
    return func;
}

llvm::Value* create_function_call(IRGenerationContext& context, llvm::Function* function, const std::vector<llvm::Value*>& args) {
    return function ? context.get_builder().CreateCall(function, args, "call") : nullptr;
}

llvm::ReturnInst* create_return(IRGenerationContext& context, llvm::Value* value) {
    auto& B = context.get_builder();
    if (!value) {
        return B.CreateRetVoid();
    }
    return B.CreateRet(value);
}

void create_conditional_branch(IRGenerationContext& context, llvm::Value* condition, llvm::BasicBlock* then_block, llvm::BasicBlock* else_block) {
    context.get_builder().CreateCondBr(condition, then_block, else_block);
}

void create_unconditional_branch(IRGenerationContext& context, llvm::BasicBlock* target_block) {
    context.get_builder().CreateBr(target_block);
}

IfElseBlocks create_if_else_structure(IRGenerationContext& context, const std::string& base_name) {
    IfElseBlocks b;
    auto* func = context.get_current_function();
    b.then_block = llvm::BasicBlock::Create(context.get_context(), base_name + "_then", func);
    b.else_block = llvm::BasicBlock::Create(context.get_context(), base_name + "_else", func);
    b.merge_block = llvm::BasicBlock::Create(context.get_context(), base_name + "_merge", func);
    return b;
}

void finalize_if_else(IRGenerationContext& context, const IfElseBlocks& blocks, bool has_else) {
    auto& builder = context.get_builder();
    if (builder.GetInsertBlock() == blocks.then_block && !builder.GetInsertBlock()->getTerminator())
        builder.CreateBr(blocks.merge_block);
    if (has_else && builder.GetInsertBlock() == blocks.else_block && !builder.GetInsertBlock()->getTerminator())
        builder.CreateBr(blocks.merge_block);
    builder.SetInsertPoint(blocks.merge_block);
}

// === Tipos básicos ===
llvm::Type* get_i32(IRGenerationContext& ctx) { return llvm::Type::getInt32Ty(ctx.get_context()); }
llvm::Type* get_i64(IRGenerationContext& ctx) { return llvm::Type::getInt64Ty(ctx.get_context()); }
llvm::Type* get_f64(IRGenerationContext& ctx) { return llvm::Type::getDoubleTy(ctx.get_context()); }
llvm::Type* get_i1(IRGenerationContext& ctx)  { return llvm::Type::getInt1Ty(ctx.get_context()); }
llvm::Type* get_void(IRGenerationContext& ctx){ return llvm::Type::getVoidTy(ctx.get_context()); }
llvm::Type* get_i8(IRGenerationContext& ctx)  { return llvm::Type::getInt8Ty(ctx.get_context()); }
llvm::Type* get_i8_ptr(IRGenerationContext& ctx) { return llvm::PointerType::getUnqual(get_i8(ctx)); }

llvm::StructType* get_value_struct(IRGenerationContext& ctx) {
    auto* t = llvm::StructType::getTypeByName(ctx.get_context(), "rph.rt.Value");
    if (!t) t = llvm::StructType::create(ctx.get_context(), { get_i32(ctx), get_i64(ctx), get_i8_ptr(ctx) }, "rph.rt.Value");
    return t;
}
llvm::PointerType* get_value_ptr(IRGenerationContext& ctx) { return llvm::PointerType::getUnqual(get_value_struct(ctx)); }

// === String → LLVM Type ===
// === String → LLVM Type (CORRIGIDO) ===
llvm::Type* llvm_type_from_string(IRGenerationContext& ctx, const std::string& raw) {
    if (type_cache.count(raw)) return type_cache[raw];

    std::string s;
    for (char c : raw) if (!std::isspace(c)) s += c;

    size_t p = 0;
    llvm::Type* result = parse_type_recursive(s, p, ctx);

    if (result && p == s.size()) {
        return type_cache[raw] = result;
    }
    return type_cache[raw] = get_void(ctx);
}

// === NOVO: Parser recursivo descendente ===
static llvm::Type* parse_type_recursive(const std::string& s, size_t& p, IRGenerationContext& ctx) {
    if (p >= s.size()) return nullptr;

    // --- Primitivos ---
    if (s.substr(p, 3) == "int") { 
        p += 3;
        // Verificar se é array: int[10]
        if (p < s.size() && s[p] == '[') {
            ++p;
            std::string num;
            while (p < s.size() && std::isdigit(s[p])) num += s[p++];
            if (p < s.size() && s[p] == ']') {
                ++p;
                if (!num.empty()) {
                    return llvm::ArrayType::get(get_i32(ctx), std::stoi(num));
                }
            }
            return nullptr; // Erro de parsing
        }
        return get_i32(ctx); 
    }
    if (s.substr(p, 5) == "float") { 
        p += 5;
        // Verificar se é array: float[10]
        if (p < s.size() && s[p] == '[') {
            ++p;
            std::string num;
            while (p < s.size() && std::isdigit(s[p])) num += s[p++];
            if (p < s.size() && s[p] == ']') {
                ++p;
                if (!num.empty()) {
                    return llvm::ArrayType::get(get_f64(ctx), std::stoi(num));
                }
            }
            return nullptr; // Erro de parsing
        }
        return get_f64(ctx); 
    }
    if (s.substr(p, 4) == "bool") { 
        p += 4;
        // Verificar se é array: bool[10]
        if (p < s.size() && s[p] == '[') {
            ++p;
            std::string num;
            while (p < s.size() && std::isdigit(s[p])) num += s[p++];
            if (p < s.size() && s[p] == ']') {
                ++p;
                if (!num.empty()) {
                    return llvm::ArrayType::get(get_i1(ctx), std::stoi(num));
                }
            }
            return nullptr; // Erro de parsing
        }
        return get_i1(ctx); 
    }
    if (s.substr(p, 6) == "string") {
        p += 6;
        // Verificar se é array: string[10]
        if (p < s.size() && s[p] == '[') {
            ++p;
            std::string num;
            while (p < s.size() && std::isdigit(s[p])) num += s[p++];
            if (p < s.size() && s[p] == ']') {
                ++p;
                if (!num.empty()) {
                    return llvm::ArrayType::get(get_i8_ptr(ctx), std::stoi(num));
                }
            }
            return nullptr; // Erro de parsing
        }
        return get_i8_ptr(ctx);
    }
    if (s.substr(p, 3) == "str") { p += 3; return get_i8_ptr(ctx); }
    if (s.substr(p, 6) == "vector") { p += 6; return get_value_ptr(ctx); }
    if (s.substr(p, 4) == "json") { p += 4; return get_value_ptr(ctx); }
    if (s.substr(p, 4) == "void") { p += 4; return get_void(ctx); }


    // --- map<K,V> ---
    if (s.substr(p, 4) == "map<") {
        p += 4;
        llvm::Type* key = parse_type_recursive(s, p, ctx);
        if (!key || p >= s.size() || s[p] != ',') return nullptr;
        ++p;
        llvm::Type* val = parse_type_recursive(s, p, ctx);
        if (!val || p >= s.size() || s[p] != '>') return nullptr;
        ++p;
        // Represent maps as dynamic Value at runtime
        return get_value_struct(ctx);
    }

    // --- [N]T ou []T ---
    if (s[p] == '[') {
        ++p;
        std::string num;
        while (p < s.size() && std::isdigit(s[p])) num += s[p++];
        if (p >= s.size() || s[p] != ']') return nullptr;
        ++p;

        llvm::Type* elem = parse_type_recursive(s, p, ctx);
        if (!elem) return nullptr;

        if (num.empty()) {
            // Dynamic arrays as Value
            return get_value_struct(ctx);
        } else {
            return llvm::ArrayType::get(elem, std::stoi(num)); // [2]int
        }
    }

    // --- (int, str, ...) ---
    if (s[p] == '(') {
        ++p;
        std::vector<llvm::Type*> fields;
        while (p < s.size() && s[p] != ')') {
            while (p < s.size() && isspace(static_cast<unsigned char>(s[p]))) ++p;
            llvm::Type* f = parse_type_recursive(s, p, ctx);
            if (!f) return nullptr;
            fields.push_back(f);
            while (p < s.size() && isspace(static_cast<unsigned char>(s[p]))) ++p;
            if (p < s.size() && s[p] == ',') ++p;
        }
        if (p >= s.size() || s[p] != ')') return nullptr;
        ++p;
        return llvm::StructType::get(ctx.get_context(), fields);
    }

    return nullptr;
}

} // namespace ir_utils
} // namespace rph