#pragma once

// the file is responsible for checking the type,mutability,place,divergence...
// All info that comes from a top-down analyse of a expr tree
#include "type/type.hpp"
#include "expr_info.hpp"
#include "semantic/hir/hir.hpp"
#include "utils.hpp"
#include <variant>
namespace semantic{
// The class "evaluates" the ExprInfo for a expr node
// Also do checks related to the ExprInfo for all children nodes
class ExprChecker{
public:
    ExprInfo check(hir::Expr& expr){
        if(expr.expr_info){
            return *expr.expr_info;
        }
        // resolve and visit it
    }
private:
    


};

}
