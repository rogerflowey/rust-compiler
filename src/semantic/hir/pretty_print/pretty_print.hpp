#pragma once

#include <iostream>
#include <memory>
#include <optional>
#include <variant>
#include <string>
#include <string_view>
#include <sstream>
#include <iomanip>

#include "semantic/hir/hir.hpp"
#include "semantic/hir/visitor/visitor_base.hpp"
#include "semantic/hir/helper.hpp"
#include "type/type.hpp"
#include "semantic/utils.hpp"
#include "semantic/pass/semantic_check/expr_info.hpp"

namespace hir {

// Forward declare visitor structs
struct HirExprVisitor;
struct HirStmtVisitor;
struct HirItemVisitor;
struct HirTypeVisitor;
struct HirPatternVisitor;

class HirPrettyPrinter {
public:
    explicit HirPrettyPrinter(std::ostream& out) : out_(out) {}

    // Main entry points
    void print_program(const Program& program);
    void print_item(const Item& item);
    void print_stmt(const Stmt& stmt);
    void print_expr(const Expr& expr);
    void print_type_node(const TypeNode& type_node);
    void print_pattern(const Pattern& pattern);

    // Helper methods for printing different types
    void print(const Item& item);
    void print(const Stmt& stmt);
    void print(const Expr& expr);
    void print(const TypeNode& type_node);
    void print(const Pattern& pattern);
    void print(const Block& block);
    void print(const AssociatedItem& associated_item);
    
    // Type node specific print methods
    void print(const DefType& def_type);
    void print(const PrimitiveType& primitive_type);
    void print(const ArrayType& array_type);
    void print(const ReferenceType& reference_type);
    void print(const UnitType& unit_type);
    
    // Helper methods
    void print_type_annotation(const TypeAnnotation& annotation);
    void print_expr_info(const std::optional<semantic::ExprInfo>& info);
    void print_pointer(const void* ptr, const char* type_name);
    std::string format_type_id(semantic::TypeId type_id, int depth = 0) const;
    std::string describe_type(semantic::TypeId type_id, int depth) const;
    std::string primitive_kind_to_string(semantic::PrimitiveKind kind) const;
    std::string pointer_to_string(const void* ptr, const char* type_name) const;

private:
    // RAII helper for managing indentation
    class IndentGuard {
    public:
        IndentGuard(HirPrettyPrinter& printer) : printer_(printer) { 
            printer_.indent_level_++; 
        }
        ~IndentGuard() { 
            printer_.indent_level_--; 
        }
    private:
        HirPrettyPrinter& printer_;
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
    
    // Specialized print_field for variants
    template<typename... Types>
    void print_field(const std::string& name, const std::variant<Types...>& variant) {
        prefix();
        out_ << name << ": ";
        std::visit([&](const auto& value) {
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, ast::Identifier>) {
                out_ << value.name; // Identifier has a name field
            } else if constexpr (std::is_same_v<std::decay_t<decltype(value)>, hir::StructDef*> ||
                               std::is_same_v<std::decay_t<decltype(value)>, hir::EnumDef*> ||
                               std::is_same_v<std::decay_t<decltype(value)>, hir::Trait*>) {
                out_ << "ptr(" << value << ")"; // Print pointer values
            } else if constexpr (std::is_same_v<std::decay_t<decltype(value)>, std::variant<hir::StructDef*, hir::EnumDef*, hir::Trait*>>) {
                // Handle nested variant
                std::visit([&](const auto& inner_value) {
                    out_ << "ptr(" << inner_value << ")";
                }, value);
            } else {
                out_ << value;
            }
        }, variant);
        out_ << "\n";
    }
    
    // Specialized print_field for TypeAnnotation
    void print_field(const std::string& name, const TypeAnnotation& type_annotation) {
        prefix();
        out_ << name << ": ";
        std::visit([&](const auto& value) {
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, std::unique_ptr<TypeNode>>) {
                if (value) {
                    print(*value);
                } else {
                    out_ << "nullptr";
                }
            } else {
                out_ << format_type_id(value);
            }
        }, type_annotation);
        out_ << "\n";
    }

    template<typename T>
    void print_ptr_field(const std::string& name, const std::unique_ptr<T>& ptr) {
        prefix();
        out_ << name << ": ";
        if (ptr) {
            out_ << "\n";
            {
                IndentGuard guard(*this);
                print(*ptr);
            }
        } else {
            out_ << "nullptr\n";
        }
    }

    template<typename T>
    void print_optional_ptr_field(const std::string& name, const std::optional<std::unique_ptr<T>>& opt_ptr) {
        prefix();
        out_ << name << ": ";
        if (!opt_ptr) {
            out_ << "nullopt\n";
            return;
        }

        if (*opt_ptr) {
            out_ << "\n";
            {
                IndentGuard guard(*this);
                print(**opt_ptr);
            }
        } else {
            out_ << "nullptr\n";
        }
    }

