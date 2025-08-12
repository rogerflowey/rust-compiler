#pragma once

#include <optional>
#include <string>
#include <vector>

#include "expr.hpp"
#include "stmt.hpp"
#include "type.hpp"

class FuncItem{
    std::string name;
    Type returnType;
    std::vector<std::pair<std::string, Type>> params;
    std::vector<Statement*> body;
    std::optional<Expr*> returnValue;
public:
    FuncItem(std::string name, Type returnType,
             std::vector<std::pair<std::string, Type>> params,
             std::vector<Statement*> body,
             std::optional<Expr*> returnValue)
        : name(std::move(name)), returnType(returnType), params(std::move(params)),
          body(std::move(body)), returnValue(returnValue) {}

    const std::string& getName() const { return name; }
    Type getReturnType() const { return returnType; }
    const std::vector<std::pair<std::string, Type>>& getParams() const { return params; }
    const std::vector<Statement*>& getBody() const { return body; }
    std::optional<Expr*> getReturnValue() const { return returnValue; }
};

