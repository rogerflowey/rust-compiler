#pragma once

#include "ast/common.hpp"
#include "semantic/common.hpp"
#include "semantic/const/const.hpp"
#include "semantic/type/type.hpp"
#include "ast/ast.hpp"
#include "span/span.hpp"

#include "semantic/pass/semantic_check/expr_info.hpp"

#include <memory>
#include <optional>
#include <variant>
#include <vector>


//some utils
namespace semantic {

struct Field {
    ast::Identifier name;
    std::optional<TypeId> type;
    span::Span span = span::Span::invalid();
};

struct EnumVariant {
    ast::Identifier name;
    span::Span span = span::Span::invalid();
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
struct Local; // New canonical definition for a local variable

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
    span::Span span = span::Span::invalid();
};
using TypeAnnotation = std::variant<std::unique_ptr<TypeNode>, semantic::TypeId>;

struct DefType {
    std::variant<ast::Identifier, TypeDef> def;
    span::Span span = span::Span::invalid();
};

struct PrimitiveType {
    ast::PrimitiveType::Kind kind;
    span::Span span = span::Span::invalid();
};

struct ArrayType {
    TypeAnnotation element_type;
    std::unique_ptr<Expr> size;
    span::Span span = span::Span::invalid();
};

struct ReferenceType {
    TypeAnnotation referenced_type;
    bool is_mutable;
    span::Span span = span::Span::invalid();
};

struct UnitType {
    span::Span span = span::Span::invalid();
};

// --- HIR Local Variable Abstraction ---
struct Local {
    ast::Identifier name; // not really neccesary
    bool is_mutable;
    std::optional<TypeAnnotation> type_annotation;
    span::Span span = span::Span::invalid();
    
    // Disable copy to prevent dangling pointers
    Local(const Local&) = delete;
    Local& operator=(const Local&) = delete;
    Local(Local&&) noexcept = default;
    Local& operator=(Local&&) noexcept = default;
    Local() = default;
    Local(ast::Identifier name_, bool is_mutable_, std::optional<TypeAnnotation> type_annotation_)
        : name(std::move(name_)), is_mutable(is_mutable_), type_annotation(std::move(type_annotation_)) {}
};


struct BindingDef {
    //Changed
    struct Unresolved{
        bool is_mutable;
        bool is_ref;
        ast::Identifier name;
    };
    std::variant<Unresolved,Local*> local;
    span::Span span = span::Span::invalid();
    
    // Disable copy to prevent dangling pointers
    BindingDef(const BindingDef&) = delete;
    BindingDef& operator=(const BindingDef&) = delete;
    BindingDef(BindingDef&&) noexcept = default;
    BindingDef& operator=(BindingDef&&) noexcept = default;
    BindingDef() = default;
    BindingDef(BindingDef::Unresolved&& unresolved)
        : local(std::move(unresolved)) {}
};

struct ReferencePattern {
    std::unique_ptr<Pattern> subpattern;
    bool is_mutable;
    span::Span span = span::Span::invalid();
};

using PatternVariant = std::variant<BindingDef, ReferencePattern>;
struct Pattern {
    PatternVariant value;
    span::Span span = span::Span::invalid();
    Pattern(PatternVariant&& val)
        : value(std::move(val)) {}
    
    // Disable copy to prevent dangling pointers
    Pattern(const Pattern&) = delete;
    Pattern& operator=(const Pattern&) = delete;
    Pattern(Pattern&&) noexcept = default;
    Pattern& operator=(Pattern&&) noexcept = default;
};

// --- HIR Expression Variants ---

struct Literal {
    struct Integer {
        uint64_t value = 0;
        ast::IntegerLiteralExpr::Type suffix_type = ast::IntegerLiteralExpr::NOT_SPECIFIED;
        bool is_negative = false;
    };
    struct String {
        std::string value{};
        bool is_cstyle = false;
    };
    using Value = std::variant<
        Integer,
        bool,
        char,
        String
    >;