    template<typename T>
    void print_list_field(const std::string& name, const std::vector<std::unique_ptr<T>>& list) {
        prefix();
        out_ << name << ": [\n";
        {
            IndentGuard guard(*this);
            for (size_t idx = 0; idx < list.size(); ++idx) {
                const auto& elem = list[idx];
                if (elem) {
                    print(*elem);
                } else {
                    prefix();
                    out_ << "nullptr\n";
                }
                if (idx + 1 < list.size()) {
                    prefix();
                    out_ << ",\n";
                }
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
            for (size_t idx = 0; idx < list.size(); ++idx) {
                const auto& elem = list[idx];
                print(elem);
                if (idx + 1 < list.size()) {
                    prefix();
                    out_ << ",\n";
                }
            }
        }
        prefix();
        out_ << "]\n";
    }

    // Make the visitors friends so they can access private members
    friend struct HirExprVisitor;
    friend struct HirStmtVisitor;
    friend struct HirItemVisitor;
    friend struct HirTypeVisitor;
    friend struct HirPatternVisitor;

    std::ostream& out_;
    int indent_level_ = 0;
};

// --- Helper Functions for printing Enums ---

inline const char* to_string(ast::IntegerLiteralExpr::Type suffix_type) {
    switch (suffix_type) {
        case ast::IntegerLiteralExpr::NOT_SPECIFIED: return "NOT_SPECIFIED";
        case ast::IntegerLiteralExpr::I32: return "I32";
        case ast::IntegerLiteralExpr::U32: return "U32";
        case ast::IntegerLiteralExpr::ISIZE: return "ISIZE";
        case ast::IntegerLiteralExpr::USIZE: return "USIZE";
    }
    return "???";
}

inline const char* to_string(UnaryNot::Kind kind) {
    switch (kind) {
        case UnaryNot::Kind::Unspecified: return "Unspecified";
        case UnaryNot::Kind::Bool: return "Bool";
        case UnaryNot::Kind::Int: return "Int";
    }
    return "???";
}

inline const char* to_string(UnaryNegate::Kind kind) {
    switch (kind) {
        case UnaryNegate::Kind::Unspecified: return "Unspecified";
        case UnaryNegate::Kind::SignedInt: return "SignedInt";
        case UnaryNegate::Kind::UnsignedInt: return "UnsignedInt";
    }
    return "???";
}

inline std::string to_string(const UnaryOperator& op) {
    return std::visit(Overloaded{
        [](const UnaryNot& not_op) {
            return std::string("NOT(") + to_string(not_op.kind) + ")";
        },
        [](const UnaryNegate& neg_op) {
            return std::string("NEGATE(") + to_string(neg_op.kind) + ")";
        },
        [](const Dereference&) { return std::string("DEREFERENCE"); },
        [](const Reference& reference) {
            return reference.is_mutable ? std::string("MUTABLE_REFERENCE")
                                         : std::string("REFERENCE");
        }
    }, op);
}

template <typename T>
inline const char* to_arithmetic_kind(T kind) {
    switch (kind) {
        case T::Unspecified: return "Unspecified";
        case T::SignedInt: return "SignedInt";
        case T::UnsignedInt: return "UnsignedInt";
    }
    return "???";
}

template <typename T>
inline const char* to_comparison_kind(T kind) {
    switch (kind) {
        case T::Unspecified: return "Unspecified";
        case T::SignedInt: return "SignedInt";
        case T::UnsignedInt: return "UnsignedInt";
        case T::Bool: return "Bool";
        case T::Char: return "Char";
    }
    return "???";
}

inline std::string to_string(const BinaryOperator& op) {
    return std::visit(Overloaded{
        [](const Add& add) {
            return std::string("ADD(") + to_arithmetic_kind(add.kind) + ")";
        },
        [](const Subtract& sub) {
            return std::string("SUB(") + to_arithmetic_kind(sub.kind) + ")";
        },
        [](const Multiply& mul) {
            return std::string("MUL(") + to_arithmetic_kind(mul.kind) + ")";
        },
        [](const Divide& div) {
            return std::string("DIV(") + to_arithmetic_kind(div.kind) + ")";
        },
        [](const Remainder& rem) {
            return std::string("REM(") + to_arithmetic_kind(rem.kind) + ")";
        },
        [](const LogicalAnd& logical_and) {
            return std::string("AND(") + (logical_and.kind == LogicalAnd::Kind::Bool ? "Bool" : "Unspecified") + ")";
        },
        [](const LogicalOr& logical_or) {
            return std::string("OR(") + (logical_or.kind == LogicalOr::Kind::Bool ? "Bool" : "Unspecified") + ")";
        },
        [](const BitAnd& bit_and) {
            return std::string("BIT_AND(") + to_arithmetic_kind(bit_and.kind) + ")";
        },
        [](const BitXor& bit_xor) {
            return std::string("BIT_XOR(") + to_arithmetic_kind(bit_xor.kind) + ")";
        },
        [](const BitOr& bit_or) {
            return std::string("BIT_OR(") + to_arithmetic_kind(bit_or.kind) + ")";
        },
        [](const ShiftLeft& shift_left) {
            return std::string("SHL(") + to_arithmetic_kind(shift_left.kind) + ")";
        },
        [](const ShiftRight& shift_right) {
            return std::string("SHR(") + to_arithmetic_kind(shift_right.kind) + ")";
        },
        [](const Equal& eq) {
            return std::string("EQ(") + to_comparison_kind(eq.kind) + ")";
        },
        [](const NotEqual& ne) {
            return std::string("NE(") + to_comparison_kind(ne.kind) + ")";
        },
        [](const LessThan& lt) {
            return std::string("LT(") + to_comparison_kind(lt.kind) + ")";
        },
        [](const GreaterThan& gt) {
            return std::string("GT(") + to_comparison_kind(gt.kind) + ")";
        },
        [](const LessEqual& le) {
            return std::string("LE(") + to_comparison_kind(le.kind) + ")";
        },
        [](const GreaterEqual& ge) {
            return std::string("GE(") + to_comparison_kind(ge.kind) + ")";
        }
    }, op);
}

inline const char* to_string(ast::PrimitiveType::Kind kind) {
    switch (kind) {
        case ast::PrimitiveType::I32: return "I32";
        case ast::PrimitiveType::U32: return "U32";
        case ast::PrimitiveType::ISIZE: return "ISIZE";
        case ast::PrimitiveType::USIZE: return "USIZE";
        case ast::PrimitiveType::BOOL: return "BOOL";
        case ast::PrimitiveType::CHAR: return "CHAR";
        case ast::PrimitiveType::STRING: return "STRING";
    }
    return "???";
}

// --- Visitor Struct Definitions ---

struct HirExprVisitor {
    HirPrettyPrinter& p;

