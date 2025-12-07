#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <limits>
#include <variant>
#include <vector>

namespace hir {
struct StructDef;
struct EnumDef;
};

namespace type {

struct Type;

using TypeId = std::uint32_t;
inline constexpr TypeId invalid_type_id = std::numeric_limits<TypeId>::max();

using StructId = std::uint32_t;
using EnumId = std::uint32_t;

enum class PrimitiveKind {
    I32,
    U32,
    ISIZE,
    USIZE,
    BOOL,
    CHAR,
    STRING,
};

struct StructType {
    StructId id;

    bool operator==(const StructType& other) const { return id == other.id; }
};

struct EnumType {
    EnumId id;

    bool operator==(const EnumType& other) const { return id == other.id; }
};

struct ReferenceType {
    TypeId referenced_type = invalid_type_id;
    bool is_mutable = false;

    bool operator==(const ReferenceType& other) const {
        return referenced_type == other.referenced_type && is_mutable == other.is_mutable;
    }
};

struct ArrayType {
    TypeId element_type = invalid_type_id;
    size_t size = 0;

    bool operator==(const ArrayType& other) const {
        return element_type == other.element_type && size == other.size;
    }
};

struct UnitType {
    bool operator==(const UnitType&) const { return true; }
};
struct NeverType {
    bool operator==(const NeverType&) const { return true; }
};
struct UnderscoreType {
    bool operator==(const UnderscoreType&) const { return true; }
};


using TypeVariant = std::variant<
    PrimitiveKind,
    StructType,
    EnumType,
    ReferenceType,
    ArrayType,
    UnitType,
    NeverType,
    UnderscoreType
>;

struct Type {
    TypeVariant value;

    bool operator==(const Type& other) const { return value == other.value; }
};

struct TypeHash {
    size_t operator()(const PrimitiveKind& pk) const { return std::hash<int>()(static_cast<int>(pk)); }
    size_t operator()(const StructType& st) const { return std::hash<StructId>()(st.id); }
    size_t operator()(const EnumType& et) const { return std::hash<EnumId>()(et.id); }
    size_t operator()(const ReferenceType& rt) const {
        return std::hash<TypeId>()(rt.referenced_type) ^ (std::hash<bool>()(rt.is_mutable) << 1);
    }
    size_t operator()(const ArrayType& at) const {
        return std::hash<TypeId>()(at.element_type) ^ (std::hash<size_t>()(at.size) << 1);
    }
    size_t operator()(const UnitType&) const { return 0xDABCABCC; }
    size_t operator()(const NeverType&) const { return 0xDABCABCD; }
    size_t operator()(const UnderscoreType&) const { return 0xDABCABCE; }
    size_t operator()(const Type& t) const { return std::visit(*this, t.value); }

};

struct StructFieldInfo {
    std::string name;
    TypeId type = invalid_type_id;
};

struct StructInfo {
    std::string name;
    std::vector<StructFieldInfo> fields;
};

struct EnumVariantInfo {
    std::string name;
};

struct EnumInfo {
    std::string name;
    std::vector<EnumVariantInfo> variants;
};

class TypeContext {
public:
    TypeContext();

    TypeId get_id(const Type& t);

    StructId register_struct(StructInfo info, const hir::StructDef* def = nullptr) {
        StructId id = static_cast<StructId>(structs.size());
        structs.push_back(std::move(info));
        struct_defs.push_back(def);
        if (def) {
            struct_ids.emplace(def, id);
        }
        return id;
    }

    EnumId register_enum(EnumInfo info, const hir::EnumDef* def = nullptr) {
        EnumId id = static_cast<EnumId>(enums.size());
        enums.push_back(std::move(info));
        enum_defs.push_back(def);
        if (def) {
            enum_ids.emplace(def, id);
        }
        return id;
    }

    // Lookup methods: get the ID for an already-registered struct/enum
    // Returns invalid_struct_id (std::numeric_limits<StructId>::max()) if not found
    StructId get_struct_id(const hir::StructDef* def) const {
        auto it = struct_ids.find(def);
        return it != struct_ids.end() ? it->second : std::numeric_limits<StructId>::max();
    }
    
    // Returns invalid_enum_id (std::numeric_limits<EnumId>::max()) if not found
    EnumId get_enum_id(const hir::EnumDef* def) const {
        auto it = enum_ids.find(def);
        return it != enum_ids.end() ? it->second : std::numeric_limits<EnumId>::max();
    }

    // Safer lookup: returns std::optional, forcing explicit handling of missing IDs
    // Preferred over get_struct_id() for new code
    std::optional<StructId> try_get_struct_id(const hir::StructDef* def) const {
        if (!def) return std::nullopt;
        auto it = struct_ids.find(def);
        if (it != struct_ids.end()) return it->second;
        return std::nullopt;
    }
    
    std::optional<EnumId> try_get_enum_id(const hir::EnumDef* def) const {
        if (!def) return std::nullopt;
        auto it = enum_ids.find(def);
        if (it != enum_ids.end()) return it->second;
        return std::nullopt;
    }

    const StructInfo& get_struct(StructId id) const { return structs.at(id); }
    const EnumInfo& get_enum(EnumId id) const { return enums.at(id); }
    hir::StructDef* get_struct_def(StructId id) const {
        return id < struct_defs.size() ? const_cast<hir::StructDef*>(struct_defs[id]) : nullptr;
    }
    hir::EnumDef* get_enum_def(EnumId id) const {
        return id < enum_defs.size() ? const_cast<hir::EnumDef*>(enum_defs[id]) : nullptr;
    }

    const std::vector<StructInfo>& get_all_structs() const { return structs; };

    const Type& get_type(TypeId id) const;
    Type get_type_copy(TypeId id) const;

    static TypeContext& get_instance() {
        static TypeContext instance;
        return instance;
    };
private:
    std::unordered_map<Type, TypeId, TypeHash> registered_types;
    std::vector<Type> types;
    std::unordered_map<const hir::StructDef*, StructId> struct_ids;
    std::unordered_map<const hir::EnumDef*, EnumId> enum_ids;
    std::vector<StructInfo> structs;
    std::vector<EnumInfo> enums;
    std::vector<const hir::StructDef*> struct_defs;
    std::vector<const hir::EnumDef*> enum_defs;

    static StructInfo make_struct_info(const hir::StructDef& def);
    static EnumInfo make_enum_info(const hir::EnumDef& def);
};

inline TypeContext::TypeContext() = default;


inline TypeId get_typeID(const Type& t) {
    return TypeContext::get_instance().get_id(t);
}
inline const Type& get_type_from_id(TypeId id) {
    return TypeContext::get_instance().get_type(id);
}
inline const StructInfo& get_struct(StructId id) {
    return TypeContext::get_instance().get_struct(id);
}
inline const EnumInfo& get_enum(EnumId id) {
    return TypeContext::get_instance().get_enum(id);
}

} // namespace type

// Temporary compatibility exports while semantic code transitions to the standalone
// type namespace. All type system ownership remains in `namespace type`.
namespace semantic {
using type::ArrayType;
using type::EnumId;
using type::EnumInfo;
using type::EnumType;
using type::PrimitiveKind;
using type::ReferenceType;
using type::StructFieldInfo;
using type::StructId;
using type::StructInfo;
using type::StructType;
using type::Type;
using type::TypeContext;
using type::TypeHash;
using type::TypeId;
using type::TypeVariant;
using type::UnitType;
using type::NeverType;
using type::UnderscoreType;
using type::get_type_from_id;
using type::get_typeID;
inline constexpr auto invalid_type_id = type::invalid_type_id;
} // namespace semantic
