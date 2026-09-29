#pragma once
#include <memory>
#include <string>
#include <vector>

// Forward declarations to avoid circular dependencies
namespace llvm { class Value; }
namespace nv { class NIRGenerationContext; }

template <typename T>
using NvList = std::vector<T>;

enum class NodeType {
    Program,
    NumericLiteral,
    BooleanLiteral,
    CharLiteral,
    Identifier,
    BinaryExpression,
    AssignmentExpression,
    DeclarationStatement,
    FunctionStatement,
    Parameter,
    Argument,
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
    BreakStatement,
    ContinueStatement,
    ForStatement,
    ForeverStatement,
    WhileStatement,
    ConditionalExpression,
    MatchStatement,
    ListComprehension,
    VectorExpression,
    RangeExpression,
    ClosureExpression,
    ImportStatement,
    ClassStatement,
    ClassField,
    ClassMethod,
    NewExpression,
    SelfExpression,
    SuperExpression,
    InstanceofExpression,
    TryStatement,
    ThrowStatement,
    EnumStatement,
    OrExpression,
    PropagateStatement,
    NoneLiteral,
    InterfaceStatement,
    SliceExpression,
    DeferErrorStatement,
    DeferStatement,
    ExternStatement,
    ExternFromImportStatement,
    ModuleAttrStatement,
    DecoratorStatement,
    AttributeStatement,
    InlineAsmStatement,
    AwaitExpression,
    // Compile-time execution (CTE)
    ComptimeDecl,
    ComptimeFuncDef,
    ComptimeFor,
    ComptimeIf,
    ComptimeBlock,
    ComptimeExpr,
    ComptimeWhile,
    TypeReflectExpr,
    BuiltinCall,
    MacroCall,
};

// Definido em parser.cpp: qual arquivo o parser está lendo agora.
std::string nv_parse_file();

class PositionData {
public:
    size_t line;
    size_t col[2];
    size_t pos[2];
    std::string filename;

    // O arquivo de ORIGEM do nó. Fica carimbado aqui para que o endereço do diagnóstico continue
    // certo depois do merge: o corpo de um módulo é clonado para o programa e checado pelo
    // checker do programa, cujo `current_filename` é o do programa — sem isto, um aviso sobre
    // stdlib/crypto.nv saía com o nome do arquivo do usuário e a linha do módulo.
    //
    // O default é o arquivo que o parser está lendo no momento, então as construções que não
    // passam o sexto argumento (a maioria) já saem carimbadas, sem tocar em 78 sítios.
    PositionData(size_t line, size_t col_start, size_t col_end, size_t pos_start, size_t pos_end,
                 std::string filename = nv_parse_file())
        : line(line), col{col_start, col_end}, pos{pos_start, pos_end},
          filename(std::move(filename)) {}
};

class Node {
public:
    NodeType kind;
    std::unique_ptr<PositionData> position;
    explicit Node(NodeType k) : kind(k) {}
    virtual ~Node() = default;
    virtual Node* clone() const = 0;

    virtual void nir_codegen(nv::NIRGenerationContext& ctx) {}
};

class Stmt : public Node {
public:
    explicit Stmt(NodeType k) : Node(k) {}
};

using CodeBlock = NvList<std::unique_ptr<Stmt>>;

class Expr : public Stmt {
public:
    explicit Expr(NodeType k) : Stmt(k) {}
};

