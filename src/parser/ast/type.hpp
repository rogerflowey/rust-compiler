#pragma once

#include <cstddef>
class Type{};

class NullType : public Type {};

class I32Type : public Type {};

class U32Type : public Type {};

class StringType : public Type {};

class ArrayType : public Type {
    Type* elementType;
    size_t size;
public:
    ArrayType(Type* elementType, size_t size) : elementType(elementType), size(size) {}
};

