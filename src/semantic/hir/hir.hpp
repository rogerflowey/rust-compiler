#pragma once

#include "semantic/common.hpp"
#include "semantic/type/type.hpp"
#include "ast/ast.hpp"

#include <memory>
#include <optional>
#include <variant>
#include <vector>


//some utils
namespace semantic {

struct Field {
    ast::Identifier name;
    TypeId type;
};

struct EnumVariant {
    ast::Identifier name;
};

}

namespace hir {

// Forward declarations for HIR nodes
struct Expr;
struct Stmt;
struct Block;
struct Item;
struct Function;
struct StructDef;
struct EnumDef;
struct ConstDef;

struct Binding {
    bool is_mutable;
    std::optional<semantic::TypeId> type; // fill in Type Checking pass
    const ast::IdentifierPattern* ast_node = nullptr;
};

using Pattern = Binding;

// --- HIR Expression Variants ---

struct Literal {
    struct Integer {
        uint64_t value;
        ast::IntegerLiteralExpr::Type suffix_type;
    };
    struct String {
        std::string value;
        bool is_cstyle;
    };
    using Value = std::variant<
        Integer,
        bool,  
        char,  
        String 
    >;

    Value value;
    const ast::Expr* ast_node = nullptr;
};

struct Variable {
    std::optional<ValueDef> definition;
    const ast::Expr* ast_node = nullptr;
};

struct FieldAccess {
    std::unique_ptr<Expr> base;
    std::optional<const semantic::Field*> field; // fill in Type Checking pass
    const ast::Expr* ast_node = nullptr;
};

struct StructLiteral {
    hir::StructDef* struct_def = nullptr;
    struct FieldInit {
        std::optional<const semantic::Field*> field; // fill in Type Checking pass
        std::unique_ptr<Expr> initializer;
    };
    std::vector<FieldInit> fields;
    const ast::StructExpr* ast_node = nullptr;
};

struct ArrayLiteral {
    std::vector<std::unique_ptr<Expr>> elements;
    const ast::ArrayInitExpr* ast_node = nullptr;
};

struct ArrayRepeat {
    std::unique_ptr<Expr> value;
    std::optional<size_t> count; // fill in Const Evaluation pass
    const ast::ArrayRepeatExpr* ast_node = nullptr;
};

struct Index {
    std::unique_ptr<Expr> base;
    std::unique_ptr<Expr> index;
    const ast::IndexExpr* ast_node = nullptr;
};

struct Assignment {
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
    const ast::AssignExpr* ast_node = nullptr;
};

struct UnaryOp {
    enum Op { NOT, NEGATE, DEREFERENCE, REFERENCE, MUTABLE_REFERENCE };
    Op op;
    std::unique_ptr<Expr> rhs;
    const ast::UnaryExpr* ast_node = nullptr;
};

struct BinaryOp {
    enum Op { ADD, SUB, MUL, DIV, REM, AND, OR, BIT_AND, BIT_XOR, BIT_OR, SHL, SHR, EQ, NE, LT, GT, LE, GE };
    Op op;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
    const ast::Expr* ast_node = nullptr;
};

struct Cast {
    std::unique_ptr<Expr> expr;
    std::optional<semantic::TypeId> target_type; // fill in Type Checking pass
    const ast::CastExpr* ast_node = nullptr;
};

struct Call {
    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> args;
    const ast::Expr* ast_node = nullptr;
};

struct If {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Block> then_block;
    std::optional<std::unique_ptr<Expr>> else_expr;
    const ast::IfExpr* ast_node = nullptr;
};

struct Loop {
    std::unique_ptr<Block> body;
    const ast::LoopExpr* ast_node = nullptr;
};

struct While {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Block> body;
    const ast::WhileExpr* ast_node = nullptr;
};

struct Break {
    std::optional<std::unique_ptr<Expr>> value;
    const ast::BreakExpr* ast_node = nullptr;
};

struct Continue {
    const ast::ContinueExpr* ast_node = nullptr;
};

struct Return {
    std::optional<std::unique_ptr<Expr>> value;
    const ast::ReturnExpr* ast_node = nullptr;
};

struct Block {
    const ast::BlockExpr* ast_node = nullptr;
    std::vector<std::unique_ptr<Item>> items;
    std::vector<std::unique_ptr<Stmt>> stmts;
    std::optional<std::unique_ptr<Expr>> final_expr;

    ~Block();
    Block(Block&&) noexcept;
    Block& operator=(Block&&) noexcept;
    Block();
};


using ExprVariant = std::variant<
    Literal, Variable, FieldAccess, StructLiteral, ArrayLiteral, ArrayRepeat,
    Index, Assignment, UnaryOp, BinaryOp, Cast, Call, Block, If, Loop, While,
    Break, Continue, Return
>;


struct Expr {
    std::optional<semantic::TypeId> type_id; // fill in Type Checking pass
    ExprVariant value;

    Expr(ExprVariant&& val)
        : type_id(std::nullopt), value(std::move(val)) {}

    ~Expr();
    Expr(Expr&&) noexcept;
    Expr& operator=(Expr&&) noexcept;
};

struct LetStmt {
    Pattern pattern;
    std::optional<semantic::TypeId> type;
    std::unique_ptr<Expr> initializer;
    const ast::LetStmt* ast_node = nullptr;
};

struct ExprStmt {
    std::unique_ptr<Expr> expr;
    const ast::ExprStmt* ast_node = nullptr;
};

using StmtVariant = std::variant<LetStmt, ExprStmt>;

struct Stmt {
    StmtVariant value;
    Stmt(StmtVariant&& val)
        : value(std::move(val)) {}

    ~Stmt();
    Stmt(Stmt&&) noexcept;
    Stmt& operator=(Stmt&&) noexcept;
};


struct Function {
    std::vector<Pattern> params;
    std::optional<semantic::TypeId> return_type;
    std::unique_ptr<Block> body;
    const ast::FunctionItem* ast_node = nullptr;
};

struct StructDef {
    std::vector<semantic::Field> fields;
    const ast::StructItem* ast_node = nullptr;
};

struct EnumDef {
    std::vector<semantic::EnumVariant> variants;
    const ast::EnumItem* ast_node = nullptr;
};

struct ConstDef {
    std::optional<semantic::TypeId> type;
    std::unique_ptr<Expr> value;
    const ast::ConstItem* ast_node = nullptr;
};

struct Trait {
    std::vector<std::unique_ptr<Item>> items;
    const ast::TraitItem* ast_node = nullptr;
};

struct Impl {
    std::optional<const Trait*> trait_symbol; // nullopt for inherent impls
    std::optional<semantic::TypeId> for_type; // to be filled by name resolution
    std::vector<std::unique_ptr<Item>> items;
    const ast::Item* ast_node = nullptr;
};

using ItemVariant = std::variant<Function, StructDef, EnumDef, ConstDef, Trait, Impl>;

struct Item {
    ItemVariant value;
    Item(ItemVariant&& val)
        : value(std::move(val)) {}
};

struct Program {
    std::vector<std::unique_ptr<Item>> items;
};


inline Block::~Block() = default;
inline Block::Block(Block&&) noexcept = default;
inline Block& Block::operator=(Block&&) noexcept = default;
inline Block::Block() = default;


inline Expr::~Expr() = default;
inline Expr::Expr(Expr&&) noexcept = default;
inline Expr& Expr::operator=(Expr&&) noexcept = default;


inline Stmt::~Stmt() = default;
inline Stmt::Stmt(Stmt&&) noexcept = default;
inline Stmt& Stmt::operator=(Stmt&&) noexcept = default;

} // namespace hir