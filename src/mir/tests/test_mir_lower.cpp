#include "mir/lower/lower.hpp"
#include "mir/lower/lower_common.hpp"

#include "mir/mir.hpp"
#include "semantic/const/const.hpp"
#include "semantic/pass/semantic_check/expr_info.hpp"
#include "semantic/type/type.hpp"

#include "tests/catch_gtest_compat.hpp"
#include <unordered_map>

namespace {

semantic::TypeId make_type(semantic::PrimitiveKind kind) {
    return semantic::get_typeID(semantic::Type{kind});
}

semantic::TypeId make_unit_type() {
    return semantic::get_typeID(semantic::Type{semantic::UnitType{}});
}

semantic::ExprInfo make_value_info(semantic::TypeId type, bool is_place = false) {
    semantic::ExprInfo info;
    info.type = type;
    info.has_type = true;
    info.is_mut = false;
    info.is_place = is_place;
    info.endpoints.clear();
    info.endpoints.insert(semantic::NormalEndpoint{});
    return info;
}

std::unique_ptr<hir::Expr> make_bool_literal_expr(bool value, semantic::TypeId type) {
    hir::Literal literal{
        .value = hir::Literal::Value{value},
        
    };
    auto expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(literal)});
    expr->expr_info = make_value_info(type, false);
    return expr;
}

std::unique_ptr<hir::Expr> make_int_literal_expr(uint64_t value, semantic::TypeId type) {
    hir::Literal literal{
        .value = hir::Literal::Value{hir::Literal::Integer{.value = value, .suffix_type = ast::IntegerLiteralExpr::I32, .is_negative = false}},
        
    };
    auto expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(literal)});
    expr->expr_info = make_value_info(type, false);
    return expr;
}

std::unique_ptr<hir::Expr> make_char_literal_expr(char value, semantic::TypeId type) {
    hir::Literal literal{
        .value = hir::Literal::Value{value},
        
    };
    auto expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(literal)});
    expr->expr_info = make_value_info(type, false);
    return expr;
}

std::unique_ptr<hir::Expr> make_string_literal_expr(std::string value, semantic::TypeId type, bool is_cstyle = false) {
    hir::Literal literal{
        .value = hir::Literal::Value{hir::Literal::String{.value = std::move(value), .is_cstyle = is_cstyle}},
        
    };
    auto expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(literal)});
    expr->expr_info = make_value_info(type, false);
    return expr;
}

semantic::TypeId make_string_ref_type() {
    semantic::TypeId string_type = make_type(semantic::PrimitiveKind::STRING);
    return semantic::get_typeID(semantic::Type{semantic::ReferenceType{string_type, false}});
}

std::unique_ptr<hir::Expr> make_binary_expr(hir::BinaryOperator op,
                                            std::unique_ptr<hir::Expr> lhs,
                                            std::unique_ptr<hir::Expr> rhs,
                                            semantic::TypeId type) {
    hir::BinaryOp binary;
    binary.op = std::move(op);
    binary.lhs = std::move(lhs);
    binary.rhs = std::move(rhs);
    
    auto expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(binary)});
    expr->expr_info = make_value_info(type, false);
    return expr;
}

std::unique_ptr<hir::Block> make_block_with_expr(std::unique_ptr<hir::Expr> expr) {
    auto block = std::make_unique<hir::Block>();
    block->final_expr = std::move(expr);
    return block;
}

std::unique_ptr<hir::Expr> make_func_use_expr(hir::Function* function) {
    hir::FuncUse func_use;
    func_use.def = function;
    
    return std::make_unique<hir::Expr>(hir::ExprVariant{std::move(func_use)});
}

} // namespace

TEST(MirLowerTest, LowersFunctionReturningLiteral) {
    semantic::TypeId bool_type = make_type(semantic::PrimitiveKind::BOOL);

    hir::Function function;
    function.return_type = hir::TypeAnnotation(bool_type);

    auto body = std::make_unique<hir::Block>();
    body->final_expr = make_bool_literal_expr(true, bool_type);
    function.body = std::move(body);

    mir::MirFunction mir_function = mir::lower_function(function);
    ASSERT_EQ(mir_function.basic_blocks.size(), 1u);
    const auto& block = mir_function.basic_blocks.front();
    ASSERT_TRUE(block.statements.empty());
    ASSERT_TRUE(std::holds_alternative<mir::ReturnTerminator>(block.terminator.value));
    const auto& ret = std::get<mir::ReturnTerminator>(block.terminator.value);
    ASSERT_TRUE(ret.value.has_value());
    const auto& operand = ret.value.value();
    ASSERT_TRUE(std::holds_alternative<mir::Constant>(operand.value));
    const auto& constant = std::get<mir::Constant>(operand.value);
    EXPECT_EQ(constant.type, bool_type);
    ASSERT_TRUE(std::holds_alternative<mir::BoolConstant>(constant.value));
    EXPECT_TRUE(std::get<mir::BoolConstant>(constant.value).value);
}

TEST(MirLowerTest, LowersCharLiteral) {
    semantic::TypeId char_type = make_type(semantic::PrimitiveKind::CHAR);

    hir::Function function;
    function.return_type = hir::TypeAnnotation(char_type);

    auto body = std::make_unique<hir::Block>();
    body->final_expr = make_char_literal_expr('z', char_type);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    ASSERT_EQ(lowered.basic_blocks.size(), 1u);
    const auto& block = lowered.basic_blocks.front();
    ASSERT_TRUE(std::holds_alternative<mir::ReturnTerminator>(block.terminator.value));
    const auto& ret = std::get<mir::ReturnTerminator>(block.terminator.value);
    ASSERT_TRUE(ret.value.has_value());
    const auto& operand = ret.value.value();
    ASSERT_TRUE(std::holds_alternative<mir::Constant>(operand.value));
    const auto& constant = std::get<mir::Constant>(operand.value);
    ASSERT_TRUE(std::holds_alternative<mir::CharConstant>(constant.value));
    EXPECT_EQ(std::get<mir::CharConstant>(constant.value).value, 'z');
}

TEST(MirLowerTest, LowersStringLiteralWithNullTerminator) {
    semantic::TypeId string_ref_type = make_string_ref_type();

    hir::Function function;
    function.return_type = hir::TypeAnnotation(string_ref_type);

    auto body = std::make_unique<hir::Block>();
    body->final_expr = make_string_literal_expr("hello", string_ref_type);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    ASSERT_EQ(lowered.basic_blocks.size(), 1u);
    const auto& block = lowered.basic_blocks.front();
    ASSERT_TRUE(std::holds_alternative<mir::ReturnTerminator>(block.terminator.value));
    const auto& ret = std::get<mir::ReturnTerminator>(block.terminator.value);
    ASSERT_TRUE(ret.value.has_value());
    const auto& operand = ret.value.value();
    ASSERT_TRUE(std::holds_alternative<mir::Constant>(operand.value));
    const auto& constant = std::get<mir::Constant>(operand.value);
    ASSERT_TRUE(std::holds_alternative<mir::StringConstant>(constant.value));
    const auto& string_const = std::get<mir::StringConstant>(constant.value);
    EXPECT_EQ(string_const.length, 5u);
    ASSERT_FALSE(string_const.data.empty());
    EXPECT_EQ(string_const.data.back(), '\0');
    EXPECT_EQ(std::string(string_const.data.c_str()), "hello");
    EXPECT_FALSE(string_const.is_cstyle);
}