    void operator()(const Literal& e) const;
    void operator()(const UnresolvedIdentifier& e) const;
    void operator()(const Variable& e) const;
    void operator()(const ConstUse& e) const;
    void operator()(const FuncUse& e) const;
    void operator()(const TypeStatic& e) const;
    void operator()(const Underscore& e) const;
    void operator()(const FieldAccess& e) const;
    void operator()(const StructLiteral& e) const;
    void operator()(const StructConst& e) const;
    void operator()(const EnumVariant& e) const;
    void operator()(const ArrayLiteral& e) const;
    void operator()(const ArrayRepeat& e) const;
    void operator()(const Index& e) const;
    void operator()(const Assignment& e) const;
    void operator()(const UnaryOp& e) const;
    void operator()(const BinaryOp& e) const;
    void operator()(const Cast& e) const;
    void operator()(const Call& e) const;
    void operator()(const MethodCall& e) const;
    void operator()(const Block& e) const;
    void operator()(const If& e) const;
    void operator()(const Loop& e) const;
    void operator()(const While& e) const;
    void operator()(const Break& e) const;
    void operator()(const Continue& e) const;
    void operator()(const Return& e) const;
};

struct HirStmtVisitor {
    HirPrettyPrinter& p;

    void operator()(const LetStmt& s) const;
    void operator()(const ExprStmt& s) const;
};

struct HirItemVisitor {
    HirPrettyPrinter& p;

    void operator()(const Function& i) const;
    void operator()(const Method& i) const;
    void operator()(const StructDef& i) const;
    void operator()(const EnumDef& i) const;
    void operator()(const ConstDef& i) const;
    void operator()(const Trait& i) const;
    void operator()(const Impl& i) const;
};

struct HirTypeVisitor {
    HirPrettyPrinter& p;

    void operator()(const DefType& t) const;
    void operator()(const PrimitiveType& t) const;
    void operator()(const ArrayType& t) const;
    void operator()(const ReferenceType& t) const;
    void operator()(const UnitType& t) const;
};

struct HirPatternVisitor {
    HirPrettyPrinter& p;

