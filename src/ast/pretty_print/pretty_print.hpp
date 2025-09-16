
#pragma once

#include <iostream>
#include <vector>
#include <optional>
#include <memory>
#include <string>
#include <variant>
#include <stdexcept>
#include <string_view>

// Assuming these are your AST node definitions
#include "../item.hpp" 
#include "../stmt.hpp"
#include "../expr.hpp"
#include "../type.hpp"
#include "../pattern.hpp"
#include "../common.hpp"


// Forward declare the visitor structs
struct DebugItemVisitor;
struct DebugStmtVisitor;
struct DebugExprVisitor;
struct DebugTypeVisitor;
struct DebugPatternVisitor;

class AstDebugPrinter {
public:
    explicit AstDebugPrinter(std::ostream& out) : out_(out) {}

    // Main entry point for a whole program (a vector of items)
    void print_program(const std::vector<ItemPtr>& items) {
        out_ << "Program [\n";
        {
            IndentGuard guard(*this);
            for (const auto& item : items) {
                prefix();
                if (item) {
                    print(*item);
                } else {
                    out_ << "nullptr";
                }
                out_ << ",\n";
            }
        }
        out_ << "]\n";
    }

    // Public print methods for each AST node wrapper type
    // DECLARE these here, but DEFINE them after the visitor structs are fully defined.
    void print(const Item& item);
    void print(const Statement& stmt);
    void print(const Expr& expr);
    void print(const Type& type);
    void print(const Pattern& pattern);
    
    // Public print methods for non-variant helper types can remain inline
    void print(const Identifier& id) { 
        out_ << "Identifier(\"" << id.name << "\")"; 
    }

    void print(const Path& path) {
        out_ << "Path { segments: [";
        for (size_t i = 0; i < path.segments.size(); ++i) {
            const auto& seg = path.segments[i];
            switch (seg.type) {
                case PathSegType::IDENTIFIER: print(**seg.id); break;
                case PathSegType::SELF: out_ << "Self"; break;
                case PathSegType::self: out_ << "self"; break;
            }
            if (i < path.segments.size() - 1) out_ << ", ";
        }
        out_ << "] }";
    }

    void print(const FunctionItem::SelfParam& param) {
        out_ << "SelfParam { is_reference: " << param.is_reference
             << ", is_mutable: " << param.is_mutable << " }";
    }

    void print(const BlockExpr& block) {
        out_ << "BlockExpr {\n";
        {
            IndentGuard guard(*this);
            print_list_field("statements", block.statements);
            print_optional_ptr_field("final_expr", block.final_expr);
        }
        prefix();
        out_ << "}";
    }

    void print(const StructExpr::FieldInit& field_init) {
        out_ << "FieldInit {\n";
        {
            IndentGuard guard(*this);
            print_ptr_field("name", field_init.name);
            print_ptr_field("value", field_init.value);
        }
        prefix();
        out_ << "}";
    }

// Make the visitors friends so they can access private members
private:
    friend struct DebugItemVisitor;
    friend struct DebugStmtVisitor;
    friend struct DebugExprVisitor;
    friend struct DebugTypeVisitor;
    friend struct DebugPatternVisitor;

    // RAII helper for managing indentation
    class IndentGuard {
    public:
        IndentGuard(AstDebugPrinter& printer) : printer_(printer) { 
            printer_.indent_level_++; 
        }
        ~IndentGuard() { 
            printer_.indent_level_--; 
        }
    private:
        AstDebugPrinter& printer_;
    };

    // Helper to print the current indentation prefix
    void prefix() {
        for (int i = 0; i < indent_level_; ++i) {
            out_ << "  "; // 2 spaces for debug printing
        }
    }

    // --- Templated helpers to reduce boilerplate ---
    template<typename T>
    void print_field(const std::string& name, const T& value) {
        prefix();
        out_ << name << ": " << value << "\n";
    }

    template<typename T>
    void print_ptr_field(const std::string& name, const std::unique_ptr<T>& ptr) {
        prefix();
        out_ << name << ": ";
        if (ptr) {
            print(*ptr);
        } else {
            out_ << "nullptr";
        }
        out_ << "\n";
    }