TEST(MirLowerTest, LowersLetAndFinalVariableExpr) {
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);

    auto local = std::make_unique<hir::Local>();
    local->name = ast::Identifier{"x"};
    local->is_mutable = false;
    local->type_annotation = hir::TypeAnnotation(int_type);
    hir::Local* local_ptr = local.get();

    hir::Function function;
    function.return_type = hir::TypeAnnotation(int_type);
    function.locals.push_back(std::move(local));

    hir::BindingDef binding;
    binding.local = local_ptr;
    auto pattern = std::make_unique<hir::Pattern>(hir::PatternVariant{std::move(binding)});

    hir::LetStmt let_stmt;
    let_stmt.pattern = std::move(pattern);
    let_stmt.type_annotation = hir::TypeAnnotation(int_type);
    let_stmt.initializer = make_int_literal_expr(1, int_type);

    auto stmt_variant = hir::StmtVariant{std::move(let_stmt)};
    auto let_stmt_node = std::make_unique<hir::Stmt>(std::move(stmt_variant));

    auto body = std::make_unique<hir::Block>();
    body->stmts.push_back(std::move(let_stmt_node));

    hir::Variable variable;
    variable.local_id = local_ptr;
    
    auto final_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(variable)});
    final_expr->expr_info = make_value_info(int_type, true);
    body->final_expr = std::move(final_expr);

    function.body = std::move(body);

    hir::Program program;
    program.items.push_back(std::make_unique<hir::Item>(std::move(function)));

    mir::MirModule module = mir::lower_program(program);
    ASSERT_EQ(module.functions.size(), 1u);
    const auto& lowered = module.functions.front();

    ASSERT_EQ(lowered.locals.size(), 1u);
    EXPECT_EQ(lowered.locals[0].type, int_type);
    ASSERT_EQ(lowered.basic_blocks.size(), 1u);
    const auto& block = lowered.basic_blocks.front();
    ASSERT_EQ(block.statements.size(), 2u);

    const auto& assign_stmt = std::get<mir::AssignStatement>(block.statements[0].value);
    ASSERT_TRUE(std::holds_alternative<mir::LocalPlace>(assign_stmt.dest.base));
    EXPECT_EQ(std::get<mir::LocalPlace>(assign_stmt.dest.base).id, 0u);
    ASSERT_TRUE(std::holds_alternative<mir::Constant>(assign_stmt.src.value));
    const auto& assigned_constant = std::get<mir::Constant>(assign_stmt.src.value);
    EXPECT_EQ(assigned_constant.type, int_type);
    ASSERT_TRUE(std::holds_alternative<mir::IntConstant>(assigned_constant.value));
    EXPECT_EQ(std::get<mir::IntConstant>(assigned_constant.value).value, 1u);

    const auto& load_stmt = std::get<mir::LoadStatement>(block.statements[1].value);
    ASSERT_TRUE(std::holds_alternative<mir::LocalPlace>(load_stmt.src.base));
    EXPECT_EQ(std::get<mir::LocalPlace>(load_stmt.src.base).id, 0u);
    ASSERT_EQ(lowered.temp_types.size(), 1u);
    EXPECT_EQ(lowered.temp_types[0], int_type);

    ASSERT_TRUE(std::holds_alternative<mir::ReturnTerminator>(block.terminator.value));
    const auto& ret = std::get<mir::ReturnTerminator>(block.terminator.value);
    ASSERT_TRUE(ret.value.has_value());
    const auto& operand = ret.value.value();
    ASSERT_TRUE(std::holds_alternative<mir::TempId>(operand.value));
    EXPECT_EQ(std::get<mir::TempId>(operand.value), load_stmt.dest);
}

TEST(MirLowerTest, RecordsFunctionParameters) {
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);

    auto param_local = std::make_unique<hir::Local>();
    param_local->name = ast::Identifier{"x"};
    param_local->is_mutable = false;
    param_local->type_annotation = hir::TypeAnnotation(int_type);
    hir::Local* param_local_ptr = param_local.get();

    hir::Function function;
    function.return_type = hir::TypeAnnotation(int_type);
    function.locals.push_back(std::move(param_local));

    hir::BindingDef param_binding;
    param_binding.local = param_local_ptr;
    auto param_pattern = std::make_unique<hir::Pattern>(hir::PatternVariant{std::move(param_binding)});
    function.params.push_back(std::move(param_pattern));
    function.param_type_annotations.push_back(hir::TypeAnnotation(int_type));

    function.body = make_block_with_expr(make_int_literal_expr(0, int_type));

    hir::Program program;
    program.items.push_back(std::make_unique<hir::Item>(std::move(function)));

    mir::MirModule module = mir::lower_program(program);
    ASSERT_EQ(module.functions.size(), 1u);
    const auto& lowered = module.functions.front();
    ASSERT_EQ(lowered.params.size(), 1u);
    EXPECT_EQ(lowered.params[0].local, 0u);
    semantic::TypeId expected_param_type = mir::detail::canonicalize_type_for_mir(int_type);
    EXPECT_EQ(lowered.params[0].type, expected_param_type);
    EXPECT_EQ(lowered.params[0].name, "x");
}

TEST(MirLowerTest, LowersBinaryAddition) {
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);

    hir::Function function;
    function.return_type = hir::TypeAnnotation(int_type);

    auto body = std::make_unique<hir::Block>();
    body->final_expr = make_binary_expr(
        hir::Add{.kind = hir::Add::Kind::SignedInt},
        make_int_literal_expr(1, int_type),
        make_int_literal_expr(2, int_type),
        int_type);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    ASSERT_EQ(lowered.basic_blocks.size(), 1u);
    const auto& block = lowered.basic_blocks.front();
    ASSERT_EQ(block.statements.size(), 1u);

    const auto& define_stmt = std::get<mir::DefineStatement>(block.statements[0].value);
    ASSERT_TRUE(std::holds_alternative<mir::BinaryOpRValue>(define_stmt.rvalue.value));
    const auto& binary = std::get<mir::BinaryOpRValue>(define_stmt.rvalue.value);
    EXPECT_EQ(binary.kind, mir::BinaryOpRValue::Kind::IAdd);
    ASSERT_TRUE(std::holds_alternative<mir::Constant>(binary.lhs.value));
    ASSERT_TRUE(std::holds_alternative<mir::Constant>(binary.rhs.value));

    ASSERT_TRUE(std::holds_alternative<mir::ReturnTerminator>(block.terminator.value));
    const auto& ret = std::get<mir::ReturnTerminator>(block.terminator.value);
    ASSERT_TRUE(ret.value.has_value());
    ASSERT_TRUE(std::holds_alternative<mir::TempId>(ret.value->value));
    EXPECT_EQ(std::get<mir::TempId>(ret.value->value), define_stmt.dest);
}

TEST(MirLowerTest, LowersSignedComparison) {
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);
    semantic::TypeId bool_type = make_type(semantic::PrimitiveKind::BOOL);

    hir::Function function;
    function.return_type = hir::TypeAnnotation(bool_type);

    auto body = std::make_unique<hir::Block>();
    body->final_expr = make_binary_expr(
        hir::LessThan{.kind = hir::LessThan::Kind::SignedInt},
        make_int_literal_expr(1, int_type),
        make_int_literal_expr(2, int_type),
        bool_type);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    ASSERT_EQ(lowered.temp_types.size(), 1u);
    EXPECT_EQ(lowered.temp_types[0], bool_type);

    const auto& block = lowered.basic_blocks.front();
    ASSERT_EQ(block.statements.size(), 1u);
    const auto& define_stmt = std::get<mir::DefineStatement>(block.statements[0].value);
    ASSERT_TRUE(std::holds_alternative<mir::BinaryOpRValue>(define_stmt.rvalue.value));
    const auto& binary = std::get<mir::BinaryOpRValue>(define_stmt.rvalue.value);
    EXPECT_EQ(binary.kind, mir::BinaryOpRValue::Kind::ICmpLt);

    ASSERT_TRUE(std::holds_alternative<mir::ReturnTerminator>(block.terminator.value));
    const auto& ret = std::get<mir::ReturnTerminator>(block.terminator.value);
    ASSERT_TRUE(ret.value.has_value());
    ASSERT_TRUE(std::holds_alternative<mir::TempId>(ret.value->value));
    EXPECT_EQ(std::get<mir::TempId>(ret.value->value), define_stmt.dest);
}

TEST(MirLowerTest, LowersCastExpression) {
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);
    semantic::TypeId usize_type = make_type(semantic::PrimitiveKind::USIZE);

    hir::Function function;
    function.return_type = hir::TypeAnnotation(usize_type);

    hir::Cast cast_expr;
    cast_expr.expr = make_int_literal_expr(5, int_type);
    cast_expr.target_type = hir::TypeAnnotation(usize_type);
    auto expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(cast_expr)});
    expr->expr_info = make_value_info(usize_type, false);

    auto body = std::make_unique<hir::Block>();
    body->final_expr = std::move(expr);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    ASSERT_EQ(lowered.basic_blocks.size(), 1u);
    const auto& block = lowered.basic_blocks.front();
    ASSERT_EQ(block.statements.size(), 1u);
    const auto& define_stmt = std::get<mir::DefineStatement>(block.statements[0].value);
    ASSERT_TRUE(std::holds_alternative<mir::CastRValue>(define_stmt.rvalue.value));
    const auto& cast_rvalue = std::get<mir::CastRValue>(define_stmt.rvalue.value);
    EXPECT_EQ(cast_rvalue.target_type, usize_type);
    ASSERT_TRUE(std::holds_alternative<mir::ReturnTerminator>(block.terminator.value));
    const auto& ret = std::get<mir::ReturnTerminator>(block.terminator.value);
    ASSERT_TRUE(ret.value.has_value());
    ASSERT_TRUE(std::holds_alternative<mir::TempId>(ret.value->value));
    EXPECT_EQ(std::get<mir::TempId>(ret.value->value), define_stmt.dest);
}