    void operator()(const BindingDef& pat) const;
    void operator()(const ReferencePattern& pat) const;
};

// --- Main Implementation Methods ---

inline void HirPrettyPrinter::print_program(const Program& program) {
    out_ << "Program [\n";
    {
        IndentGuard guard(*this);
        for (const auto& item : program.items) {
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

inline void HirPrettyPrinter::print_item(const Item& item) {
    std::visit(HirItemVisitor{*this}, item.value);
}

inline void HirPrettyPrinter::print(const Item& item) {
    prefix();
    out_ << "Item {\n";
    {
        IndentGuard guard(*this);
        print_item(item);
    }
    prefix();
    out_ << "}\n";
}

inline void HirPrettyPrinter::print(const TypeNode& type_node) {
    prefix();
    out_ << "TypeNode {\n";
    {
        IndentGuard guard(*this);
        print_type_node(type_node);
    }
    prefix();
    out_ << "}\n";
}

inline void HirPrettyPrinter::print(const Expr& expr) {
    print_expr(expr);
}

inline void HirPrettyPrinter::print(const Stmt& stmt) {
    print_stmt(stmt);
}

inline void HirPrettyPrinter::print(const Pattern& pattern) {
    print_pattern(pattern);
}

inline void HirPrettyPrinter::print_stmt(const Stmt& stmt) {
    std::visit(HirStmtVisitor{*this}, stmt.value);
}

inline void HirPrettyPrinter::print_expr(const Expr& expr) {
    std::visit(HirExprVisitor{*this}, expr.value);
    
    // Print expression info if available
    if (expr.expr_info) {
        print_expr_info(expr.expr_info);
    }
}

inline void HirPrettyPrinter::print_type_node(const TypeNode& type_node) {
    std::visit([&](const auto& type_ptr) {
        if (type_ptr) {
            print(*type_ptr);
        } else {
            out_ << "nullptr";
        }
    }, type_node.value);
}

inline void HirPrettyPrinter::print_pattern(const Pattern& pattern) {
    std::visit(HirPatternVisitor{*this}, pattern.value);
}

inline void HirPrettyPrinter::print_type_annotation(const TypeAnnotation& annotation) {
    prefix();
    out_ << "type_annotation: ";
    if (auto* type_node = std::get_if<std::unique_ptr<TypeNode>>(&annotation)) {
        if (*type_node) {
            out_ << "\n";
            {
                IndentGuard guard(*this);
                print_type_node(**type_node);
            }
        } else {
            out_ << "nullptr\n";
        }
    } else if (auto* type_id = std::get_if<semantic::TypeId>(&annotation)) {
        out_ << format_type_id(*type_id) << "\n";
    }
}

inline void HirPrettyPrinter::print_expr_info(const std::optional<semantic::ExprInfo>& info) {
    if (!info) return;
    
    prefix();
    out_ << "expr_info: {\n";
    {
        IndentGuard guard(*this);
        prefix();
    out_ << "type: " << format_type_id(info->type) << "\n";
        prefix();
        out_ << "is_mut: " << (info->is_mut ? "true" : "false") << "\n";
        prefix();
        out_ << "is_place: " << (info->is_place ? "true" : "false") << "\n";
        
        prefix();
        out_ << "endpoints: {";
        bool first = true;
        for (const auto& endpoint : info->endpoints) {
            if (!first) out_ << ", ";
            std::visit([&](const auto& ep) {
                using T = std::decay_t<decltype(ep)>;
                if constexpr (std::is_same_v<T, semantic::NormalEndpoint>) {
                    out_ << "Normal";
                } else if constexpr (std::is_same_v<T, semantic::BreakEndpoint>) {
                    out_ << "Break";
                } else if constexpr (std::is_same_v<T, semantic::ContinueEndpoint>) {
                    out_ << "Continue";
                } else if constexpr (std::is_same_v<T, semantic::ReturnEndpoint>) {
                    out_ << "Return";
                }
            }, endpoint);
            first = false;
        }
        out_ << "}\n";
    }
    prefix();
    out_ << "}\n";
}

inline void HirPrettyPrinter::print_pointer(const void* ptr, const char* type_name) {
    if (ptr) {
        out_ << type_name << "@0x" << std::hex << reinterpret_cast<uintptr_t>(ptr) << std::dec;
    } else {
        out_ << "nullptr";
    }
}

inline std::string HirPrettyPrinter::pointer_to_string(const void* ptr, const char* type_name) const {
    if (!ptr) {
        return "nullptr";
    }
    std::ostringstream oss;
    oss << type_name << "@0x" << std::hex << reinterpret_cast<uintptr_t>(ptr) << std::dec;
    return oss.str();
}

inline std::string HirPrettyPrinter::primitive_kind_to_string(semantic::PrimitiveKind kind) const {
    switch (kind) {
        case semantic::PrimitiveKind::I32: return "I32";
        case semantic::PrimitiveKind::U32: return "U32";
        case semantic::PrimitiveKind::ISIZE: return "ISIZE";
        case semantic::PrimitiveKind::USIZE: return "USIZE";
        case semantic::PrimitiveKind::BOOL: return "BOOL";
        case semantic::PrimitiveKind::CHAR: return "CHAR";
        case semantic::PrimitiveKind::STRING: return "STRING";
    }
    return "UNKNOWN";
}

inline std::string HirPrettyPrinter::describe_type(semantic::TypeId type_id, int depth) const {
    constexpr int kMaxDepth = 8;
    if (!type_id) {
        return "null";
    }
    if (depth > kMaxDepth) {
        return "...";
    }

    const semantic::Type& type = type::get_type_from_id(type_id);
    return std::visit(
        [&](const auto& value) -> std::string {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, semantic::PrimitiveKind>) {
                return std::string("Primitive(") + primitive_kind_to_string(value) + ")";
            } else if constexpr (std::is_same_v<T, semantic::StructType>) {
                const auto& info = semantic::TypeContext::get_instance().get_struct(value.id);
                return std::string("Struct(") + info.name + "#" + std::to_string(value.id) + ")";
            } else if constexpr (std::is_same_v<T, semantic::EnumType>) {
                const auto& info = semantic::TypeContext::get_instance().get_enum(value.id);
                return std::string("Enum(") + info.name + "#" + std::to_string(value.id) + ")";
            } else if constexpr (std::is_same_v<T, semantic::ReferenceType>) {
                std::string inner = describe_type(value.referenced_type, depth + 1);
                return std::string("Reference(") + (value.is_mutable ? "mut" : "const") + ", " + inner + ")";
            } else if constexpr (std::is_same_v<T, semantic::ArrayType>) {
                std::string inner = describe_type(value.element_type, depth + 1);
                std::ostringstream oss;
                oss << "Array(size=" << value.size << ", elem=" << inner << ")";
                return oss.str();
            } else if constexpr (std::is_same_v<T, semantic::UnitType>) {
                return "Unit";
            } else if constexpr (std::is_same_v<T, semantic::NeverType>) {
                return "Never";
            } else if constexpr (std::is_same_v<T, semantic::UnderscoreType>) {
                return "Underscore";
            } else {
                return "<unknown type>";
            }
        },
        type.value);
}

inline std::string HirPrettyPrinter::format_type_id(semantic::TypeId type_id, int depth) const {
    std::ostringstream oss;
    oss << "TypeId{";
    if (type_id == semantic::invalid_type_id) {
        oss << "invalid";
    } else {
        oss << "type=" << describe_type(type_id, depth + 1);
        oss << ", id=" << type_id;
    }
    oss << "}";
    return oss.str();
}

// --- Expression Visitor Implementations ---

inline void HirExprVisitor::operator()(const Literal& e) const {
    p.prefix();
    p.out_ << "Literal {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.prefix();
        p.out_ << "value: ";
        std::visit([&](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, Literal::Integer>) {
                p.out_ << "Integer { value: " << val.value
                       << ", suffix_type: " << to_string(val.suffix_type)
                       << ", is_negative: " << (val.is_negative ? "true" : "false") << " }";
            } else if constexpr (std::is_same_v<T, bool>) {
                p.out_ << "Bool { value: " << (val ? "true" : "false") << " }";
            } else if constexpr (std::is_same_v<T, char>) {
                p.out_ << "Char { value: '" << val << "' }";
            } else if constexpr (std::is_same_v<T, Literal::String>) {
                p.out_ << "String { value: \"" << val.value << "\", is_cstyle: " << (val.is_cstyle ? "true" : "false") << " }";
            }
        }, e.value);
        p.out_ << "\n";
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const UnresolvedIdentifier& e) const {
    p.prefix();
    p.out_ << "UnresolvedIdentifier {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_field("name", e.name.name);
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const Variable& e) const {
    p.prefix();
    p.out_ << "Variable {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.prefix();
        p.out_ << "local_id: ";
        p.print_pointer(e.local_id, "Local");
        p.out_ << "\n";
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const ConstUse& e) const {
    p.prefix();
    p.out_ << "ConstUse {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.prefix();
        p.out_ << "def: ";
        p.print_pointer(e.def, "ConstDef");
        p.out_ << "\n";
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const FuncUse& e) const {
    p.prefix();
    p.out_ << "FuncUse {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.prefix();
        p.out_ << "def: ";
        p.print_pointer(e.def, "Function");
        p.out_ << "\n";
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const TypeStatic& e) const {
    p.prefix();
    p.out_ << "TypeStatic {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.prefix();
        p.out_ << "type: ";
        std::visit([&](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, ast::Identifier>) {
                p.out_ << "Identifier(\"" << val.name << "\")";
            } else {
                p.out_ << "TypeDef";
            }
        }, e.type);
        p.out_ << "\n";
        p.print_field("name", e.name.name);
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const Underscore& e) const {
    (void)e;
    p.prefix();
    p.out_ << "Underscore {}\n";
}

inline void HirExprVisitor::operator()(const FieldAccess& e) const {
    p.prefix();
    p.out_ << "FieldAccess {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_ptr_field("base", e.base);
        p.prefix();
        p.out_ << "field: ";
        std::visit([&](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, ast::Identifier>) {
                p.out_ << "Identifier(\"" << val.name << "\")";
            } else {
                p.out_ << "Index(" << val << ")";
            }
        }, e.field);
        p.out_ << "\n";
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const StructLiteral& e) const {
    p.prefix();
    p.out_ << "StructLiteral {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.prefix();
        p.out_ << "struct_path: ";
        std::visit([&](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, ast::Identifier>) {
                p.out_ << "Identifier(\"" << val.name << "\")";
            } else {
                p.out_ << "StructDef@";
                p.print_pointer(val, "StructDef");
            }
        }, e.struct_path);
        p.out_ << "\n";
        p.prefix();
        p.out_ << "fields: ";
        std::visit([&](const auto& fields_variant) {
            using T = std::decay_t<decltype(fields_variant)>;
            if constexpr (std::is_same_v<T, StructLiteral::SyntacticFields>) {
                p.out_ << "SyntacticFields [\n";
                {
                    HirPrettyPrinter::IndentGuard guard2(p);
                    for (const auto& field : fields_variant.initializers) {
                        p.prefix();
                        p.out_ << "Identifier(\"" << field.first.name << "\"): ";
                        if (field.second) {
                            p.print(*field.second);
                        } else {
                            p.out_ << "nullptr";
                        }
                        p.out_ << ",\n";
                    }
                }
                p.prefix();
                p.out_ << "]";
            } else if constexpr (std::is_same_v<T, StructLiteral::CanonicalFields>) {
                p.out_ << "CanonicalFields [\n";
                {
                    HirPrettyPrinter::IndentGuard guard2(p);
                    for (const auto& field : fields_variant.initializers) {
                        p.prefix();
                        if (field) {
                            p.print(*field);
                        } else {
                            p.out_ << "nullptr";
                        }
                        p.out_ << ",\n";
                    }
                }
                p.prefix();
                p.out_ << "]";
            }
        }, e.fields);
        p.out_ << "\n";
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const StructConst& e) const {
    p.prefix();
    p.out_ << "StructConst {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.prefix();
        p.out_ << "struct_def: ";
        p.print_pointer(e.struct_def, "StructDef");
        p.out_ << "\n";
        p.prefix();
        p.out_ << "assoc_const: ";
        p.print_pointer(e.assoc_const, "ConstDef");
        p.out_ << "\n";
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const EnumVariant& e) const {
    p.prefix();
    p.out_ << "EnumVariant {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.prefix();
        p.out_ << "enum_def: ";
        p.print_pointer(e.enum_def, "EnumDef");
        p.out_ << "\n";
        p.print_field("variant_index", e.variant_index);
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const ArrayLiteral& e) const {
    p.prefix();
    p.out_ << "ArrayLiteral {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_list_field("elements", e.elements);
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const ArrayRepeat& e) const {
    p.prefix();
    p.out_ << "ArrayRepeat {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_ptr_field("value", e.value);
        p.prefix();
        p.out_ << "count: ";
        std::visit([&](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::unique_ptr<Expr>>) {
                if (val) {
                    p.out_ << "\n";
                    {
                        HirPrettyPrinter::IndentGuard guard2(p);
                        p.print(*val);
                    }
                    p.prefix();
                } else {
                    p.out_ << "nullptr";
                }
            } else {
                p.out_ << val;
            }
        }, e.count);
        p.out_ << "\n";
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const Index& e) const {
    p.prefix();
    p.out_ << "Index {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_ptr_field("base", e.base);
        p.print_ptr_field("index", e.index);
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const Assignment& e) const {
    p.prefix();
    p.out_ << "Assignment {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_ptr_field("lhs", e.lhs);
        p.print_ptr_field("rhs", e.rhs);
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const UnaryOp& e) const {
    p.prefix();
    p.out_ << "UnaryOp {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_field("op", to_string(e.op));
        p.print_ptr_field("rhs", e.rhs);
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const BinaryOp& e) const {
    p.prefix();
    p.out_ << "BinaryOp {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_field("op", to_string(e.op));
        p.print_ptr_field("lhs", e.lhs);
        p.print_ptr_field("rhs", e.rhs);
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const Cast& e) const {
    p.prefix();
    p.out_ << "Cast {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_ptr_field("expr", e.expr);
        p.print_type_annotation(e.target_type);
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const Call& e) const {
    p.prefix();
    p.out_ << "Call {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_ptr_field("callee", e.callee);
        p.print_list_field("args", e.args);
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const MethodCall& e) const {
    p.prefix();
    p.out_ << "MethodCall {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_ptr_field("receiver", e.receiver);
        p.prefix();
        p.out_ << "method: ";
        std::visit([&](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, ast::Identifier>) {
                p.out_ << "Identifier(\"" << val.name << "\")";
            } else {
                p.out_ << "Method@";
                p.print_pointer(val, "Method");
            }
        }, e.method);
        p.out_ << "\n";
        p.print_list_field("args", e.args);
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const Block& e) const {
    p.prefix();
    p.out_ << "Block {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        if (!e.items.empty()) {
            p.print_list_field("items", e.items);
        }
        if (!e.stmts.empty()) {
            p.print_list_field("stmts", e.stmts);
        }
        p.print_optional_ptr_field("final_expr", e.final_expr);
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const If& e) const {
    p.prefix();
    p.out_ << "If {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_ptr_field("condition", e.condition);
        p.print_ptr_field("then_block", e.then_block);
        p.print_optional_ptr_field("else_expr", e.else_expr);
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const Loop& e) const {
    p.prefix();
    p.out_ << "Loop {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_ptr_field("body", e.body);
        p.prefix();
        p.out_ << "break_type: ";
        if (e.break_type) {
            p.out_ << p.format_type_id(*e.break_type);
        } else {
            p.out_ << "nullopt";
        }
        p.out_ << "\n";
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const While& e) const {
    p.prefix();
    p.out_ << "While {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_ptr_field("condition", e.condition);
        p.print_ptr_field("body", e.body);
        p.prefix();
        p.out_ << "break_type: ";
        if (e.break_type) {
            p.out_ << p.format_type_id(*e.break_type);
        } else {
            p.out_ << "nullopt";
        }
        p.out_ << "\n";
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const Break& e) const {
    p.prefix();
    p.out_ << "Break {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_optional_ptr_field("value", e.value);
        p.prefix();
        p.out_ << "target: ";
        if (e.target) {
            std::visit([&](const auto* target) {
                p.print_pointer(target, typeid(*target).name());
            }, *e.target);
        } else {
            p.out_ << "nullopt";
        }
        p.out_ << "\n";
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const Continue& e) const {
    p.prefix();
    p.out_ << "Continue {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.prefix();
        p.out_ << "target: ";
        if (e.target) {
            std::visit([&](const auto* target) {
                p.print_pointer(target, typeid(*target).name());
            }, *e.target);
        } else {
            p.out_ << "nullopt";
        }
        p.out_ << "\n";
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirExprVisitor::operator()(const Return& e) const {
    p.prefix();
    p.out_ << "Return {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_optional_ptr_field("value", e.value);
        p.prefix();
        p.out_ << "target: ";
        if (e.target) {
            std::visit([&](const auto* target) {
                p.print_pointer(target, typeid(*target).name());
            }, *e.target);
        } else {
            p.out_ << "nullopt";
        }
        p.out_ << "\n";
    }
    p.prefix();
    p.out_ << "}\n";
}