    template<typename T>
    void print_optional_ptr_field(const std::string& name, const std::optional<std::unique_ptr<T>>& opt_ptr) {
        prefix();
        out_ << name << ": ";
        if (opt_ptr) {
            if (*opt_ptr) {
                print(**opt_ptr);
            } else {
                out_ << "nullptr";
            }
        } else {
            out_ << "nullopt";
        }
        out_ << "\n";
    }

    template<typename T>
    void print_list_field(const std::string& name, const std::vector<std::unique_ptr<T>>& list) {
        prefix();
        out_ << name << ": [\n";
        {
            IndentGuard guard(*this);
            for (const auto& elem : list) {
                prefix();
                if (elem) {
                    print(*elem);
                } else {
                    out_ << "nullptr";
                }
                out_ << ",\n";
            }
        }
        prefix();
        out_ << "]\n";
    }
    
    template<typename T>
    void print_list_field(const std::string& name, const std::vector<T>& list) {
        prefix();
        out_ << name << ": [\n";
        {
            IndentGuard guard(*this);
            for (const auto& elem : list) {
                prefix();
                print(elem);
                out_ << ",\n";
            }
        }
        prefix();
        out_ << "]\n";
    }

    template<typename T1, typename T2>
    void print_pair_list_field(const std::string& name, const std::vector<std::pair<std::unique_ptr<T1>, std::unique_ptr<T2>>>& list) {
        prefix();
        out_ << name << ": [\n";
        {
            IndentGuard guard(*this);
            for (const auto& pair : list) {
                prefix();
                out_ << "pair(\n";
                {
                    IndentGuard guard2(*this);
                    print_ptr_field("first", pair.first);
                    print_ptr_field("second", pair.second);
                }
                prefix();
                out_ << "),\n";
            }
        }
        prefix();
        out_ << "]\n";
    }

    std::ostream& out_;
    int indent_level_ = 0;
};



// --- Helper Functions for printing Enums ---

inline const char* to_string(UnaryExpr::Op op) {
    switch (op) {
        case UnaryExpr::Op::NOT: return "NOT";
        case UnaryExpr::Op::NEGATE: return "NEGATE";
        case UnaryExpr::Op::DEREFERENCE: return "DEREFERENCE";
        case UnaryExpr::Op::REFERENCE: return "REFERENCE";
        case UnaryExpr::Op::MUTABLE_REFERENCE: return "MUTABLE_REFERENCE";
    }
    return "???";
}

inline const char* to_string(BinaryExpr::Op op) {
    switch (op) {
        case BinaryExpr::Op::ADD: return "ADD"; case BinaryExpr::Op::SUB: return "SUB";
        case BinaryExpr::Op::MUL: return "MUL"; case BinaryExpr::Op::DIV: return "DIV";
        case BinaryExpr::Op::REM: return "REM"; case BinaryExpr::Op::AND: return "AND";
        case BinaryExpr::Op::OR: return "OR"; case BinaryExpr::Op::BIT_AND: return "BIT_AND";
        case BinaryExpr::Op::EQ: return "EQ"; case BinaryExpr::Op::NE: return "NE";
        case BinaryExpr::Op::LT: return "LT"; case BinaryExpr::Op::GT: return "GT";
        case BinaryExpr::Op::LE: return "LE"; case BinaryExpr::Op::GE: return "GE";
    }
    return "???";
}

inline const char* to_string(AssignExpr::Op op) {
    switch (op) {
        case AssignExpr::Op::ASSIGN: return "ASSIGN";
        case AssignExpr::Op::ADD_ASSIGN: return "ADD_ASSIGN";
        case AssignExpr::Op::SUB_ASSIGN: return "SUB_ASSIGN";
    }
    return "???";
}

// --- Visitor Struct Definitions ---
// These can now be fully defined because AstDebugPrinter is a complete type.

struct DebugExprVisitor {
    AstDebugPrinter& p;