    Value value;
    span::Span span = span::Span::invalid();
};

// NEW: Temporary node for an unresolved identifier in an expression.
struct UnresolvedIdentifier {
    ast::Identifier name;
    span::Span span = span::Span::invalid();
};

// REDEFINED: A resolved reference to a local variable.
struct Variable {
    Local* local_id;
    span::Span span = span::Span::invalid();
    
    // Disable copy to prevent dangling pointers
    Variable(const Variable&) = delete;
    Variable& operator=(const Variable&) = delete;
    Variable(Variable&&) noexcept = default;
    Variable& operator=(Variable&&) noexcept = default;
    Variable() = default;
    Variable(Local* local_id_)
        : local_id(local_id_) {}
};

// NEW: A resolved reference to a constant.
struct ConstUse {
    const hir::ConstDef* def = nullptr;
    span::Span span = span::Span::invalid();
    
    // Disable copy to prevent dangling pointers
    ConstUse(const ConstUse&) = delete;
    ConstUse& operator=(const ConstUse&) = delete;
    ConstUse(ConstUse&&) noexcept = default;
    ConstUse& operator=(ConstUse&&) noexcept = default;
    ConstUse() = default;
    ConstUse(const hir::ConstDef* def_)
        : def(def_) {}
};

// NEW: A resolved reference to a function.
struct FuncUse {
    const hir::Function* def = nullptr;
    span::Span span = span::Span::invalid();
    
    // Disable copy to prevent dangling pointers
    FuncUse(const FuncUse&) = delete;
    FuncUse& operator=(const FuncUse&) = delete;
    FuncUse(FuncUse&&) noexcept = default;
    FuncUse& operator=(FuncUse&&) noexcept = default;
    FuncUse() = default;
    FuncUse(const hir::Function* def_)
        : def(def_) {}
};

// Represents a path with two segments, like `MyType::something`.
struct TypeStatic {
    std::variant<ast::Identifier, TypeDef> type; // The first segment of the path, e.g., `MyType`
    ast::Identifier name;
    span::Span span = span::Span::invalid();
};

struct Underscore {
    span::Span span = span::Span::invalid();
};

struct FieldAccess {
    std::unique_ptr<Expr> base;
    std::variant<ast::Identifier, size_t> field;
    span::Span span = span::Span::invalid();
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

    span::Span span = span::Span::invalid();
};

struct StructConst {
    hir::StructDef* struct_def = nullptr;
    hir::ConstDef* assoc_const = nullptr;
    
    // Disable copy to prevent dangling pointers
    StructConst(const StructConst&) = delete;
    StructConst& operator=(const StructConst&) = delete;
    StructConst(StructConst&&) noexcept = default;
    StructConst& operator=(StructConst&&) noexcept = default;
    StructConst() = default;
    StructConst(hir::StructDef* struct_def_, hir::ConstDef* assoc_const_)
        : struct_def(struct_def_), assoc_const(assoc_const_) {}
    span::Span span = span::Span::invalid();
};

struct EnumVariant {
    hir::EnumDef* enum_def = nullptr;
    size_t variant_index;
    
