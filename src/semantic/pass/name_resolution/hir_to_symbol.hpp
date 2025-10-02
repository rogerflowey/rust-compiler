#pragma once
#include "semantic/hir/hir.hpp"
#include "semantic/symbol/scope.hpp"
#include "semantic/symbol/symbol.hpp"
#include "semantic/type.hpp"

namespace semantic{

class UndefinedSymbolConvertor{
    SymbolTable& symbol_table;
public:
    explicit UndefinedSymbolConvertor(SymbolTable& sym_table): symbol_table(sym_table) {}
    SymbolId convert(const hir::Function& func){
        return symbol_table.create_symbol(UndefinedSymbol{SymbolKind::Function, func.ast_node});
    }
    SymbolId convert(const hir::StructDef& s){
        return symbol_table.create_symbol(UndefinedSymbol{SymbolKind::Struct, s.ast_node});
    }
    SymbolId convert(const hir::EnumDef& e){
        return symbol_table.create_symbol(UndefinedSymbol{SymbolKind::Enum, e.ast_node});
    }
    SymbolId convert(const hir::ConstDef& c){
        return symbol_table.create_symbol(UndefinedSymbol{SymbolKind::Const, c.ast_node});
    }
    SymbolId convert(const hir::Trait& t){
        return symbol_table.create_symbol(UndefinedSymbol{SymbolKind::Trait, t.ast_node});
    }
};

class FilledSymbolConvertor{
    SymbolTable& symbol_table;
public:
    explicit FilledSymbolConvertor(SymbolTable& sym_table): symbol_table(sym_table) {}
    SymbolId convert(const hir::Function& func,Scope* scope){
        FunctionSymbol func_symbol;
        
        return symbol_table.create_symbol();
    }
};

    
}