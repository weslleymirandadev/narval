#pragma once
#include "../types.hpp"
#include <unordered_map>
#include <string>
#include <memory>

class ParamNode : public Expr {
public:
    std::unordered_map<std::string, std::string> parameter;
    std::unique_ptr<Expr> default_value;  // Optional default value expression
    // `comptime N: int` — the argument must be known at compile time; the
    // ComptimeEvaluator folds the call-site argument to a literal.
    bool is_comptime = false;

    ParamNode(std::unordered_map<std::string, std::string> param)
        : Expr(NodeType::Parameter), parameter(std::move(param)) {}

    ParamNode(std::unordered_map<std::string, std::string> param, std::unique_ptr<Expr> default_val)
        : Expr(NodeType::Parameter), parameter(std::move(param)), default_value(std::move(default_val)) {}

    ParamNode(const ParamNode& other)
        : Expr(NodeType::Parameter), parameter(other.parameter),
          is_comptime(other.is_comptime) {
        if (other.default_value) {
            default_value.reset(static_cast<Expr*>(other.default_value->clone()));
        }
    }
        
    ~ParamNode() override = default;
    
    ParamNode& operator=(const ParamNode& other) {
        if (this != &other) {
            parameter = other.parameter;
            is_comptime = other.is_comptime;
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

};