    // Disable copy to prevent dangling pointers
    EnumVariant(const EnumVariant&) = delete;
    EnumVariant& operator=(const EnumVariant&) = delete;
    EnumVariant(EnumVariant&&) noexcept = default;
    EnumVariant& operator=(EnumVariant&&) noexcept = default;
    EnumVariant() = default;
    EnumVariant(hir::EnumDef* enum_def_, size_t variant_index_)
        : enum_def(enum_def_), variant_index(variant_index_) {}
    span::Span span = span::Span::invalid();
};

struct ArrayLiteral {
    std::vector<std::unique_ptr<Expr>> elements;
    span::Span span = span::Span::invalid();
};

struct ArrayRepeat {
    std::unique_ptr<Expr> value;
    std::variant<std::unique_ptr<Expr>, size_t> count;
    span::Span span = span::Span::invalid();
};

struct Index {
    std::unique_ptr<Expr> base;
    std::unique_ptr<Expr> index;
    span::Span span = span::Span::invalid();
};

struct Assignment {
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
    span::Span span = span::Span::invalid();
};

struct UnaryNot {
    enum class Kind { Unspecified, Bool, Int };
    Kind kind = Kind::Unspecified;
};

struct UnaryNegate {
    enum class Kind { Unspecified, SignedInt, UnsignedInt };
    Kind kind = Kind::Unspecified;
};

struct Dereference {
};

struct Reference {
    bool is_mutable = false;
};

using UnaryOperator = std::variant<UnaryNot, UnaryNegate, Dereference, Reference>;

struct UnaryOp {
    UnaryOperator op;
    std::unique_ptr<Expr> rhs;
    span::Span span = span::Span::invalid();
};

struct Add {
    enum class Kind { Unspecified, SignedInt, UnsignedInt };
    Kind kind = Kind::Unspecified;
};

struct Subtract {
    enum class Kind { Unspecified, SignedInt, UnsignedInt };
    Kind kind = Kind::Unspecified;
};

struct Multiply {
    enum class Kind { Unspecified, SignedInt, UnsignedInt };
    Kind kind = Kind::Unspecified;
};

struct Divide {
    enum class Kind { Unspecified, SignedInt, UnsignedInt };
    Kind kind = Kind::Unspecified;
};

struct Remainder {
    enum class Kind { Unspecified, SignedInt, UnsignedInt };
    Kind kind = Kind::Unspecified;
};

struct BitAnd {
    enum class Kind { Unspecified, SignedInt, UnsignedInt };
    Kind kind = Kind::Unspecified;
};

struct BitXor {
    enum class Kind { Unspecified, SignedInt, UnsignedInt };
    Kind kind = Kind::Unspecified;
};

struct BitOr {
    enum class Kind { Unspecified, SignedInt, UnsignedInt };
    Kind kind = Kind::Unspecified;
};

struct ShiftLeft {
    enum class Kind { Unspecified, SignedInt, UnsignedInt };
    Kind kind = Kind::Unspecified;
};

struct ShiftRight {
    enum class Kind { Unspecified, SignedInt, UnsignedInt };
    Kind kind = Kind::Unspecified;
};

struct Equal {
    enum class Kind { Unspecified, SignedInt, UnsignedInt, Bool, Char };
    Kind kind = Kind::Unspecified;
};

struct NotEqual {
    enum class Kind { Unspecified, SignedInt, UnsignedInt, Bool, Char };
    Kind kind = Kind::Unspecified;
};

struct LessThan {
    enum class Kind { Unspecified, SignedInt, UnsignedInt, Bool, Char };
    Kind kind = Kind::Unspecified;
};

struct GreaterThan {
    enum class Kind { Unspecified, SignedInt, UnsignedInt, Bool, Char };
    Kind kind = Kind::Unspecified;
};

struct LessEqual {
    enum class Kind { Unspecified, SignedInt, UnsignedInt, Bool, Char };
    Kind kind = Kind::Unspecified;
};

struct GreaterEqual {
    enum class Kind { Unspecified, SignedInt, UnsignedInt, Bool, Char };
    Kind kind = Kind::Unspecified;
};

struct LogicalAnd {
    enum class Kind { Unspecified, Bool };
    Kind kind = Kind::Unspecified;
};

struct LogicalOr {
    enum class Kind { Unspecified, Bool };
    Kind kind = Kind::Unspecified;
};

using BinaryOperator = std::variant<
    Add,
    Subtract,
    Multiply,
    Divide,
    Remainder,
    LogicalAnd,
    LogicalOr,
    BitAnd,
    BitXor,
    BitOr,
    ShiftLeft,
    ShiftRight,
    Equal,
    NotEqual,
    LessThan,
    GreaterThan,
    LessEqual,
    GreaterEqual>;

struct BinaryOp {
    BinaryOperator op;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
    span::Span span = span::Span::invalid();
};

struct Cast {
    std::unique_ptr<Expr> expr;
    TypeAnnotation target_type;
    span::Span span = span::Span::invalid();
};

struct Call {
    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> args;
    span::Span span = span::Span::invalid();
};

struct MethodCall {
    std::unique_ptr<Expr> receiver;
    std::variant<ast::Identifier, const Method*> method;
    std::vector<std::unique_ptr<Expr>> args;
    span::Span span = span::Span::invalid();
};

struct If {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Block> then_block;
    std::optional<std::unique_ptr<Expr>> else_expr;
    span::Span span = span::Span::invalid();
};

struct Loop {
    std::unique_ptr<Block> body;
    std::optional<semantic::TypeId> break_type = std::nullopt;
    span::Span span = span::Span::invalid();
};

struct While {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Block> body;
    std::optional<semantic::TypeId> break_type = std::nullopt;
    span::Span span = span::Span::invalid();
};

struct Break {
    std::optional<std::unique_ptr<Expr>> value;
    std::optional<std::variant<Loop*, While*>> target = std::nullopt;
    span::Span span = span::Span::invalid();
};

struct Continue {
    std::optional<std::variant<Loop*, While*>> target = std::nullopt;
    span::Span span = span::Span::invalid();
};

struct Return {
    std::optional<std::unique_ptr<Expr>> value;
    std::optional<std::variant<Function*, Method*>> target = std::nullopt;
    span::Span span = span::Span::invalid();
};

struct Block {
    std::vector<std::unique_ptr<Item>> items;
    std::vector<std::unique_ptr<Stmt>> stmts;
    std::optional<std::unique_ptr<Expr>> final_expr;
    span::Span span = span::Span::invalid();

