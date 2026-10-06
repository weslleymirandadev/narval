#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/class_stmt_node.hpp"
#include "frontend/ast/statements/function_stmt_node.hpp"
#include "mlir/Dialect/Func/IR/FuncOps.h"

// Emit one class method.
//
// The method is emitted as a `narval.func` (mirroring FunctionStmtNode) rather
// than a raw func.func: narval.func goes through LowerNarvalFunctionsPass, which
// converts the signature to the runtime pointer ABI exactly like a plain `def`.
// Emitting func.func directly baked !narval.value into the ABI and left
// unresolved materializations at the call site.
//
//   constructor ("new") -> __ctor_<Class>(self, p0, ...) -> void
//   other methods       -> __method_<Class>_<name>(self, p0, ...) -> <ret>
// Symbol name, signature and whether the method returns nothing. The up-front declaration
// pass and the body emission both go through this, so they cannot disagree.
struct ClassMethodProto {
    std::string        symbol;
    mlir::FunctionType type;
    bool               is_void_ret = false;
};

static ClassMethodProto class_method_proto(nv::NIRGenerationContext& ctx,
                                           const std::string& class_name,
                                           const std::string& method_name,
                                           FunctionStmtNode* fn_node)
{
    auto& mlir_ctx = ctx.get_mlir_context();
    auto  vt       = ctx.get_narval_value_type();

    ClassMethodProto proto;
    bool is_ctor = (method_name == "new");
    proto.symbol = is_ctor ? "__ctor_" + class_name
                           : "__method_" + class_name + "_" + method_name;

    // self + declared params, all !narval.value at this stage.
    size_t nparams = 1;
    for (const auto& p : fn_node->parameters)
        nparams += p.parameter.size();

    std::vector<mlir::Type> param_types(nparams, vt);
    proto.is_void_ret = is_ctor
        || (fn_node->return_type == "None"
            || fn_node->return_type == "void"
            || fn_node->return_type.empty());

    // Never build a TypeRange from a braced initializer list: it stores an
    // ArrayRef into the temporary backing array, so the FunctionType would be
    // uniqued with a dangling (garbage) result type.
    std::vector<mlir::Type> ret_types;
    if (!proto.is_void_ret) ret_types.push_back(vt);
    proto.type = mlir::FunctionType::get(&mlir_ctx, param_types, ret_types);
    return proto;
}

// Declare every method of the class before any body is emitted: a narval.func with no body
// reserves the symbol. Without this, a method calling a sibling declared LOWER in the class
// hit "method 'x' of class 'Y' has no body to call" and silently produced 0, because the
// symbol only springs into existence when that sibling's body is emitted (source order).
static void nir_declare_class_methods(nv::NIRGenerationContext& ctx,
                                      const std::string& class_name,
                                      const std::vector<std::unique_ptr<ClassMethodNode>>& methods)
{
    auto& b = ctx.get_builder();

    for (const auto& method : methods) {
        if (!method || !method->method_def) continue;
        if (method->method_def->kind != NodeType::FunctionStatement) continue;
        auto* fn_node = static_cast<FunctionStmtNode*>(method->method_def.get());
        auto proto = class_method_proto(ctx, class_name, method->name, fn_node);
        if (ctx.get_module().lookupSymbol(proto.symbol)) continue;

        mlir::OpBuilder::InsertionGuard guard(b);
        b.setInsertionPointToEnd(ctx.get_module().getBody());
        mlir::narval::FuncOp::create(b, ctx.loc(fn_node->position.get()), proto.symbol,
                                     proto.type, "", false, false, false, nullptr);
    }
}

// `new A(...)` emite a chamada do ctor so' quando o simbolo ja' existe no modulo, e `obj.method()`
// e' mangled a partir dos nomes registrados: as duas decisoes sao tomadas enquanto um CORPO esta
// sendo emitido. Com a classe declarada DEPOIS de uma funcao que a usa, os simbolos ainda nao tinham
// sido reservados — entao `new A(5)` nao emitia chamada nenhuma (o argumento ficava como constante
// orfa no IR) e o campo nunca era escrito: `get()` devolvia None, em silencio. Declarar toda classe
// antes de qualquer corpo resolve para todos os corpos de uma vez. Mesma forma da declaracao
// dentro da classe, um nivel acima.
void nir_predeclare_class(nv::NIRGenerationContext& ctx, const ClassStmtNode& node) {
    // SO' os simbolos. O registro dos NOMES de metodo fica preguiçoso, como estava, de proposito:
    // `nir_call_expr` resolve o dono pelo tipo estatico do receptor (resolved_owner, preenchido pelo
    // checker) e usa `find_method_owner` apenas como fallback — um fallback que, nas palavras do
    // proprio codigo, "nao consegue distinguir duas classes com o mesmo metodo". Registrar todas as
    // classes de antemao deixava esse fallback ambiguo (`find_method_owner` devolve "" quando dois
    // nomes empatam) e quebrou `net_frame` e `net_packet` na suite: medido, 185/187. O que o `new`
    // consulta e' o SIMBOLO (`lookupSymbol("__ctor_A")`), entao declarar os simbolos basta.
    nir_declare_class_methods(ctx, node.name, node.methods);
}

