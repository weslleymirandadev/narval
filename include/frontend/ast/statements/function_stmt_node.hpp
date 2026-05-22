#pragma once
#include "../types.hpp"
#include "../expressions/param_node.hpp"
#include <string>
#include <vector>
#include <memory>

class FunctionStmtNode : public Stmt {
public:
    std::string name;
    std::vector<ParamNode> parameters;
    std::string return_type;
    CodeBlock body;
    // Inferido pelo checker: true quando o corpo contém `propagate`.
    // O codegen então emite retornos como Result::Ok e propagate como Result::Err.
    bool is_fallible = false;

    // ABI explícito via @[abi("sysv64")] — vazio = Narval padrão
    std::string abi;
    bool is_low_level = false; // true quando abi != ""

    // naked_asm def NAME: asm { return `...`; }
    bool is_naked_asm = false;
    std::string naked_asm_body;

    // async def NAME — body runs in a fiber, returns Future<T>
    bool is_async = false;

    // Generic type parameters: def foo<T, E>(...)
    std::vector<std::string> type_params;

    FunctionStmtNode(
        std::string function_name,
        std::vector<ParamNode> parameters,
        std::string ret_type,
        std::vector<std::unique_ptr<Stmt>> body_stmts
    )
        : Stmt(NodeType::FunctionStatement),
          name(std::move(function_name)),
          parameters(std::move(parameters)),
          return_type(std::move(ret_type)),
          body(std::move(body_stmts)) {}

    ~FunctionStmtNode() override = default;

    Node* clone() const override {
        std::vector<std::unique_ptr<Stmt>> cloned_body;
        for (const auto& stmt : body) {
            cloned_body.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
        }

        auto* node = new FunctionStmtNode(name, parameters, return_type, std::move(cloned_body));
        node->is_fallible   = is_fallible;
        node->abi           = abi;
        node->is_low_level  = is_low_level;
        node->is_naked_asm  = is_naked_asm;
        node->naked_asm_body = naked_asm_body;
        node->is_async      = is_async;
        node->type_params   = type_params;
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
#ifdef NARVAL_USE_NIR
    void nir_codegen(nv::NIRGenerationContext& ctx) override;
#endif
};