    ~Block();
    Block(Block&&) noexcept;
    Block& operator=(Block&&) noexcept;
    Block();
};


using ExprVariant = std::variant<
    Literal, UnresolvedIdentifier, TypeStatic, Underscore, FieldAccess, StructLiteral, ArrayLiteral, ArrayRepeat,
    Index, Assignment, UnaryOp, BinaryOp, Cast, Call, MethodCall, Block, If, Loop, While,
    Break, Continue, Return,
    // Resolved uses
    Variable,
    ConstUse,
    FuncUse,
    StructConst, EnumVariant
>;


struct Expr {
    std::optional<semantic::ExprInfo> expr_info;
    ExprVariant value;

    Expr(ExprVariant&& val)
        : expr_info(std::nullopt), value(std::move(val)) {}

    ~Expr();
    Expr(Expr&&) noexcept;
    Expr& operator=(Expr&&) noexcept;
    
    // Disable copy to prevent dangling pointers
    Expr(const Expr&) = delete;
    Expr& operator=(const Expr&) = delete;
};

struct LetStmt {
    std::unique_ptr<Pattern> pattern;
    std::optional<TypeAnnotation> type_annotation;
    std::unique_ptr<Expr> initializer;
    span::Span span = span::Span::invalid();
};

struct ExprStmt {
    std::unique_ptr<Expr> expr;
    span::Span span = span::Span::invalid();
};

using StmtVariant = std::variant<LetStmt, ExprStmt>;

struct Stmt {
    StmtVariant value;
    span::Span span = span::Span::invalid();
    Stmt(StmtVariant&& val)
        : value(std::move(val)) {}

    ~Stmt();
    Stmt(Stmt&&) noexcept;
    Stmt& operator=(Stmt&&) noexcept;
};


struct Function {
    ast::Identifier name;
    std::vector<std::unique_ptr<Pattern>> params;// Changed
    std::vector<std::optional<TypeAnnotation>> param_type_annotations; // NEW: Parameter type annotations
    std::optional<TypeAnnotation> return_type;
    std::unique_ptr<Block> body;
    std::vector<std::unique_ptr<Local>> locals; // NEW: Owning list of all locals
    span::Span span = span::Span::invalid();
    
