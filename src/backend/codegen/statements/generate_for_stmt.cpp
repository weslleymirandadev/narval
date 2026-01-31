#include "frontend/ast/statements/for_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"

void ForStmtNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());

    llvm::DIBuilder* dib = ctx.get_debug_builder();
    llvm::DIFile* dif = ctx.get_debug_file();
    llvm::DIScope* old_scope = ctx.get_debug_scope();
    llvm::DIScope* for_scope = old_scope;

    if (dib && dif && position) {
        for_scope = dib->createLexicalBlock(
            old_scope ? old_scope : static_cast<llvm::DIScope*>(dif),
            dif,
            static_cast<unsigned>(position->line),
            static_cast<unsigned>(position->col[0] + 1)
        );
        ctx.set_debug_scope(for_scope);
    }
    
    auto& b = ctx.get_builder();
    auto* func = ctx.get_current_function();
    if (!func) throw std::runtime_error("for statement outside of function");
    // Executed flag: usado para else-block tanto em range quanto em iterable
    auto* executed = ctx.create_and_register_variable("__for_executed", llvm::Type::getInt1Ty(ctx.get_context()), nullptr, false);
    b.CreateStore(llvm::ConstantInt::getFalse(ctx.get_context()), executed);

    // Blocos comuns
    auto* after_bb  = llvm::BasicBlock::Create(ctx.get_context(), "for.after",  func);
    auto* else_bb   = else_block.empty() ? nullptr : llvm::BasicBlock::Create(ctx.get_context(), "for.else", func);

    // Caso 1: Iterable nativo (array view: { i32 len, i32* data })
    if (iterable) {
        iterable->codegen(ctx);
        auto* iter_val = ctx.pop_value();
        if (!iter_val) {
            throw std::runtime_error("unsupported iterable: null value");
        }

        
        auto& llctx = ctx.get_context();
        auto* i32 = llvm::Type::getInt32Ty(llctx);
        llvm::Value* len = nullptr;
        llvm::Type* elemTy = nullptr;
        enum class IterKind { View, Vector, Array, String, Count, RTArray };
        IterKind kind;

        llvm::Value* data_ptr_val = nullptr;

        auto* ty = iter_val->getType();
        // If we got a pointer to runtime Value, load it
        if (ty->isPointerTy()) {
            auto* valuePrphy = nv::ir_utils::get_value_ptr(ctx);
            if (ty == valuePrphy) {
                iter_val = b.CreateLoad(nv::ir_utils::get_value_struct(ctx), iter_val);
                ty = iter_val->getType();
            }
        }
        // Integer iterable: for i : 10 => i in [0,10)
        if (ty->isIntegerTy()) {
            kind = IterKind::Count;
            auto* i32 = llvm::Type::getInt32Ty(llctx);
            if (ty != i32) iter_val = nv::ir_utils::promote_type(ctx, iter_val, i32);
            len = iter_val;
            elemTy = i32;
        } else
        if (ty->isStructTy() && llvm::cast<llvm::StructType>(ty)->hasName() && llvm::cast<llvm::StructType>(ty)->getName() == "nv.array.view") {
            auto* s = llvm::cast<llvm::StructType>(ty);
            kind = IterKind::View;
            auto* viewAlloca = ctx.create_alloca(s, "iter.view");
            b.CreateStore(iter_val, viewAlloca);
            auto* lenPtr  = b.CreateStructGEP(s, viewAlloca, 0);
            auto* dataPtr = b.CreateStructGEP(s, viewAlloca, 1);
            len = b.CreateLoad(i32, lenPtr);
            auto* i32ptr = llvm::PointerType::getUnqual(i32);
            data_ptr_val = b.CreateLoad(i32ptr, dataPtr);
            elemTy = i32;
        } else if (ty->isPointerTy()) {
            auto* pty = llvm::cast<llvm::PointerType>(ty);
            // Opaque-vs-non-opaque pointer handling
            // First, handle i8* (string)
            if (ty == llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(llctx))) {
                kind = IterKind::String;
                auto* strlenTy = llvm::FunctionType::get(llvm::Type::getInt64Ty(llctx), { llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(llctx)) }, false);
                auto* strlenFn = llvm::cast<llvm::Function>(ctx.get_module().getOrInsertFunction("strlen", strlenTy).getCallee());
                auto* l64 = b.CreateCall(strlenFn, { iter_val });
                len = b.CreateTrunc(l64, i32);
                elemTy = llvm::Type::getInt8Ty(llctx);
                data_ptr_val = iter_val;
            } else {
                throw std::runtime_error("unsupported pointer iterable type in this LLVM build. Supported: i8* (string). For vectors, iterate a view or array by value.");
            }
        } else if (ty->isArrayTy()) {
            // Treat fixed LLVM array as a view of elements. For now, support string arrays (element is i8*).
            auto* arr = llvm::cast<llvm::ArrayType>(ty);
            kind = IterKind::Array;
            len = llvm::ConstantInt::get(i32, (int)arr->getNumElements());
            // Alloca to take pointer to first element
            auto* arrAlloca = ctx.create_alloca(ty, "iter.arr");
            b.CreateStore(iter_val, arrAlloca);
            auto* zero = llvm::ConstantInt::get(i32, 0);
            std::vector<llvm::Value*> idx = { zero, zero };
            auto* firstElPtr = b.CreateGEP(ty, arrAlloca, idx);
            // Assume array of strings: cast to i8** and iterate
            auto* i8  = llvm::Type::getInt8Ty(llctx);
            auto* i8p = llvm::PointerType::getUnqual(i8);
            auto* i8pp= llvm::PointerType::getUnqual(i8p);
            data_ptr_val = b.CreateBitCast(firstElPtr, i8pp);
            elemTy = i8p;
        } else if (ty->isStructTy()) {
            // Accept a generic array view struct passed by value: { i32 len, ptr data }
            auto* s = llvm::cast<llvm::StructType>(ty);
            // First, handle runtime Value arrays: nv.rt.Value with TAG_ARRAY
            auto* valueStruct = nv::ir_utils::get_value_struct(ctx);
            auto* i64 = llvm::Type::getInt64Ty(llctx);
            // Default structs (that are not named nv.array.view handled above) are treated as runtime Value containers
            if ((s == valueStruct) || (s->hasName() && s->getName() == "nv.rt.Value")) {
                // Treat as runtime Value and iterate its array payload
                kind = IterKind::RTArray;
                // Alloca with the actual struct type 's'
                auto* valAllocaUntyped = ctx.create_alloca(s, "iter.rtarray");
                b.CreateStore(iter_val, valAllocaUntyped);
                // Bitcast to Value* for runtime helpers
                auto* vprphy = nv::ir_utils::get_value_ptr(ctx);
                auto* valAlloca = b.CreateBitCast(valAllocaUntyped, vprphy);
                // Load field 1 (integral) as pointer-sized integer then inttoptr to Array*
                auto* f1Ptr = b.CreateStructGEP(s, valAllocaUntyped, 1);
                auto* f1Ty  = s->getElementType(1);
                auto* rawInt = b.CreateLoad(f1Ty, f1Ptr);
                llvm::Value* raw64 = rawInt;
                if (f1Ty != i64) raw64 = b.CreateZExt(rawInt, i64);
                auto* valPrphy = nv::ir_utils::get_value_ptr(ctx);
                auto* arrStruct = llvm::StructType::get(llctx, { valPrphy, i32, i32 });
                auto* arrPrphy = llvm::PointerType::getUnqual(arrStruct);
                auto* arrPtr = b.CreateIntToPtr(raw64, arrPrphy);
                // Read size from field 1 of Array
                auto* sizePtr = b.CreateStructGEP(arrStruct, arrPtr, 1);
                len = b.CreateLoad(i32, sizePtr);
                // Keep Value* pointer for element access via array_get_index_v
                data_ptr_val = valAlloca;
                elemTy = valueStruct;
            } else if ((s->hasName() && s->getName() == "nv.array.view") || ((s->getNumElements() >= 2) && s->getElementType(1)->isPointerTy())) {
                kind = IterKind::View;
                auto* viewAlloca = ctx.create_alloca(s, "iter.view");
                b.CreateStore(iter_val, viewAlloca);
                auto* lenPtr  = b.CreateStructGEP(s, viewAlloca, 0);
                auto* dataPtr = b.CreateStructGEP(s, viewAlloca, 1);
                // len may be i32 or i64; normalize to i32
                auto* lenTy = s->getElementType(0);
                auto* loadedLen = b.CreateLoad(lenTy, lenPtr);
                if (lenTy->isIntegerTy(32)) {
                    len = loadedLen;
                } else if (lenTy->isIntegerTy(64)) {
                    len = b.CreateTrunc(loadedLen, i32);
                } else {
                    throw std::runtime_error("unsupported iterable struct len field type");
                }
                // Load data pointer as declared then bitcast to i8** (string arrays/views)
                auto* dataFieldTy = s->getElementType(1);
                auto* rawData = b.CreateLoad(dataFieldTy, dataPtr);
                auto* i8  = llvm::Type::getInt8Ty(llctx);
                auto* i8p = llvm::PointerType::getUnqual(i8);
                auto* i8pp= llvm::PointerType::getUnqual(i8p);
                data_ptr_val = b.CreateBitCast(rawData, i8pp);
                elemTy = i8p;
            } else {
                throw std::runtime_error("unsupported iterable struct type");
            }
        } else {
            throw std::runtime_error("unsupported iterable type for for-statement");
        }

        
        IdentifierNode* indexId = nullptr;
        std::vector<IdentifierNode*> elemBindings;
        if (!bindings.empty()) {
            size_t start = 0;
            if (bindings.size() >= 2) {
                indexId = dynamic_cast<IdentifierNode*>(bindings[0].get());
                if (!indexId) throw std::runtime_error("for iterable index binding must be identifier");
                start = 1;
            }
            for (size_t k = start; k < bindings.size(); ++k) {
                auto* id = dynamic_cast<IdentifierNode*>(bindings[k].get());
                if (!id) throw std::runtime_error("for iterable element binding must be identifier");
                elemBindings.push_back(id);
            }
        }

        auto* header_bb = llvm::BasicBlock::Create(ctx.get_context(), "for.iter.header", func);
        auto* body_bb   = llvm::BasicBlock::Create(ctx.get_context(), "for.iter.body",   func);
        auto* step_bb   = llvm::BasicBlock::Create(ctx.get_context(), "for.iter.step",   func);
        auto* exit_bb   = llvm::BasicBlock::Create(ctx.get_context(), "for.iter.exit",   func);

        
        auto* i_alloca = ctx.create_and_register_variable(indexId ? indexId->symbol : std::string("__idx"), i32, nullptr, false);
        b.CreateStore(llvm::ConstantInt::get(i32, 0), i_alloca);
        b.CreateBr(header_bb);

        b.SetInsertPoint(header_bb);
        auto* i_val = b.CreateLoad(i32, i_alloca);
        auto* cond = b.CreateICmpSLT(i_val, len, "itcond");
        b.CreateCondBr(cond, body_bb, exit_bb);

        // Enter loop context for break/continue support
        ctx.get_control_flow().enter_loop("for.iterable", header_bb, body_bb, step_bb, exit_bb);
        
        b.SetInsertPoint(body_bb);
        
        llvm::Value* elemVal = nullptr;
        if (kind == IterKind::Count) {
            elemVal = i_val;
        } else if (kind == IterKind::RTArray) {
            // Use runtime helper to get element Value into a temp
            auto* valueStruct = nv::ir_utils::get_value_struct(ctx);
            auto* tmp = ctx.create_alloca(valueStruct, "el.val");
            // declare void @array_get_index_v(ptr, ptr, i32)
            auto* voidTy = llvm::Type::getVoidTy(llctx);
            auto* vptr   = llvm::PointerType::getUnqual(valueStruct);
            auto* fty = llvm::FunctionType::get(voidTy, { vptr, vptr, i32 }, false);
            auto calleeFc = ctx.get_module().getOrInsertFunction("array_get_index_v", fty);
            b.CreateCall(calleeFc, { tmp, data_ptr_val, i_val });
            elemTy = valueStruct;
            elemVal = b.CreateLoad(valueStruct, tmp);
        } else {
            llvm::Value* elemPtr = nullptr;
            if (kind == IterKind::String) {
                elemPtr = b.CreateInBoundsGEP(llvm::Type::getInt8Ty(llctx), data_ptr_val, i_val);
            } else {
                elemPtr = b.CreateInBoundsGEP(elemTy, data_ptr_val, i_val);
            }
            elemVal = b.CreateLoad(elemTy, elemPtr);
        }
        if (!elemBindings.empty()) {
            if (elemTy->isStructTy()) {
                auto* sEl = llvm::cast<llvm::StructType>(elemTy);
                if (elemBindings.size() == 1) {
                    auto* tmp = ctx.create_alloca(elemTy, "el.tmp");
                    b.CreateStore(elemVal, tmp);
                    auto sym = ctx.get_symbol_table().lookup_symbol(elemBindings[0]->symbol);
                    llvm::Value* dst = sym.has_value() ? sym->value : (llvm::Value*)ctx.create_and_register_variable(elemBindings[0]->symbol, elemTy, nullptr, false);
                    auto* loaded = b.CreateLoad(elemTy, tmp);
                    b.CreateStore(loaded, dst);
                } else {
                    auto* tmp = ctx.create_alloca(elemTy, "el.tup");
                    b.CreateStore(elemVal, tmp);
                    size_t fields = std::min<size_t>(elemBindings.size(), sEl->getNumElements());
                    for (size_t fi = 0; fi < fields; ++fi) {
                        auto* fieldPtr = b.CreateStructGEP(sEl, tmp, (unsigned)fi);
                        auto* fTy = sEl->getElementType((unsigned)fi);
                        auto* fVal = b.CreateLoad(fTy, fieldPtr);
                        auto sym = ctx.get_symbol_table().lookup_symbol(elemBindings[fi]->symbol);
                        llvm::Value* dst = sym.has_value() ? sym->value : (llvm::Value*)ctx.create_and_register_variable(elemBindings[fi]->symbol, fTy, nullptr, false);
                        b.CreateStore(fVal, dst);
                    }
                }
            } else {
                auto symElem = ctx.get_symbol_table().lookup_symbol(elemBindings[0]->symbol);
                llvm::Value* elemAlloca = symElem.has_value() ? symElem->value : (llvm::Value*)ctx.create_and_register_variable(elemBindings[0]->symbol, elemTy, nullptr, false);
                b.CreateStore(elemVal, elemAlloca);
            }
        }
        b.CreateStore(llvm::ConstantInt::getTrue(ctx.get_context()), executed);

        ctx.enter_scope();
        for (auto& stmt : body) {
            if (stmt) stmt->codegen(ctx);
        }
        ctx.exit_scope();
        if (!b.GetInsertBlock()->getTerminator()) {
            b.CreateBr(step_bb);
        }

        b.SetInsertPoint(step_bb);
        auto* one = llvm::ConstantInt::get(i32, 1);
        auto* next = b.CreateAdd(b.CreateLoad(i32, i_alloca), one, "inc");
        b.CreateStore(next, i_alloca);
        b.CreateBr(header_bb);

        b.SetInsertPoint(exit_bb);
        ctx.get_control_flow().exit_loop();
        if (else_bb) {
            auto* ran = b.CreateLoad(llvm::Type::getInt1Ty(ctx.get_context()), executed);
            b.CreateCondBr(ran, after_bb, else_bb);
            b.SetInsertPoint(else_bb);
            ctx.enter_scope();
            for (auto& stmt : else_block) {
                if (stmt) stmt->codegen(ctx);
            }
            ctx.exit_scope();
            b.CreateBr(after_bb);
        } else {
            b.CreateBr(after_bb);
        }

        b.SetInsertPoint(after_bb);
        return;
    }

    // Caso 2: Range (start..end, ..=end)
    if (!range_start || !range_end) {
        throw std::runtime_error("for statement requires range or iterable");
    }
    if (bindings.empty() || bindings.size() > 2) {
        throw std::runtime_error("for statement supports 1 or 2 bindings (index[, value])");
    }
    auto* id0 = dynamic_cast<IdentifierNode*>(bindings[0].get());
    if (!id0) throw std::runtime_error("for statement first binding must be identifier");
    IdentifierNode* id1 = nullptr;
    if (bindings.size() == 2) {
        id1 = dynamic_cast<IdentifierNode*>(bindings[1].get());
        if (!id1) throw std::runtime_error("for statement second binding must be identifier");
    }

    auto* i32 = llvm::Type::getInt32Ty(ctx.get_context());
    range_start->codegen(ctx);
    auto* start_v = ctx.pop_value();
    range_end->codegen(ctx);
    auto* end_v   = ctx.pop_value();
    if (!start_v || !end_v) throw std::runtime_error("for statement without start or end value");
    if (start_v->getType() != i32) start_v = nv::ir_utils::promote_type(ctx, start_v, i32);
    if (end_v->getType()   != i32) end_v   = nv::ir_utils::promote_type(ctx, end_v,   i32);

    auto* header_bb = llvm::BasicBlock::Create(ctx.get_context(), "for.header", func);
    auto* body_bb   = llvm::BasicBlock::Create(ctx.get_context(), "for.body",   func);
    auto* step_bb   = llvm::BasicBlock::Create(ctx.get_context(), "for.step",   func);
    auto* exit_bb   = llvm::BasicBlock::Create(ctx.get_context(), "for.exit",   func);

    // Enter loop context for break/continue support
    ctx.get_control_flow().enter_loop("for.range", header_bb, body_bb, step_bb, exit_bb);

    auto* i_alloca = ctx.create_and_register_variable(id0->symbol, i32, nullptr, false);
    b.CreateStore(start_v, i_alloca);
    b.CreateBr(header_bb);

    b.SetInsertPoint(header_bb);
    auto* i_val = b.CreateLoad(i32, i_alloca, id0->symbol + "_val");
    llvm::Value* cond = range_inclusive
        ? (llvm::Value*)b.CreateICmpSLE(i_val, end_v, "forcond")
        : (llvm::Value*)b.CreateICmpSLT(i_val, end_v, "forcond");
    b.CreateCondBr(cond, body_bb, exit_bb);

    b.SetInsertPoint(body_bb);
    if (id1) {
        auto* val_alloca = ctx.get_symbol_table().lookup_symbol(id1->symbol).has_value()
            ? ctx.get_symbol_table().lookup_symbol(id1->symbol)->value
            : (llvm::Value*)ctx.create_and_register_variable(id1->symbol, i32, nullptr, false);
        auto* cur = b.CreateLoad(i32, i_alloca);
        b.CreateStore(cur, val_alloca);
    }
    b.CreateStore(llvm::ConstantInt::getTrue(ctx.get_context()), executed);

    ctx.enter_scope();
    for (auto& stmt : body) {
        if (stmt) stmt->codegen(ctx);
    }
    ctx.exit_scope();
    if (!b.GetInsertBlock()->getTerminator()) {
        b.CreateBr(step_bb);
    }

    b.SetInsertPoint(step_bb);
    auto* one = llvm::ConstantInt::get(i32, 1);
    auto* next = b.CreateAdd(b.CreateLoad(i32, i_alloca), one, "inc");
    b.CreateStore(next, i_alloca);
    b.CreateBr(header_bb);

    b.SetInsertPoint(exit_bb);
    ctx.get_control_flow().exit_loop();
    if (else_bb) {
        auto* ran = b.CreateLoad(llvm::Type::getInt1Ty(ctx.get_context()), executed);
        b.CreateCondBr(ran, after_bb, else_bb);
        b.SetInsertPoint(else_bb);
        ctx.enter_scope();
        for (auto& stmt : else_block) {
            if (stmt) stmt->codegen(ctx);
        }
        ctx.exit_scope();
        b.CreateBr(after_bb);
    } else {
        b.CreateBr(after_bb);
    }

    if (dib && dif && position) {
        ctx.set_debug_scope(old_scope);
    }

    b.SetInsertPoint(after_bb);
}