    void operator()(const BlockExpr& e) const { p.print(e); }
    void operator()(const IntegerLiteralExpr& e) const { p.out_ << "IntegerLiteralExpr { value: " << e.value << " }"; }
    void operator()(const BoolLiteralExpr& e) const { p.out_ << "BoolLiteralExpr { value: " << (e.value ? "true" : "false") << " }"; }
    void operator()(const CharLiteralExpr& e) const { p.out_ << "CharLiteralExpr { value: '" << e.value << "' }"; }
    void operator()(const StringLiteralExpr& e) const { p.out_ << "StringLiteralExpr { value: \"" << e.value << "\" }"; }
    void operator()(const PathExpr& e) const { p.out_ << "PathExpr { path: "; p.print(*e.path); p.out_ << " }"; }
    void operator()(const ContinueExpr&) const { p.out_ << "ContinueExpr {}"; }

    void operator()(const GroupedExpr& e) const {
        p.out_ << "GroupedExpr {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_ptr_field("expr", e.expr);
        p.prefix(); p.out_ << "}";
    }
    
    void operator()(const UnaryExpr& e) const {
        p.out_ << "UnaryExpr {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_field("op", std::string_view(to_string(e.op)));
        p.print_ptr_field("operand", e.operand);
        p.prefix(); p.out_ << "}";
    }

    void operator()(const BinaryExpr& e) const {
        p.out_ << "BinaryExpr {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_field("op", std::string_view(to_string(e.op)));
        p.print_ptr_field("left", e.left);
        p.print_ptr_field("right", e.right);
        p.prefix(); p.out_ << "}";
    }

    void operator()(const AssignExpr& e) const {
        p.out_ << "AssignExpr {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_field("op", std::string_view(to_string(e.op)));
        p.print_ptr_field("left", e.left);
        p.print_ptr_field("right", e.right);
        p.prefix(); p.out_ << "}";
    }

    void operator()(const CastExpr& e) const {
        p.out_ << "CastExpr {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_ptr_field("expr", e.expr);
        p.print_ptr_field("type", e.type);
        p.prefix(); p.out_ << "}";
    }
    
    void operator()(const ArrayInitExpr& e) const {
        p.out_ << "ArrayInitExpr {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_list_field("elements", e.elements);
        p.prefix(); p.out_ << "}";
    }

    void operator()(const ArrayRepeatExpr& e) const {
        p.out_ << "ArrayRepeatExpr {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_ptr_field("value", e.value);
        p.print_ptr_field("count", e.count);
        p.prefix(); p.out_ << "}";
    }

    void operator()(const IndexExpr& e) const {
        p.out_ << "IndexExpr {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_ptr_field("array", e.array);
        p.print_ptr_field("index", e.index);
        p.prefix(); p.out_ << "}";
    }

    void operator()(const StructExpr& e) const {
        p.out_ << "StructExpr {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_ptr_field("path", e.path);
        p.print_list_field("fields", e.fields);
        p.prefix(); p.out_ << "}";
    }
    
    void operator()(const CallExpr& e) const {
        p.out_ << "CallExpr {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_ptr_field("callee", e.callee);
        p.print_list_field("args", e.args);
        p.prefix(); p.out_ << "}";
    }

    void operator()(const MethodCallExpr& e) const {
        p.out_ << "MethodCallExpr {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_ptr_field("receiver", e.receiver);
        p.print_ptr_field("method_name", e.method_name);
        p.print_list_field("args", e.args);
        p.prefix(); p.out_ << "}";
    }

    void operator()(const FieldAccessExpr& e) const {
        p.out_ << "FieldAccessExpr {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_ptr_field("object", e.object);
        p.print_ptr_field("field_name", e.field_name);
        p.prefix(); p.out_ << "}";
    }

    void operator()(const IfExpr& e) const {
        p.out_ << "IfExpr {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_ptr_field("condition", e.condition);
        p.print_ptr_field("then_branch", e.then_branch);
        p.print_optional_ptr_field("else_branch", e.else_branch);
        p.prefix(); p.out_ << "}";
    }

    void operator()(const LoopExpr& e) const {
        p.out_ << "LoopExpr {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_ptr_field("body", e.body);
        p.prefix(); p.out_ << "}";
    }

