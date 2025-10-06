#pragma once

#include "ast/common.hpp"
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
struct AssociatedItem;
struct Pattern;
struct Function;
struct Method;
struct StructDef;
struct EnumDef;
struct ConstDef;
struct Trait;
struct Impl;

// --- HIR Type System Nodes ---
struct DefType;
struct PrimitiveType;
struct ArrayType;
struct ReferenceType;
struct UnitType;
struct TypeNode;

using TypeNodeVariant = std::variant<
    std::unique_ptr<DefType>,
    std::unique_ptr<PrimitiveType>,
    std::unique_ptr<ArrayType>,
    std::unique_ptr<ReferenceType>,
    std::unique_ptr<UnitType>
>;

struct TypeNode {
    TypeNodeVariant value;
};
using TypeAnnotation = std::variant<std::unique_ptr<TypeNode>, semantic::TypeId>;

struct DefType {
    ast::Identifier name;
    const ast::PathType* ast_node = nullptr;
};

struct PrimitiveType {
    ast::PrimitiveType::Kind kind;
    const ast::PrimitiveType* ast_node = nullptr;
};

struct ArrayType {
    TypeAnnotation element_type;
    std::unique_ptr<Expr> size;
    const ast::ArrayType* ast_node = nullptr;
};

struct ReferenceType {
    TypeAnnotation referenced_type;
    bool is_mutable;
    const ast::ReferenceType* ast_node = nullptr;
};

struct UnitType {
    const ast::UnitType* ast_node = nullptr;
};





struct Binding {
    bool is_mutable;
    std::optional<TypeAnnotation> type_annotation; // fill in Type Checking pass
    const ast::IdentifierPattern* ast_node = nullptr;
};
struct WildCardPattern {
    const ast::WildcardPattern* ast_node = nullptr;
};

using PatternVariant = std::variant<Binding, WildCardPattern>;
struct Pattern {
    PatternVariant value;
    Pattern(PatternVariant&& val)
        : value(std::move(val)) {}
};

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
    using AstNode = std::variant<
        const ast::IntegerLiteralExpr*,
        const ast::BoolLiteralExpr*,
        const ast::CharLiteralExpr*,
        const ast::StringLiteralExpr*
    >;
    AstNode ast_node;
};

struct Variable {
    std::variant<ast::Identifier, ValueDef> definition;
    const ast::PathExpr* ast_node = nullptr;
};

// Represents a path with two segments, like `MyType::something`.
// Will be resolved into a more specific node like `StructStatic` during name resolution.
struct TypeStatic {
    std::variant<ast::Identifier, TypeDef> type; // The first segment of the path, e.g., `MyType`
    ast::Identifier name;
    const ast::PathExpr* ast_node = nullptr;
};

struct Underscore {
    const ast::UnderscoreExpr* ast_node = nullptr;
};

struct FieldAccess {
    std::unique_ptr<Expr> base;
    std::variant<ast::Identifier, size_t> field;
    const ast::FieldAccessExpr* ast_node = nullptr;
};

struct StructLiteral {
    std::variant<ast::Identifier, hir::StructDef*> struct_path;

    struct SyntacticFields {
        std::vector<std::pair<ast::Identifier, std::unique_ptr<Expr>>> initializers;
    };
    struct CanonicalFields {
        std::vector<std::unique_ptr<Expr>> initializers;
    };

    std::variant<SyntacticFields, CanonicalFields> fields;

    const ast::StructExpr* ast_node = nullptr;
};

struct StructConst {
    hir::StructDef* struct_def = nullptr;
    hir::ConstDef* assoc_const = nullptr;
};

struct StructStatic {
    hir::StructDef* struct_def = nullptr;
    hir::Function* assoc_fn = nullptr;
};

struct EnumVariant {
    hir::EnumDef* enum_def = nullptr;
    size_t variant_index;
};

struct ArrayLiteral {
    std::vector<std::unique_ptr<Expr>> elements;
    const ast::ArrayInitExpr* ast_node = nullptr;
};

struct ArrayRepeat {
    std::unique_ptr<Expr> value;
    std::variant<std::unique_ptr<Expr>, size_t> count;
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
    using AstNode = std::variant<const ast::BinaryExpr*, const ast::AssignExpr*>;
    AstNode ast_node;
};

struct Cast {
    std::unique_ptr<Expr> expr;
    TypeAnnotation target_type;
    const ast::CastExpr* ast_node = nullptr;
};

struct Call {
    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> args;
    const ast::CallExpr* ast_node = nullptr;
};

struct MethodCall {
    std::unique_ptr<Expr> receiver;
    std::variant<ast::Identifier, const Method*> method;
    std::vector<std::unique_ptr<Expr>> args;
    const ast::MethodCallExpr* ast_node = nullptr;
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
    Literal, Variable, TypeStatic, Underscore, FieldAccess, StructLiteral, ArrayLiteral, ArrayRepeat,
    Index, Assignment, UnaryOp, BinaryOp, Cast, Call, MethodCall, Block, If, Loop, While,
    Break, Continue, Return,
    // Resolved static items
    StructConst, StructStatic, EnumVariant
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
    std::optional<TypeAnnotation> type_annotation;
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
    std::optional<TypeAnnotation> return_type;
    std::unique_ptr<Block> body;
    const ast::FunctionItem* ast_node = nullptr;
};

struct Method {
    struct SelfParam {
        bool is_reference;
        bool is_mutable;
        const ast::FunctionItem::SelfParam* ast_node = nullptr;
    };
    
    SelfParam self_param;
    std::vector<Pattern> params;
    std::optional<TypeAnnotation> return_type;
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
    std::optional<TypeAnnotation> type;
    std::unique_ptr<Expr> value;
    const ast::ConstItem* ast_node = nullptr;
};

struct Trait {
    std::vector<std::unique_ptr<Item>> items;
    const ast::TraitItem* ast_node = nullptr;
};

using AssociatedItemVariant = std::variant<Function, Method, ConstDef>;
struct AssociatedItem {
    AssociatedItemVariant value;
    AssociatedItem(AssociatedItemVariant&& val) : value(std::move(val)) {}
};

struct Impl {
    std::optional<std::variant<ast::Identifier, const Trait*>> trait; // nullopt for inherent impls
    TypeAnnotation for_type;
    std::vector<std::unique_ptr<AssociatedItem>> items;
    using AstNode = std::variant<const ast::TraitImplItem*, const ast::InherentImplItem*>;
    AstNode ast_node;
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