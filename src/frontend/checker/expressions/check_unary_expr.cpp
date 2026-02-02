#include "frontend/checker/expressions/check_unary_expr.hpp"
#include "frontend/ast/expressions/logical_not_expr_node.hpp"
#include "frontend/ast/expressions/unary_minus_expr_node.hpp"
#include "frontend/ast/expressions/increment_expr_node.hpp"
#include "frontend/ast/expressions/decrement_expr_node.hpp"
#include "frontend/ast/expressions/post_increment_expr_node.hpp"
#include "frontend/ast/expressions/post_decrement_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/checker/unification.hpp"
#include <stdexcept>

std::shared_ptr<nv::Type>& check_unary_expr(nv::Checker* ch, Node* node) {
    static thread_local std::shared_ptr<nv::Type> temp_result;
    switch (node->kind) {
        case NodeType::LogicalNotExpression: {
            auto* not_expr = static_cast<LogicalNotExprNode*>(node);
            if (!not_expr->operand) {
                ch->error(node, "Logical not expression requires an operand");
                return ch->gettyptr("void");
            }
            
            auto operand_type = ch->infer_expr(not_expr->operand.get());
            operand_type = ch->unify_ctx.resolve(operand_type);
            
            // Verificar que o operando é bool
            try {
                ch->unify_ctx.unify(operand_type, ch->gettyptr("bool"));
            } catch (std::runtime_error& e) {
                ch->error(not_expr->operand.get(), 
                          "Logical not operand must be of type 'bool', but got '" + operand_type->toString() + "'");
                return ch->gettyptr("void");
            }
            
            return ch->gettyptr("bool");
        }
        
        case NodeType::UnaryMinusExpression: {
            auto* minus_expr = static_cast<UnaryMinusExprNode*>(node);
            if (!minus_expr->operand) {
                ch->error(node, "Unary minus expression requires an operand");
                return ch->gettyptr("void");
            }
            
            auto operand_type = ch->infer_expr(minus_expr->operand.get());
            operand_type = ch->unify_ctx.resolve(operand_type);
            
            // Verificar que o operando é numérico (int ou float)
            bool is_int = operand_type->kind == nv::Kind::INT;
            bool is_float = operand_type->kind == nv::Kind::FLOAT;
            
            if (!is_int && !is_float) {
                ch->error(minus_expr->operand.get(), 
                          "Unary minus operand must be numeric (int or float), but got '" + operand_type->toString() + "'");
                return ch->gettyptr("void");
            }
            
            temp_result = operand_type; // Retorna o mesmo tipo (int ou float)
            return temp_result;
        }
        
        case NodeType::IncrementExpression: {
            auto* inc_expr = static_cast<IncrementExprNode*>(node);
            if (!inc_expr->operand) {
                ch->error(node, "Pre-increment expression requires an operand");
                return ch->gettyptr("void");
            }
            
            // Verificar que é um lvalue (Identifier ou AccessExpression)
            if (inc_expr->operand->kind != NodeType::Identifier && 
                inc_expr->operand->kind != NodeType::AccessExpression) {
                ch->error(inc_expr->operand.get(), 
                          "Pre-increment operand must be a variable or array access");
                return ch->gettyptr("void");
            }
            
            // Verificar tipo numérico
            auto operand_type = ch->infer_expr(inc_expr->operand.get());
            operand_type = ch->unify_ctx.resolve(operand_type);
            
            bool is_int = operand_type->kind == nv::Kind::INT;
            bool is_float = operand_type->kind == nv::Kind::FLOAT;
            bool is_type_var = operand_type->kind == nv::Kind::TYPE_VAR;
            
            // Se for AccessExpression e TypeVar, tentar unificar com int ou float
            if (is_type_var && inc_expr->operand->kind == NodeType::AccessExpression) {
                try {
                    ch->unify_ctx.unify(operand_type, ch->gettyptr("int"));
                    operand_type = ch->gettyptr("int");
                    is_int = true;
                } catch (std::runtime_error&) {
                    try {
                        ch->unify_ctx.unify(operand_type, ch->gettyptr("float"));
                        operand_type = ch->gettyptr("float");
                        is_float = true;
                    } catch (std::runtime_error&) {
                        ch->error(inc_expr->operand.get(), 
                                  "Pre-increment operand must be numeric (int or float), but got '" + 
                                  operand_type->toString() + "'");
                        return ch->gettyptr("void");
                    }
                }
            }
            
            if (!is_int && !is_float) {
                ch->error(inc_expr->operand.get(), 
                          "Pre-increment operand must be numeric (int or float), but got '" + 
                          operand_type->toString() + "'");
                return ch->gettyptr("void");
            }
            
            temp_result = operand_type;
            return temp_result;
        }
        
        case NodeType::DecrementExpression: {
            auto* dec_expr = static_cast<DecrementExprNode*>(node);
            if (!dec_expr->operand) {
                ch->error(node, "Pre-decrement expression requires an operand");
                return ch->gettyptr("void");
            }
            
            // Verificar que é um lvalue (Identifier ou AccessExpression)
            if (dec_expr->operand->kind != NodeType::Identifier && 
                dec_expr->operand->kind != NodeType::AccessExpression) {
                ch->error(dec_expr->operand.get(), 
                          "Pre-decrement operand must be a variable or array access");
                return ch->gettyptr("void");
            }
            
            // Verificar tipo numérico
            auto operand_type = ch->infer_expr(dec_expr->operand.get());
            operand_type = ch->unify_ctx.resolve(operand_type);
            
            bool is_int = operand_type->kind == nv::Kind::INT;
            bool is_float = operand_type->kind == nv::Kind::FLOAT;
            bool is_type_var = operand_type->kind == nv::Kind::TYPE_VAR;
            
            // Se for AccessExpression e TypeVar, tentar unificar com int ou float
            if (is_type_var && dec_expr->operand->kind == NodeType::AccessExpression) {
                try {
                    ch->unify_ctx.unify(operand_type, ch->gettyptr("int"));
                    operand_type = ch->gettyptr("int");
                    is_int = true;
                } catch (std::runtime_error&) {
                    try {
                        ch->unify_ctx.unify(operand_type, ch->gettyptr("float"));
                        operand_type = ch->gettyptr("float");
                        is_float = true;
                    } catch (std::runtime_error&) {
                        ch->error(dec_expr->operand.get(), 
                                  "Pre-decrement operand must be numeric (int or float), but got '" + 
                                  operand_type->toString() + "'");
                        return ch->gettyptr("void");
                    }
                }
            }
            
            if (!is_int && !is_float) {
                ch->error(dec_expr->operand.get(), 
                          "Pre-decrement operand must be numeric (int or float), but got '" + 
                          operand_type->toString() + "'");
                return ch->gettyptr("void");
            }
            
            temp_result = operand_type;
            return temp_result;
        }
        
        case NodeType::PostIncrementExpression: {
            auto* post_inc_expr = static_cast<PostIncrementExprNode*>(node);
            if (!post_inc_expr->operand) {
                ch->error(node, "Post-increment expression requires an operand");
                return ch->gettyptr("void");
            }
            
            // Verificar que é um lvalue (Identifier ou AccessExpression)
            if (post_inc_expr->operand->kind != NodeType::Identifier && 
                post_inc_expr->operand->kind != NodeType::AccessExpression) {
                ch->error(post_inc_expr->operand.get(), 
                          "Post-increment operand must be a variable or array access");
                return ch->gettyptr("void");
            }
            
            // Verificar tipo numérico
            auto operand_type = ch->infer_expr(post_inc_expr->operand.get());
            operand_type = ch->unify_ctx.resolve(operand_type);
            
            bool is_int = operand_type->kind == nv::Kind::INT;
            bool is_float = operand_type->kind == nv::Kind::FLOAT;
            bool is_type_var = operand_type->kind == nv::Kind::TYPE_VAR;
            
            // Se for AccessExpression e TypeVar, tentar unificar com int ou float
            if (is_type_var && post_inc_expr->operand->kind == NodeType::AccessExpression) {
                try {
                    ch->unify_ctx.unify(operand_type, ch->gettyptr("int"));
                    operand_type = ch->gettyptr("int");
                    is_int = true;
                } catch (std::runtime_error&) {
                    try {
                        ch->unify_ctx.unify(operand_type, ch->gettyptr("float"));
                        operand_type = ch->gettyptr("float");
                        is_float = true;
                    } catch (std::runtime_error&) {
                        ch->error(post_inc_expr->operand.get(), 
                                  "Post-increment operand must be numeric (int or float), but got '" + 
                                  operand_type->toString() + "'");
                        return ch->gettyptr("void");
                    }
                }
            }
            
            if (!is_int && !is_float) {
                ch->error(post_inc_expr->operand.get(), 
                          "Post-increment operand must be numeric (int or float), but got '" + 
                          operand_type->toString() + "'");
                return ch->gettyptr("void");
            }
            
            temp_result = operand_type;
            return temp_result;
        }
        
        case NodeType::PostDecrementExpression: {
            auto* post_dec_expr = static_cast<PostDecrementExprNode*>(node);
            if (!post_dec_expr->operand) {
                ch->error(node, "Post-decrement expression requires an operand");
                return ch->gettyptr("void");
            }
            
            // Verificar que é um lvalue (Identifier ou AccessExpression)
            if (post_dec_expr->operand->kind != NodeType::Identifier && 
                post_dec_expr->operand->kind != NodeType::AccessExpression) {
                ch->error(post_dec_expr->operand.get(), 
                          "Post-decrement operand must be a variable or array access");
                return ch->gettyptr("void");
            }
            
            // Verificar tipo numérico
            auto operand_type = ch->infer_expr(post_dec_expr->operand.get());
            operand_type = ch->unify_ctx.resolve(operand_type);
            
            bool is_int = operand_type->kind == nv::Kind::INT;
            bool is_float = operand_type->kind == nv::Kind::FLOAT;
            bool is_type_var = operand_type->kind == nv::Kind::TYPE_VAR;
            
            // Se for AccessExpression e TypeVar, tentar unificar com int ou float
            if (is_type_var && post_dec_expr->operand->kind == NodeType::AccessExpression) {
                try {
                    ch->unify_ctx.unify(operand_type, ch->gettyptr("int"));
                    operand_type = ch->gettyptr("int");
                    is_int = true;
                } catch (std::runtime_error&) {
                    try {
                        ch->unify_ctx.unify(operand_type, ch->gettyptr("float"));
                        operand_type = ch->gettyptr("float");
                        is_float = true;
                    } catch (std::runtime_error&) {
                        ch->error(post_dec_expr->operand.get(), 
                                  "Post-decrement operand must be numeric (int or float), but got '" + 
                                  operand_type->toString() + "'");
                        return ch->gettyptr("void");
                    }
                }
            }
            
            if (!is_int && !is_float) {
                ch->error(post_dec_expr->operand.get(), 
                          "Post-decrement operand must be numeric (int or float), but got '" + 
                          operand_type->toString() + "'");
                return ch->gettyptr("void");
            }
            
            temp_result = operand_type;
            return temp_result;
        }
        
        default:
            return ch->gettyptr("void");
    }
}
