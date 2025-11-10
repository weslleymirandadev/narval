#pragma once
#include <memory>
#include <vector>

// Forward declarations to avoid circular dependencies
namespace llvm { class Value; }
namespace rph { class IRGenerationContext; }

template <typename T>
using RphList = std::vector<T>;

enum class NodeType {
    Program,
    NumericLiteral,
    BooleanLiteral,
    Identifier,
    BinaryExpression,
    AssignmentExpression,
    DeclarationStatement,
    LabelStatement,
    Parameter,
    IfStatement,
    LogicalNotExpression,
    UnaryMinusExpression,
    IncrementExpression,
    DecrementExpression,
    PostIncrementExpression,
    PostDecrementExpression,
    AccessExpression,
    MemberExpression,
    CallExpression,
    Map,
    KeyValue,
    ArrayExpression,
    TupleExpression,
    StringLiteral,
    ReturnStatement,
    ForStatement,
    LoopStatement,
    WhileStatement,
    ConditionalExpression,
    MatchStatement,
    ListComprehension,
    VectorExpression
};

class PositionData {
public:
    size_t line;
    size_t col[2];
    size_t pos[2];

    PositionData(size_t line, size_t col_start, size_t col_end, size_t pos_start, size_t pos_end)
        : line(line), col{col_start, col_end}, pos{pos_start, pos_end} {}
};

class Node {
public:
    NodeType kind;
    std::unique_ptr<PositionData> position;
    explicit Node(NodeType k) : kind(k) {}
    virtual ~Node() = default;
    virtual Node* clone() const = 0;
    virtual void codegen(rph::IRGenerationContext& ctx) = 0;
};

class Stmt : public Node {
public:
    explicit Stmt(NodeType k) : Node(k) {}
    // Statements não retornam valores; geração é por efeito no contexto
    virtual void codegen(rph::IRGenerationContext& ctx) override {}
};

using CodeBlock = RphList<std::unique_ptr<Stmt>>;

class Expr : public Stmt {
public:
    explicit Expr(NodeType k) : Stmt(k) {}
};

