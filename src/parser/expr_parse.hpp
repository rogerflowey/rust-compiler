#pragma once

#include "../ast/common.hpp"
#include "../ast/expr.hpp"
#include "../lexer/lexer.hpp"
#include "../utils/helpers.hpp"
#include "common.hpp"
#include "utils.hpp"
#include <functional>
#include <memory>
#include <vector>

struct ParserRegistry;
using namespace parsec;

class ExprParserBuilder {
public:
    ExprParserBuilder() = default;

    void finalize(
        const ParserRegistry& registry,
        std::function<void(ExprParser)> set_parser,
        std::function<void(ExprParser)> set_with_block_parser,
        std::function<void(ExprParser)> set_literal_parser
    ) {
        // Pull dependencies
        const auto& pathParser = registry.path;
        const auto& typeParser = registry.type;
        const auto& stmtParser = registry.stmt;
        const auto& selfParser = registry.expr; // For recursion

        // Build sub-parsers by calling helper methods
        auto literalParser = buildLiteralParser();
        auto groupedParser = buildGroupedParser(selfParser);
        auto arrayParser = buildArrayParser(selfParser);
        auto pathExprParser = buildPathExprParser(pathParser);
        auto blockParser = buildBlockParser(stmtParser, selfParser);
        // NEW: Build the struct expression parser
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

        // Set all the lazy parsers this builder is responsible for
        set_parser(finalParser);
        set_with_block_parser(withBlockParser);
        set_literal_parser(literalParser);
    }

private:
    ExprParser buildLiteralParser() const {
        auto p_string = satisfy<Token>([](const Token& t) { return t.type == TOKEN_STRING; }, "string literal")
            .map([](Token t) -> ExprPtr {
                return std::make_unique<StringLiteralExpr>(t.value);
            });
        auto p_char = satisfy<Token>([](const Token& t) { return t.type == TOKEN_CHAR; }, "char literal")
            .map([](Token t) -> ExprPtr { return std::make_unique<CharLiteralExpr>(t.value[0]); });
        auto p_bool = satisfy<Token>([](const Token& t) { return t.type == TOKEN_KEYWORD && (t.value == "true" || t.value == "false"); }, "boolean literal")
            .map([](Token t) -> ExprPtr { return std::make_unique<BoolLiteralExpr>(t.value == "true"); });
        auto p_number = satisfy<Token>([](const Token& t) ->bool {
            return t.type == TOKEN_NUMBER;
        }, "number literal").map([](Token t) -> ExprPtr {
            std::string num_part;
            std::string suffix;
            size_t i = 0;
            while (i < t.value.length() && std::isdigit(t.value[i])) {
                num_part += t.value[i];
                i++;
            }
            suffix = t.value.substr(i);

            auto value = std::stoll(num_part);
            if (suffix == "u32") {
                return std::make_unique<IntegerLiteralExpr>(value, IntegerLiteralExpr::U32);
            } else if (suffix == "isize") {
                return std::make_unique<IntegerLiteralExpr>(value, IntegerLiteralExpr::ISIZE);
            } else if (suffix == "usize") {
                return std::make_unique<IntegerLiteralExpr>(value, IntegerLiteralExpr::USIZE);
            } else { // i32 or no suffix
                return std::make_unique<IntegerLiteralExpr>(value, IntegerLiteralExpr::I32);
            }
        });
        return (p_string | p_char | p_bool | p_number).label("a literal expression");
    }

    ExprParser buildGroupedParser(const ExprParser& self) const {
        return (equal({TOKEN_DELIMITER, "("}) > self < equal({TOKEN_DELIMITER, ")"}))
            .map([](ExprPtr&& inner) -> ExprPtr { return std::make_unique<GroupedExpr>(std::move(inner)); }).label("a grouped expression");
    }

    ExprParser buildArrayParser(const ExprParser& self) const {
        auto p_list = self.list(equal({TOKEN_SEPARATOR, ","}))
            .map([](std::vector<ExprPtr>&& elems) -> ExprPtr { return std::make_unique<ArrayInitExpr>(std::move(elems)); });
        auto p_repeat = (self < equal({TOKEN_SEPARATOR, ";"}))
            .andThen(self)
            .map([](auto&& pair) -> ExprPtr { return std::make_unique<ArrayRepeatExpr>(std::move(std::get<0>(pair)), std::move(std::get<1>(pair))); });
        return (equal({TOKEN_DELIMITER, "["}) > (p_repeat | p_list).optional() < equal({TOKEN_DELIMITER, "]"}))
            .map([](std::optional<ExprPtr>&& maybe_elements) -> ExprPtr {
                return maybe_elements ? std::move(*maybe_elements) : std::make_unique<ArrayInitExpr>(std::vector<ExprPtr>{});
            }).label("an array expression");
    }