    void operator()(const WhileExpr& e) const {
        p.out_ << "WhileExpr {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_ptr_field("condition", e.condition);
        p.print_ptr_field("body", e.body);
        p.prefix(); p.out_ << "}";
    }
    
    void operator()(const ReturnExpr& e) const {
        p.out_ << "ReturnExpr {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_optional_ptr_field("value", e.value);
        p.prefix(); p.out_ << "}";
    }

    void operator()(const BreakExpr& e) const {
        p.out_ << "BreakExpr {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_optional_ptr_field("label", e.label);
        p.print_optional_ptr_field("value", e.value);
        p.prefix(); p.out_ << "}";
    }
};

struct DebugStmtVisitor {
    AstDebugPrinter& p;
    
    void operator()(const LetStmt& s) const {
        p.out_ << "LetStmt {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_ptr_field("pattern", s.pattern);
        p.print_optional_ptr_field("type_annotation", s.type_annotation);
        p.print_optional_ptr_field("initializer", s.initializer);
        p.prefix(); p.out_ << "}";
    }

    void operator()(const ExprStmt& s) const {
        p.out_ << "ExprStmt {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_ptr_field("expr", s.expr);
        p.prefix(); p.out_ << "}";
    }
    
    void operator()(const EmptyStmt&) const { p.out_ << "EmptyStmt {}"; }
    void operator()(const ItemStmt& s) const {
        p.out_ << "ItemStmt {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_ptr_field("item", s.item);
        p.prefix(); p.out_ << "}";
    }
};

struct DebugItemVisitor {
    AstDebugPrinter& p;

    void operator()(const FunctionItem& i) const {
        p.out_ << "FunctionItem {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_ptr_field("name", i.name);
        p.print_ptr_field("self_param", i.self_param);
        p.print_pair_list_field("params", i.params);
        p.print_ptr_field("return_type", i.return_type);
        p.print_ptr_field("body", i.body);
        p.prefix(); p.out_ << "}";
    }

    void operator()(const StructItem& i) const {
        p.out_ << "StructItem {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_ptr_field("name", i.name);
        p.print_pair_list_field("fields", i.fields);
        p.prefix(); p.out_ << "}";
    }
    
    void operator()(const EnumItem& i) const {
        p.out_ << "EnumItem {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_ptr_field("name", i.name);
        p.print_list_field("variants", i.variants);
        p.prefix(); p.out_ << "}";
    }

    void operator()(const ConstItem& i) const {
        p.out_ << "ConstItem {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_ptr_field("name", i.name);
        p.print_ptr_field("type", i.type);
        p.print_ptr_field("value", i.value);
        p.prefix(); p.out_ << "}";
    }

    void operator()(const TraitItem& i) const {
        p.out_ << "TraitItem {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_ptr_field("name", i.name);
        p.print_list_field("items", i.items);
        p.prefix(); p.out_ << "}";
    }

    void operator()(const TraitImplItem& i) const {
        p.out_ << "TraitImplItem {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_ptr_field("trait_name", i.trait_name);
        p.print_ptr_field("for_type", i.for_type);
        p.print_list_field("items", i.items);
        p.prefix(); p.out_ << "}";
    }

    void operator()(const InherentImplItem& i) const {
        p.out_ << "InherentImplItem {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_ptr_field("for_type", i.for_type);
        p.print_list_field("items", i.items);
        p.prefix(); p.out_ << "}";
    }
};