TEST(MirLowerTest, LowersConstUseExpression) {
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);

    auto const_owner = std::make_unique<hir::ConstDef>();
    const_owner->type = hir::TypeAnnotation(int_type);
    const_owner->const_value = semantic::ConstVariant{semantic::IntConst{42}};

    hir::Function function;
    function.return_type = hir::TypeAnnotation(int_type);

    hir::ConstUse const_use;
    const_use.def = const_owner.get();
    auto expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(const_use)});
    expr->expr_info = make_value_info(int_type, false);

    auto body = std::make_unique<hir::Block>();
    body->final_expr = std::move(expr);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    ASSERT_EQ(lowered.basic_blocks.size(), 1u);
    const auto& block = lowered.basic_blocks.front();
    ASSERT_TRUE(std::holds_alternative<mir::ReturnTerminator>(block.terminator.value));
    const auto& ret = std::get<mir::ReturnTerminator>(block.terminator.value);
    ASSERT_TRUE(ret.value.has_value());
    const auto& operand = ret.value.value();
    ASSERT_TRUE(std::holds_alternative<mir::Constant>(operand.value));
    const auto& constant = std::get<mir::Constant>(operand.value);
    EXPECT_EQ(constant.type, int_type);
    ASSERT_TRUE(std::holds_alternative<mir::IntConstant>(constant.value));
    EXPECT_EQ(std::get<mir::IntConstant>(constant.value).value, 42u);
}

TEST(MirLowerTest, LowersStringConstUseExpression) {
    semantic::TypeId string_ref_type = make_string_ref_type();

    auto const_owner = std::make_unique<hir::ConstDef>();
    const_owner->type = hir::TypeAnnotation(string_ref_type);
    semantic::StringConst string_const;
    string_const.value = "hi";
    const_owner->const_value = semantic::ConstVariant{string_const};

    hir::Function function;
    function.return_type = hir::TypeAnnotation(string_ref_type);

    hir::ConstUse const_use;
    const_use.def = const_owner.get();
    auto expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(const_use)});
    expr->expr_info = make_value_info(string_ref_type, false);

    auto body = std::make_unique<hir::Block>();
    body->final_expr = std::move(expr);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    ASSERT_EQ(lowered.basic_blocks.size(), 1u);
    const auto& block = lowered.basic_blocks.front();
    ASSERT_TRUE(std::holds_alternative<mir::ReturnTerminator>(block.terminator.value));
    const auto& ret = std::get<mir::ReturnTerminator>(block.terminator.value);
    ASSERT_TRUE(ret.value.has_value());
    const auto& operand = ret.value.value();
    ASSERT_TRUE(std::holds_alternative<mir::Constant>(operand.value));
    const auto& constant = std::get<mir::Constant>(operand.value);
    ASSERT_TRUE(std::holds_alternative<mir::StringConstant>(constant.value));
    const auto& string_constant = std::get<mir::StringConstant>(constant.value);
    EXPECT_EQ(string_constant.length, 2u);
    ASSERT_FALSE(string_constant.data.empty());
    EXPECT_EQ(string_constant.data.back(), '\0');
    EXPECT_EQ(std::string(string_constant.data.c_str()), "hi");
}

TEST(MirLowerTest, LowersStructConstExpression) {
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);

    auto struct_def = std::make_unique<hir::StructDef>();
    auto assoc_const = std::make_unique<hir::ConstDef>();
    assoc_const->type = hir::TypeAnnotation(int_type);
    assoc_const->const_value = semantic::ConstVariant{semantic::IntConst{7}};

    hir::Function function;
    function.return_type = hir::TypeAnnotation(int_type);

    hir::StructConst struct_const;
    struct_const.struct_def = struct_def.get();
    struct_const.assoc_const = assoc_const.get();
    auto expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(struct_const)});
    expr->expr_info = make_value_info(int_type, false);

    auto body = std::make_unique<hir::Block>();
    body->final_expr = std::move(expr);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    ASSERT_EQ(lowered.basic_blocks.size(), 1u);
    const auto& block = lowered.basic_blocks.front();
    const auto& ret = std::get<mir::ReturnTerminator>(block.terminator.value);
    ASSERT_TRUE(ret.value.has_value());
    const auto& operand = ret.value.value();
    ASSERT_TRUE(std::holds_alternative<mir::Constant>(operand.value));
    const auto& constant = std::get<mir::Constant>(operand.value);
    EXPECT_EQ(constant.type, int_type);
    ASSERT_TRUE(std::holds_alternative<mir::IntConstant>(constant.value));
    EXPECT_EQ(std::get<mir::IntConstant>(constant.value).value, 7u);
}

TEST(MirLowerTest, LowersEnumVariantExpression) {
    auto enum_def = std::make_unique<hir::EnumDef>();
    enum_def->variants.push_back(semantic::EnumVariant{.name = ast::Identifier{"A"}});
    semantic::TypeId enum_type = semantic::get_typeID(semantic::Type{semantic::EnumType{enum_def.get()}});
    semantic::TypeId usize_type = make_type(semantic::PrimitiveKind::USIZE);

    hir::Function function;
    function.return_type = hir::TypeAnnotation(enum_type);

    hir::EnumVariant enum_variant;
    enum_variant.enum_def = enum_def.get();
    enum_variant.variant_index = 0;
    auto expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(enum_variant)});
    expr->expr_info = make_value_info(enum_type, false);

    auto body = std::make_unique<hir::Block>();
    body->final_expr = std::move(expr);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    EXPECT_EQ(lowered.return_type, usize_type);
    ASSERT_EQ(lowered.basic_blocks.size(), 1u);
    const auto& block = lowered.basic_blocks.front();
    const auto& ret = std::get<mir::ReturnTerminator>(block.terminator.value);
    ASSERT_TRUE(ret.value.has_value());
    const auto& operand = ret.value.value();
    ASSERT_TRUE(std::holds_alternative<mir::Constant>(operand.value));
    const auto& constant = std::get<mir::Constant>(operand.value);
    EXPECT_EQ(constant.type, usize_type);
    ASSERT_TRUE(std::holds_alternative<mir::IntConstant>(constant.value));
    EXPECT_EQ(std::get<mir::IntConstant>(constant.value).value, 0u);
}

TEST(MirLowerTest, LowersIfExpressionWithPhi) {
    semantic::TypeId bool_type = make_type(semantic::PrimitiveKind::BOOL);
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);

    hir::If if_expr;
    if_expr.condition = make_bool_literal_expr(true, bool_type);
    if_expr.then_block = make_block_with_expr(make_int_literal_expr(10, int_type));
    auto else_expr = make_int_literal_expr(20, int_type);
    if_expr.else_expr = std::optional<std::unique_ptr<hir::Expr>>(std::move(else_expr));

    auto if_expr_node = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(if_expr)});
    if_expr_node->expr_info = make_value_info(int_type, false);

    hir::Function function;
    function.return_type = hir::TypeAnnotation(int_type);
    auto body = std::make_unique<hir::Block>();
    body->final_expr = std::move(if_expr_node);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    ASSERT_EQ(lowered.basic_blocks.size(), 4u);
    const auto& entry = lowered.basic_blocks[0];
    ASSERT_TRUE(std::holds_alternative<mir::SwitchIntTerminator>(entry.terminator.value));

    const auto& join = lowered.basic_blocks.back();
    ASSERT_EQ(join.phis.size(), 1u);
    const auto& phi = join.phis.front();
    ASSERT_LT(phi.dest, lowered.temp_types.size());
    EXPECT_EQ(lowered.temp_types[phi.dest], int_type);

    ASSERT_TRUE(std::holds_alternative<mir::ReturnTerminator>(join.terminator.value));
    const auto& ret = std::get<mir::ReturnTerminator>(join.terminator.value);
    ASSERT_TRUE(ret.value.has_value());
    ASSERT_TRUE(std::holds_alternative<mir::TempId>(ret.value->value));
    EXPECT_EQ(std::get<mir::TempId>(ret.value->value), phi.dest);
}

