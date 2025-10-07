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
#include <type_traits>


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
	using ExprUpdate = typename HirVisitorBase<NameResolver>::ExprUpdate;
    std::stack<Scope> scopes;
	ImplTable& impl_table;

	std::vector<TypeStatic*> unresolved_statics; 
	std::vector<Function*> function_stack;

	Function* current_function() {
		if (function_stack.empty()) {
			return nullptr;
		}
		return function_stack.back();
	}
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
		function_stack.push_back(&func);
		// the body will be treated as a pure block
		base().visit(func);
		function_stack.pop_back();
		scopes.pop();
	}
	void visit(Trait& trait) {
		scopes.push(Scope{&scopes.top(),true});
		base().visit(trait);
		scopes.pop();
	}
	void visit(Impl& impl) {
		
		//resolve the type being implemented
        auto* type_node_ptr = std::get_if<std::unique_ptr<TypeNode>>(&impl.for_type);
        if (!type_node_ptr || !*type_node_ptr) {
            throw std::logic_error("Impl `for_type` is missing or already resolved.");
        }
        auto* def_type_ptr = std::get_if<std::unique_ptr<DefType>>(&(*type_node_ptr)->value);
        if (!def_type_ptr) {
            throw std::logic_error("Impl for non-path type is **temporarily** not supported");
        }
        const auto& type_name_variant = (*def_type_ptr)->def;
        auto* type_name_ptr = std::get_if<ast::Identifier>(&type_name_variant);
        if (!type_name_ptr) {
            throw std::logic_error("Expected DefType to hold an identifier for resolution in impl.");
        }
        const auto& type_name = *type_name_ptr;

		auto type_def = scopes.top().lookup_type(type_name);
		if(!type_def){
			throw std::runtime_error("Impl for undefined type "+type_name.name);
		}
        
        auto resolved_type_id = get_typeID(helper::to_type(*type_def));
		impl.for_type = resolved_type_id;
		
        // register itself
		impl_table.add_impl(resolved_type_id, impl);

		scopes.push(Scope{&scopes.top(),true});
		// add the Self declaration
		scopes.top().define_type("Self", *type_def);

		base().visit(impl);
		scopes.pop();
	}

	void visit(BindingDef& binding){
		if (auto* unresolved = std::get_if<BindingDef::Unresolved>(&binding.local)) {
			auto* func = current_function();
			if (!func) {
				throw std::logic_error("Binding resolved outside of function context is not supported yet");
			}
			auto local = std::make_unique<Local>(Local{
				.name = unresolved->name,
				.is_mutable = unresolved->is_mutable,
				.type_annotation = std::nullopt,
				.def_site = binding.ast_node
			});
			auto* local_ptr = local.get();
			func->locals.push_back(std::move(local));
			binding.local = local_ptr;
			scopes.top().define_binding(local_ptr->name, &binding);
		} else if (auto* resolved_local = std::get_if<Local*>(&binding.local)) {
			scopes.top().define_binding((*resolved_local)->name, &binding);
		} else {
			throw std::logic_error("BindingDef.local holds an unexpected alternative");
		}
	}

	//resolve names
	ExprUpdate visit(TypeStatic& ts){
        auto* type_name_ptr = std::get_if<ast::Identifier>(&ts.type);
        if (!type_name_ptr) {
			return std::nullopt;
        }
        const auto& name = *type_name_ptr;

		auto type_def = scopes.top().lookup_type(name);
		if (!type_def) {
			throw std::runtime_error("Undefined type " + name.name);
		}

		ts.type = *type_def;
		unresolved_statics.push_back(&ts);

		return HirVisitorBase<NameResolver>::visit(ts);
	}
	ExprUpdate visit(UnresolvedIdentifier& ident){
		auto def = scopes.top().lookup_value(ident.name);
		if (!def) {
			throw std::runtime_error("Undefined identifier " + ident.name.name);
		}
		return std::visit([
			&ident
		](auto* symbol_ptr) -> ExprUpdate {
			using T = std::decay_t<decltype(symbol_ptr)>;
			if constexpr (std::is_same_v<T, hir::BindingDef*>) {
				auto* local_ptr = std::get_if<Local*>( &symbol_ptr->local );
				if (!local_ptr || !*local_ptr) {
					throw std::logic_error("BindingDef resolved without Local assignment");
				}
				return ExprVariant{Variable{
					.local_id = *local_ptr,
					.ast_node = ident.ast_node
				}};
			} else if constexpr (std::is_same_v<T, hir::ConstDef*>) {
				return ExprVariant{ConstUse{
					.def = symbol_ptr,
					.ast_node = ident.ast_node
				}};
			} else if constexpr (std::is_same_v<T, hir::Function*>) {
				return ExprVariant{FuncUse{
					.def = symbol_ptr,
					.ast_node = ident.ast_node
				}};
			} else {
				static_assert(std::is_same_v<T, void>, "Unhandled symbol pointer type in name resolution");
			}
		}, *def);
	}
	ExprUpdate visit(StructLiteral& sl){
		auto* name_ptr = std::get_if<ast::Identifier>(&sl.struct_path);
        if (!name_ptr) {
            return HirVisitorBase<NameResolver>::visit(sl);
        }
        const auto& name = *name_ptr;

        auto def = scopes.top().lookup_type(name);
        if (!def) {
            throw std::runtime_error("Undefined struct " + name.name);
        }
        auto* struct_def = std::get_if<hir::StructDef*>(&def.value());
        if (!struct_def) {
            throw std::runtime_error(name.name + " is not a struct");
        }
        sl.struct_path = *struct_def;

		//reorder the fields to canonical order
		auto syntactic_fields = std::get_if<StructLiteral::SyntacticFields>(&sl.fields);
		if (!syntactic_fields) {
			throw std::runtime_error("Struct literal fields are not in the expected format");
		}
		std::vector<std::unique_ptr<Expr>> fields;
		fields.resize((*struct_def)->fields.size());
		for (auto& init : syntactic_fields->initializers) {
			auto it = std::find_if((*struct_def)->fields.begin(), (*struct_def)->fields.end(),
				[&](const semantic::Field& f) { return f.name == init.first.name; });
			if (it == (*struct_def)->fields.end()) {
				throw std::runtime_error("Field " + init.first.name + " not found in struct " + name.name);
			}
			size_t index = std::distance((*struct_def)->fields.begin(), it);
			if (fields[index]) {
				throw std::runtime_error("Duplicate initialization of field " + init.first.name + " in struct " + name.name);
			}
			fields[index] = std::move(init.second);
		}
		sl.fields = StructLiteral::CanonicalFields{
			.initializers = std::move(fields)
		};

		HirVisitorBase<NameResolver>::visit(sl);
		return std::nullopt;
	}


};

}