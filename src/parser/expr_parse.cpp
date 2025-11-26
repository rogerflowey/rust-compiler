#include "expr_parse.hpp"

#include "src/ast/stmt.hpp"
#include "src/lexer/lexer.hpp"
#include "src/utils/helpers.hpp"
#include "parser_registry.hpp" // For the full ParserRegistry definition
#include "utils.hpp"
#include <cctype>
#include <stdexcept>

using namespace parsec;
using namespace ast;

namespace {

struct ParsedIntegerLiteral {
    int64_t value;
    ast::IntegerLiteralExpr::Type type;
};

template <typename T>
ExprPtr annotate_expr(ExprPtr expr, const span::Span &sp) {
    expr->span = sp;
    std::get<T>(expr->value).span = sp;
    return expr;
}

bool isValidDigitForBase(char c, int base) {
    unsigned char uc = static_cast<unsigned char>(c);
    switch (base) {
        case 2:  return uc == '0' || uc == '1';
        case 8:  return uc >= '0' && uc <= '7';
        case 10: return std::isdigit(uc) != 0;
        case 16: return std::isxdigit(uc) != 0;
        default: return false;
    }
}

ParsedIntegerLiteral parseIntegerLiteral(const std::string& literal) {
    using Type = ast::IntegerLiteralExpr::Type;

    if (literal.empty()) {
        return {0, Type::NOT_SPECIFIED};
    }

    int base = 10;
    std::size_t digits_start = 0;
    if (literal.size() > 2 && literal[0] == '0') {
        char prefix = static_cast<char>(std::tolower(static_cast<unsigned char>(literal[1])));
        if (prefix == 'x') {
            base = 16;
            digits_start = 2;
        } else if (prefix == 'b') {
            base = 2;
            digits_start = 2;
        } else if (prefix == 'o') {
            base = 8;
            digits_start = 2;
        }
    }

    std::size_t pos = digits_start;
    std::string digits;
    digits.reserve(literal.size() - digits_start);
    for (; pos < literal.size(); ++pos) {
        char c = literal[pos];
        if (c == '_') {
            continue;
        }
        if (!isValidDigitForBase(c, base)) {
            break;
        }
        digits.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    std::string suffix = literal.substr(pos);

    if (digits.empty()) {
        return {0, Type::NOT_SPECIFIED};
    }

    int64_t value = std::stoll(digits, nullptr, base);

    Type type = Type::NOT_SPECIFIED;
    if (suffix == "i32") type = Type::I32;
    else if (suffix == "u32") type = Type::U32;
    else if (suffix == "isize") type = Type::ISIZE;
    else if (suffix == "usize") type = Type::USIZE;

    return {value, type};
}

} // namespace

void ExprParserBuilder::finalize(
    const ParserRegistry& registry,
    std::function<void(ExprParser)> set_parser,
    std::function<void(ExprParser)> set_with_block_parser,
    std::function<void(ExprParser)> set_literal_parser
) {
    const auto& pathParser = registry.path;
    const auto& typeParser = registry.type;
    const auto& stmtParser = registry.stmt;
    const auto& selfParser = registry.expr;

    auto literalParser = buildLiteralParser();
    auto groupedParser = buildGroupedParser(selfParser);
    auto arrayParser = buildArrayParser(selfParser);
    auto pathExprParser = buildPathExprParser(pathParser);
    auto blockParser = buildBlockParser(stmtParser, selfParser);
    auto structExprParser = buildStructExprParser(pathParser, selfParser);

    auto [ifExprParser, whileExprParser, loopExprParser] = buildControlFlowParsers(selfParser, blockParser);
    auto withBlockParser = blockParser | ifExprParser | whileExprParser | loopExprParser;

    auto [returnExprParser, breakExprParser, continueExprParser] = buildFlowTerminators(selfParser);

    auto prefixAndCastParser = buildPrefixAndCastChain(
        literalParser, groupedParser, arrayParser, structExprParser, pathExprParser, withBlockParser,
        returnExprParser, breakExprParser, continueExprParser,
        selfParser, typeParser
    );

    auto prattBuilder = parsec::PrattParserBuilder<ExprPtr, Token>();
    addInfixOperators(prattBuilder);
    prattBuilder.withAtomParser(prefixAndCastParser);
    auto finalParser = prattBuilder.build();

    set_parser(finalParser);
    set_with_block_parser(withBlockParser);
    set_literal_parser(literalParser);
}

ExprParser ExprParserBuilder::buildLiteralParser() const {
    auto p_string = satisfy<Token>([](const Token& t) { return t.type == TOKEN_STRING; }, "string literal")
        .map([](Token t) -> ExprPtr {
            auto expr = make_expr<StringLiteralExpr>(t.value);
            return annotate_expr<StringLiteralExpr>(std::move(expr), t.span);
        });
    auto p_char = satisfy<Token>([](const Token& t) { return t.type == TOKEN_CHAR; }, "char literal")
        .map([](Token t) -> ExprPtr {
            auto expr = make_expr<CharLiteralExpr>(t.value[0]);
            return annotate_expr<CharLiteralExpr>(std::move(expr), t.span);
        });
    auto p_bool = satisfy<Token>([](const Token& t) { return t.type == TOKEN_KEYWORD && (t.value == "true" || t.value == "false"); }, "boolean literal")
        .map([](Token t) -> ExprPtr {
            auto expr = make_expr<BoolLiteralExpr>(t.value == "true");
            return annotate_expr<BoolLiteralExpr>(std::move(expr), t.span);
        });
    auto p_number = satisfy<Token>([](const Token& t) -> bool { return t.type == TOKEN_NUMBER; }, "number literal")
        .map([](Token t) -> ExprPtr {
            auto parsed = parseIntegerLiteral(t.value);
            auto expr = make_expr<IntegerLiteralExpr>(parsed.value, parsed.type);
            return annotate_expr<IntegerLiteralExpr>(std::move(expr), t.span);
        });
    return (p_string | p_char | p_bool | p_number).label("a literal expression");
}

ExprParser ExprParserBuilder::buildGroupedParser(const ExprParser& self) const {
    return (equal({TOKEN_DELIMITER, "("}) > self < equal({TOKEN_DELIMITER, ")"}))
        .map([](ExprPtr&& inner) -> ExprPtr {
            auto expr = make_expr<GroupedExpr>(std::move(inner));
            auto span = std::get<GroupedExpr>(expr->value).expr ? std::get<GroupedExpr>(expr->value).expr->span : span::Span::invalid();
            return annotate_expr<GroupedExpr>(std::move(expr), span);
        }).label("a grouped expression");
}

ExprParser ExprParserBuilder::buildArrayParser(const ExprParser& self) const {
    auto p_list = self.tuple(equal({TOKEN_SEPARATOR, ","}))
        .map([](std::vector<ExprPtr>&& elems) -> ExprPtr {
            auto expr = make_expr<ArrayInitExpr>(std::move(elems));
            std::vector<span::Span> spans;
            for (const auto &e : std::get<ArrayInitExpr>(expr->value).elements) {
                if (e) spans.push_back(e->span);
            }
            auto merged = merge_span_list(spans);
            return annotate_expr<ArrayInitExpr>(std::move(expr), merged);
        });
    auto p_repeat = (self < equal({TOKEN_SEPARATOR, ";"}))
        .andThen(self)
        .map([](auto&& pair) -> ExprPtr {
            auto expr = make_expr<ArrayRepeatExpr>(std::move(std::get<0>(pair)), std::move(std::get<1>(pair)));
            auto &node = std::get<ArrayRepeatExpr>(expr->value);
            auto merged = merge_span_pair(node.value ? node.value->span : span::Span::invalid(), node.count ? node.count->span : span::Span::invalid());
            return annotate_expr<ArrayRepeatExpr>(std::move(expr), merged);
        });
    auto p_body = (p_repeat | p_list).optional();
    return (equal({TOKEN_DELIMITER, "["}) > p_body < equal({TOKEN_DELIMITER, "]"}))
        .map([](std::optional<ExprPtr>&& maybe_elements) -> ExprPtr {
            if (maybe_elements) return std::move(*maybe_elements);
            auto expr = make_expr<ArrayInitExpr>(std::vector<ExprPtr>{});
            return annotate_expr<ArrayInitExpr>(std::move(expr), span::Span::invalid());
        }).label("an array expression");
}

ExprParser ExprParserBuilder::buildPathExprParser(const PathParser& pathParser) const {
    return pathParser.map([](PathPtr&& p) -> ExprPtr {
        if (p->segments.size() == 1 && p->segments[0].id && (*p->segments[0].id)->name == "_") {
            auto expr = make_expr<UnderscoreExpr>();
            return annotate_expr<UnderscoreExpr>(std::move(expr), p->span);
        }
        auto expr = make_expr<PathExpr>(std::move(p));
        auto span = std::get<PathExpr>(expr->value).path ? std::get<PathExpr>(expr->value).path->span : span::Span::invalid();
        return annotate_expr<PathExpr>(std::move(expr), span);
    }).label("a path expression");
}

ExprParser ExprParserBuilder::buildStructExprParser(const PathParser& pathParser, const ExprParser& self) const {
    auto p_field_init = p_identifier.andThen(equal({TOKEN_SEPARATOR, ":"}) > self)
        .map([](auto&& pair) {
            StructExpr::FieldInit init{std::move(std::get<0>(pair)), std::move(std::get<1>(pair))};
            span::Span merged = span::Span::invalid();
            if (init.name) merged = span::Span::merge(merged, init.name->span);
            if (init.value) merged = span::Span::merge(merged, init.value->span);
            init.span = merged;
            return init;
        });
    auto p_fields_block = equal({TOKEN_DELIMITER, "{"}) > p_field_init.tuple(equal({TOKEN_SEPARATOR, ","})).optional() < equal({TOKEN_DELIMITER, "}"});
    return pathParser.andThen(p_fields_block)
        .map([](auto&& pair) -> ExprPtr {
            auto path = std::move(std::get<0>(pair));
            auto maybe_fields = std::move(std::get<1>(pair));
            std::vector<StructExpr::FieldInit> fields;
            if (maybe_fields) fields = std::move(*maybe_fields);
            auto expr = make_expr<StructExpr>(std::move(path), std::move(fields));
            auto &node = std::get<StructExpr>(expr->value);
            std::vector<span::Span> spans;
            if (node.path) spans.push_back(node.path->span);
            for (const auto &f : node.fields) spans.push_back(f.span);
            auto merged = merge_span_list(spans);
            return annotate_expr<StructExpr>(std::move(expr), merged);
        }).label("a struct expression");
}

ExprParser ExprParserBuilder::buildBlockParser(const StmtParser& stmtParser, const ExprParser& self) const {
    return (equal({TOKEN_DELIMITER, "{"}) > stmtParser.many().andThen(self.optional()) < equal({TOKEN_DELIMITER, "}"}))
        .map([](auto&& pair) -> ExprPtr {
            auto statements = std::move(std::get<0>(pair));
            auto final_expr = std::move(std::get<1>(pair));

            if (!final_expr && !statements.empty()) {
                auto& last_stmt_ptr = statements.back();
                if (last_stmt_ptr) {
                    if (auto* expr_stmt = std::get_if<ExprStmt>(&last_stmt_ptr->value);
                        expr_stmt && !expr_stmt->has_trailing_semicolon && expr_stmt->expr) {
                        const auto& variant = expr_stmt->expr->value;
                        const bool is_with_block_expr =
                            std::holds_alternative<BlockExpr>(variant) ||
                            std::holds_alternative<IfExpr>(variant) ||
                            std::holds_alternative<WhileExpr>(variant) ||
                            std::holds_alternative<LoopExpr>(variant);
                        if (is_with_block_expr) {
                            final_expr = std::move(expr_stmt->expr);
                            statements.pop_back();
                        }
                    }
                }
            }

            auto expr = make_expr<BlockExpr>(std::move(statements), std::move(final_expr));
            auto &node = std::get<BlockExpr>(expr->value);
            std::vector<span::Span> spans;
            for (const auto &stmt : node.statements) {
                if (stmt) spans.push_back(stmt->span);
            }
            if (node.final_expr && *node.final_expr) spans.push_back((*node.final_expr)->span);
            auto merged = merge_span_list(spans);
            return annotate_expr<BlockExpr>(std::move(expr), merged);
        }).label("a block expression");
}

std::tuple<ExprParser, ExprParser, ExprParser> ExprParserBuilder::buildControlFlowParsers(const ExprParser& self, const ExprParser& blockParser) const {
    auto [p_if_lazy, set_if_lazy] = lazy<ExprPtr, Token>();
    auto p_else_branch = (equal({TOKEN_KEYWORD, "else"}) > (blockParser | p_if_lazy)).optional();
    auto p_if_core = (equal({TOKEN_KEYWORD, "if"}) > equal({TOKEN_DELIMITER, "("}) > self < equal({TOKEN_DELIMITER, ")"}) ).andThen(blockParser).andThen(p_else_branch)
        .map([](auto&& t) -> ExprPtr {
            auto& [cond, then_b, else_b] = t;
            auto then_ptr = std::make_unique<BlockExpr>(std::get<BlockExpr>(std::move(then_b->value)));
            return make_expr<IfExpr>(std::move(cond), std::move(then_ptr), std::move(else_b));
        }).label("an if expression");
    set_if_lazy(p_if_core);

    auto whileExprParser = (equal({TOKEN_KEYWORD, "while"}) > self).andThen(blockParser)
        .map([](auto&& t) -> ExprPtr {
            auto& [cond, body] = t;
            auto body_ptr = std::make_unique<BlockExpr>(std::get<BlockExpr>(std::move(body->value)));
            return make_expr<WhileExpr>(std::move(cond), std::move(body_ptr));
        }).label("a while expression");

    auto loopExprParser = (equal({TOKEN_KEYWORD, "loop"}) > blockParser)
        .map([](ExprPtr&& body) -> ExprPtr {
            auto body_ptr = std::make_unique<BlockExpr>(std::get<BlockExpr>(std::move(body->value)));
            return make_expr<LoopExpr>(std::move(body_ptr));
        }).label("a loop expression");
    return { p_if_lazy, whileExprParser, loopExprParser };
}

std::tuple<ExprParser, ExprParser, ExprParser> ExprParserBuilder::buildFlowTerminators(const ExprParser& self) const {
    auto p_label = (equal({TOKEN_OPERATOR, "'"}) > p_identifier).label("a label");
    auto returnExprParser = (equal({TOKEN_KEYWORD, "return"}) > self.optional())
        .map([](auto&& v) -> ExprPtr { return make_expr<ReturnExpr>(std::move(v)); }).label("a return expression");
    auto breakExprParser = (equal({TOKEN_KEYWORD, "break"}) > p_label.optional().andThen(self.optional()))
        .map([](auto&& t) -> ExprPtr { return make_expr<BreakExpr>(std::move(std::get<0>(t)), std::move(std::get<1>(t))); }).label("a break expression");
    auto continueExprParser = (equal({TOKEN_KEYWORD, "continue"}) > p_label.optional())
        .map([](auto&& label) -> ExprPtr { return make_expr<ContinueExpr>(std::move(label)); }).label("a continue expression");
    return { returnExprParser, breakExprParser, continueExprParser };
}

ExprParser ExprParserBuilder::buildPostfixChainParser(const ExprParser& base, const ExprParser& self) const {
    struct PostfixOp { enum Kind { Call, Index, Field } kind; std::vector<ExprPtr> args{}; ExprPtr index = nullptr; IdPtr ident = nullptr; };
    auto p_args = (equal({TOKEN_DELIMITER, "("}) > self.tuple(equal({TOKEN_SEPARATOR, ","})).optional() < equal({TOKEN_DELIMITER, ")"}))
        .map([](auto&& maybe) -> std::vector<ExprPtr> { return maybe ? std::move(*maybe) : std::vector<ExprPtr>{}; });
    auto p_call = p_args.map([](auto&& args) { return PostfixOp{PostfixOp::Call, std::move(args)}; }).label("a function call");
    auto p_index = (equal({TOKEN_DELIMITER, "["}) > self < equal({TOKEN_DELIMITER, "]"}))
        .map([](auto&& idx) { return PostfixOp{PostfixOp::Index, {}, std::move(idx)}; }).label("an index expression");
    auto p_field = (equal({TOKEN_OPERATOR, "."}) > p_identifier)
        .map([](auto&& id) { return PostfixOp{PostfixOp::Field, {}, nullptr, std::move(id)}; }).label("a field access");
    auto p_postfix_op = p_call | p_index | p_field;

    return base.andThen(p_postfix_op.many())
        .map([&](auto&& tup) -> ExprPtr {
            auto expr = std::move(std::get<0>(tup));
            auto& ops = std::get<1>(tup);
            for (size_t i = 0; i < ops.size(); ++i) {
                auto& op = ops[i];
                if (op.kind == PostfixOp::Field && i + 1 < ops.size() && ops[i + 1].kind == PostfixOp::Call) {
                    expr = make_expr<MethodCallExpr>(std::move(expr), std::move(op.ident), std::move(ops[++i].args));
                } else if (op.kind == PostfixOp::Call) {
                    expr = make_expr<CallExpr>(std::move(expr), std::move(op.args));
                } else if (op.kind == PostfixOp::Index) {
                    expr = make_expr<IndexExpr>(std::move(expr), std::move(op.index));
                } else if (op.kind == PostfixOp::Field) {
                    expr = make_expr<FieldAccessExpr>(std::move(expr), std::move(op.ident));
                }
            }
            return expr;
        }).label("a postfix expression");
}

ExprParser ExprParserBuilder::buildPrefixAndCastChain(
    const ExprParser& literal, const ExprParser& grouped, const ExprParser& array, const ExprParser& structExpr, const ExprParser& path,
    const ExprParser& withBlock, const ExprParser& ret, const ExprParser& brk, const ExprParser& cont,
    const ExprParser& self, const TypeParser& typeParser
) const {
    auto p_base_atoms = (literal | grouped | array | structExpr | path | withBlock | ret | brk | cont).label("an atomic expression");
    auto p_postfix = buildPostfixChainParser(p_base_atoms, self);
    using Wrap = std::function<ExprPtr(ExprPtr)>;
    auto p_not = equal({TOKEN_OPERATOR, "!"}).map([](Token) -> Wrap { return [](ExprPtr e) { return make_expr<UnaryExpr>(UnaryExpr::NOT, std::move(e)); }; });
    auto p_neg = equal({TOKEN_OPERATOR, "-"}).map([](Token) -> Wrap { return [](ExprPtr e) { return make_expr<UnaryExpr>(UnaryExpr::NEGATE, std::move(e)); }; });
    auto p_deref = equal({TOKEN_OPERATOR, "*"}).map([](Token) -> Wrap { return [](ExprPtr e) { return make_expr<UnaryExpr>(UnaryExpr::DEREFERENCE, std::move(e)); }; });
    auto p_ref = (equal({TOKEN_OPERATOR, "&"}) >> equal({TOKEN_KEYWORD, "mut"}).optional())
        .map([](auto&& t) -> Wrap {
            bool is_mut = std::get<1>(t).has_value();
            return [is_mut](ExprPtr e) { return make_expr<UnaryExpr>(is_mut ? UnaryExpr::MUTABLE_REFERENCE : UnaryExpr::REFERENCE, std::move(e)); };
        });
    auto p_unary_op = (p_not | p_neg | p_deref | p_ref).label("a unary operator");
    auto p_unary = p_unary_op.many().andThen(p_postfix)
        .map([](auto&& t) -> ExprPtr {
            auto wraps = std::move(std::get<0>(t));
            auto expr = std::move(std::get<1>(t));
            for (auto it = wraps.rbegin(); it != wraps.rend(); ++it) { expr = (*it)(std::move(expr)); }
            return expr;
        });
    return p_unary.andThen((equal({TOKEN_KEYWORD, "as"}) > typeParser).many())
        .map([](auto&& t) -> ExprPtr {
            auto expr = std::move(std::get<0>(t));
            for (auto& ty : std::get<1>(t)) { expr = make_expr<CastExpr>(std::move(expr), std::move(ty)); }
            return expr;
        }).label("a cast expression");
}

void ExprParserBuilder::addInfixOperators(parsec::PrattParserBuilder<ExprPtr, Token>& builder) const {
    auto bin = [](BinaryExpr::Op op) {
        return [op](ExprPtr lhs, ExprPtr rhs) -> ExprPtr {
            return make_expr<BinaryExpr>(op, std::move(lhs), std::move(rhs));
        };
    };
    auto assign = [](AssignExpr::Op op) {
        return [op](ExprPtr lhs, ExprPtr rhs) -> ExprPtr {
            return make_expr<AssignExpr>(op, std::move(lhs), std::move(rhs));
        };
    };

    builder.addInfixLeft({TOKEN_OPERATOR, "*"}, 60, bin(BinaryExpr::MUL));
    builder.addInfixLeft({TOKEN_OPERATOR, "/"}, 60, bin(BinaryExpr::DIV));
    builder.addInfixLeft({TOKEN_OPERATOR, "%"}, 60, bin(BinaryExpr::REM));
    builder.addInfixLeft({TOKEN_OPERATOR, "+"}, 50, bin(BinaryExpr::ADD));
    builder.addInfixLeft({TOKEN_OPERATOR, "-"}, 50, bin(BinaryExpr::SUB));
    builder.addInfixLeft({TOKEN_OPERATOR, "<<"}, 48, bin(BinaryExpr::SHL));
    builder.addInfixLeft({TOKEN_OPERATOR, ">>"}, 48, bin(BinaryExpr::SHR));
    builder.addInfixLeft({TOKEN_OPERATOR, "&"}, 45, bin(BinaryExpr::BIT_AND));
    builder.addInfixLeft({TOKEN_OPERATOR, "^"}, 42, bin(BinaryExpr::BIT_XOR));
    builder.addInfixLeft({TOKEN_OPERATOR, "|"}, 41, bin(BinaryExpr::BIT_OR));
    builder.addInfixLeft({TOKEN_OPERATOR, "=="}, 40, bin(BinaryExpr::EQ));
    builder.addInfixLeft({TOKEN_OPERATOR, "!="}, 40, bin(BinaryExpr::NE));
    builder.addInfixLeft({TOKEN_OPERATOR, "<"}, 40, bin(BinaryExpr::LT));
    builder.addInfixLeft({TOKEN_OPERATOR, ">"}, 40, bin(BinaryExpr::GT));
    builder.addInfixLeft({TOKEN_OPERATOR, "<="}, 40, bin(BinaryExpr::LE));
    builder.addInfixLeft({TOKEN_OPERATOR, ">="}, 40, bin(BinaryExpr::GE));
    builder.addInfixLeft({TOKEN_OPERATOR, "&&"}, 30, bin(BinaryExpr::AND));
    builder.addInfixLeft({TOKEN_OPERATOR, "||"}, 20, bin(BinaryExpr::OR));
    builder.addInfixRight({TOKEN_OPERATOR, "="}, 10, assign(AssignExpr::ASSIGN));
    builder.addInfixRight({TOKEN_OPERATOR, "+="}, 10, assign(AssignExpr::ADD_ASSIGN));
    builder.addInfixRight({TOKEN_OPERATOR, "-="}, 10, assign(AssignExpr::SUB_ASSIGN));
    builder.addInfixRight({TOKEN_OPERATOR, "*="}, 10, assign(AssignExpr::MUL_ASSIGN));
    builder.addInfixRight({TOKEN_OPERATOR, "/="}, 10, assign(AssignExpr::DIV_ASSIGN));
    builder.addInfixRight({TOKEN_OPERATOR, "%="}, 10, assign(AssignExpr::REM_ASSIGN));
    builder.addInfixRight({TOKEN_OPERATOR, "&="}, 10, assign(AssignExpr::BIT_AND_ASSIGN));
    builder.addInfixRight({TOKEN_OPERATOR, "|="}, 10, assign(AssignExpr::BIT_OR_ASSIGN));
    builder.addInfixRight({TOKEN_OPERATOR, "^="}, 10, assign(AssignExpr::XOR_ASSIGN));
    builder.addInfixRight({TOKEN_OPERATOR, "<<="}, 10, assign(AssignExpr::SHL_ASSIGN));
    builder.addInfixRight({TOKEN_OPERATOR, ">>="}, 10, assign(AssignExpr::SHR_ASSIGN));
}