TEST(MirLowerTest, LowersShortCircuitAnd) {
    semantic::TypeId bool_type = make_type(semantic::PrimitiveKind::BOOL);

    auto lhs = make_bool_literal_expr(true, bool_type);
    auto rhs = make_bool_literal_expr(false, bool_type);

    auto and_expr = make_binary_expr(hir::LogicalAnd{}, std::move(lhs), std::move(rhs), bool_type);

    hir::Function function;
    function.return_type = hir::TypeAnnotation(bool_type);
    auto body = std::make_unique<hir::Block>();
    body->final_expr = std::move(and_expr);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    ASSERT_GE(lowered.basic_blocks.size(), 3u);

    const mir::PhiNode* found_phi = nullptr;
    for (const auto& block : lowered.basic_blocks) {
        if (!block.phis.empty()) {
            found_phi = &block.phis.front();
            break;
        }
    }
    ASSERT_NE(found_phi, nullptr);
    ASSERT_LT(found_phi->dest, lowered.temp_types.size());
    EXPECT_EQ(lowered.temp_types[found_phi->dest], bool_type);
}

TEST(MirLowerTest, LowersShortCircuitOr) {
    semantic::TypeId bool_type = make_type(semantic::PrimitiveKind::BOOL);

    auto lhs = make_bool_literal_expr(true, bool_type);
    auto rhs = make_bool_literal_expr(false, bool_type);

    auto or_expr = make_binary_expr(hir::LogicalOr{}, std::move(lhs), std::move(rhs), bool_type);

    hir::Function function;
    function.return_type = hir::TypeAnnotation(bool_type);
    auto body = std::make_unique<hir::Block>();
    body->final_expr = std::move(or_expr);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    ASSERT_GE(lowered.basic_blocks.size(), 3u);
    const auto& entry_block = lowered.basic_blocks.front();
    ASSERT_FALSE(entry_block.statements.empty());
    ASSERT_TRUE(std::holds_alternative<mir::DefineStatement>(entry_block.statements.front().value));
    const auto& first_stmt = std::get<mir::DefineStatement>(entry_block.statements.front().value);
    ASSERT_TRUE(std::holds_alternative<mir::ConstantRValue>(first_stmt.rvalue.value));

    const mir::PhiNode* phi = nullptr;
    for (const auto& block : lowered.basic_blocks) {
        if (!block.phis.empty()) {
            phi = &block.phis.front();
            break;
        }
    }
    ASSERT_NE(phi, nullptr);
    ASSERT_EQ(phi->incoming.size(), 2u);
    ASSERT_LT(phi->dest, lowered.temp_types.size());
    EXPECT_EQ(lowered.temp_types[phi->dest], bool_type);
}

TEST(MirLowerTest, LowersLoopWithBreakValue) {
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);

    auto loop_expr_node = std::make_unique<hir::Expr>(hir::ExprVariant{hir::Loop{}});
    auto& loop = std::get<hir::Loop>(loop_expr_node->value);
    loop.break_type = int_type;

    auto break_expr_node = std::make_unique<hir::Expr>(hir::ExprVariant{hir::Break{}});
    auto& break_expr = std::get<hir::Break>(break_expr_node->value);
    break_expr.value = std::optional<std::unique_ptr<hir::Expr>>(make_int_literal_expr(5, int_type));
    break_expr.target = std::variant<hir::Loop*, hir::While*>(&loop);
    break_expr_node->expr_info = make_value_info(int_type, false);

    loop.body = make_block_with_expr(std::move(break_expr_node));
    loop_expr_node->expr_info = make_value_info(int_type, false);

    hir::Function function;
    function.return_type = hir::TypeAnnotation(int_type);
    auto body = std::make_unique<hir::Block>();
    body->final_expr = std::move(loop_expr_node);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    ASSERT_GE(lowered.basic_blocks.size(), 3u);
    const auto& break_block = lowered.basic_blocks.back();
    ASSERT_EQ(break_block.phis.size(), 1u);
    const auto& phi = break_block.phis.front();
    ASSERT_LT(phi.dest, lowered.temp_types.size());
    EXPECT_EQ(lowered.temp_types[phi.dest], int_type);

    ASSERT_TRUE(std::holds_alternative<mir::ReturnTerminator>(break_block.terminator.value));
    const auto& ret = std::get<mir::ReturnTerminator>(break_block.terminator.value);
    ASSERT_TRUE(ret.value.has_value());
    ASSERT_TRUE(std::holds_alternative<mir::TempId>(ret.value->value));
    EXPECT_EQ(std::get<mir::TempId>(ret.value->value), phi.dest);
}

TEST(MirLowerTest, LowersWhileLoopControlFlow) {
    semantic::TypeId bool_type = make_type(semantic::PrimitiveKind::BOOL);
    semantic::TypeId unit_type = make_unit_type();

    auto while_expr = std::make_unique<hir::Expr>(hir::ExprVariant{hir::While{}});
    auto& while_node = std::get<hir::While>(while_expr->value);
    while_node.condition = make_bool_literal_expr(true, bool_type);
    while_node.body = std::make_unique<hir::Block>();
    while_node.break_type = std::nullopt;
    while_expr->expr_info = make_value_info(unit_type, false);

    hir::Function function;
    function.return_type = hir::TypeAnnotation(unit_type);
    auto body = std::make_unique<hir::Block>();
    body->final_expr = std::move(while_expr);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    ASSERT_GE(lowered.basic_blocks.size(), 4u);
    const auto& cond_block = lowered.basic_blocks[1];
    ASSERT_TRUE(std::holds_alternative<mir::SwitchIntTerminator>(cond_block.terminator.value));
    const auto& body_block = lowered.basic_blocks[2];
    ASSERT_TRUE(std::holds_alternative<mir::GotoTerminator>(body_block.terminator.value));
}

TEST(MirLowerTest, LowersDirectFunctionCall) {
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);

    auto callee_item = std::make_unique<hir::Item>(hir::Function{});
    auto& callee = std::get<hir::Function>(callee_item->value);
    callee.return_type = hir::TypeAnnotation(int_type);
    auto callee_body = std::make_unique<hir::Block>();
    callee_body->final_expr = make_int_literal_expr(7, int_type);
    callee.body = std::move(callee_body);

    auto caller_item = std::make_unique<hir::Item>(hir::Function{});
    auto& caller = std::get<hir::Function>(caller_item->value);
    caller.return_type = hir::TypeAnnotation(int_type);

    hir::Call call_expr;
    call_expr.callee = make_func_use_expr(&callee);
    
    auto call_expr_node = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(call_expr)});
    call_expr_node->expr_info = make_value_info(int_type, false);

    auto caller_body = std::make_unique<hir::Block>();
    caller_body->final_expr = std::move(call_expr_node);
    caller.body = std::move(caller_body);

    hir::Program program;
    program.items.push_back(std::move(callee_item));
    program.items.push_back(std::move(caller_item));

    mir::MirModule module = mir::lower_program(program);
    ASSERT_EQ(module.functions.size(), 2u);
    const auto& callee_mir = module.functions[0];
    const auto& caller_mir = module.functions[1];
    const auto& block = caller_mir.basic_blocks.front();
    ASSERT_EQ(block.statements.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<mir::CallStatement>(block.statements[0].value));
    const auto& call_stmt = std::get<mir::CallStatement>(block.statements[0].value);
    ASSERT_TRUE(call_stmt.dest.has_value());
    EXPECT_EQ(call_stmt.function, callee_mir.id);
    ASSERT_TRUE(std::holds_alternative<mir::ReturnTerminator>(block.terminator.value));
    const auto& ret = std::get<mir::ReturnTerminator>(block.terminator.value);
    ASSERT_TRUE(ret.value.has_value());
    ASSERT_TRUE(std::holds_alternative<mir::TempId>(ret.value->value));
    EXPECT_EQ(std::get<mir::TempId>(ret.value->value), call_stmt.dest.value());
}

TEST(MirLowerTest, LowerFunctionUsesProvidedIdMapForCalls) {
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);

    hir::Function callee;
    callee.return_type = hir::TypeAnnotation(int_type);
    auto callee_body = std::make_unique<hir::Block>();
    callee_body->final_expr = make_int_literal_expr(11, int_type);
    callee.body = std::move(callee_body);

    hir::Function caller;
    caller.return_type = hir::TypeAnnotation(int_type);

    hir::Call call_expr;
    call_expr.callee = make_func_use_expr(&callee);
    
    auto call_expr_node = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(call_expr)});
    call_expr_node->expr_info = make_value_info(int_type, false);

    auto caller_body = std::make_unique<hir::Block>();
    caller_body->final_expr = std::move(call_expr_node);
    caller.body = std::move(caller_body);

    std::unordered_map<const void*, mir::FunctionId> ids;
    ids.emplace(&callee, static_cast<mir::FunctionId>(0));
    ids.emplace(&caller, static_cast<mir::FunctionId>(1));

    mir::MirFunction lowered = mir::lower_function(caller, ids, ids.at(&caller));
    ASSERT_EQ(lowered.basic_blocks.size(), 1u);
    const auto& block = lowered.basic_blocks.front();
    ASSERT_EQ(block.statements.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<mir::CallStatement>(block.statements[0].value));
    const auto& call_stmt = std::get<mir::CallStatement>(block.statements[0].value);
    ASSERT_TRUE(call_stmt.dest.has_value());
    EXPECT_EQ(call_stmt.function, ids.at(&callee));
}

