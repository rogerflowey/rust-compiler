#pragma once
#include "ast/type.hpp"
#include "semantic/hir/hir.hpp"
#include "semantic/hir/helper.hpp"
#include "semantic/hir/visitor/visitor_base.hpp"
#include "semantic/symbol/scope.hpp"
#include "semantic/symbol/predefined.hpp"
#include "semantic/type/type.hpp"
#include "semantic/type/helper.hpp"
#include "ast/ast.hpp"
#include "type/impl_table.hpp"
#include <stack>
#include <stdexcept>
#include <variant>


namespace semantic {

/*The plan:
* for each scope, we first collect all item names
* then resolve items in the scope
* then collect binding & resolve expressions
* finally, we pop the scope
* for path, it should be collected and resolve the second segment after the pass is finished
*/

using namespace hir;

class NameResolver: public HirVisitorBase<NameResolver>{
    std::stack<Scope> scopes;
	ImplTable& impl_table;

	std::vector<TypeStatic*> unresolved_statics; 
public:
	NameResolver(ImplTable& impl_table): impl_table(impl_table) {};

	void define_item(Item& item){
		auto namedptr = hir::helper::to_named_ptr(item.value);
		if (!namedptr) return; // impl do not need define
		auto name = hir::helper::get_name(*namedptr);
		if (!scopes.top().define(name, *namedptr)) {
			throw std::runtime_error("Duplicate definition of " + name.name);
		}
	}

    void visit_program(Program& program) {
        scopes.push(Scope{&get_predefined_scope(),true});// the global scope

		for (auto& item : program.items) {
			define_item(*item);
		}
		// the base visit item first then stmt and expr
		base().visit_program(program);
		scopes.pop();
	}


	void visit(Block& block) {
		// block scope allows capture of outer bindings
		scopes.push(Scope{&scopes.top(),false});
		for(auto& item : block.items){
			define_item(*item);
		}
		base().visit(block);
		scopes.pop();
	}

	// --- Items ---
	// Function, Trait, Impl create new scopes
	// StructDef, EnumDef, ConstDef do not
	void visit(Function& func) {
		// function scope does not allow capture of outer bindings
		scopes.push(Scope{&scopes.top(),true});
		// the body will be treated as a pure block
		base().visit(func);
		scopes.pop();
	}
	void visit(Trait& trait) {
		scopes.push(Scope{&scopes.top(),true});
		base().visit(trait);
		scopes.pop();
	}
	void visit(Impl& impl) {
		
		//resolve the type being implemented
		auto type = std::visit([](auto&& impl){return impl->for_type.get();}, impl.ast_node);
		auto name = std::get_if<ast::PathType>(&type->value);
		if(!name){
			throw std::logic_error("Impl for non-path type is **temporarily** not supported");
		}
		if(name->path->segments.size()!=1){
			throw std::logic_error("Impl for complex type is **temporarily** not supported");
		}
		auto type_name = name->path->segments[0].id->get();
		auto type_def = scopes.top().lookup_type(*type_name);
		if(!type_def){
			throw std::runtime_error("Impl for undefined type "+type_name->name);
		}
		impl.for_type = *type_def;
		// register itself
		impl_table.add_impl(get_typeID(helper::to_type(*type_def)), impl);

		scopes.push(Scope{&scopes.top(),true});
		// add the Self declaration
		scopes.top().define_type("Self", *type_def);

		base().visit(impl);
		scopes.pop();
	}

	void visit(Binding& binding){
		scopes.top().define_binding(*binding.ast_node->name,&binding);
	}

	//resolve names
	void visit(TypeStatic& ts){
		auto& segments = ts.ast_node->path->segments;
		if(segments.size() != 2) {
			throw std::logic_error("TypeStatic can only resolve two segment paths");
		}
		auto name = *segments[0].id->get();
		auto type = scopes.top().lookup_type(name);
		if (!type) {
			throw std::runtime_error("Undefined type " + name.name);
		}
		ts.type_def = *type;
		unresolved_statics.push_back(&ts);
		ts.name = *segments[1].id->get();

		base().visit(ts);
	}
	void visit(Variable& var){
		auto& segments = var.ast_node->path->segments;
		if(segments.size() != 1) {
			throw std::runtime_error("Variable can only resolve single segment paths");
		}
		auto name = *segments[0].id->get();
		auto def = scopes.top().lookup_value(name);
		if (!def) {
			throw std::runtime_error("Undefined variable " + name.name);
		}
		var.definition = *def;
		base().visit(var);
	}
	void visit(StructLiteral& sl){
		
	}

};

}