    ExprParser buildPathExprParser(const PathParser& pathParser) const {
        return pathParser.map([](PathPtr&& p) -> ExprPtr {
            return std::make_unique<PathExpr>(std::move(p));
        }).label("a path expression");
    }

    // NEW: Parser for struct expressions like `MyStruct { field: value }`
    ExprParser buildStructExprParser(const PathParser& pathParser, const ExprParser& self) const {
        // Parses a single field initializer: `field: value`
        auto p_field_init = p_identifier
            .andThen(equal({TOKEN_SEPARATOR, ":"}) > self)
            .map([](auto&& pair) {
                return StructExpr::FieldInit{std::move(std::get<0>(pair)), std::move(std::get<1>(pair))};
            });

        // Parses the braced list of fields: `{ field1: val1, field2: val2 }`
        auto p_fields_block =
            equal({TOKEN_DELIMITER, "{"}) >
            p_field_init.tuple(equal({TOKEN_SEPARATOR, ","})).optional() <
            equal({TOKEN_DELIMITER, "}"});

        // Parses the full struct expression: `Path { ... }`
        return pathParser.andThen(p_fields_block)
            .map([](auto&& pair) -> ExprPtr {
                auto path = std::move(std::get<0>(pair));
                auto maybe_fields = std::move(std::get<1>(pair));
                std::vector<StructExpr::FieldInit> fields;
                if (maybe_fields) {
                    fields = std::move(*maybe_fields);
                }
                return std::make_unique<StructExpr>(std::move(path), std::move(fields));
            }).label("a struct expression");
    }

    ExprParser buildBlockParser(const StmtParser& stmtParser, const ExprParser& self) const {
        return (equal({TOKEN_DELIMITER, "{"}) > stmtParser.many().andThen(self.optional()) < equal({TOKEN_DELIMITER, "}"}))
            .map([](auto&& pair) -> ExprPtr {
                return std::make_unique<BlockExpr>(std::move(std::get<0>(pair)), std::move(std::get<1>(pair)));
            }).label("a block expression");
    }

    std::tuple<ExprParser, ExprParser, ExprParser> buildControlFlowParsers(const ExprParser& self, const ExprParser& blockParser) const {
        auto [p_if_lazy, set_if_lazy] = lazy<ExprPtr, Token>();
        auto p_else_branch = (equal({TOKEN_KEYWORD, "else"}) > (blockParser | p_if_lazy)).optional();
        auto p_if_core = (equal({TOKEN_KEYWORD, "if"}) > self).andThen(blockParser).andThen(p_else_branch)
            .map([](auto&& t) -> ExprPtr {
                auto& cond = std::get<0>(t);
                auto& then_b = std::get<1>(t);
                auto& else_b = std::get<2>(t);
                return std::make_unique<IfExpr>(std::move(cond),
                    std::unique_ptr<BlockExpr>(static_cast<BlockExpr*>(then_b.release())),
                    std::move(else_b));
            }).label("an if expression");
        set_if_lazy(p_if_core);

        auto whileExprParser = (equal({TOKEN_KEYWORD, "while"}) > self).andThen(blockParser)
            .map([](auto&& t) -> ExprPtr {
                auto& [cond, body] = t;
                return std::make_unique<WhileExpr>(std::move(cond),
                    std::unique_ptr<BlockExpr>(static_cast<BlockExpr*>(body.release())));
            }).label("a while expression");

        auto loopExprParser = (equal({TOKEN_KEYWORD, "loop"}) > blockParser)
            .map([](ExprPtr&& body) -> ExprPtr {
                return std::make_unique<LoopExpr>(
                    std::unique_ptr<BlockExpr>(static_cast<BlockExpr*>(body.release())));
            }).label("a loop expression");
        return { p_if_lazy, whileExprParser, loopExprParser };
    }

    std::tuple<ExprParser, ExprParser, ExprParser> buildFlowTerminators(const ExprParser& self) const {
        auto returnExprParser = (equal({TOKEN_KEYWORD, "return"}) > self.optional())
            .map([](auto&& v) -> ExprPtr { return std::make_unique<ReturnExpr>(std::move(v)); }).label("a return expression");
        auto breakExprParser = (equal({TOKEN_KEYWORD, "break"}) > p_identifier.optional().andThen(self.optional()))
            .map([](auto&& t) -> ExprPtr { return std::make_unique<BreakExpr>(std::move(std::get<0>(t)), std::move(std::get<1>(t))); }).label("a break expression");
        auto continueExprParser = equal({TOKEN_KEYWORD, "continue"})
            .map([](Token) -> ExprPtr { return std::make_unique<ContinueExpr>(); }).label("a continue expression");
        return { returnExprParser, breakExprParser, continueExprParser };
    }