TEST(MirLowerTest, LowersLoopWithContinue) {
    semantic::TypeId unit_type = make_unit_type();

    auto loop_expr = std::make_unique<hir::Expr>(hir::ExprVariant{hir::Loop{}});
    auto& loop = std::get<hir::Loop>(loop_expr->value);
    loop.break_type = std::nullopt;

    auto continue_expr = std::make_unique<hir::Expr>(hir::ExprVariant{hir::Continue{}});
    auto& continue_node = std::get<hir::Continue>(continue_expr->value);
    continue_node.target = std::variant<hir::Loop*, hir::While*>(&loop);
    continue_expr->expr_info = make_value_info(unit_type, false);

    loop.body = make_block_with_expr(std::move(continue_expr));
    loop_expr->expr_info = make_value_info(unit_type, false);

    hir::Function function;
    function.return_type = hir::TypeAnnotation(unit_type);
    auto body = std::make_unique<hir::Block>();
    body->final_expr = std::move(loop_expr);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    ASSERT_GE(lowered.basic_blocks.size(), 3u);

    const auto& loop_body_block = lowered.basic_blocks[1];
    ASSERT_TRUE(std::holds_alternative<mir::GotoTerminator>(loop_body_block.terminator.value));
    const auto& term = std::get<mir::GotoTerminator>(loop_body_block.terminator.value);
    EXPECT_EQ(term.target, 1u);
}

TEST(MirLowerTest, LowersNestedLoopBreakValue) {
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);

    auto outer_loop_expr = std::make_unique<hir::Expr>(hir::ExprVariant{hir::Loop{}});
    auto& outer_loop = std::get<hir::Loop>(outer_loop_expr->value);
    outer_loop.break_type = int_type;

    auto inner_loop_expr = std::make_unique<hir::Expr>(hir::ExprVariant{hir::Loop{}});
    auto& inner_loop = std::get<hir::Loop>(inner_loop_expr->value);
    inner_loop.break_type = int_type;

    auto inner_break_expr = std::make_unique<hir::Expr>(hir::ExprVariant{hir::Break{}});
    auto& inner_break = std::get<hir::Break>(inner_break_expr->value);
    inner_break.value = std::optional<std::unique_ptr<hir::Expr>>(make_int_literal_expr(7, int_type));
    inner_break.target = std::variant<hir::Loop*, hir::While*>(&inner_loop);
    inner_break_expr->expr_info = make_value_info(int_type, false);

    inner_loop.body = make_block_with_expr(std::move(inner_break_expr));
    inner_loop_expr->expr_info = make_value_info(int_type, false);

    auto outer_break_expr = std::make_unique<hir::Expr>(hir::ExprVariant{hir::Break{}});
    auto& outer_break = std::get<hir::Break>(outer_break_expr->value);
    outer_break.value = std::optional<std::unique_ptr<hir::Expr>>(std::move(inner_loop_expr));
    outer_break.target = std::variant<hir::Loop*, hir::While*>(&outer_loop);
    outer_break_expr->expr_info = make_value_info(int_type, false);

    outer_loop.body = make_block_with_expr(std::move(outer_break_expr));
    outer_loop_expr->expr_info = make_value_info(int_type, false);

    hir::Function function;
    function.return_type = hir::TypeAnnotation(int_type);
    auto body = std::make_unique<hir::Block>();
    body->final_expr = std::move(outer_loop_expr);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    size_t phi_count = 0;
    for (const auto& block : lowered.basic_blocks) {
        phi_count += block.phis.size();
        for (const auto& phi : block.phis) {
            ASSERT_LT(phi.dest, lowered.temp_types.size());
            EXPECT_EQ(lowered.temp_types[phi.dest], int_type);
        }
    }
    EXPECT_GE(phi_count, 2u);

    bool found_return = false;
    for (const auto& block : lowered.basic_blocks) {
        if (std::holds_alternative<mir::ReturnTerminator>(block.terminator.value)) {
            found_return = true;
            break;
        }
    }
    ASSERT_TRUE(found_return);
}

TEST(MirLowerTest, LowersStructLiteralAggregate) {
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);

    auto struct_item = std::make_unique<hir::Item>(hir::StructDef{});
    auto& struct_def = std::get<hir::StructDef>(struct_item->value);
    struct_def.fields.push_back(semantic::Field{.name = ast::Identifier{"a"}, .type = std::nullopt});
    struct_def.fields.push_back(semantic::Field{.name = ast::Identifier{"b"}, .type = std::nullopt});
    struct_def.field_type_annotations.push_back(hir::TypeAnnotation(int_type));
    struct_def.field_type_annotations.push_back(hir::TypeAnnotation(int_type));

    semantic::TypeId struct_type = semantic::get_typeID(semantic::Type{semantic::StructType{&struct_def}});

    hir::StructLiteral literal;
    literal.struct_path = &struct_def;
    hir::StructLiteral::CanonicalFields canonical;
    canonical.initializers.push_back(make_int_literal_expr(1, int_type));
    canonical.initializers.push_back(make_int_literal_expr(2, int_type));
    literal.fields = std::move(canonical);

    auto literal_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(literal)});
    literal_expr->expr_info = make_value_info(struct_type, false);

    hir::Function function;
    function.return_type = hir::TypeAnnotation(struct_type);
    auto body = std::make_unique<hir::Block>();
    body->final_expr = std::move(literal_expr);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    ASSERT_EQ(lowered.basic_blocks.size(), 1u);
    const auto& block = lowered.basic_blocks.front();
    ASSERT_EQ(block.statements.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<mir::DefineStatement>(block.statements[0].value));
    const auto& define_stmt = std::get<mir::DefineStatement>(block.statements[0].value);
    ASSERT_TRUE(std::holds_alternative<mir::AggregateRValue>(define_stmt.rvalue.value));
    const auto& aggregate = std::get<mir::AggregateRValue>(define_stmt.rvalue.value);
    EXPECT_EQ(aggregate.kind, mir::AggregateRValue::Kind::Struct);
    ASSERT_EQ(aggregate.elements.size(), 2u);
    ASSERT_TRUE(std::holds_alternative<mir::Constant>(aggregate.elements[0].value));
    ASSERT_TRUE(std::holds_alternative<mir::Constant>(aggregate.elements[1].value));
}

TEST(MirLowerTest, LowersArrayLiteralAggregate) {
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);
    semantic::TypeId array_type = semantic::get_typeID(semantic::Type{semantic::ArrayType{int_type, 2}});

    hir::ArrayLiteral array_literal;
    array_literal.elements.push_back(make_int_literal_expr(3, int_type));
    array_literal.elements.push_back(make_int_literal_expr(4, int_type));

    auto array_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(array_literal)});
    array_expr->expr_info = make_value_info(array_type, false);

    hir::Function function;
    function.return_type = hir::TypeAnnotation(array_type);
    auto body = std::make_unique<hir::Block>();
    body->final_expr = std::move(array_expr);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    ASSERT_EQ(lowered.basic_blocks.size(), 1u);
    const auto& block = lowered.basic_blocks.front();
    ASSERT_EQ(block.statements.size(), 1u);
    const auto& define_stmt = std::get<mir::DefineStatement>(block.statements[0].value);
    ASSERT_TRUE(std::holds_alternative<mir::AggregateRValue>(define_stmt.rvalue.value));
    const auto& aggregate = std::get<mir::AggregateRValue>(define_stmt.rvalue.value);
    EXPECT_EQ(aggregate.kind, mir::AggregateRValue::Kind::Array);
    ASSERT_EQ(aggregate.elements.size(), 2u);
}

