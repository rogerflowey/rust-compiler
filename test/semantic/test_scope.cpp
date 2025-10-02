#include "semantic/symbol/scope.hpp"
#include "ast/ast.hpp"
#include <gtest/gtest.h>

using namespace semantic;


TEST(ScopeTest, BasicInsertAndLookup) {
    Scope scope;
    ast::Identifier item_name{"my_item"};
    ast::Identifier binding_name{"my_binding"};
    ast::Identifier type_name{"MyType"};

    SymbolId item_id{1};
    SymbolId binding_id{2};
    SymbolId type_id{3};

    EXPECT_TRUE(scope.insert_item(item_name, item_id));
    scope.insert_binding(binding_name, binding_id);
    EXPECT_TRUE(scope.insert_type(type_name, type_id));

    EXPECT_EQ(scope.lookup_value(item_name).value(), item_id);
    EXPECT_EQ(scope.lookup_value(binding_name).value(), binding_id);
    EXPECT_EQ(scope.lookup_type(type_name).value(), type_id);
}

TEST(ScopeTest, LookupFailureForUnknownSymbol) {
    Scope scope;
    ast::Identifier name{"x"};
    scope.insert_item(name, SymbolId{1});

    ast::Identifier unknown_name{"y"};
    EXPECT_FALSE(scope.lookup_value(unknown_name).has_value());
    EXPECT_FALSE(scope.lookup_type(unknown_name).has_value());
}

// --- Insertion Rules and Conflicts ---

TEST(ScopeTest, DuplicateSymbolInsert) {
    Scope scope;
    ast::Identifier name{"my_symbol"};
    SymbolId id1{1};
    SymbolId id2{2};

    EXPECT_TRUE(scope.insert_item(name, id1));
    EXPECT_FALSE(scope.insert_item(name, id2)); // Cannot insert item with same name

    EXPECT_TRUE(scope.insert_type(name, id1));
    EXPECT_FALSE(scope.insert_type(name, id2)); // Cannot insert type with same name
}

// --- Scoping and Shadowing ---

TEST(ScopeTest, NestedScopeLookup) {
    Scope parent_scope;
    ast::Identifier parent_item{"parent_item"};
    ast::Identifier parent_type{"ParentType"};
    parent_scope.insert_item(parent_item, SymbolId{10});
    parent_scope.insert_type(parent_type, SymbolId{11});

    Scope child_scope(&parent_scope);
    ast::Identifier child_binding{"child_binding"};
    child_scope.insert_binding(child_binding, SymbolId{20});

    // Child can access parent's symbols
    EXPECT_EQ(child_scope.lookup_value(parent_item).value(), SymbolId{10});
    EXPECT_EQ(child_scope.lookup_type(parent_type).value(), SymbolId{11});

    // Child can access its own symbols
    EXPECT_EQ(child_scope.lookup_value(child_binding).value(), SymbolId{20});

    // Parent cannot access child's symbols
    EXPECT_FALSE(parent_scope.lookup_value(child_binding).has_value());
}

TEST(ScopeTest, MultiLevelNestedScopeLookup) {
    Scope grandparent_scope;
    ast::Identifier name_g{"g"};
    grandparent_scope.insert_item(name_g, SymbolId{1});

    Scope parent_scope(&grandparent_scope);
    ast::Identifier name_p{"p"};
    parent_scope.insert_item(name_p, SymbolId{2});

    Scope child_scope(&parent_scope);
    ast::Identifier name_c{"c"};
    child_scope.insert_item(name_c, SymbolId{3});

    // Child should be able to see all levels.
    EXPECT_EQ(child_scope.lookup_value(name_g).value(), SymbolId{1});
    EXPECT_EQ(child_scope.lookup_value(name_p).value(), SymbolId{2});
    EXPECT_EQ(child_scope.lookup_value(name_c).value(), SymbolId{3});
}


TEST(ScopeTest, SymbolShadowingInChildScope) {
    Scope parent_scope;
    ast::Identifier name{"my_symbol"};
    parent_scope.insert_item(name, SymbolId{100});
    parent_scope.insert_type(name, SymbolId{101});

    Scope child_scope(&parent_scope);
    child_scope.insert_binding(name, SymbolId{200});
    child_scope.insert_type(name, SymbolId{201});

    // Lookup in child scope should return child's symbols
    EXPECT_EQ(child_scope.lookup_value(name).value(), SymbolId{200});
    EXPECT_EQ(child_scope.lookup_type(name).value(), SymbolId{201});

    // Lookup in parent scope should still return parent's symbols
    EXPECT_EQ(parent_scope.lookup_value(name).value(), SymbolId{100});
    EXPECT_EQ(parent_scope.lookup_type(name).value(), SymbolId{101});
}

TEST(ScopeTest, BindingShadowsItemInSameScope) {
    Scope scope;
    ast::Identifier name{"my_symbol"};
    
    scope.insert_item(name, SymbolId{1});
    scope.insert_binding(name, SymbolId{2});

    // The binding (variable) should shadow the item (function, etc.)
    EXPECT_EQ(scope.lookup_value(name).value(), SymbolId{2});
}

TEST(ScopeTest, SequentialBindingShadowingInSameScope) {
    Scope scope;
    ast::Identifier name{"x"};
    
    scope.insert_binding(name, SymbolId{1});
    EXPECT_EQ(scope.lookup_value(name).value(), SymbolId{1});

    // A new binding shadows the previous one.
    scope.insert_binding(name, SymbolId{2});
    
    // Lookup should now find the newest binding.
    // This directly verifies the "overwrite" strategy.
    EXPECT_EQ(scope.lookup_value(name).value(), SymbolId{2});
}