struct DebugTypeVisitor {
    AstDebugPrinter& p;
    void operator()(const PathType& t) const { p.out_ << "PathType { path: "; p.print(*t.path); p.out_ << " }"; }
    void operator()(const PrimitiveType& t) const {
        p.out_ << "PrimitiveType { kind: ";
        switch (t.kind) {
            case PrimitiveType::I32: p.out_ << "I32"; break;
            case PrimitiveType::U32: p.out_ << "U32"; break;
            case PrimitiveType::ISIZE: p.out_ << "ISIZE"; break;
            case PrimitiveType::USIZE: p.out_ << "USIZE"; break;
            case PrimitiveType::BOOL: p.out_ << "BOOL"; break;
            case PrimitiveType::CHAR: p.out_ << "CHAR"; break;
            case PrimitiveType::STRING: p.out_ << "STRING"; break;
        }
        p.out_ << " }";
    }
    void operator()(const ArrayType& t) const {
        p.out_ << "ArrayType {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_ptr_field("element_type", t.element_type);
        p.print_ptr_field("size", t.size);
        p.prefix(); p.out_ << "}";
    }
    void operator()(const ReferenceType& t) const {
        p.out_ << "ReferenceType {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_field("is_mutable", t.is_mutable);
        p.print_ptr_field("referenced_type", t.referenced_type);
        p.prefix(); p.out_ << "}";
    }
    void operator()(const UnitType&) const { p.out_ << "UnitType {}"; }
};

struct DebugPatternVisitor {
    AstDebugPrinter& p;

    void operator()(const LiteralPattern& pat) const {
        p.out_ << "LiteralPattern {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_field("is_negative", pat.is_negative);
        p.print_ptr_field("literal", pat.literal);
        p.prefix(); p.out_ << "}";
    }
    void operator()(const IdentifierPattern& pat) const {
        p.out_ << "IdentifierPattern {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_ptr_field("name", pat.name);
        p.print_field("is_ref", pat.is_ref);
        p.print_field("is_mut", pat.is_mut);
        p.prefix(); p.out_ << "}";
    }
    void operator()(const WildcardPattern&) const { p.out_ << "WildcardPattern {}"; }
    void operator()(const ReferencePattern& pat) const {
        p.out_ << "ReferencePattern {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_field("is_mut", pat.is_mut);
        p.print_ptr_field("subpattern", pat.subpattern);
        p.prefix(); p.out_ << "}";
    }
    void operator()(const PathPattern& pat) const {
        p.out_ << "PathPattern {\n";
        AstDebugPrinter::IndentGuard guard(p);
        p.print_ptr_field("path", pat.path);
        p.prefix(); p.out_ << "}";
    }
};


inline void AstDebugPrinter::print(const Item& item) { std::visit(DebugItemVisitor{*this}, item.value); }
inline void AstDebugPrinter::print(const Statement& stmt) { std::visit(DebugStmtVisitor{*this}, stmt.value); }
inline void AstDebugPrinter::print(const Expr& expr) { std::visit(DebugExprVisitor{*this}, expr.value); }
inline void AstDebugPrinter::print(const Type& type) { std::visit(DebugTypeVisitor{*this}, type.value); }
inline void AstDebugPrinter::print(const Pattern& pattern) { std::visit(DebugPatternVisitor{*this}, pattern.value); }


inline std::ostream& operator<<(std::ostream& os, const std::vector<ItemPtr>& program) {
    AstDebugPrinter(os).print_program(program);
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const Item& item) {
    AstDebugPrinter(os).print(item);
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const Statement& stmt) {
    AstDebugPrinter(os).print(stmt);
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const Expr& expr) {
    AstDebugPrinter(os).print(expr);
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const Type& type) {
    AstDebugPrinter(os).print(type);
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const Pattern& pattern) {
    AstDebugPrinter(os).print(pattern);
    return os;
}

// Overloads for common non-variant helper types
inline std::ostream& operator<<(std::ostream& os, const Identifier& id) {
    AstDebugPrinter(os).print(id);
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const Path& path) {
    AstDebugPrinter(os).print(path);
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const FunctionItem::SelfParam& param) {
    AstDebugPrinter(os).print(param);
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const BlockExpr& block) {
    AstDebugPrinter(os).print(block);
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const StructExpr::FieldInit& field_init) {
    AstDebugPrinter(os).print(field_init);
    return os;
}

// Generic overload for unique_ptr to any AST node
template<typename T>
inline std::ostream& operator<<(std::ostream& os, const std::unique_ptr<T>& ptr) {
    if (ptr) {
        // Recursively call operator<< for the dereferenced object
        os << *ptr;
    } else {
        os << "nullptr";
    }
    return os;
}