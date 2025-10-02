#pragma once
#include "semantic/hir/hir.hpp"
#include "semantic/hir/visitor/visitor_base.hpp"
#include "semantic/symbol/scope.hpp"
#include "semantic/symbol/symbol.hpp"
#include "semantic/symbol/impl_table.hpp"
#include "semantic/type/type.hpp"
#include "ast/ast.hpp"
#include <stack>


namespace semantic {

/*The plan:
* for each scope, we first collect all item names
* then resolve items in the scope
* then collect binding & resolve expressions
* finally, we pop the scope
*/

}