TEST(MirLowerTest, LowersArrayRepeatAggregate) {
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);
    semantic::TypeId array_type = semantic::get_typeID(semantic::Type{semantic::ArrayType{int_type, 3}});

    hir::ArrayRepeat array_repeat;
    array_repeat.value = make_int_literal_expr(9, int_type);
    array_repeat.count = static_cast<size_t>(3);

    auto array_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(array_repeat)});
    array_expr->expr_info = make_value_info(array_type, false);

    hir::Function function;
    function.return_type = hir::TypeAnnotation(array_type);
    auto body = std::make_unique<hir::Block>();
    body->final_expr = std::move(array_expr);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    ASSERT_EQ(lowered.basic_blocks.size(), 1u);
    const auto& block = lowered.basic_blocks.front();
    ASSERT_EQ(block.statements.size(), 1u);
    const auto& define_stmt = std::get<mir::DefineStatement>(block.statements[0].value);
    ASSERT_TRUE(std::holds_alternative<mir::ArrayRepeatRValue>(define_stmt.rvalue.value));
    const auto& repeat = std::get<mir::ArrayRepeatRValue>(define_stmt.rvalue.value);
    EXPECT_EQ(repeat.count, 3u);
    ASSERT_TRUE(std::holds_alternative<mir::Constant>(repeat.value.value));
}

TEST(MirLowerTest, LowersMethodCallWithReceiver) {
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);

    auto struct_item = std::make_unique<hir::Item>(hir::StructDef{});
    auto& struct_def = std::get<hir::StructDef>(struct_item->value);
    semantic::TypeId struct_type = semantic::get_typeID(semantic::Type{semantic::StructType{&struct_def}});

    auto impl_item = std::make_unique<hir::Item>(hir::Impl{});
    auto& impl = std::get<hir::Impl>(impl_item->value);
    impl.for_type = hir::TypeAnnotation(struct_type);

    auto method_assoc = std::make_unique<hir::AssociatedItem>(hir::AssociatedItemVariant{hir::Method{}});
    auto& method = std::get<hir::Method>(method_assoc->value);
    method.self_param.is_reference = false;
    method.self_param.is_mutable = false;
    method.return_type = hir::TypeAnnotation(int_type);
    method.body = make_block_with_expr(make_int_literal_expr(11, int_type));
    const hir::Method* method_ptr = &method;
    impl.items.push_back(std::move(method_assoc));

    hir::StructLiteral receiver_literal;
    receiver_literal.struct_path = &struct_def;
    hir::StructLiteral::CanonicalFields receiver_fields;
    receiver_fields.initializers.push_back(make_int_literal_expr(5, int_type));
    receiver_fields.initializers.push_back(make_int_literal_expr(6, int_type));
    receiver_literal.fields = std::move(receiver_fields);
    auto receiver_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(receiver_literal)});
    receiver_expr->expr_info = make_value_info(struct_type, false);

    hir::MethodCall method_call;
    method_call.receiver = std::move(receiver_expr);
    method_call.method = method_ptr;
    auto method_call_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(method_call)});
    method_call_expr->expr_info = make_value_info(int_type, false);

    auto caller_item = std::make_unique<hir::Item>(hir::Function{});
    auto& caller = std::get<hir::Function>(caller_item->value);
    caller.return_type = hir::TypeAnnotation(int_type);
    auto body = std::make_unique<hir::Block>();
    body->final_expr = std::move(method_call_expr);
    caller.body = std::move(body);

    hir::Program program;
    program.items.push_back(std::move(struct_item));
    program.items.push_back(std::move(impl_item));
    program.items.push_back(std::move(caller_item));

    mir::MirModule module = mir::lower_program(program);
    ASSERT_EQ(module.functions.size(), 2u);
    const auto& method_mir = module.functions[0];
    const auto& caller_mir = module.functions[1];
    const auto& entry = caller_mir.basic_blocks.front();
    ASSERT_EQ(entry.statements.size(), 2u);
    const auto& aggregate_define = std::get<mir::DefineStatement>(entry.statements[0].value);
    ASSERT_TRUE(std::holds_alternative<mir::AggregateRValue>(aggregate_define.rvalue.value));
    const auto& call_stmt = std::get<mir::CallStatement>(entry.statements[1].value);
    EXPECT_EQ(call_stmt.function, method_mir.id);
    ASSERT_EQ(call_stmt.args.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<mir::TempId>(call_stmt.args[0].value));
    EXPECT_EQ(std::get<mir::TempId>(call_stmt.args[0].value), aggregate_define.dest);
}

TEST(MirLowerTest, LowersReferenceToLocalPlace) {
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);
    semantic::TypeId ref_type = semantic::get_typeID(semantic::Type{semantic::ReferenceType{int_type, false}});

    auto local = std::make_unique<hir::Local>();
    local->name = ast::Identifier{"x"};
    local->is_mutable = true;
    local->type_annotation = hir::TypeAnnotation(int_type);
    hir::Local* local_ptr = local.get();

    hir::BindingDef binding;
    binding.local = local_ptr;
    auto pattern = std::make_unique<hir::Pattern>(hir::PatternVariant{std::move(binding)});

    hir::LetStmt let_stmt;
    let_stmt.pattern = std::move(pattern);
    let_stmt.type_annotation = hir::TypeAnnotation(int_type);
    let_stmt.initializer = make_int_literal_expr(1, int_type);
    auto let_stmt_node = std::make_unique<hir::Stmt>(hir::StmtVariant{std::move(let_stmt)});

    hir::Variable variable;
    variable.local_id = local_ptr;
    auto var_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(variable)});
    var_expr->expr_info = make_value_info(int_type, true);

    hir::UnaryOp ref_unary;
    ref_unary.op = hir::Reference{.is_mutable = false};
    ref_unary.rhs = std::move(var_expr);
    auto ref_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(ref_unary)});
    ref_expr->expr_info = make_value_info(ref_type, false);

    hir::Function function;
    function.return_type = hir::TypeAnnotation(ref_type);
    function.locals.push_back(std::move(local));
    auto body = std::make_unique<hir::Block>();
    body->stmts.push_back(std::move(let_stmt_node));
    body->final_expr = std::move(ref_expr);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    ASSERT_EQ(lowered.basic_blocks.size(), 1u);
    const auto& block = lowered.basic_blocks.front();
    const mir::DefineStatement* ref_define = nullptr;
    for (const auto& stmt : block.statements) {
        if (const auto* define = std::get_if<mir::DefineStatement>(&stmt.value)) {
            if (std::holds_alternative<mir::RefRValue>(define->rvalue.value)) {
                ref_define = define;
                break;
            }
        }
    }
    ASSERT_NE(ref_define, nullptr);
    const auto& ref_rvalue = std::get<mir::RefRValue>(ref_define->rvalue.value);
    ASSERT_TRUE(std::holds_alternative<mir::LocalPlace>(ref_rvalue.place.base));
    EXPECT_EQ(std::get<mir::LocalPlace>(ref_rvalue.place.base).id, 0u);
    EXPECT_TRUE(ref_rvalue.place.projections.empty());
}

