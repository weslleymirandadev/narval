#include "backend/codegen/ir_utils.hpp"
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <unordered_map>
#include <cctype>
#include <iostream>

namespace nv {
namespace ir_utils {

// === Cache e helpers ===
static std::unordered_map<std::string, llvm::Type*> type_cache;

static llvm::StructType* get_vec_struct(IRGenerationContext& ctx, llvm::Type* elem) {
    std::string name = "vec." + std::to_string((uintptr_t)elem);
    auto* s = llvm::StructType::getTypeByName(ctx.get_context(), name);
    if (!s) {
        s = llvm::StructType::create(ctx.get_context(), {
            llvm::PointerType::getUnqual(ctx.get_context()),
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
    return global_str;
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

llvm::Value* promote_nv_type(
    IRGenerationContext& context,
    llvm::Value* value,
    std::shared_ptr<Type>& source_type,
    std::shared_ptr<Type>& target_type
) {
    if (!value || !source_type || !target_type) return nullptr;
    if (source_type->equals(*target_type)) return value;
    auto* target_llvm_type = context.nv_type_to_llvm(target_type);
    return promote_type(context, value, target_llvm_type);
}

// Helper: extrai valor numérico de um Value via runtime
static llvm::Value* extract_int_from_value(IRGenerationContext& context, llvm::Value* val) {
    if (!val) return nullptr;
    auto* valueStruct = get_value_struct(context);
    if (val->getType() != valueStruct) return val; // Não é Value, retornar como está

    auto& builder = context.get_builder();
    auto* i32 = get_i32(context);
    auto* valPtr = get_value_ptr(context);

    auto* tmp = context.create_alloca(valueStruct, "val.tmp");
    builder.CreateStore(val, tmp);

    auto* extractFn = context.ensure_runtime_func("extract_int_from_value", {valPtr}, i32);
    return builder.CreateCall(extractFn, {tmp}, "int.val");
}

// Helper: extrai valor float (double) de um Value via runtime
static llvm::Value* extract_float_from_value(IRGenerationContext& context, llvm::Value* val) {
    if (!val) return nullptr;
    auto* valueStruct = get_value_struct(context);
    if (val->getType() != valueStruct) return val; // Não é Value, retornar como está

    auto& builder = context.get_builder();
    auto* f64 = get_f64(context);
    auto* valPtr = get_value_ptr(context);

    auto* tmp = context.create_alloca(valueStruct, "val.tmp");
    builder.CreateStore(val, tmp);

    auto* extractFn = context.ensure_runtime_func("extract_float_from_value", {valPtr}, f64);
    return builder.CreateCall(extractFn, {tmp}, "float.val");
}

llvm::Value* create_comparison(IRGenerationContext& context, llvm::Value* lhs, llvm::Value* rhs, const std::string& op) {
    if (!lhs || !rhs) return nullptr;
    auto& builder = context.get_builder();
    auto* valueStruct = get_value_struct(context);

    // Print input types for debugging
    {
        std::string ltype, rtype;
        llvm::raw_string_ostream lo(ltype), ro(rtype);
        if (lhs) lhs->getType()->print(lo); else lo << "null";
        if (rhs) rhs->getType()->print(ro); else ro << "null";
    }

    llvm::Value* out = nullptr;

    // Se ambos são Value structs, usar comparação type-aware (int vs float resolvido em runtime)
    if (lhs->getType() == valueStruct && rhs->getType() == valueStruct) {
        auto* ValuePtr = get_value_ptr(context);
        auto* I32 = get_i32(context);
        auto* lhs_alloca = context.create_alloca(valueStruct, "cmp_lhs");
        auto* rhs_alloca = context.create_alloca(valueStruct, "cmp_rhs");
        builder.CreateStore(lhs, lhs_alloca);
        builder.CreateStore(rhs, rhs_alloca);
        auto* cmp_fn = context.ensure_runtime_func("nv_value_cmp", {ValuePtr, ValuePtr}, I32);
        auto* cmp_result = builder.CreateCall(cmp_fn, {lhs_alloca, rhs_alloca}, "cmp_val");
        auto* zero = llvm::ConstantInt::get(I32, 0);
        if (op == "==") out = builder.CreateICmpEQ(cmp_result, zero, "cmpeq");
        else if (op == "!=") out = builder.CreateICmpNE(cmp_result, zero, "cmpne");
        else if (op == "<")  out = builder.CreateICmpSLT(cmp_result, zero, "cmplt");
        else if (op == ">")  out = builder.CreateICmpSGT(cmp_result, zero, "cmpgt");
        else if (op == "<=") out = builder.CreateICmpSLE(cmp_result, zero, "cmple");
        else if (op == ">=") out = builder.CreateICmpSGE(cmp_result, zero, "cmpge");
        return out;
    } else if (lhs->getType() == valueStruct) {
        // Se o outro for numérico (int ou float), extrair float por segurança;
        // caso contrário, extrair int
        if (rhs->getType()->isFloatingPointTy() || rhs->getType()->isIntegerTy()) lhs = extract_float_from_value(context, lhs);
        else lhs = extract_int_from_value(context, lhs);
    } else if (rhs->getType() == valueStruct) {
        if (lhs->getType()->isFloatingPointTy() || lhs->getType()->isIntegerTy()) rhs = extract_float_from_value(context, rhs);
        else rhs = extract_int_from_value(context, rhs);
    }

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
            if (op == "==") out = builder.CreateICmpEQ(cmpRes, zero, "cmpeq");
            else if (op == "!=") out = builder.CreateICmpNE(cmpRes, zero, "cmpne");
            else if (op == "<")  out = builder.CreateICmpSLT(cmpRes, zero, "cmplt");
            else if (op == ">")  out = builder.CreateICmpSGT(cmpRes, zero, "cmpgt");
            else if (op == "<=") out = builder.CreateICmpSLE(cmpRes, zero, "cmple");
            else if (op == ">=") out = builder.CreateICmpSGE(cmpRes, zero, "cmpge");
        }
    }

    // Converter tipos diferentes (int <-> float, i1 <-> i32)
    if (lhs_type != rhs->getType()) {
        // CORREÇÃO: Converter i1 para i32 quando necessário
        if (lhs_type->isIntegerTy(1) && rhs->getType()->isIntegerTy(32)) {
            lhs = builder.CreateZExt(lhs, rhs->getType());
            lhs_type = lhs->getType();
        } else if (lhs_type->isIntegerTy(32) && rhs->getType()->isIntegerTy(1)) {
            rhs = builder.CreateZExt(rhs, lhs_type);
        } else if (lhs_type->isIntegerTy() && rhs->getType()->isFloatingPointTy()) {
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

    if (op == "==") out = is_float
        ? builder.CreateFCmpOEQ(lhs, rhs, "cmpeq") : builder.CreateICmpEQ(lhs, rhs, "cmpeq");
    else if (op == "!=") out = is_float
        ? builder.CreateFCmpONE(lhs, rhs, "cmpne") : builder.CreateICmpNE(lhs, rhs, "cmpne");
    else if (op == "<") out = is_float
        ? builder.CreateFCmpOLT(lhs, rhs, "cmplt") : builder.CreateICmpSLT(lhs, rhs, "cmplt");
    else if (op == ">") out = is_float
        ? builder.CreateFCmpOGT(lhs, rhs, "cmpgt") : builder.CreateICmpSGT(lhs, rhs, "cmpgt");
    else if (op == "<=") out = is_float
        ? builder.CreateFCmpOLE(lhs, rhs, "cmple") : builder.CreateICmpSLE(lhs, rhs, "cmple");
    else if (op == ">=") out = is_float
        ? builder.CreateFCmpOGE(lhs, rhs, "cmpge") : builder.CreateICmpSGE(lhs, rhs, "cmpge");
    return out;
}

llvm::Value* create_binary_op(IRGenerationContext& context, llvm::Value* lhs, llvm::Value* rhs, const std::string& op) {
    if (!lhs || !rhs) return nullptr;
    auto& builder = context.get_builder();
    auto* valueStruct = get_value_struct(context);
    
    auto* lhs_type = lhs->getType();
    auto* rhs_type = rhs->getType();
    
    // Sistema de sobrecarga de operadores para "+"
    if (op == "+") {
        auto* i8p = llvm::PointerType::getUnqual(context.get_context());
        auto* i32 = get_i32(context);
        auto* f64 = get_f64(context);
        auto* valuePtr = get_value_ptr(context);
        
        bool lhs_is_str = (lhs_type == i8p);
        bool rhs_is_str = (rhs_type == i8p);
        bool lhs_is_int = (lhs_type == i32);
        bool rhs_is_int = (rhs_type == i32);
        bool lhs_is_float = (lhs_type == f64);
        bool rhs_is_float = (rhs_type == f64);
        bool lhs_is_value = (lhs_type == valueStruct);
        bool rhs_is_value = (rhs_type == valueStruct);
        
        // Caso 3: String literal + String literal -> concat
        if (lhs_is_str && rhs_is_str) {
            auto* catTy = llvm::FunctionType::get(i8p, { i8p, i8p }, false);
            auto* catFn = llvm::cast<llvm::Function>(context.get_module().getOrInsertFunction("string_concat", catTy).getCallee());
            return builder.CreateCall(catFn, {lhs, rhs}, "strcat");
        }

        // Caso 4: Int literal + String literal -> fallback para operador padrão
        if (lhs_is_int && rhs_is_str) {
            // TODO: Implementar através de sistema de classes quando disponível
            return nullptr;
        }
        
        // Caso 4.5: String literal + Int literal -> fallback para operador padrão
        if (lhs_is_str && rhs_is_int) {
            // TODO: Implementar através de sistema de classes quando disponível
            return nullptr;
        }
        
        // Caso 5: Value + String literal -> extrair string do Value e concatenar
        if (lhs_is_value && rhs_is_str) {
            auto* lhsAlloca = context.create_alloca(valueStruct, "lhs_val");
            builder.CreateStore(lhs, lhsAlloca);
            auto* extractFn = context.ensure_runtime_func("extract_string_from_value", {valuePtr}, i8p);
            auto* lhsStr = builder.CreateCall(extractFn, {lhsAlloca}, "lhs_str");
            
            auto* catTy = llvm::FunctionType::get(i8p, { i8p, i8p }, false);
            auto* catFn = llvm::cast<llvm::Function>(context.get_module().getOrInsertFunction("string_concat", catTy).getCallee());
            std::vector<llvm::Value*> args = {lhsStr, rhs};
            return builder.CreateCall(catFn, args, "strcat");
        }
        
        // Caso 6: String literal + Value -> extrair string do Value e concatenar
        if (lhs_is_str && rhs_is_value) {
            auto* rhsAlloca = context.create_alloca(valueStruct, "rhs_val");
            builder.CreateStore(rhs, rhsAlloca);
            auto* extractFn = context.ensure_runtime_func("extract_string_from_value", {valuePtr}, i8p);
            auto* rhsStr = builder.CreateCall(extractFn, {rhsAlloca}, "rhs_str");
            
            auto* catTy = llvm::FunctionType::get(i8p, { i8p, i8p }, false);
            auto* catFn = llvm::cast<llvm::Function>(context.get_module().getOrInsertFunction("string_concat", catTy).getCallee());
            std::vector<llvm::Value*> args = {lhs, rhsStr};
            return builder.CreateCall(catFn, args, "strcat");
        }
        
        // Caso 7: Int literal + Int literal -> soma direta
        if (lhs_is_int && rhs_is_int) {
            return builder.CreateAdd(lhs, rhs, "add");
        }
        
        // Caso 8: Float literal + Float literal -> soma direta
        if (lhs_is_float && rhs_is_float) {
            return builder.CreateFAdd(lhs, rhs, "add");
        }
        
        // Caso 9: Int literal + Float literal ou vice-versa -> soma com promoção
        if ((lhs_is_int && rhs_is_float) || (lhs_is_float && rhs_is_int)) {
            if (lhs_is_int) {
                lhs = builder.CreateSIToFP(lhs, f64);
            } else {
                rhs = builder.CreateSIToFP(rhs, f64);
            }
            return builder.CreateFAdd(lhs, rhs, "add");
        }
    }
    
    // Para operações numéricas com dois Value structs, usar funções type-aware do runtime
    // (detectam int vs float em runtime e produzem o resultado correto)
    if (lhs_type == valueStruct && rhs_type == valueStruct) {
        auto* ValuePtr = get_value_ptr(context);
        auto* VoidTy   = llvm::Type::getVoidTy(context.get_context());
        auto* lhs_alloca = context.create_alloca(valueStruct, "arith_lhs");
        auto* rhs_alloca = context.create_alloca(valueStruct, "arith_rhs");
        auto* out_alloca = context.create_alloca(valueStruct, "arith_out");
        builder.CreateStore(lhs, lhs_alloca);
        builder.CreateStore(rhs, rhs_alloca);
        const char* fn_name = nullptr;
        if (op == "+")  fn_name = "nv_value_add";
        else if (op == "-")  fn_name = "nv_value_sub";
        else if (op == "*")  fn_name = "nv_value_mul";
        else if (op == "/")  fn_name = "nv_value_div";
        else if (op == "%")  fn_name = "nv_value_mod";
        if (fn_name) {
            auto* fn = context.ensure_runtime_func(fn_name, {ValuePtr, ValuePtr, ValuePtr}, VoidTy);
            builder.CreateCall(fn, {out_alloca, lhs_alloca, rhs_alloca});
            return builder.CreateLoad(valueStruct, out_alloca, "arith_val");
        }
        // Divisão inteira: extrair ints, SDiv, empacotar como int
        if (op == "//") {
            auto* li = extract_int_from_value(context, lhs);
            auto* ri = extract_int_from_value(context, rhs);
            auto* qv = builder.CreateSDiv(li, ri, "idiv");
            auto* out_alloca = context.create_alloca(valueStruct, "idiv_out");
            auto* ValuePtr = get_value_ptr(context);
            auto* I32 = get_i32(context);
            auto* VoidTy = llvm::Type::getVoidTy(context.get_context());
            auto* ci = context.ensure_runtime_func("create_int", {ValuePtr, I32}, VoidTy);
            auto* iv = qv->getType()->isIntegerTy(32) ? qv : builder.CreateSExtOrTrunc(qv, I32);
            builder.CreateCall(ci, {out_alloca, iv});
            return builder.CreateLoad(valueStruct, out_alloca, "idiv_val");
        }
        // Operações bitwise: extrair ints, operar, empacotar de volta em Value
        if (op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>") {
            auto* li = extract_int_from_value(context, lhs);
            auto* ri = extract_int_from_value(context, rhs);
            llvm::Value* bitres = nullptr;
            if (op == "&")  bitres = builder.CreateAnd(li, ri, "band");
            else if (op == "|")  bitres = builder.CreateOr(li, ri, "bor");
            else if (op == "^")  bitres = builder.CreateXor(li, ri, "bxor");
            else if (op == "<<") bitres = builder.CreateShl(li, builder.CreateZExtOrTrunc(ri, li->getType()), "shl");
            else if (op == ">>") bitres = builder.CreateAShr(li, builder.CreateZExtOrTrunc(ri, li->getType()), "shr");
            auto* out_alloca = context.create_alloca(valueStruct, "bitop_out");
            auto* ValuePtr = get_value_ptr(context);
            auto* I32 = get_i32(context);
            auto* VoidTy = llvm::Type::getVoidTy(context.get_context());
            auto* ci = context.ensure_runtime_func("create_int", {ValuePtr, I32}, VoidTy);
            auto* iv = bitres->getType()->isIntegerTy(32) ? bitres : builder.CreateSExtOrTrunc(bitres, I32);
            builder.CreateCall(ci, {out_alloca, iv});
            return builder.CreateLoad(valueStruct, out_alloca, "bitop_val");
        }
        // Tratamento para potência: extrair floats e aplicar llvm.pow.f64, depois empacotar
        if (op == "**") {
            auto* lf = extract_float_from_value(context, lhs);
            auto* rf = extract_float_from_value(context, rhs);
            auto* f64 = get_f64(context);
            auto* powDecl = llvm::cast<llvm::Function>(context.get_module().getOrInsertFunction(
                "llvm.pow.f64", llvm::FunctionType::get(f64, { f64, f64 }, false)
            ).getCallee());
            std::vector<llvm::Value*> powArgs = { lf, rf };
            auto* outf = builder.CreateCall(powDecl, powArgs, "powf");
            auto* out_alloca = context.create_alloca(valueStruct, "pow_out");
            auto* cf = context.ensure_runtime_func("create_float", {ValuePtr, f64});
            std::vector<llvm::Value*> createArgs = { out_alloca, outf };
            builder.CreateCall(cf, createArgs);
            return builder.CreateLoad(valueStruct, out_alloca, "pow_val");
        }
        return nullptr;
    }
    if (lhs_type == valueStruct) {
        // Se o outro operando é numérico (int ou float), extrair float por segurança;
        // caso contrário, extrair int
        if (rhs_type->isFloatingPointTy() || rhs_type->isIntegerTy()) lhs = extract_float_from_value(context, lhs);
        else lhs = extract_int_from_value(context, lhs);
    } else if (rhs_type == valueStruct) {
        if (lhs_type->isFloatingPointTy() || lhs_type->isIntegerTy()) rhs = extract_float_from_value(context, rhs);
        else rhs = extract_int_from_value(context, rhs);
    }
    
    // Atualizar tipos após extração
    lhs_type = lhs->getType();
    rhs_type = rhs->getType();

    // Converter tipos diferentes antes das operações
    if (lhs_type != rhs_type) {
        if (lhs_type->isIntegerTy() && rhs_type->isFloatingPointTy()) {
            lhs = builder.CreateSIToFP(lhs, rhs_type);
            lhs_type = rhs_type;
        } else if (lhs_type->isFloatingPointTy() && rhs_type->isIntegerTy()) {
            rhs = builder.CreateSIToFP(rhs, lhs_type);
            rhs_type = lhs_type;
        }
    }

    // Verificar se algum operando é string após extração (não deveria acontecer para operações aritméticas)
    if (op == "+" && (lhs_type->isPointerTy() || rhs_type->isPointerTy())) {
        // Se chegamos aqui com ponteiros, é um caso não suportado
        // TODO: Gerar erro de tipo em tempo de execução
        return nullptr;
    }

    // Operações aritméticas padrão para tipos numéricos
    if (op == "+") return lhs_type->isFloatingPointTy() ? builder.CreateFAdd(lhs, rhs, "add") : builder.CreateAdd(lhs, rhs, "add");
    if (op == "-") return lhs_type->isFloatingPointTy() ? builder.CreateFSub(lhs, rhs, "sub") : builder.CreateSub(lhs, rhs, "sub");
    if (op == "*") return lhs_type->isFloatingPointTy() ? builder.CreateFMul(lhs, rhs, "mul") : builder.CreateMul(lhs, rhs, "mul");
    if (op == "/") return lhs_type->isFloatingPointTy() ? builder.CreateFDiv(lhs, rhs, "div") : builder.CreateSDiv(lhs, rhs, "div");
    if (op == "**") {
        auto* f64 = get_f64(context);
        // Garantir que ambos são f64
        if (!lhs->getType()->isFloatingPointTy()) lhs = builder.CreateSIToFP(lhs, f64);
        if (!rhs->getType()->isFloatingPointTy()) rhs = builder.CreateSIToFP(rhs, f64);
        auto* powDecl = llvm::cast<llvm::Function>(context.get_module().getOrInsertFunction(
            "llvm.pow.f64", llvm::FunctionType::get(f64, { f64, f64 }, false)
        ).getCallee());
        std::vector<llvm::Value*> powArgs2 = { lhs, rhs };
        return builder.CreateCall(powDecl, powArgs2, "pow");
    }
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

    if (lhs_type->isIntegerTy() && rhs_type->isIntegerTy()) {
        auto* rhs_cast = rhs->getType() == lhs_type ? rhs : builder.CreateZExtOrTrunc(rhs, lhs_type);
        if (op == "&")  return builder.CreateAnd(lhs, rhs_cast, "band");
        if (op == "|")  return builder.CreateOr(lhs, rhs_cast, "bor");
        if (op == "^")  return builder.CreateXor(lhs, rhs_cast, "bxor");
        if (op == "<<") return builder.CreateShl(lhs, rhs_cast, "shl");
        if (op == ">>") return builder.CreateAShr(lhs, rhs_cast, "shr");
    }

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
// O tipo do nó é obtido via checker ou context.nv_type_to_llvm()

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

llvm::Function* create_function(IRGenerationContext& context, const std::string& name, std::unique_ptr<Def>& def_type) {
    if (!def_type) return nullptr;
    std::vector<llvm::Type*> param_types;
    for (const auto& p : def_type->paramstype)
        param_types.push_back(context.nv_type_to_llvm(p));
    auto* ret_type = context.nv_type_to_llvm(def_type->returntype);
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
        auto* current_func = context.get_current_function();
        if (current_func && !current_func->getReturnType()->isVoidTy()) {
            return B.CreateRet(llvm::UndefValue::get(current_func->getReturnType()));
        }
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
llvm::Type* get_i8_ptr(IRGenerationContext& ctx) { 
    // Para strings, queremos um tipo específico que possa ser identificado
    static llvm::Type* string_type = nullptr;
    if (!string_type) {
        string_type = llvm::PointerType::getUnqual(ctx.get_context());
        // Dar um nome ao tipo para facilitar debug
        if (auto* ptr = llvm::dyn_cast<llvm::PointerType>(string_type)) {
            // Não podemos nomear ponteiros diretamente, mas podemos usar um tipo identificado
        }
    }
    return string_type;
}

llvm::StructType* get_value_struct(IRGenerationContext& ctx) {
    auto* t = llvm::StructType::getTypeByName(ctx.get_context(), "nv.rt.Value.v2");
    if (!t) {
        // Value struct: { NvObject* obj } - corresponde à estrutura C
        t = llvm::StructType::create(
            ctx.get_context(), 
            { get_i8_ptr(ctx) }, 
            "nv.rt.Value.v2"
        );
    }
    return t;
}
llvm::PointerType* get_value_ptr(IRGenerationContext& ctx) { return llvm::PointerType::getUnqual(ctx.get_context()); }

// Cria uma constante Value com a tag de tipo correta para inicialização de GlobalVariables
// Isso permite que a tag "viaje" entre arquivos através de constantes LLVM
llvm::Constant* create_value_constant_with_tag(IRGenerationContext& ctx, int32_t tag, int64_t value) {
    auto* ValueTy = get_value_struct(ctx);
    auto* I32 = get_i32(ctx);
    auto* I64 = get_i64(ctx);
    auto* I8Ptr = get_i8_ptr(ctx);
    
    // Criar constantes para cada campo da struct Value
    // Value struct: { int32_t type; int64_t value; void* prototype; void* type_info; uint32_t flags; }
    auto* tag_const = llvm::ConstantInt::get(I32, tag);
    auto* value_const = llvm::ConstantInt::get(I64, value);
    auto* prototype_const = llvm::Constant::getNullValue(I8Ptr);  // NULL para tipos primitivos
    auto* type_info_const = llvm::Constant::getNullValue(I8Ptr);  // NULL para tipos builtin
    auto* flags_const = llvm::ConstantInt::get(I32, 0);  // Sem flags
    
    // Criar a constante struct com todos os campos
    return llvm::ConstantStruct::get(
        llvm::cast<llvm::StructType>(ValueTy),
        {tag_const, value_const, prototype_const, type_info_const, flags_const}
    );
}

// Cria uma constante Value genérica baseada no tipo inferido e valor LLVM
// Retorna nullptr se não for possível criar uma constante (deve usar runtime init)
llvm::Constant* create_value_constant_from_llvm_value(
    IRGenerationContext& ctx,
    llvm::Value* value,
    std::shared_ptr<Type> nv_type
) {
    if (!value) return nullptr;
    
    auto* ValueTy = get_value_struct(ctx);
    
    // Se já é uma constante Value, retornar diretamente
    if (value->getType() == ValueTy && llvm::isa<llvm::Constant>(value)) {
        return llvm::cast<llvm::Constant>(value);
    }
    
    // Para Value v2 (NvObject*), não podemos criar constantes úteis para strings/objetos
    // porque precisamos de alocação runtime. Retornar nullptr força runtime init.
    // Apenas para inteiros/floats/bools puros podemos criar uma constante null (será inicializada depois).
    return nullptr;
}

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
    if (s.substr(p, 6) == "str") {
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

void emit_push_frame(IRGenerationContext& ctx, const std::string& file, const std::string& func) {
    auto& B   = ctx.get_builder();
    auto* I8P = llvm::PointerType::getUnqual(ctx.get_context());
    auto* VoidTy = llvm::Type::getVoidTy(ctx.get_context());
    auto* fn  = ctx.ensure_runtime_func("nv_push_frame", {I8P, I8P}, VoidTy);
    auto* fs  = B.CreateGlobalString(file, "frame.file");
    auto* fn2 = B.CreateGlobalString(func, "frame.func");
    B.CreateCall(fn, {fs, fn2});
}

void emit_pop_frame(IRGenerationContext& ctx) {
    auto* VoidTy = llvm::Type::getVoidTy(ctx.get_context());
    auto* fn = ctx.ensure_runtime_func("nv_pop_frame", {}, VoidTy);
    ctx.get_builder().CreateCall(fn, {});
}

void emit_set_line(IRGenerationContext& ctx, int line) {
    auto* I32    = llvm::Type::getInt32Ty(ctx.get_context());
    auto* VoidTy = llvm::Type::getVoidTy(ctx.get_context());
    auto* fn = ctx.ensure_runtime_func("nv_set_line", {I32}, VoidTy);
    ctx.get_builder().CreateCall(fn, {llvm::ConstantInt::get(I32, line)});
}

} // namespace ir_utils
} // namespace nv