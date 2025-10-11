#pragma once

// this file defines the struct of Expr Info used in the top-down expr type checks
#include "type/type.hpp"
namespace semantic{
struct ControllFlowInfo{
    bool diverge;
};

struct ExprInfo{
    TypeId type; // the type of the expr
    bool is_mut; // mutability of the expr
    bool is_place;
    ControllFlowInfo control;
};
}