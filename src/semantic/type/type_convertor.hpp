#pragma once

#include "ast/type.hpp"
#include "semantic/type/type.hpp"
namespace semantic {

class TypeConvertor {
public:
    static TypeId convert(const ast::Type& type){

    };
private:
    static TypeId convert_primitive(const ast::PrimitiveType& prim){
        PrimitiveKind kind;
        switch(prim.kind){
            case ast::PrimitiveType::I32: kind = PrimitiveKind::I32; break;
            case ast::PrimitiveType::U32: kind = PrimitiveKind::U32; break;
            case ast::PrimitiveType::ISIZE: kind = PrimitiveKind::ISIZE; break;
            case ast::PrimitiveType::USIZE: kind = PrimitiveKind::USIZE; break;
            case ast::PrimitiveType::BOOL: kind = PrimitiveKind::BOOL; break;
            case ast::PrimitiveType::CHAR: kind = PrimitiveKind::CHAR; break;
            case ast::PrimitiveType::STRING: kind = PrimitiveKind::STRING; break;
        }
        return TypeContext::get_instance().get_id(Type{kind});
    };
    static TypeId convert_array(const ast::ArrayType& arr){
        return TypeContext::get_instance().get_id(Type{ArrayType{
            .element_type = convert(*arr.element_type),
            .size = 0
        }});
    };
    static TypeId convert_reference(const ast::ReferenceType& ref){

    };

};