static void nir_emit_class_method(nv::NIRGenerationContext& ctx,
                                   const std::string& class_name,
                                   const std::string& method_name,
                                   FunctionStmtNode* fn_node)
{
    auto& b = ctx.get_builder();

    mlir::OpBuilder::InsertionGuard guard(b);

    auto vt = ctx.get_narval_value_type();
    auto proto = class_method_proto(ctx, class_name, method_name, fn_node);
    const std::string fn_name = proto.symbol;
    auto fn_type     = proto.type;
    bool is_void_ret = proto.is_void_ret;

    if (mlir::Operation* existing = ctx.get_module().lookupSymbol(fn_name)) {
        // The up-front declaration: erase it so the definition can take the name (creating a
        // symbol that already exists is a redefinition). A func that already has a body is
        // the real thing — nothing left to do.
        auto old = mlir::dyn_cast<mlir::narval::FuncOp>(existing);
        if (!old || !old.getBody().empty()) return;
        old.erase();
    }

    auto loc = ctx.loc(fn_node->position.get());

    // func-like ops must live in the module body (the symbol table op). Creating
    // it at the caller's insertion point nests it inside whichever function is
    // being emitted and corrupts the module symbol table (segfault on the next
    // lookupSymbol).
    b.setInsertionPointToEnd(ctx.get_module().getBody());
    auto fn = mlir::narval::FuncOp::create(b, loc, fn_name, fn_type, "",
                                           false, false, false, nullptr);

    auto* entry = new mlir::Block();
    fn.getBody().push_back(entry);
    llvm::SmallVector<mlir::Location> arg_locs(fn_type.getNumInputs(), loc);
    entry->addArguments(fn_type.getInputs(), arg_locs);
    b.setInsertionPointToStart(entry);

    ctx.push_scope();

    // Bind self.
    ctx.define("__this", entry->getArgument(0));
    ctx.define("self",   entry->getArgument(0));

    // Bind declared parameters.
    size_t arg_idx = 1;
    for (const auto& param : fn_node->parameters) {
        for (const auto& [pname, _] : param.parameter) {
            if (arg_idx < entry->getNumArguments())
                ctx.define(pname, entry->getArgument(arg_idx));
            ++arg_idx;
        }
    }

    nir_emit_body(fn_node->body, ctx);

    if (entry->empty() || !entry->back().hasTrait<mlir::OpTrait::IsTerminator>()) {
        if (is_void_ret) {
            mlir::narval::ReturnOp::create(b, loc, mlir::ValueRange{});
        } else {
            auto zero = mlir::narval::ConstantOp::create(b, loc, vt,
                b.getI64IntegerAttr(0)).getResult();
            mlir::narval::ReturnOp::create(b, loc, mlir::ValueRange{zero});
        }
    }

    ctx.pop_scope();
}

void ClassStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    // Hand the class hierarchy to the runtime, for `instanceof`. An instance carries only its own
    // class name, so the runtime cannot see inheritance — `b instanceof A` answered false for
    // `class B extends A`. The call is emitted where the class is declared, so it runs before any
    // use. Interfaces count as parents: `implements I` means the instance IS an I.
    {
        auto  loc = ctx.loc(position.get());
        auto  vt  = ctx.get_narval_value_type();
        auto& b   = ctx.get_builder();
        auto  name_const = [&](const std::string& s) {
            return mlir::narval::ConstantOp::create(
                b, loc, vt, mlir::StringAttr::get(&ctx.get_mlir_context(), s)).getResult();
        };
        auto declare_parent = [&](const std::string& parent) {
            if (parent.empty()) return;
            nir_call_runtime(ctx, loc, "nv_register_class",
                             {name_const(name), name_const(parent)}, {vt});
        };
        declare_parent(parent_class);
        for (const auto& iface : implements_interfaces) declare_parent(iface);
    }

    // Register the method names first so later call sites can resolve
    // `obj.method()` to the mangled symbol.
    std::vector<std::string> method_names;
    for (const auto& m : methods)
        if (m && m->name != "new") method_names.push_back(m->name);
    ctx.register_class_methods(name, method_names);

    nir_declare_class_methods(ctx, name, methods);

    for (const auto& method : methods) {
        if (!method || !method->method_def) continue;
        if (method->method_def->kind != NodeType::FunctionStatement) continue;
        nir_emit_class_method(ctx, name, method->name,
                              static_cast<FunctionStmtNode*>(method->method_def.get()));
    }
}
