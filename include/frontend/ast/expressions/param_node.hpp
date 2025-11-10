#pragma once
#include "../types.hpp"
#include <unordered_map>
#include <string>

class ParamNode : public Expr {
public:
    std::unordered_map<std::string, std::string> parameter;

    ParamNode(std::unordered_map<std::string, std::string> param)
        : Expr(NodeType::Parameter), parameter(std::move(param)) {}

    ParamNode(const ParamNode& other)
        : Expr(NodeType::Parameter), parameter(other.parameter) {}
        
    ~ParamNode() override = default;
    
    ParamNode& operator=(const ParamNode& other) {
        if (this != &other) {
            parameter = other.parameter;
        }
        return *this;
    }

    Node* clone() const override {
        return new ParamNode(*this);
    }

    void codegen(rph::IRGenerationContext& ctx) override;
};

