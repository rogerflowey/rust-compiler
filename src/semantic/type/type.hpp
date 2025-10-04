#pragma once

#include <variant>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <functional>


namespace hir{
    struct StructDef;
    struct EnumDef;
};

namespace semantic {

struct Type;

using TypeId = const Type*;

enum class PrimitiveKind { I32, U32, ISIZE, USIZE, BOOL, CHAR, STRING};

struct StructType {
    const hir::StructDef* symbol;
    
    bool operator==(const StructType& other) const {
        return symbol == other.symbol;
    }
};

struct EnumType {
    const hir::EnumDef* symbol;

    bool operator==(const EnumType& other) const {
        return symbol == other.symbol;
    }
};

struct ReferenceType {
    TypeId referenced_type;
    bool is_mutable;
    
    bool operator==(const ReferenceType& other) const {
        return referenced_type == other.referenced_type && is_mutable == other.is_mutable;
    }
};

struct ArrayType {
    TypeId element_type;
    size_t size;
    
    bool operator==(const ArrayType& other) const {
        return element_type == other.element_type && size == other.size;
    }
};

struct UnitType{
    bool operator==(const UnitType&) const {
        return true;
    }
};
struct NeverType{
    bool operator==(const NeverType&) const {
        return true;
    }
};


using TypeVariant = std::variant<
    PrimitiveKind,
    StructType,
    EnumType,
    ReferenceType,
    ArrayType,
    UnitType,
    NeverType
>;

struct Type {
    TypeVariant value;

    bool operator==(const Type& other) const{
        return value == other.value;
    }
};

struct TypeHash {
    size_t operator()(const PrimitiveKind& pk) const {
        return std::hash<int>()(static_cast<int>(pk));
    }
    size_t operator()(const StructType& st) const {
        return std::hash<const hir::StructDef*>()(st.symbol);
    }
    size_t operator()(const EnumType& et) const {
        return std::hash<const hir::EnumDef*>()(et.symbol);
    }
    size_t operator()(const ReferenceType& rt) const {
        return std::hash<TypeId>()(rt.referenced_type) ^ std::hash<bool>()(rt.is_mutable);
    }
    size_t operator()(const ArrayType& at) const {
        return std::hash<TypeId>()(at.element_type) ^ std::hash<size_t>()(at.size);
    }
    size_t operator()(const UnitType&) const {
        return 0xDABCABCC;
    }
    size_t operator()(const NeverType&) const {
        return 0xDABCABCD;
    }
    size_t operator()(const Type& t) const {
        return std::visit(*this, t.value);
    }

};

class TypeContext {
public:
    TypeContext();

    TypeId get_id(const Type& t){
        auto it = registered_types.find(t);
        if(it != registered_types.end()){
            return it->second.get();
        }
        auto ptr = std::make_unique<Type>(t);
        TypeId id = ptr.get();
        registered_types.emplace(*ptr, std::move(ptr));
        return id;
    };

    static TypeContext& get_instance(){
        static TypeContext instance;
        return instance;
    };
private:
    std::unordered_map<Type, std::unique_ptr<Type>, TypeHash> registered_types;
};


inline TypeId get_typeID(const Type& t){
    return TypeContext::get_instance().get_id(t);
};
}