TEST(MirLowerTest, LowersReferenceToFieldPlace) {
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);

    auto struct_item = std::make_unique<hir::Item>(hir::StructDef{});
    auto& struct_def = std::get<hir::StructDef>(struct_item->value);
    struct_def.fields.push_back(semantic::Field{.name = ast::Identifier{"a"}, .type = std::nullopt});
    struct_def.fields.push_back(semantic::Field{.name = ast::Identifier{"b"}, .type = std::nullopt});
    struct_def.field_type_annotations.push_back(hir::TypeAnnotation(int_type));
    struct_def.field_type_annotations.push_back(hir::TypeAnnotation(int_type));
    semantic::TypeId struct_type = semantic::get_typeID(semantic::Type{semantic::StructType{&struct_def}});
    semantic::TypeId ref_type = semantic::get_typeID(semantic::Type{semantic::ReferenceType{int_type, false}});

    auto local = std::make_unique<hir::Local>();
    local->name = ast::Identifier{"s"};
    local->is_mutable = true;
    local->type_annotation = hir::TypeAnnotation(struct_type);
    hir::Local* local_ptr = local.get();

    hir::BindingDef binding;
    binding.local = local_ptr;
    auto pattern = std::make_unique<hir::Pattern>(hir::PatternVariant{std::move(binding)});

    hir::StructLiteral literal;
    literal.struct_path = &struct_def;
    hir::StructLiteral::CanonicalFields canonical;
    canonical.initializers.push_back(make_int_literal_expr(1, int_type));
    canonical.initializers.push_back(make_int_literal_expr(2, int_type));
    literal.fields = std::move(canonical);
    auto literal_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(literal)});
    literal_expr->expr_info = make_value_info(struct_type, false);

    hir::LetStmt let_stmt;
    let_stmt.pattern = std::move(pattern);
    let_stmt.type_annotation = hir::TypeAnnotation(struct_type);
    let_stmt.initializer = std::move(literal_expr);
    auto let_stmt_node = std::make_unique<hir::Stmt>(hir::StmtVariant{std::move(let_stmt)});

    hir::Variable variable;
    variable.local_id = local_ptr;
    auto var_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(variable)});
    var_expr->expr_info = make_value_info(struct_type, true);

    hir::FieldAccess field_access;
    field_access.base = std::move(var_expr);
    field_access.field = static_cast<size_t>(0);
    auto field_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(field_access)});
    field_expr->expr_info = make_value_info(int_type, true);

    hir::UnaryOp ref_unary;
    ref_unary.op = hir::Reference{.is_mutable = false};
    ref_unary.rhs = std::move(field_expr);
    auto ref_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(ref_unary)});
    ref_expr->expr_info = make_value_info(ref_type, false);

    hir::Function function;
    function.return_type = hir::TypeAnnotation(ref_type);
    function.locals.push_back(std::move(local));
    auto body = std::make_unique<hir::Block>();
    body->stmts.push_back(std::move(let_stmt_node));
    body->final_expr = std::move(ref_expr);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    ASSERT_EQ(lowered.locals.size(), 1u);
    ASSERT_EQ(lowered.basic_blocks.size(), 1u);
    const auto& block = lowered.basic_blocks.front();
    const mir::DefineStatement* ref_define = nullptr;
    for (const auto& stmt : block.statements) {
        if (const auto* define = std::get_if<mir::DefineStatement>(&stmt.value)) {
            if (std::holds_alternative<mir::RefRValue>(define->rvalue.value)) {
                ref_define = define;
                break;
            }
        }
    }
    ASSERT_NE(ref_define, nullptr);
    const auto& ref_rvalue = std::get<mir::RefRValue>(ref_define->rvalue.value);
    ASSERT_TRUE(std::holds_alternative<mir::LocalPlace>(ref_rvalue.place.base));
    EXPECT_EQ(std::get<mir::LocalPlace>(ref_rvalue.place.base).id, 0u);
    ASSERT_EQ(ref_rvalue.place.projections.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<mir::FieldProjection>(ref_rvalue.place.projections[0]));
    EXPECT_EQ(std::get<mir::FieldProjection>(ref_rvalue.place.projections[0]).index, 0u);
}

TEST(MirLowerTest, LowersReferenceToIndexedPlace) {
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);
    semantic::TypeId index_type = make_type(semantic::PrimitiveKind::USIZE);
    semantic::TypeId array_type = semantic::get_typeID(semantic::Type{semantic::ArrayType{int_type, 2}});
    semantic::TypeId ref_type = semantic::get_typeID(semantic::Type{semantic::ReferenceType{int_type, false}});

    auto local = std::make_unique<hir::Local>();
    local->name = ast::Identifier{"arr"};
    local->is_mutable = true;
    local->type_annotation = hir::TypeAnnotation(array_type);
    hir::Local* local_ptr = local.get();

    hir::BindingDef binding;
    binding.local = local_ptr;
    auto pattern = std::make_unique<hir::Pattern>(hir::PatternVariant{std::move(binding)});

    hir::ArrayLiteral literal;
    literal.elements.push_back(make_int_literal_expr(1, int_type));
    literal.elements.push_back(make_int_literal_expr(2, int_type));
    auto literal_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(literal)});
    literal_expr->expr_info = make_value_info(array_type, false);

    hir::LetStmt let_stmt;
    let_stmt.pattern = std::move(pattern);
    let_stmt.type_annotation = hir::TypeAnnotation(array_type);
    let_stmt.initializer = std::move(literal_expr);
    auto let_stmt_node = std::make_unique<hir::Stmt>(hir::StmtVariant{std::move(let_stmt)});

    hir::Variable variable;
    variable.local_id = local_ptr;
    auto var_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(variable)});
    var_expr->expr_info = make_value_info(array_type, true);

    hir::Index index_expr;
    index_expr.base = std::move(var_expr);
    index_expr.index = make_int_literal_expr(0, index_type);
    auto indexed_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(index_expr)});
    indexed_expr->expr_info = make_value_info(int_type, true);

    hir::UnaryOp ref_unary;
    ref_unary.op = hir::Reference{.is_mutable = false};
    ref_unary.rhs = std::move(indexed_expr);
    auto ref_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(ref_unary)});
    ref_expr->expr_info = make_value_info(ref_type, false);

    hir::Function function;
    function.return_type = hir::TypeAnnotation(ref_type);
    function.locals.push_back(std::move(local));
    auto body = std::make_unique<hir::Block>();
    body->stmts.push_back(std::move(let_stmt_node));
    body->final_expr = std::move(ref_expr);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    const auto& block = lowered.basic_blocks.front();
    const mir::DefineStatement* ref_define = nullptr;
    for (const auto& stmt : block.statements) {
        if (const auto* define = std::get_if<mir::DefineStatement>(&stmt.value)) {
            if (std::holds_alternative<mir::RefRValue>(define->rvalue.value)) {
                ref_define = define;
                break;
            }
        }
    }
    ASSERT_NE(ref_define, nullptr);
    const auto& ref_rvalue = std::get<mir::RefRValue>(ref_define->rvalue.value);
    ASSERT_TRUE(std::holds_alternative<mir::LocalPlace>(ref_rvalue.place.base));
    EXPECT_EQ(std::get<mir::LocalPlace>(ref_rvalue.place.base).id, 0u);
    ASSERT_EQ(ref_rvalue.place.projections.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<mir::IndexProjection>(ref_rvalue.place.projections[0]));
}

TEST(MirLowerTest, LowersReferenceToRValueByMaterializingLocal) {
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);
    semantic::TypeId ref_type = semantic::get_typeID(semantic::Type{semantic::ReferenceType{int_type, false}});

    hir::Literal literal{
        .value = hir::Literal::Integer{.value = 5, .suffix_type = ast::IntegerLiteralExpr::I32, .is_negative = false},
        
    };
    auto literal_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(literal)});
    literal_expr->expr_info = make_value_info(int_type, false);

    hir::UnaryOp ref_unary;
    ref_unary.op = hir::Reference{.is_mutable = false};
    ref_unary.rhs = std::move(literal_expr);
    auto ref_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(ref_unary)});
    ref_expr->expr_info = make_value_info(ref_type, false);

    hir::Function function;
    function.return_type = hir::TypeAnnotation(ref_type);
    auto body = std::make_unique<hir::Block>();
    body->final_expr = std::move(ref_expr);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    ASSERT_EQ(lowered.locals.size(), 1u);
    EXPECT_EQ(lowered.locals[0].type, int_type);
    EXPECT_EQ(lowered.locals[0].debug_name.rfind("_ref_tmp", 0), 0u);

    ASSERT_EQ(lowered.basic_blocks.size(), 1u);
    const auto& block = lowered.basic_blocks.front();
    ASSERT_EQ(block.statements.size(), 2u);
    const auto& assign = std::get<mir::AssignStatement>(block.statements[0].value);
    ASSERT_TRUE(std::holds_alternative<mir::LocalPlace>(assign.dest.base));
    EXPECT_EQ(std::get<mir::LocalPlace>(assign.dest.base).id, 0u);
    ASSERT_TRUE(std::holds_alternative<mir::Constant>(assign.src.value));

    const auto& define = std::get<mir::DefineStatement>(block.statements[1].value);
    ASSERT_TRUE(std::holds_alternative<mir::RefRValue>(define.rvalue.value));
    const auto& ref_rvalue = std::get<mir::RefRValue>(define.rvalue.value);
    ASSERT_TRUE(std::holds_alternative<mir::LocalPlace>(ref_rvalue.place.base));
    EXPECT_EQ(std::get<mir::LocalPlace>(ref_rvalue.place.base).id, 0u);
    EXPECT_TRUE(ref_rvalue.place.projections.empty());
}