// --- Statement Visitor Implementations ---

inline void HirStmtVisitor::operator()(const LetStmt& s) const {
    p.prefix();
    p.out_ << "LetStmt {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_ptr_field("pattern", s.pattern);
        p.prefix();
        p.out_ << "type_annotation: ";
        if (s.type_annotation) {
            p.out_ << "\n";
            {
                HirPrettyPrinter::IndentGuard guard2(p);
                p.print_type_annotation(*s.type_annotation);
            }
        } else {
            p.out_ << "nullopt\n";
        }
        p.print_ptr_field("initializer", s.initializer);
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirStmtVisitor::operator()(const ExprStmt& s) const {
    p.prefix();
    p.out_ << "ExprStmt {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_ptr_field("expr", s.expr);
    }
    p.prefix();
    p.out_ << "}\n";
}

// --- Item Visitor Implementations ---

inline void HirItemVisitor::operator()(const Function& i) const {
    p.prefix();
    p.out_ << "Function@";
    p.print_pointer(&i, "Function");
    p.out_ << " {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_field("name", i.name.name);
        if (!i.params.empty()) {
            p.print_list_field("params", i.params);
            p.prefix();
            p.out_ << "param_type_annotations: [\n";
            {
                HirPrettyPrinter::IndentGuard guard2(p);
                for (const auto& annotation : i.param_type_annotations) {
                    if (annotation) {
                        p.print_type_annotation(*annotation);
                    } else {
                        p.prefix();
                        p.out_ << "nullopt\n";
                    }
                }
            }
            p.prefix();
            p.out_ << "]\n";
        }
        p.prefix();
        p.out_ << "return_type: ";
        if (i.return_type) {
            p.out_ << "\n";
            {
                HirPrettyPrinter::IndentGuard guard2(p);
                p.print_type_annotation(*i.return_type);
            }
        } else {
            p.out_ << "nullopt\n";
        }
        p.print_ptr_field("body", i.body);
        if (!i.locals.empty()) {
            p.prefix();
            p.out_ << "locals: [\n";
            {
                HirPrettyPrinter::IndentGuard guard2(p);
                for (const auto& local : i.locals) {
                    if (local) {
                        p.prefix();
                        p.out_ << "Local@";
                        p.print_pointer(local.get(), "Local");
                        p.out_ << " {\n";
                        {
                            HirPrettyPrinter::IndentGuard guard3(p);
                            p.print_field("name", local->name.name);
                            p.prefix();
                            p.out_ << "is_mutable: " << (local->is_mutable ? "true" : "false") << "\n";
                            if (local->type_annotation) {
                                p.print_type_annotation(*local->type_annotation);
                            }
                        }
                        p.prefix();
                        p.out_ << "}\n";
                    } else {
                        p.prefix();
                        p.out_ << "nullptr\n";
                    }
                }
            }
            p.prefix();
            p.out_ << "]\n";
        }
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirItemVisitor::operator()(const Method& i) const {
    p.prefix();
    p.out_ << "Method@";
    p.print_pointer(&i, "Method");
    p.out_ << " {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_field("name", i.name.name);
        p.prefix();
        p.out_ << "self_param: SelfParam { is_reference: "
               << (i.self_param.is_reference ? "true" : "false")
               << ", is_mutable: " << (i.self_param.is_mutable ? "true" : "false") << " }\n";
        if (!i.params.empty()) {
            p.print_list_field("params", i.params);
            p.prefix();
            p.out_ << "param_type_annotations: [\n";
            {
                HirPrettyPrinter::IndentGuard guard2(p);
                for (const auto& annotation : i.param_type_annotations) {
                    if (annotation) {
                        p.print_type_annotation(*annotation);
                    } else {
                        p.prefix();
                        p.out_ << "nullopt\n";
                    }
                }
            }
            p.prefix();
            p.out_ << "]\n";
        }
        p.prefix();
        p.out_ << "return_type: ";
        if (i.return_type) {
            p.out_ << "\n";
            {
                HirPrettyPrinter::IndentGuard guard2(p);
                p.print_type_annotation(*i.return_type);
            }
        } else {
            p.out_ << "nullopt\n";
        }
        p.print_ptr_field("body", i.body);
        if (!i.locals.empty()) {
            p.prefix();
            p.out_ << "locals: [\n";
            {
                HirPrettyPrinter::IndentGuard guard2(p);
                for (const auto& local : i.locals) {
                    if (local) {
                        p.prefix();
                        p.out_ << "Local@";
                        p.print_pointer(local.get(), "Local");
                        p.out_ << " {\n";
                        {
                            HirPrettyPrinter::IndentGuard guard3(p);
                            p.print_field("name", local->name.name);
                            p.prefix();
                            p.out_ << "is_mutable: " << (local->is_mutable ? "true" : "false") << "\n";
                            if (local->type_annotation) {
                                p.print_type_annotation(*local->type_annotation);
                            }
                        }
                        p.prefix();
                        p.out_ << "}\n";
                    } else {
                        p.prefix();
                        p.out_ << "nullptr\n";
                    }
                }
            }
            p.prefix();
            p.out_ << "]\n";
        }
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirItemVisitor::operator()(const StructDef& i) const {
    p.prefix();
    p.out_ << "StructDef@";
    p.print_pointer(&i, "StructDef");
    p.out_ << " {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_field("name", i.name.name);
        if (!i.fields.empty()) {
            p.prefix();
            p.out_ << "fields: [\n";
            {
                HirPrettyPrinter::IndentGuard guard2(p);
                for (const auto& field : i.fields) {
                    p.prefix();
                    p.out_ << "{ name: \"" << field.name.name << "\"";
                    if (field.type) {
                        p.out_ << ", type: " << p.format_type_id(*field.type);
                    } else {
                        p.out_ << ", type: nullopt";
                    }
                    p.out_ << " }\n";
                }
            }
            p.prefix();
            p.out_ << "]\n";
        }
        if (!i.field_type_annotations.empty()) {
            p.prefix();
            p.out_ << "field_type_annotations: [\n";
            {
                HirPrettyPrinter::IndentGuard guard2(p);
                for (const auto& type_ann : i.field_type_annotations) {
                    p.print_type_annotation(type_ann);
                }
            }
            p.prefix();
            p.out_ << "]\n";
        }
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirItemVisitor::operator()(const EnumDef& i) const {
    p.prefix();
    p.out_ << "EnumDef@";
    p.print_pointer(&i, "EnumDef");
    p.out_ << " {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_field("name", i.name.name);
        if (!i.variants.empty()) {
            p.prefix();
            p.out_ << "variants: [\n";
            {
                HirPrettyPrinter::IndentGuard guard2(p);
                for (const auto& variant : i.variants) {
                    p.prefix();
                    p.out_ << "{ name: \"" << variant.name.name << "\" }\n";
                }
            }
            p.prefix();
            p.out_ << "]\n";
        }
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirItemVisitor::operator()(const ConstDef& i) const {
    p.prefix();
    p.out_ << "ConstDef@";
    p.print_pointer(&i, "ConstDef");
    p.out_ << " {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_field("name", i.name.name);
        p.print_ptr_field("expr", i.expr);
        p.prefix();
        p.out_ << "const_value: " << (i.const_value ? "some_const_value" : "nullopt") << "\n";
        p.prefix();
        p.out_ << "type: ";
        if (i.type) {
            p.out_ << "\n";
            {
                HirPrettyPrinter::IndentGuard guard2(p);
                p.print_type_annotation(*i.type);
            }
        } else {
            p.out_ << "nullopt\n";
        }
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirItemVisitor::operator()(const Trait& i) const {
    p.prefix();
    p.out_ << "Trait@";
    p.print_pointer(&i, "Trait");
    p.out_ << " {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_field("name", i.name.name);
        p.print_list_field("items", i.items);
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirItemVisitor::operator()(const Impl& i) const {
    p.prefix();
    p.out_ << "Impl@";
    p.print_pointer(&i, "Impl");
    p.out_ << " {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.prefix();
        p.out_ << "trait: ";
        if (i.trait) {
            std::visit([&](const auto& val) {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, ast::Identifier>) {
                    p.out_ << "Identifier(\"" << val.name << "\")";
                } else {
                    p.out_ << "Trait@";
                    p.print_pointer(val, "Trait");
                }
            }, *i.trait);
        } else {
            p.out_ << "nullopt (inherent impl)";
        }
        p.out_ << "\n";
        p.print_type_annotation(i.for_type);
        p.print_list_field("items", i.items);
    }
    p.prefix();
    p.out_ << "}\n";
}

// --- Type Visitor Implementations ---

inline void HirTypeVisitor::operator()(const DefType& t) const {
    p.prefix();
    p.out_ << "DefType {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.prefix();
        p.out_ << "def: ";
        std::visit([&](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, ast::Identifier>) {
                p.out_ << "Identifier(\"" << val.name << "\")";
            } else {
                p.out_ << "TypeDef";
            }
        }, t.def);
        p.out_ << "\n";
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirTypeVisitor::operator()(const PrimitiveType& t) const {
    p.prefix();
    p.out_ << "PrimitiveType {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_field("kind", std::string_view(to_string(t.kind)));
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirTypeVisitor::operator()(const ArrayType& t) const {
    p.prefix();
    p.out_ << "ArrayType {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_type_annotation(t.element_type);
        p.print_ptr_field("size", t.size);
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirTypeVisitor::operator()(const ReferenceType& t) const {
    p.prefix();
    p.out_ << "ReferenceType {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_field("is_mutable", t.is_mutable);
        p.print_type_annotation(t.referenced_type);
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirTypeVisitor::operator()(const UnitType&) const {
    p.prefix();
    p.out_ << "UnitType {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
    }
    p.prefix();
    p.out_ << "}\n";
}

// --- Pattern Visitor Implementations ---

inline void HirPatternVisitor::operator()(const BindingDef& pat) const {
    p.prefix();
    p.out_ << "BindingDef {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.prefix();
        p.out_ << "local: ";
        std::visit([&](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, BindingDef::Unresolved>) {
                p.out_ << "Unresolved { is_mutable: " << (val.is_mutable ? "true" : "false")
                       << ", is_ref: " << (val.is_ref ? "true" : "false")
                       << ", name: \"" << val.name.name << "\" }";
            } else {
                p.out_ << "Local@";
                p.print_pointer(val, "Local");
            }
        }, pat.local);
        p.out_ << "\n";
    }
    p.prefix();
    p.out_ << "}\n";
}

inline void HirPatternVisitor::operator()(const ReferencePattern& pat) const {
    p.prefix();
    p.out_ << "ReferencePattern {\n";
    {
        HirPrettyPrinter::IndentGuard guard(p);
        p.print_field("is_mutable", pat.is_mutable);
        p.print_ptr_field("subpattern", pat.subpattern);
    }
    p.prefix();
    p.out_ << "}\n";
}

// --- Stream Operators ---

inline std::ostream& operator<<(std::ostream& os, const Program& program) {
    HirPrettyPrinter(os).print_program(program);
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const Item& item) {
    HirPrettyPrinter(os).print_item(item);
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const Stmt& stmt) {
    HirPrettyPrinter(os).print_stmt(stmt);
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const Expr& expr) {
    HirPrettyPrinter(os).print_expr(expr);
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const TypeNode& type_node) {
    HirPrettyPrinter(os).print_type_node(type_node);
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const Pattern& pattern) {
    HirPrettyPrinter(os).print_pattern(pattern);
    return os;
}

// Generic overload for unique_ptr to any HIR node
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

// Type node specific print method implementations
inline void HirPrettyPrinter::print(const DefType& def_type) {
    prefix();
    out_ << "DefType {\n";
    {
        IndentGuard guard(*this);
        print_field("def", def_type.def);
    }
    prefix();
    out_ << "}\n";
}

inline void HirPrettyPrinter::print(const PrimitiveType& primitive_type) {
    prefix();
    out_ << "PrimitiveType {\n";
    {
        IndentGuard guard(*this);
        print_field("kind", static_cast<int>(primitive_type.kind));
    }
    prefix();
    out_ << "}\n";
}

inline void HirPrettyPrinter::print(const ArrayType& array_type) {
    prefix();
    out_ << "ArrayType {\n";
    {
        IndentGuard guard(*this);
        print_field("element_type", array_type.element_type);
        print_ptr_field("size", array_type.size);
    }
    prefix();
    out_ << "}\n";
}

inline void HirPrettyPrinter::print(const ReferenceType& reference_type) {
    prefix();
    out_ << "ReferenceType {\n";
    {
        IndentGuard guard(*this);
        print_field("referenced_type", reference_type.referenced_type);
        print_field("is_mutable", reference_type.is_mutable);
    }
    prefix();
    out_ << "}\n";
}

inline void HirPrettyPrinter::print(const UnitType& /* unit_type */) {
    prefix();
    out_ << "UnitType {}\n";
}

inline void HirPrettyPrinter::print(const Block& block) {
    prefix();
    out_ << "Block {\n";
    {
        IndentGuard guard(*this);
        print_list_field("items", block.items);
        print_list_field("stmts", block.stmts);
        print_optional_ptr_field("final_expr", block.final_expr);
    }
    prefix();
    out_ << "}\n";
}

inline void HirPrettyPrinter::print(const AssociatedItem& associated_item) {
    std::visit(HirItemVisitor{*this}, associated_item.value);
}

} // namespace hir