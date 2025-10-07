#pragma once
#include "ast/type.hpp"
#include "semantic/hir/hir.hpp"
#include "semantic/hir/helper.hpp"
#include "semantic/hir/visitor/visitor_base.hpp"
#include "semantic/symbol/scope.hpp"
#include "semantic/symbol/predefined.hpp"
#include "semantic/type/type.hpp"
#include "semantic/type/helper.hpp"
#include "semantic/type/impl_table.hpp"
#include "semantic/utils.hpp"
#include "ast/ast.hpp"
#include <algorithm>
#include <optional>
#include <stack>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>


namespace semantic {

/*The plan:
* for each scope, we first collect all item names
* then resolve items in the scope
* then collect binding & resolve expressions
* finally, we pop the scope
* for path, it should be collected and resolve the second segment after the pass is finished
*/

class NameResolver: public hir::HirVisitorBase<NameResolver>{
	struct PendingTypeStatic {
		hir::Expr* container;
		hir::TypeStatic* node;
	};
    std::stack<Scope> scopes;
	ImplTable& impl_table;

	std::vector<PendingTypeStatic> unresolved_statics; 
	std::vector<std::vector<std::unique_ptr<hir::Local>>*> local_owner_stack;
	bool deferring_bindings = false;
	std::vector<hir::BindingDef*> pending_bindings;

	std::vector<std::unique_ptr<hir::Local>>* current_locals() {
		if (local_owner_stack.empty()) {
			return nullptr;
		}
		return local_owner_stack.back();
	}

	void register_binding(hir::BindingDef& binding) {
		auto* local_ptr = std::get_if<hir::Local*>(&binding.local);
		if (!local_ptr || !*local_ptr) {
			throw std::logic_error("Attempted to register a binding without a resolved local");
		}
		scopes.top().define_binding((*local_ptr)->name, &binding);
	}

	TypeDef resolve_type_identifier(const ast::Identifier& name) {
		auto type_def = scopes.top().lookup_type(name);
		if (!type_def) {
			throw std::runtime_error("Undefined type " + name.name);
		}
		return *type_def;
	}

	hir::Trait* resolve_trait_identifier(const ast::Identifier& name) {
		auto type_def = scopes.top().lookup_type(name);
		if (!type_def) {
			throw std::runtime_error("Undefined trait " + name.name);
		}
		return std::visit(Overloaded{
			[](hir::StructDef*) -> hir::Trait* {
				throw std::runtime_error("Struct used where trait expected");
			},
			[](hir::EnumDef*) -> hir::Trait* {
				throw std::runtime_error("Enum used where trait expected");
			},
			[](hir::Trait* trait) -> hir::Trait* {
				return trait;
			}
		}, *type_def);
	}

	hir::ExprVariant resolve_type_static(hir::TypeStatic& node) {
		auto* resolved_type = std::get_if<TypeDef>(&node.type);
		if (!resolved_type) {
			throw std::logic_error("TypeStatic node did not resolve its type");
		}
		const auto& target_name = node.name;
		return std::visit(Overloaded{
			[&](hir::StructDef* struct_def) -> hir::ExprVariant {
				TypeDef type_handle = struct_def;
				auto type_id = get_typeID(semantic::helper::to_type(type_handle));
				const auto& impls = impl_table.get_impls(type_id);
				for (auto* impl : impls) {
					if (impl->trait.has_value()) {
						continue;
					}
					for (auto& associated : impl->items) {
						auto resolved = std::visit(Overloaded{
							[&](hir::Function& fn) -> std::optional<hir::ExprVariant> {
								if (!fn.ast_node || !fn.ast_node->name) {
									return std::nullopt;
								}
								if ((*fn.ast_node->name).name != target_name.name) {
									return std::nullopt;
								}
								return hir::ExprVariant{hir::StructStatic{
									.struct_def = struct_def,
									.assoc_fn = &fn
								}};
							},
							[&](hir::ConstDef& constant) -> std::optional<hir::ExprVariant> {
								if (!constant.ast_node || !constant.ast_node->name) {
									return std::nullopt;
								}
								if ((*constant.ast_node->name).name != target_name.name) {
									return std::nullopt;
								}
								return hir::ExprVariant{hir::StructConst{
									.struct_def = struct_def,
									.assoc_const = &constant
								}};
							},
							[](hir::Method&) -> std::optional<hir::ExprVariant> {
								return std::nullopt;
							}
						}, associated->value);
						if (resolved) {
							return std::move(*resolved);
						}
					}
				}
				throw std::runtime_error("Unable to resolve struct associated item " + target_name.name);
			},
			[&](hir::EnumDef* enum_def) -> hir::ExprVariant {
				for (size_t idx = 0; idx < enum_def->variants.size(); ++idx) {
					if (enum_def->variants[idx].name == target_name) {
						return hir::ExprVariant{hir::EnumVariant{
							.enum_def = enum_def,
							.variant_index = idx
						}};
					}
				}
				throw std::runtime_error("Enum variant " + target_name.name + " not found");
			},
			[&](hir::Trait*) -> hir::ExprVariant {
				throw std::runtime_error("Trait associated items are not supported yet");
			}
		}, *resolved_type);
	}

