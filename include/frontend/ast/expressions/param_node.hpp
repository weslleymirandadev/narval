#pragma once
#include "../types.hpp"
#include <unordered_map>
#include <string>
#include <memory>

class ParamNode : public Expr {
public:
    std::unordered_map<std::string, std::string> parameter;
    std::unique_ptr<Expr> default_value;  // Optional default value expression

    ParamNode(std::unordered_map<std::string, std::string> param)
        : Expr(NodeType::Parameter), parameter(std::move(param)) {}

    ParamNode(std::unordered_map<std::string, std::string> param, std::unique_ptr<Expr> default_val)
        : Expr(NodeType::Parameter), parameter(std::move(param)), default_value(std::move(default_val)) {}

    ParamNode(const ParamNode& other)
        : Expr(NodeType::Parameter), parameter(other.parameter) {
        if (other.default_value) {
            default_value.reset(static_cast<Expr*>(other.default_value->clone()));
        }
    }
        
    ~ParamNode() override = default;
    
    ParamNode& operator=(const ParamNode& other) {
        if (this != &other) {
            parameter = other.parameter;
            if (other.default_value) {
                default_value.reset(static_cast<Expr*>(other.default_value->clone()));
            } else {
                default_value.reset();
            }
        }
        return *this;
    }

    Node* clone() const override {
        auto* node = new ParamNode(*this);
        if (position) {
            node->position = std::make_unique<PositionData>(*position);
        }
        return node;
    }

    void codegen(nv::IRGenerationContext& ctx) override;
};