    ExprParser buildPostfixChainParser(const ExprParser& base, const ExprParser& self) const {
        struct PostfixOp {
            enum Kind { Call, Index, Field } kind;
            std::vector<ExprPtr> args{};
            ExprPtr index = nullptr;
            IdPtr ident = nullptr;
        };
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
                        expr = std::make_unique<MethodCallExpr>(std::move(expr), std::move(op.ident), std::move(ops[++i].args));
                    } else if (op.kind == PostfixOp::Call) {
                        expr = std::make_unique<CallExpr>(std::move(expr), std::move(op.args));
                    } else if (op.kind == PostfixOp::Index) {
                        expr = std::make_unique<IndexExpr>(std::move(expr), std::move(op.index));
                    } else if (op.kind == PostfixOp::Field) {
                        expr = std::make_unique<FieldAccessExpr>(std::move(expr), std::move(op.ident));
                    }
                }
                return expr;
            }).label("a postfix expression");
    }

    ExprParser buildPrefixAndCastChain(
        const ExprParser& literal, const ExprParser& grouped, const ExprParser& array, const ExprParser& structExpr, const ExprParser& path,
        const ExprParser& withBlock, const ExprParser& ret, const ExprParser& brk, const ExprParser& cont,
        const ExprParser& self, const TypeParser& typeParser
    ) const {
        // NEW: Added structExpr to the list of base atoms.
        // The order is important: structExpr must come before path to resolve ambiguity.
        auto p_base_atoms = (literal | grouped | array | structExpr | path | withBlock | ret | brk | cont).label("an atomic expression");
        auto p_postfix = buildPostfixChainParser(p_base_atoms, self);
        using Wrap = std::function<ExprPtr(ExprPtr)>;
        auto p_not = equal({TOKEN_OPERATOR, "!"}).map([](Token) -> Wrap { return [](ExprPtr e) { return std::make_unique<UnaryExpr>(UnaryExpr::NOT, std::move(e)); }; });
        auto p_neg = equal({TOKEN_OPERATOR, "-"}).map([](Token) -> Wrap { return [](ExprPtr e) { return std::make_unique<UnaryExpr>(UnaryExpr::NEGATE, std::move(e)); }; });
        auto p_deref = equal({TOKEN_OPERATOR, "*"}).map([](Token) -> Wrap { return [](ExprPtr e) { return std::make_unique<UnaryExpr>(UnaryExpr::DEREFERENCE, std::move(e)); }; });
        auto p_ref = (equal({TOKEN_OPERATOR, "&"}) >> equal({TOKEN_KEYWORD, "mut"}).optional())
            .map([](auto&& t) -> Wrap {
                bool is_mut = std::get<1>(t).has_value();
                return [is_mut](ExprPtr e) { return std::make_unique<UnaryExpr>(is_mut ? UnaryExpr::MUTABLE_REFERENCE : UnaryExpr::REFERENCE, std::move(e)); };
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
                for (auto& ty : std::get<1>(t)) { expr = std::make_unique<CastExpr>(std::move(expr), std::move(ty)); }
                return expr;
            }).label("a cast expression");
    }

    void addInfixOperators(parsec::PrattParserBuilder<ExprPtr, Token>& builder) const {
        auto bin = [](BinaryExpr::Op op) { return [op](ExprPtr lhs, ExprPtr rhs) -> ExprPtr { return std::make_unique<BinaryExpr>(std::move(lhs), op, std::move(rhs)); }; };
        auto assign = [](AssignExpr::Op op) { return [op](ExprPtr lhs, ExprPtr rhs) -> ExprPtr { return std::make_unique<AssignExpr>(std::move(lhs), op, std::move(rhs)); }; };
        builder.addInfixLeft({TOKEN_OPERATOR, "*"}, 60, bin(BinaryExpr::MUL));
        builder.addInfixLeft({TOKEN_OPERATOR, "/"}, 60, bin(BinaryExpr::DIV));
        builder.addInfixLeft({TOKEN_OPERATOR, "%"}, 60, bin(BinaryExpr::REM));
        builder.addInfixLeft({TOKEN_OPERATOR, "+"}, 50, bin(BinaryExpr::ADD));
        builder.addInfixLeft({TOKEN_OPERATOR, "-"}, 50, bin(BinaryExpr::SUB));
        builder.addInfixLeft({TOKEN_OPERATOR, "&"}, 45, bin(BinaryExpr::BIT_AND));
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
    }
};