	void finalize_type_statics() {
		for (auto& pending : unresolved_statics) {
			if (!pending.container || !pending.node) {
				throw std::logic_error("Encountered null pending static during finalization");
			}
			pending.container->value = resolve_type_static(*pending.node);
		}
		unresolved_statics.clear();
	}
public:
	NameResolver(ImplTable& impl_table): impl_table(impl_table) {};
	using hir::HirVisitorBase<NameResolver>::visit;

	void define_item(hir::Item& item){
		auto namedptr = hir::helper::to_named_ptr(item.value);
		if (!namedptr) return; // impl do not need define
		auto name = hir::helper::get_name(*namedptr);
		if (!scopes.top().define(name, *namedptr)) {
			throw std::runtime_error("Duplicate definition of " + name.name);
		}
	}

	void visit_program(hir::Program& program) {
        scopes.push(Scope{&get_predefined_scope(),true});// the global scope

		for (auto& item : program.items) {
			define_item(*item);
		}
		// the base visit item first then stmt and expr
		base().visit_program(program);
		finalize_type_statics();
		scopes.pop();
	}


	void visit(hir::Block& block) {
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
	void visit(hir::Function& func) {
		// function scope does not allow capture of outer bindings
		scopes.push(Scope{&scopes.top(),true});
		local_owner_stack.push_back(&func.locals);
		// the body will be treated as a pure block
		base().visit(func);
		local_owner_stack.pop_back();
		scopes.pop();
	}
	void visit(hir::Method& method) {
		scopes.push(Scope{&scopes.top(),true});
		local_owner_stack.push_back(&method.locals);
		base().visit(method);
		local_owner_stack.pop_back();
		scopes.pop();
	}
	void visit(hir::Trait& trait) {
		scopes.push(Scope{&scopes.top(),true});
		base().visit(trait);
		scopes.pop();
	}
	void visit(hir::Impl& impl) {

		//resolve the type being implemented
		auto* type_node_ptr = std::get_if<std::unique_ptr<hir::TypeNode>>(&impl.for_type);
		if (!type_node_ptr || !*type_node_ptr) {
			throw std::logic_error("Impl `for_type` is missing or already resolved.");
		}
		auto* def_type_ptr = std::get_if<std::unique_ptr<hir::DefType>>(&(*type_node_ptr)->value);
		if (!def_type_ptr || !*def_type_ptr) {
			throw std::logic_error("Impl for non-path type is **temporarily** not supported");
		}

		auto& def_type = **def_type_ptr;
		if (auto* ident = std::get_if<ast::Identifier>(&def_type.def)) {
			def_type.def = resolve_type_identifier(*ident);
		}
		auto* resolved_def = std::get_if<TypeDef>(&def_type.def);
		if (!resolved_def) {
			throw std::logic_error("Impl for_type did not resolve to a TypeDef");
		}
		auto resolved_type_id = get_typeID(semantic::helper::to_type(*resolved_def));
		impl.for_type = resolved_type_id;

		// register itself
		impl_table.add_impl(resolved_type_id, impl);

		if (impl.trait) {
			auto& trait_variant = *impl.trait;
			if (auto* trait_ident = std::get_if<ast::Identifier>(&trait_variant)) {
				trait_variant = resolve_trait_identifier(*trait_ident);
			}
		}

		scopes.push(Scope{&scopes.top(),true});
		// add the Self declaration
		scopes.top().define_type(ast::Identifier{"Self"}, *resolved_def);

		base().visit(impl);
		scopes.pop();
	}

	void visit(hir::BindingDef& binding){
		hir::Local* local_ptr = nullptr;
		if (auto* unresolved = std::get_if<hir::BindingDef::Unresolved>(&binding.local)) {
			auto* locals = current_locals();
			if (!locals) {
				throw std::logic_error("Binding resolved outside of function or method context is not supported yet");
			}
			auto local = std::make_unique<hir::Local>(hir::Local{
				.name = unresolved->name,
				.is_mutable = unresolved->is_mutable,
				.type_annotation = std::nullopt,
				.def_site = binding.ast_node
			});
			local_ptr = local.get();
			locals->push_back(std::move(local));
			binding.local = local_ptr;
		} else if (auto* resolved_local = std::get_if<hir::Local*>(&binding.local)) {
			local_ptr = *resolved_local;
		} else {
			throw std::logic_error("BindingDef.local holds an unexpected alternative");
		}

		if (!local_ptr) {
			throw std::logic_error("BindingDef failed to produce a Local*");
		}

		if (deferring_bindings) {
			pending_bindings.push_back(&binding);
		} else {
			register_binding(binding);
		}
	}

	void visit(hir::LetStmt& stmt) {
		auto previous_deferring = deferring_bindings;
		auto saved_pending = std::move(pending_bindings);
		deferring_bindings = true;
		pending_bindings.clear();

		if (stmt.pattern) {
			hir::HirVisitorBase<NameResolver>::visit_pattern(stmt.pattern);
		}

		auto local_bindings = std::move(pending_bindings);
		pending_bindings = std::move(saved_pending);
		deferring_bindings = previous_deferring;

		visit_optional_type_annotation(stmt.type_annotation);
		if (stmt.initializer) {
			hir::HirVisitorBase<NameResolver>::visit_expr(stmt.initializer);
		}

		for (auto* binding : local_bindings) {
			register_binding(*binding);
		}
	}

	//resolve names
	void visit(hir::TypeStatic& ts, hir::Expr& container){
		auto* type_name_ptr = std::get_if<ast::Identifier>(&ts.type);
		if (type_name_ptr) {
			ts.type = resolve_type_identifier(*type_name_ptr);
		}
		unresolved_statics.push_back(PendingTypeStatic{&container, &ts});
		base().visit(ts);
	}
	void visit(hir::UnresolvedIdentifier& ident, hir::Expr& container){
		auto def = scopes.top().lookup_value(ident.name);
		if (!def) {
			throw std::runtime_error("Undefined identifier " + ident.name.name);
		}
		auto resolved = std::visit(Overloaded{
			[&](hir::BindingDef* binding) -> hir::ExprVariant {
				auto* local_ptr = std::get_if<hir::Local*>(&binding->local);
				if (!local_ptr || !*local_ptr) {
					throw std::logic_error("BindingDef resolved without Local assignment");
				}
				return hir::ExprVariant{hir::Variable{
					.local_id = *local_ptr,
					.ast_node = ident.ast_node
				}};
			},
			[&](hir::ConstDef* constant) -> hir::ExprVariant {
				return hir::ExprVariant{hir::ConstUse{
					.def = constant,
					.ast_node = ident.ast_node
				}};
			},
			[&](hir::Function* function) -> hir::ExprVariant {
				return hir::ExprVariant{hir::FuncUse{
					.def = function,
					.ast_node = ident.ast_node
				}};
			}
		}, *def);
		container.value = std::move(resolved);
	}
	void visit(hir::StructLiteral& sl){
		auto* name_ptr = std::get_if<ast::Identifier>(&sl.struct_path);
        if (!name_ptr) {
            base().visit(sl);
            return;
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
		auto syntactic_fields = std::get_if<hir::StructLiteral::SyntacticFields>(&sl.fields);
		if (!syntactic_fields) {
			throw std::runtime_error("Struct literal fields are not in the expected format");
		}
		std::vector<std::unique_ptr<hir::Expr>> fields;
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
		sl.fields = hir::StructLiteral::CanonicalFields{
			.initializers = std::move(fields)
		};

		base().visit(sl);
	}

	void visit(hir::DefType& def_type) {
		if (auto* name = std::get_if<ast::Identifier>(&def_type.def)) {
			def_type.def = resolve_type_identifier(*name);
		}
	}


};

}