    // Disable copy to prevent dangling pointers
    Function(const Function&) = delete;
    Function& operator=(const Function&) = delete;
    Function(Function&&) noexcept = default;
    Function& operator=(Function&&) noexcept = default;
    Function() = default;
        Function(std::vector<std::unique_ptr<Pattern>>&& params_,
                         std::vector<std::optional<TypeAnnotation>>&& param_type_annotations_,
                         std::optional<TypeAnnotation>&& return_type_,
                         std::unique_ptr<Block>&& body_,
                         std::vector<std::unique_ptr<Local>>&& locals_,
                         ast::Identifier name_)
                : name(std::move(name_)),
                    params(std::move(params_)),
          param_type_annotations(std::move(param_type_annotations_)),
          return_type(std::move(return_type_)),
          body(std::move(body_)),
                    locals(std::move(locals_)) {}
};

struct Method {
    struct SelfParam {
        bool is_reference;
        bool is_mutable;
        span::Span span = span::Span::invalid();
        
        // Disable copy to prevent dangling pointers
        SelfParam(const SelfParam&) = delete;
        SelfParam& operator=(const SelfParam&) = delete;
        SelfParam(SelfParam&&) noexcept = default;
        SelfParam& operator=(SelfParam&&) noexcept = default;
        SelfParam() = default;
    };
    
    ast::Identifier name;
    SelfParam self_param;
    std::vector<std::unique_ptr<Pattern>> params;// changed
    std::vector<std::optional<TypeAnnotation>> param_type_annotations; // NEW: Parameter type annotations
    std::optional<TypeAnnotation> return_type;
    std::unique_ptr<Block> body;
    std::unique_ptr<Local> self_local;
    std::vector<std::unique_ptr<Local>> locals;
    span::Span span = span::Span::invalid();
    
    // Disable copy to prevent dangling pointers
    Method(const Method&) = delete;
    Method& operator=(const Method&) = delete;
    Method(Method&&) noexcept = default;
    Method& operator=(Method&&) noexcept = default;
    Method() = default;
            Method(ast::Identifier name_,
                SelfParam&& self_param_,
                std::vector<std::unique_ptr<Pattern>>&& params_,
                std::vector<std::optional<TypeAnnotation>>&& param_type_annotations_,
                std::optional<TypeAnnotation>&& return_type_,
                std::unique_ptr<Block>&& body_)
                : name(std::move(name_)),
                    self_param(std::move(self_param_)),
          params(std::move(params_)),
          param_type_annotations(std::move(param_type_annotations_)),
          return_type(std::move(return_type_)),
          body(std::move(body_)),
          self_local(nullptr),
               locals() {}
};


struct StructDef {
    ast::Identifier name;
    std::vector<semantic::Field> fields;
    std::vector<TypeAnnotation> field_type_annotations;
    span::Span span = span::Span::invalid();

    std::optional<size_t> find_field(const ast::Identifier& name) const {
        for (size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].name == name) {
                return i;
            }
        }
        return std::nullopt;
    };
};

struct EnumDef {
    ast::Identifier name;
    std::vector<semantic::EnumVariant> variants;
    span::Span span = span::Span::invalid();
};

struct ConstDef {
    ast::Identifier name;
    std::unique_ptr<Expr> expr;
    std::optional<semantic::ConstVariant> const_value;
    std::optional<TypeAnnotation> type;
    span::Span span = span::Span::invalid();
};

struct Trait {
    ast::Identifier name;
    std::vector<std::unique_ptr<Item>> items;
    span::Span span = span::Span::invalid();
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
    span::Span span = span::Span::invalid();
    
    // Disable copy to prevent dangling pointers
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) noexcept = default;
    Impl& operator=(Impl&&) noexcept = default;
    Impl() = default;
        Impl(std::optional<std::variant<ast::Identifier, const Trait*>>&& trait_,
            TypeAnnotation&& for_type_,
            std::vector<std::unique_ptr<AssociatedItem>>&& items_)
        : trait(std::move(trait_)),
          for_type(std::move(for_type_)),
            items(std::move(items_)) {}
};

using ItemVariant = std::variant<Function, StructDef, EnumDef, ConstDef, Trait, Impl>;

struct Item {
    ItemVariant value;
    Item(ItemVariant&& val)
        : value(std::move(val)) {}
    span::Span span = span::Span::invalid();
};

struct Program {
    std::vector<std::unique_ptr<Item>> items;
    span::Span span = span::Span::invalid();
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