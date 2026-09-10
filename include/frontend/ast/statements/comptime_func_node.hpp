#pragma once
#include "../types.hpp"
#include "../expressions/param_node.hpp"
#include <memory>
#include <string>
#include <vector>

// comptime def name(params): ret { ... }
// Registered in the ComptimeEvaluator; removed from the AST (no IR emitted).
class ComptimeFuncNode : public Stmt {
public:
    std::string name;
    std::vector<ParamNode> parameters;
    std::string return_type;
    CodeBlock body;

    ComptimeFuncNode(std::string name, std::vector<ParamNode> parameters,
                     std::string return_type, CodeBlock body)
        : Stmt(NodeType::ComptimeFuncDef),
          name(std::move(name)), parameters(std::move(parameters)),
          return_type(std::move(return_type)), body(std::move(body)) {}

    ~ComptimeFuncNode() override = default;

    Node* clone() const override {
        std::vector<ParamNode> params(parameters);
        CodeBlock cloned_body;
        for (const auto& stmt : body)
            cloned_body.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt->clone())));
        auto* node = new ComptimeFuncNode(name, std::move(params), return_type, std::move(cloned_body));
        if (position) node->position = std::make_unique<PositionData>(*position);
        return node;
    }
};
