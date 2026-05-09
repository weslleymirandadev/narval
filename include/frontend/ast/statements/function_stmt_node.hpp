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
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};