TEST(MirLowerTest, LowersMutableReferenceToRValueByMaterializingLocal) {
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);
    semantic::TypeId ref_type = semantic::get_typeID(semantic::Type{semantic::ReferenceType{int_type, true}});

    hir::Literal literal{
        .value = hir::Literal::Integer{.value = 9, .suffix_type = ast::IntegerLiteralExpr::I32, .is_negative = false},
        
    };
    auto literal_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(literal)});
    literal_expr->expr_info = make_value_info(int_type, false);

    hir::UnaryOp ref_unary;
    ref_unary.op = hir::Reference{.is_mutable = true};
    ref_unary.rhs = std::move(literal_expr);
    auto ref_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(ref_unary)});
    ref_expr->expr_info = make_value_info(ref_type, false);

    hir::Function function;
    function.return_type = hir::TypeAnnotation(ref_type);
    auto body = std::make_unique<hir::Block>();
    body->final_expr = std::move(ref_expr);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    ASSERT_EQ(lowered.locals.size(), 1u);
    EXPECT_EQ(lowered.locals[0].type, int_type);
    EXPECT_EQ(lowered.locals[0].debug_name.rfind("_ref_mut_tmp", 0), 0u);

    ASSERT_EQ(lowered.basic_blocks.size(), 1u);
    const auto& block = lowered.basic_blocks.front();
    ASSERT_EQ(block.statements.size(), 2u);
    const auto& assign = std::get<mir::AssignStatement>(block.statements[0].value);
    ASSERT_TRUE(std::holds_alternative<mir::LocalPlace>(assign.dest.base));
    EXPECT_EQ(std::get<mir::LocalPlace>(assign.dest.base).id, 0u);

    const auto& define = std::get<mir::DefineStatement>(block.statements[1].value);
    ASSERT_TRUE(std::holds_alternative<mir::RefRValue>(define.rvalue.value));
    const auto& ref_rvalue = std::get<mir::RefRValue>(define.rvalue.value);
    ASSERT_TRUE(std::holds_alternative<mir::LocalPlace>(ref_rvalue.place.base));
    EXPECT_EQ(std::get<mir::LocalPlace>(ref_rvalue.place.base).id, 0u);
}

TEST(MirLowerTest, LowersAssignmentToIndexedPlace) {
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);
    semantic::TypeId unit_type = make_unit_type();
    semantic::TypeId index_type = make_type(semantic::PrimitiveKind::USIZE);
    semantic::TypeId array_type = semantic::get_typeID(semantic::Type{semantic::ArrayType{int_type, 2}});

    auto local = std::make_unique<hir::Local>();
    local->name = ast::Identifier{"arr"};
    local->is_mutable = true;
    local->type_annotation = hir::TypeAnnotation(array_type);
    hir::Local* local_ptr = local.get();

    hir::BindingDef binding;
    binding.local = local_ptr;
    auto pattern = std::make_unique<hir::Pattern>(hir::PatternVariant{std::move(binding)});

    hir::ArrayLiteral literal;
    literal.elements.push_back(make_int_literal_expr(1, int_type));
    literal.elements.push_back(make_int_literal_expr(2, int_type));
    auto literal_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(literal)});
    literal_expr->expr_info = make_value_info(array_type, false);

    hir::LetStmt let_stmt;
    let_stmt.pattern = std::move(pattern);
    let_stmt.type_annotation = hir::TypeAnnotation(array_type);
    let_stmt.initializer = std::move(literal_expr);
    auto let_stmt_node = std::make_unique<hir::Stmt>(hir::StmtVariant{std::move(let_stmt)});

    hir::Variable variable;
    variable.local_id = local_ptr;
    auto var_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(variable)});
    var_expr->expr_info = make_value_info(array_type, true);

    auto index_value = make_int_literal_expr(0, index_type);

    hir::Index index_expr;
    index_expr.base = std::move(var_expr);
    index_expr.index = std::move(index_value);
    auto lhs_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(index_expr)});
    lhs_expr->expr_info = make_value_info(int_type, true);

    hir::Assignment assignment;
    assignment.lhs = std::move(lhs_expr);
    assignment.rhs = make_int_literal_expr(9, int_type);
    auto assignment_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(assignment)});
    assignment_expr->expr_info = make_value_info(unit_type, false);

    hir::Function function;
    function.return_type = hir::TypeAnnotation(unit_type);
    function.locals.push_back(std::move(local));
    auto body = std::make_unique<hir::Block>();
    body->stmts.push_back(std::move(let_stmt_node));
    body->final_expr = std::move(assignment_expr);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    ASSERT_EQ(lowered.basic_blocks.size(), 1u);
    const auto& block = lowered.basic_blocks.front();
    const mir::AssignStatement* indexed_assign = nullptr;
    for (const auto& stmt : block.statements) {
        if (const auto* assign = std::get_if<mir::AssignStatement>(&stmt.value)) {
            if (!assign->dest.projections.empty()) {
                indexed_assign = assign;
                break;
            }
        }
    }
    ASSERT_NE(indexed_assign, nullptr);
    ASSERT_TRUE(std::holds_alternative<mir::LocalPlace>(indexed_assign->dest.base));
    EXPECT_EQ(std::get<mir::LocalPlace>(indexed_assign->dest.base).id, 0u);
    ASSERT_EQ(indexed_assign->dest.projections.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<mir::IndexProjection>(indexed_assign->dest.projections[0]));
}

TEST(MirLowerTest, LowersAssignmentToFieldPlace) {
    semantic::TypeId int_type = make_type(semantic::PrimitiveKind::I32);
    semantic::TypeId unit_type = make_unit_type();

    auto struct_item = std::make_unique<hir::Item>(hir::StructDef{});
    auto& struct_def = std::get<hir::StructDef>(struct_item->value);
    struct_def.fields.push_back(semantic::Field{.name = ast::Identifier{"a"}, .type = std::nullopt});
    struct_def.fields.push_back(semantic::Field{.name = ast::Identifier{"b"}, .type = std::nullopt});
    struct_def.field_type_annotations.push_back(hir::TypeAnnotation(int_type));
    struct_def.field_type_annotations.push_back(hir::TypeAnnotation(int_type));
    semantic::TypeId struct_type = semantic::get_typeID(semantic::Type{semantic::StructType{&struct_def}});

    auto local = std::make_unique<hir::Local>();
    local->name = ast::Identifier{"s"};
    local->is_mutable = true;
    local->type_annotation = hir::TypeAnnotation(struct_type);
    hir::Local* local_ptr = local.get();

    hir::BindingDef binding;
    binding.local = local_ptr;
    auto pattern = std::make_unique<hir::Pattern>(hir::PatternVariant{std::move(binding)});

    hir::StructLiteral literal;
    literal.struct_path = &struct_def;
    hir::StructLiteral::CanonicalFields canonical;
    canonical.initializers.push_back(make_int_literal_expr(1, int_type));
    canonical.initializers.push_back(make_int_literal_expr(2, int_type));
    literal.fields = std::move(canonical);
    auto literal_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(literal)});
    literal_expr->expr_info = make_value_info(struct_type, false);

    hir::LetStmt let_stmt;
    let_stmt.pattern = std::move(pattern);
    let_stmt.type_annotation = hir::TypeAnnotation(struct_type);
    let_stmt.initializer = std::move(literal_expr);
    auto let_stmt_node = std::make_unique<hir::Stmt>(hir::StmtVariant{std::move(let_stmt)});

    hir::Variable variable;
    variable.local_id = local_ptr;
    auto var_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(variable)});
    var_expr->expr_info = make_value_info(struct_type, true);

    hir::FieldAccess field_access;
    field_access.base = std::move(var_expr);
    field_access.field = static_cast<size_t>(0);
    auto lhs_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(field_access)});
    lhs_expr->expr_info = make_value_info(int_type, true);

    hir::Assignment assignment;
    assignment.lhs = std::move(lhs_expr);
    assignment.rhs = make_int_literal_expr(3, int_type);
    auto assignment_expr = std::make_unique<hir::Expr>(hir::ExprVariant{std::move(assignment)});
    assignment_expr->expr_info = make_value_info(unit_type, false);

    hir::Function function;
    function.return_type = hir::TypeAnnotation(unit_type);
    function.locals.push_back(std::move(local));
    auto body = std::make_unique<hir::Block>();
    body->stmts.push_back(std::move(let_stmt_node));
    body->final_expr = std::move(assignment_expr);
    function.body = std::move(body);

    mir::MirFunction lowered = mir::lower_function(function);
    const auto& block = lowered.basic_blocks.front();
    const mir::AssignStatement* field_assign = nullptr;
    for (const auto& stmt : block.statements) {
        if (const auto* assign = std::get_if<mir::AssignStatement>(&stmt.value)) {
            if (!assign->dest.projections.empty()) {
                field_assign = assign;
                break;
            }
        }
    }
    ASSERT_NE(field_assign, nullptr);
    ASSERT_TRUE(std::holds_alternative<mir::LocalPlace>(field_assign->dest.base));
    EXPECT_EQ(std::get<mir::LocalPlace>(field_assign->dest.base).id, 0u);
    ASSERT_EQ(field_assign->dest.projections.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<mir::FieldProjection>(field_assign->dest.projections[0]));
    EXPECT_EQ(std::get<mir::FieldProjection>(field_assign->dest.projections[0]).index, 0u);
}
