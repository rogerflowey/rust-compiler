#include "handwritten_parse.hpp"

#include "../ast/expr.hpp"
#include "../ast/item.hpp"
#include "../ast/pattern.hpp"
#include "../ast/stmt.hpp"
#include "../ast/type.hpp"
#include "utils.hpp"

#include <cctype>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

using namespace ast;

namespace {

struct ParsedIntegerLiteral {
    int64_t value;
    ast::IntegerLiteralExpr::Type type;
};

bool isValidDigitForBase(char c, int base) {
    unsigned char uc = static_cast<unsigned char>(c);
    switch (base) {
        case 2: return uc == '0' || uc == '1';
        case 8: return uc >= '0' && uc <= '7';
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

    if (digits.empty()) {
        return {0, Type::NOT_SPECIFIED};
    }

    auto suffix = literal.substr(pos);
    Type type = Type::NOT_SPECIFIED;
    if (suffix == "i32") type = Type::I32;
    else if (suffix == "u32") type = Type::U32;
    else if (suffix == "isize") type = Type::ISIZE;
    else if (suffix == "usize") type = Type::USIZE;

    return {std::stoll(digits, nullptr, base), type};
}

template<typename T, typename... Args>
ExprPtr makeExpr(Args&&... args) {
    return std::make_unique<Expr>(Expr{T{std::forward<Args>(args)...}});
}

template<typename T>
ExprPtr annotateExpr(ExprPtr expr, const span::Span& sp) {
    expr->span = sp;
    std::get<T>(expr->value).span = sp;
    return expr;
}

template<typename T>
StmtPtr annotateStmt(StmtPtr stmt, const span::Span& sp) {
    stmt->span = sp;
    std::get<T>(stmt->value).span = sp;
    return stmt;
}

template<typename T>
TypePtr annotateType(TypePtr type, const span::Span& sp) {
    type->span = sp;
    std::get<T>(type->value).span = sp;
    return type;
}

template<typename T>
PatternPtr annotatePattern(PatternPtr pattern, const span::Span& sp) {
    pattern->span = sp;
    std::get<T>(pattern->value).span = sp;
    return pattern;
}

ItemPtr annotateItem(ItemPtr item, const span::Span& sp) {
    item->span = sp;
    std::visit([&](auto& node) { node.span = sp; }, item->value);
    return item;
}

bool isWithBlockExpr(const ExprPtr& expr) {
    if (!expr) {
        return false;
    }
    return std::holds_alternative<BlockExpr>(expr->value) ||
           std::holds_alternative<IfExpr>(expr->value) ||
           std::holds_alternative<WhileExpr>(expr->value) ||
           std::holds_alternative<LoopExpr>(expr->value);
}

struct ParseFailure {
    parsec::ParseError error;
};

class HandwrittenParser {
public:
    explicit HandwrittenParser(parsec::ParseContext<Token>& context) : ctx(context) {}

    PathPtr parsePath() {
        std::vector<PathSegment> segments;
        segments.push_back(parsePathSegment());
        while (match(TOKEN_SEPARATOR, "::")) {
            segments.push_back(parsePathSegment());
        }

        auto path = std::make_unique<Path>(std::move(segments));
        std::vector<span::Span> spans;
        spans.reserve(path->segments.size());
        for (const auto& segment : path->segments) {
            spans.push_back(segment.span);
        }
        path->span = merge_span_list(spans);
        return path;
    }

    ExprPtr parseExpr(int min_precedence = 0) {
        auto lhs = parsePrefixPostfixCast();
        while (true) {
            auto info = currentBinaryOp();
            if (!info || info->precedence < min_precedence) {
                break;
            }

            advance();
            int next_min = info->right_assoc ? info->precedence : info->precedence + 1;
            auto rhs = parseExpr(next_min);
            auto lhs_span = lhs ? lhs->span : span::Span::invalid();
            auto rhs_span = rhs ? rhs->span : span::Span::invalid();
            if (info->is_assign) {
                lhs = annotateExpr<AssignExpr>(
                    makeExpr<AssignExpr>(info->assign_op, std::move(lhs), std::move(rhs)),
                    merge_span_pair(lhs_span, rhs_span));
            } else {
                lhs = annotateExpr<BinaryExpr>(
                    makeExpr<BinaryExpr>(info->binary_op, std::move(lhs), std::move(rhs)),
                    merge_span_pair(lhs_span, rhs_span));
            }
        }
        return lhs;
    }

    ExprPtr parseWithBlockExpr() {
        if (is(TOKEN_DELIMITER, "{")) {
            return parseBlockExpr();
        }
        if (is(TOKEN_KEYWORD, "if")) {
            return parseIfExpr();
        }
        if (is(TOKEN_KEYWORD, "while")) {
            return parseWhileExpr();
        }
        if (is(TOKEN_KEYWORD, "loop")) {
            return parseLoopExpr();
        }
        fail("an expression with a block");
    }

    ExprPtr parseLiteralExpr() {
        Token token = advance();
        if (token.type == TOKEN_STRING || token.type == TOKEN_CSTRING) {
            auto expr = makeExpr<StringLiteralExpr>(token.value, token.type == TOKEN_CSTRING);
            return annotateExpr<StringLiteralExpr>(std::move(expr), token.span);
        }
        if (token.type == TOKEN_CHAR) {
            auto expr = makeExpr<CharLiteralExpr>(token.value.empty() ? '\0' : token.value[0]);
            return annotateExpr<CharLiteralExpr>(std::move(expr), token.span);
        }
        if (token.type == TOKEN_NUMBER) {
            auto parsed = parseIntegerLiteral(token.value);
            auto expr = makeExpr<IntegerLiteralExpr>(parsed.value, parsed.type);
            return annotateExpr<IntegerLiteralExpr>(std::move(expr), token.span);
        }
        if (token.type == TOKEN_KEYWORD && (token.value == "true" || token.value == "false")) {
            auto expr = makeExpr<BoolLiteralExpr>(token.value == "true");
            return annotateExpr<BoolLiteralExpr>(std::move(expr), token.span);
        }
        fail("a literal expression", ctx.position - 1);
    }

    TypePtr parseType() {
        if (is(TOKEN_OPERATOR, "&")) {
            return parseReferenceType();
        }
        if (is(TOKEN_DELIMITER, "[")) {
            return parseArrayType();
        }
        if (is(TOKEN_DELIMITER, "(")) {
            return parseUnitType();
        }
        if (isPrimitiveType(peek())) {
            return parsePrimitiveType();
        }
        if (startsPath()) {
            auto path = parsePath();
            auto sp = path ? path->span : span::Span::invalid();
            return annotateType<PathType>(
                std::make_unique<Type>(Type{PathType{std::move(path)}}),
                sp);
        }
        fail("a type");
    }

    PatternPtr parsePattern() {
        if (is(TOKEN_OPERATOR, "&") || is(TOKEN_OPERATOR, "&&")) {
            return parseReferencePattern();
        }
        if (is(TOKEN_OPERATOR, "-") || isLiteralStart(peek())) {
            return parseLiteralPattern();
        }
        if (is(TOKEN_IDENTIFIER, "_") || is(TOKEN_KEYWORD, "_")) {
            auto token = advance();
            return annotatePattern<WildcardPattern>(
                std::make_unique<Pattern>(Pattern{WildcardPattern{}}),
                token.span);
        }
        if (is(TOKEN_KEYWORD, "self") || is(TOKEN_KEYWORD, "Self")) {
            return parsePathPattern();
        }
        if (is(TOKEN_IDENTIFIER)) {
            if (lookaheadIs(1, TOKEN_SEPARATOR, "::")) {
                return parsePathPattern();
            }
            return parseIdentifierPattern();
        }
        if (is(TOKEN_KEYWORD, "ref") || is(TOKEN_KEYWORD, "mut")) {
            return parseIdentifierPattern();
        }
        fail("a pattern");
    }

    StmtPtr parseStmt() {
        if (is(TOKEN_SEPARATOR, ";")) {
            auto token = advance();
            return annotateStmt<EmptyStmt>(
                std::make_unique<Statement>(Statement{EmptyStmt{}}),
                token.span);
        }
        if (is(TOKEN_KEYWORD, "let")) {
            return parseLetStmt();
        }
        if (startsItem()) {
            auto item = parseItem();
            auto sp = item ? item->span : span::Span::invalid();
            return annotateStmt<ItemStmt>(
                std::make_unique<Statement>(Statement{ItemStmt{std::move(item)}}),
                sp);
        }

        bool starts_with_block = startsWithBlockExpr();
        auto expr = starts_with_block ? parseWithBlockExpr() : parseExpr();
        bool has_semicolon = match(TOKEN_SEPARATOR, ";");
        if (!has_semicolon && !starts_with_block) {
            fail("';'");
        }
        auto sp = expr ? expr->span : span::Span::invalid();
        return annotateStmt<ExprStmt>(
            std::make_unique<Statement>(Statement{ExprStmt{std::move(expr), has_semicolon}}),
            sp);
    }

    ItemPtr parseItem() {
        if (is(TOKEN_KEYWORD, "fn")) return parseFunctionItem();
        if (is(TOKEN_KEYWORD, "struct")) return parseStructItem();
        if (is(TOKEN_KEYWORD, "enum")) return parseEnumItem();
        if (is(TOKEN_KEYWORD, "const")) return parseConstItem();
        if (is(TOKEN_KEYWORD, "trait")) return parseTraitItem();
        if (is(TOKEN_KEYWORD, "impl")) return parseImplItem();
        fail("an item");
    }

private:
    struct BinaryInfo {
        int precedence = 0;
        bool right_assoc = false;
        bool is_assign = false;
        BinaryExpr::Op binary_op = BinaryExpr::ADD;
        AssignExpr::Op assign_op = AssignExpr::ASSIGN;
    };

    parsec::ParseContext<Token>& ctx;

    const Token& peek(std::size_t offset = 0) const {
        if (ctx.position + offset < ctx.tokens.size()) {
            return ctx.tokens[ctx.position + offset];
        }
        return T_EOF;
    }

    bool is(TokenType type) const {
        return !ctx.isEOF() && peek().type == type;
    }

    bool is(TokenType type, const std::string& value) const {
        return !ctx.isEOF() && peek().type == type && peek().value == value;
    }

    bool lookaheadIs(std::size_t offset, TokenType type, const std::string& value) const {
        return ctx.position + offset < ctx.tokens.size() &&
               peek(offset).type == type &&
               peek(offset).value == value;
    }

    Token advance() {
        if (ctx.isEOF()) {
            fail("a token");
        }
        return ctx.tokens[ctx.position++];
    }

    bool match(TokenType type, const std::string& value) {
        if (!is(type, value)) {
            return false;
        }
        ++ctx.position;
        return true;
    }

    Token expect(TokenType type, const std::string& value) {
        if (!is(type, value)) {
            fail("'" + value + "'");
        }
        return advance();
    }

    IdPtr expectIdentifier() {
        if (!is(TOKEN_IDENTIFIER)) {
            fail("an identifier");
        }
        auto token = advance();
        auto id = std::make_unique<Identifier>(token.value);
        id->span = token.span;
        return id;
    }

    [[noreturn]] void fail(const std::string& expected) const {
        fail(expected, ctx.position);
    }

    [[noreturn]] void fail(const std::string& expected, std::size_t position) const {
        throw ParseFailure{parsec::ParseError{
            position,
            {expected},
            {},
            true,
            ctx.span_at(position),
        }};
    }

    bool startsPath() const {
        return is(TOKEN_IDENTIFIER) || is(TOKEN_KEYWORD, "self") || is(TOKEN_KEYWORD, "Self");
    }

    bool startsItem() const {
        if (!is(TOKEN_KEYWORD)) {
            return false;
        }
        const auto& value = peek().value;
        return value == "fn" || value == "struct" || value == "enum" ||
               value == "const" || value == "trait" || value == "impl";
    }

    bool startsWithBlockExpr() const {
        return is(TOKEN_DELIMITER, "{") || is(TOKEN_KEYWORD, "if") ||
               is(TOKEN_KEYWORD, "while") || is(TOKEN_KEYWORD, "loop");
    }

    bool isLiteralStart(const Token& token) const {
        return token.type == TOKEN_NUMBER || token.type == TOKEN_STRING ||
               token.type == TOKEN_CSTRING || token.type == TOKEN_CHAR ||
               (token.type == TOKEN_KEYWORD &&
                (token.value == "true" || token.value == "false"));
    }

    bool isPrimitiveType(const Token& token) const {
        return token.type == TOKEN_IDENTIFIER &&
               (token.value == "i32" || token.value == "u32" ||
                token.value == "isize" || token.value == "usize" ||
                token.value == "bool" || token.value == "char" ||
                token.value == "str");
    }

    PathSegment parsePathSegment() {
        if (!startsPath()) {
            fail("an identifier or self keyword");
        }
        auto token = advance();
        PathSegment segment;
        if (token.type == TOKEN_IDENTIFIER) {
            segment.type = PathSegType::IDENTIFIER;
        } else if (token.value == "self") {
            segment.type = PathSegType::self;
        } else {
            segment.type = PathSegType::SELF;
        }
        segment.id = std::make_unique<Identifier>(token.value);
        (*segment.id)->span = token.span;
        segment.span = token.span;
        return segment;
    }

    ExprPtr parsePrefixPostfixCast() {
        struct UnaryOp {
            UnaryExpr::Op op;
            span::Span span;
        };
        std::vector<UnaryOp> ops;
        while (true) {
            if (is(TOKEN_OPERATOR, "!")) {
                ops.push_back({UnaryExpr::NOT, advance().span});
            } else if (is(TOKEN_OPERATOR, "-")) {
                ops.push_back({UnaryExpr::NEGATE, advance().span});
            } else if (is(TOKEN_OPERATOR, "*")) {
                ops.push_back({UnaryExpr::DEREFERENCE, advance().span});
            } else if (is(TOKEN_OPERATOR, "&")) {
                auto amp = advance();
                bool is_mut = false;
                auto op_span = amp.span;
                if (is(TOKEN_KEYWORD, "mut")) {
                    auto mut = advance();
                    is_mut = true;
                    op_span = merge_span_pair(op_span, mut.span);
                }
                ops.push_back({is_mut ? UnaryExpr::MUTABLE_REFERENCE : UnaryExpr::REFERENCE, op_span});
            } else {
                break;
            }
        }

        auto expr = parsePostfix(parseAtom());
        for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
            auto operand_span = expr ? expr->span : span::Span::invalid();
            expr = annotateExpr<UnaryExpr>(
                makeExpr<UnaryExpr>(it->op, std::move(expr)),
                merge_span_pair(it->span, operand_span));
        }
        while (match(TOKEN_KEYWORD, "as")) {
            auto type = parseType();
            auto expr_span = expr ? expr->span : span::Span::invalid();
            auto type_span = type ? type->span : span::Span::invalid();
            expr = annotateExpr<CastExpr>(
                makeExpr<CastExpr>(std::move(expr), std::move(type)),
                merge_span_pair(expr_span, type_span));
        }
        return expr;
    }

    ExprPtr parseAtom() {
        if (startsPath()) {
            return parsePathOrStructExpr();
        }
        if (isLiteralStart(peek())) {
            return parseLiteralExpr();
        }
        if (is(TOKEN_DELIMITER, "(")) {
            auto left = expect(TOKEN_DELIMITER, "(");
            auto inner = parseExpr();
            auto right = expect(TOKEN_DELIMITER, ")");
            auto expr = annotateExpr<GroupedExpr>(
                makeExpr<GroupedExpr>(std::move(inner)),
                merge_span_pair(left.span, right.span));
            return expr;
        }
        if (is(TOKEN_DELIMITER, "[")) {
            return parseArrayExpr();
        }
        if (is(TOKEN_DELIMITER, "{")) {
            return parseBlockExpr();
        }
        if (is(TOKEN_KEYWORD, "if")) {
            return parseIfExpr();
        }
        if (is(TOKEN_KEYWORD, "while")) {
            return parseWhileExpr();
        }
        if (is(TOKEN_KEYWORD, "loop")) {
            return parseLoopExpr();
        }
        if (is(TOKEN_KEYWORD, "return")) {
            return parseReturnExpr();
        }
        if (is(TOKEN_KEYWORD, "break")) {
            return parseBreakExpr();
        }
        if (is(TOKEN_KEYWORD, "continue")) {
            return parseContinueExpr();
        }
        fail("an atomic expression");
    }

    ExprPtr parsePostfix(ExprPtr expr) {
        while (true) {
            if (is(TOKEN_DELIMITER, "(")) {
                auto args = parseExprList(")");
                auto callee_span = expr ? expr->span : span::Span::invalid();
                std::vector<span::Span> spans{callee_span};
                for (const auto& arg : args) {
                    if (arg) spans.push_back(arg->span);
                }
                expr = annotateExpr<CallExpr>(
                    makeExpr<CallExpr>(std::move(expr), std::move(args)),
                    merge_span_list(spans));
                continue;
            }
            if (is(TOKEN_DELIMITER, "[")) {
                expect(TOKEN_DELIMITER, "[");
                auto index = parseExpr();
                expect(TOKEN_DELIMITER, "]");
                auto base_span = expr ? expr->span : span::Span::invalid();
                auto index_span = index ? index->span : span::Span::invalid();
                expr = annotateExpr<IndexExpr>(
                    makeExpr<IndexExpr>(std::move(expr), std::move(index)),
                    merge_span_pair(base_span, index_span));
                continue;
            }
            if (is(TOKEN_OPERATOR, ".")) {
                expect(TOKEN_OPERATOR, ".");
                auto field = expectIdentifier();
                if (is(TOKEN_DELIMITER, "(")) {
                    auto args = parseExprList(")");
                    auto receiver_span = expr ? expr->span : span::Span::invalid();
                    auto field_span = field ? field->span : span::Span::invalid();
                    std::vector<span::Span> spans{receiver_span, field_span};
                    for (const auto& arg : args) {
                        if (arg) spans.push_back(arg->span);
                    }
                    expr = annotateExpr<MethodCallExpr>(
                        makeExpr<MethodCallExpr>(std::move(expr), std::move(field), std::move(args)),
                        merge_span_list(spans));
                } else {
                    auto base_span = expr ? expr->span : span::Span::invalid();
                    auto field_span = field ? field->span : span::Span::invalid();
                    expr = annotateExpr<FieldAccessExpr>(
                        makeExpr<FieldAccessExpr>(std::move(expr), std::move(field)),
                        merge_span_pair(base_span, field_span));
                }
                continue;
            }
            break;
        }
        return expr;
    }

    std::vector<ExprPtr> parseExprList(const std::string& end) {
        expect(TOKEN_DELIMITER, "(");
        std::vector<ExprPtr> args;
        if (match(TOKEN_DELIMITER, end)) {
            return args;
        }
        while (true) {
            args.push_back(parseExpr());
            if (match(TOKEN_DELIMITER, end)) {
                break;
            }
            expect(TOKEN_SEPARATOR, ",");
            if (match(TOKEN_DELIMITER, end)) {
                break;
            }
        }
        return args;
    }

    ExprPtr parseArrayExpr() {
        auto left = expect(TOKEN_DELIMITER, "[");
        if (is(TOKEN_DELIMITER, "]")) {
            auto right = advance();
            auto expr = makeExpr<ArrayInitExpr>(std::vector<ExprPtr>{});
            return annotateExpr<ArrayInitExpr>(std::move(expr), merge_span_pair(left.span, right.span));
        }

        auto first = parseExpr();
        if (match(TOKEN_SEPARATOR, ";")) {
            auto count = parseExpr();
            auto right = expect(TOKEN_DELIMITER, "]");
            auto value_span = first ? first->span : span::Span::invalid();
            auto count_span = count ? count->span : span::Span::invalid();
            auto expr = makeExpr<ArrayRepeatExpr>(std::move(first), std::move(count));
            return annotateExpr<ArrayRepeatExpr>(
                std::move(expr),
                merge_span_list(std::vector<span::Span>{left.span, value_span, count_span, right.span}));
        }

        std::vector<ExprPtr> elements;
        elements.push_back(std::move(first));
        while (match(TOKEN_SEPARATOR, ",")) {
            if (is(TOKEN_DELIMITER, "]")) {
                break;
            }
            elements.push_back(parseExpr());
        }
        auto right = expect(TOKEN_DELIMITER, "]");
        std::vector<span::Span> spans{left.span, right.span};
        for (const auto& element : elements) {
            if (element) spans.push_back(element->span);
        }
        auto expr = makeExpr<ArrayInitExpr>(std::move(elements));
        return annotateExpr<ArrayInitExpr>(std::move(expr), merge_span_list(spans));
    }

    ExprPtr parsePathOrStructExpr() {
        auto path = parsePath();
        if (!is(TOKEN_DELIMITER, "{")) {
            if (path->segments.size() == 1 && path->segments[0].id &&
                (*path->segments[0].id)->name == "_") {
                return annotateExpr<UnderscoreExpr>(makeExpr<UnderscoreExpr>(), path->span);
            }
            auto sp = path ? path->span : span::Span::invalid();
            return annotateExpr<PathExpr>(makeExpr<PathExpr>(std::move(path)), sp);
        }

        expect(TOKEN_DELIMITER, "{");
        std::vector<StructExpr::FieldInit> fields;
        if (!is(TOKEN_DELIMITER, "}")) {
            while (true) {
                auto name = expectIdentifier();
                expect(TOKEN_SEPARATOR, ":");
                auto value = parseExpr();
                StructExpr::FieldInit field{std::move(name), std::move(value)};
                auto name_span = field.name ? field.name->span : span::Span::invalid();
                auto value_span = field.value ? field.value->span : span::Span::invalid();
                field.span = merge_span_pair(name_span, value_span);
                fields.push_back(std::move(field));
                if (!match(TOKEN_SEPARATOR, ",")) {
                    break;
                }
                if (is(TOKEN_DELIMITER, "}")) {
                    break;
                }
            }
        }
        auto right = expect(TOKEN_DELIMITER, "}");
        std::vector<span::Span> spans{path ? path->span : span::Span::invalid(), right.span};
        for (const auto& field : fields) {
            spans.push_back(field.span);
        }
        return annotateExpr<StructExpr>(
            makeExpr<StructExpr>(std::move(path), std::move(fields)),
            merge_span_list(spans));
    }

    ExprPtr parseBlockExpr() {
        auto block = parseBlock();
        auto sp = block ? block->span : span::Span::invalid();
        return annotateExpr<BlockExpr>(
            makeExpr<BlockExpr>(std::move(block->statements), std::move(block->final_expr)),
            sp);
    }

    BlockExprPtr parseBlock() {
        auto left = expect(TOKEN_DELIMITER, "{");
        std::vector<StmtPtr> statements;
        std::optional<ExprPtr> final_expr;

        while (!is(TOKEN_DELIMITER, "}")) {
            if (ctx.isEOF()) {
                fail("'}'");
            }
            if (is(TOKEN_SEPARATOR, ";") || is(TOKEN_KEYWORD, "let") || startsItem()) {
                statements.push_back(parseStmt());
                continue;
            }

            bool starts_with_block = startsWithBlockExpr();
            auto expr = starts_with_block ? parseWithBlockExpr() : parseExpr();
            if (match(TOKEN_SEPARATOR, ";")) {
                auto sp = expr ? expr->span : span::Span::invalid();
                statements.push_back(annotateStmt<ExprStmt>(
                    std::make_unique<Statement>(Statement{ExprStmt{std::move(expr), true}}),
                    sp));
                continue;
            }
            if (is(TOKEN_DELIMITER, "}")) {
                final_expr = std::move(expr);
                break;
            }
            if (starts_with_block) {
                auto sp = expr ? expr->span : span::Span::invalid();
                statements.push_back(annotateStmt<ExprStmt>(
                    std::make_unique<Statement>(Statement{ExprStmt{std::move(expr), false}}),
                    sp));
                continue;
            }
            fail("';'");
        }

        auto right = expect(TOKEN_DELIMITER, "}");
        if (!final_expr && !statements.empty()) {
            auto& last = statements.back();
            if (last) {
                if (auto* expr_stmt = std::get_if<ExprStmt>(&last->value);
                    expr_stmt && !expr_stmt->has_trailing_semicolon &&
                    isWithBlockExpr(expr_stmt->expr)) {
                    final_expr = std::move(expr_stmt->expr);
                    statements.pop_back();
                }
            }
        }

        auto block = std::make_unique<BlockExpr>(std::move(statements), std::move(final_expr));
        std::vector<span::Span> spans{left.span, right.span};
        for (const auto& stmt : block->statements) {
            if (stmt) spans.push_back(stmt->span);
        }
        if (block->final_expr && *block->final_expr) {
            spans.push_back((*block->final_expr)->span);
        }
        block->span = merge_span_list(spans);
        return block;
    }

    ExprPtr parseIfExpr() {
        auto if_tok = expect(TOKEN_KEYWORD, "if");
        expect(TOKEN_DELIMITER, "(");
        auto condition = parseExpr();
        expect(TOKEN_DELIMITER, ")");
        auto then_branch = parseBlock();
        std::optional<ExprPtr> else_branch;
        if (match(TOKEN_KEYWORD, "else")) {
            if (is(TOKEN_KEYWORD, "if")) {
                else_branch = parseIfExpr();
            } else {
                else_branch = parseBlockExpr();
            }
        }
        auto cond_span = condition ? condition->span : span::Span::invalid();
        auto then_span = then_branch ? then_branch->span : span::Span::invalid();
        auto else_span = else_branch && *else_branch ? (*else_branch)->span : span::Span::invalid();
        return annotateExpr<IfExpr>(
            makeExpr<IfExpr>(std::move(condition), std::move(then_branch), std::move(else_branch)),
            merge_span_list(std::vector<span::Span>{if_tok.span, cond_span, then_span, else_span}));
    }

    ExprPtr parseWhileExpr() {
        auto while_tok = expect(TOKEN_KEYWORD, "while");
        auto condition = parseExpr();
        auto body = parseBlock();
        auto cond_span = condition ? condition->span : span::Span::invalid();
        auto body_span = body ? body->span : span::Span::invalid();
        return annotateExpr<WhileExpr>(
            makeExpr<WhileExpr>(std::move(condition), std::move(body)),
            merge_span_list(std::vector<span::Span>{while_tok.span, cond_span, body_span}));
    }

    ExprPtr parseLoopExpr() {
        auto loop_tok = expect(TOKEN_KEYWORD, "loop");
        auto body = parseBlock();
        auto body_span = body ? body->span : span::Span::invalid();
        return annotateExpr<LoopExpr>(
            makeExpr<LoopExpr>(std::move(body)),
            merge_span_pair(loop_tok.span, body_span));
    }

    std::optional<IdPtr> parseOptionalLabel() {
        if (!match(TOKEN_OPERATOR, "'")) {
            return std::nullopt;
        }
        return expectIdentifier();
    }

    bool exprCanFollowTerminator() const {
        return !ctx.isEOF() &&
               !is(TOKEN_SEPARATOR, ";") &&
               !is(TOKEN_DELIMITER, "}") &&
               !is(TOKEN_DELIMITER, ")");
    }

    ExprPtr parseReturnExpr() {
        auto ret = expect(TOKEN_KEYWORD, "return");
        std::optional<ExprPtr> value;
        if (exprCanFollowTerminator()) {
            value = parseExpr();
        }
        auto value_span = value && *value ? (*value)->span : span::Span::invalid();
        return annotateExpr<ReturnExpr>(
            makeExpr<ReturnExpr>(std::move(value)),
            merge_span_pair(ret.span, value_span));
    }

    ExprPtr parseBreakExpr() {
        auto brk = expect(TOKEN_KEYWORD, "break");
        auto label = parseOptionalLabel();
        std::optional<ExprPtr> value;
        if (exprCanFollowTerminator()) {
            value = parseExpr();
        }
        auto label_span = label && *label ? (*label)->span : span::Span::invalid();
        auto value_span = value && *value ? (*value)->span : span::Span::invalid();
        return annotateExpr<BreakExpr>(
            makeExpr<BreakExpr>(std::move(label), std::move(value)),
            merge_span_list(std::vector<span::Span>{brk.span, label_span, value_span}));
    }

    ExprPtr parseContinueExpr() {
        auto cont = expect(TOKEN_KEYWORD, "continue");
        auto label = parseOptionalLabel();
        auto label_span = label && *label ? (*label)->span : span::Span::invalid();
        return annotateExpr<ContinueExpr>(
            makeExpr<ContinueExpr>(std::move(label)),
            merge_span_pair(cont.span, label_span));
    }

    std::optional<BinaryInfo> currentBinaryOp() const {
        if (!is(TOKEN_OPERATOR)) {
            return std::nullopt;
        }
        const auto& op = peek().value;
        if (op == "*") return BinaryInfo{60, false, false, BinaryExpr::MUL};
        if (op == "/") return BinaryInfo{60, false, false, BinaryExpr::DIV};
        if (op == "%") return BinaryInfo{60, false, false, BinaryExpr::REM};
        if (op == "+") return BinaryInfo{50, false, false, BinaryExpr::ADD};
        if (op == "-") return BinaryInfo{50, false, false, BinaryExpr::SUB};
        if (op == "<<") return BinaryInfo{48, false, false, BinaryExpr::SHL};
        if (op == ">>") return BinaryInfo{48, false, false, BinaryExpr::SHR};
        if (op == "&") return BinaryInfo{45, false, false, BinaryExpr::BIT_AND};
        if (op == "^") return BinaryInfo{42, false, false, BinaryExpr::BIT_XOR};
        if (op == "|") return BinaryInfo{41, false, false, BinaryExpr::BIT_OR};
        if (op == "==") return BinaryInfo{40, false, false, BinaryExpr::EQ};
        if (op == "!=") return BinaryInfo{40, false, false, BinaryExpr::NE};
        if (op == "<") return BinaryInfo{40, false, false, BinaryExpr::LT};
        if (op == ">") return BinaryInfo{40, false, false, BinaryExpr::GT};
        if (op == "<=") return BinaryInfo{40, false, false, BinaryExpr::LE};
        if (op == ">=") return BinaryInfo{40, false, false, BinaryExpr::GE};
        if (op == "&&") return BinaryInfo{30, false, false, BinaryExpr::AND};
        if (op == "||") return BinaryInfo{20, false, false, BinaryExpr::OR};
        if (op == "=") return BinaryInfo{10, true, true, BinaryExpr::ADD, AssignExpr::ASSIGN};
        if (op == "+=") return BinaryInfo{10, true, true, BinaryExpr::ADD, AssignExpr::ADD_ASSIGN};
        if (op == "-=") return BinaryInfo{10, true, true, BinaryExpr::ADD, AssignExpr::SUB_ASSIGN};
        if (op == "*=") return BinaryInfo{10, true, true, BinaryExpr::ADD, AssignExpr::MUL_ASSIGN};
        if (op == "/=") return BinaryInfo{10, true, true, BinaryExpr::ADD, AssignExpr::DIV_ASSIGN};
        if (op == "%=") return BinaryInfo{10, true, true, BinaryExpr::ADD, AssignExpr::REM_ASSIGN};
        if (op == "&=") return BinaryInfo{10, true, true, BinaryExpr::ADD, AssignExpr::BIT_AND_ASSIGN};
        if (op == "|=") return BinaryInfo{10, true, true, BinaryExpr::ADD, AssignExpr::BIT_OR_ASSIGN};
        if (op == "^=") return BinaryInfo{10, true, true, BinaryExpr::ADD, AssignExpr::XOR_ASSIGN};
        if (op == "<<=") return BinaryInfo{10, true, true, BinaryExpr::ADD, AssignExpr::SHL_ASSIGN};
        if (op == ">>=") return BinaryInfo{10, true, true, BinaryExpr::ADD, AssignExpr::SHR_ASSIGN};
        return std::nullopt;
    }

    TypePtr parsePrimitiveType() {
        static const std::unordered_map<std::string, PrimitiveType::Kind> kinds = {
            {"i32", PrimitiveType::I32}, {"u32", PrimitiveType::U32},
            {"isize", PrimitiveType::ISIZE}, {"usize", PrimitiveType::USIZE},
            {"bool", PrimitiveType::BOOL}, {"char", PrimitiveType::CHAR},
            {"str", PrimitiveType::STRING},
        };
        auto token = advance();
        return annotateType<PrimitiveType>(
            std::make_unique<Type>(Type{PrimitiveType{kinds.at(token.value)}}),
            token.span);
    }

    TypePtr parseUnitType() {
        auto left = expect(TOKEN_DELIMITER, "(");
        auto right = expect(TOKEN_DELIMITER, ")");
        return annotateType<UnitType>(
            std::make_unique<Type>(Type{UnitType{}}),
            merge_span_pair(left.span, right.span));
    }

    TypePtr parseArrayType() {
        auto left = expect(TOKEN_DELIMITER, "[");
        auto element = parseType();
        expect(TOKEN_SEPARATOR, ";");
        auto size = parseExpr();
        auto right = expect(TOKEN_DELIMITER, "]");
        auto element_span = element ? element->span : span::Span::invalid();
        auto size_span = size ? size->span : span::Span::invalid();
        return annotateType<ArrayType>(
            std::make_unique<Type>(Type{ArrayType{std::move(element), std::move(size)}}),
            merge_span_list(std::vector<span::Span>{left.span, element_span, size_span, right.span}));
    }

    TypePtr parseReferenceType() {
        auto amp = expect(TOKEN_OPERATOR, "&");
        bool is_mut = match(TOKEN_KEYWORD, "mut");
        auto referenced = parseType();
        auto ref_span = referenced ? referenced->span : span::Span::invalid();
        return annotateType<ReferenceType>(
            std::make_unique<Type>(Type{ReferenceType{std::move(referenced), is_mut}}),
            merge_span_pair(amp.span, ref_span));
    }

    PatternPtr parseLiteralPattern() {
        std::optional<Token> neg;
        if (is(TOKEN_OPERATOR, "-")) {
            neg = advance();
        }
        auto literal = parseLiteralExpr();
        auto literal_span = literal ? literal->span : span::Span::invalid();
        auto sp = neg ? merge_span_pair(neg->span, literal_span) : literal_span;
        return annotatePattern<LiteralPattern>(
            std::make_unique<Pattern>(Pattern{LiteralPattern{std::move(literal), neg.has_value()}}),
            sp);
    }

    PatternPtr parseIdentifierPattern() {
        bool is_ref = match(TOKEN_KEYWORD, "ref");
        bool is_mut = match(TOKEN_KEYWORD, "mut");
        auto id = expectIdentifier();
        if (is(TOKEN_SEPARATOR, "::")) {
            fail("not a path segment");
        }
        auto sp = id ? id->span : span::Span::invalid();
        return annotatePattern<IdentifierPattern>(
            std::make_unique<Pattern>(Pattern{IdentifierPattern{std::move(id), is_ref, is_mut}}),
            sp);
    }

    PatternPtr parsePathPattern() {
        auto path = parsePath();
        auto sp = path ? path->span : span::Span::invalid();
        return annotatePattern<PathPattern>(
            std::make_unique<Pattern>(Pattern{PathPattern{std::move(path)}}),
            sp);
    }

    PatternPtr parseReferencePattern() {
        if (is(TOKEN_OPERATOR, "&&")) {
            auto tok = advance();
            auto inner = parsePattern();
            ReferencePattern inner_ref{std::move(inner), false};
            inner_ref.span = merge_span_pair(tok.span, inner_ref.subpattern ? inner_ref.subpattern->span : span::Span::invalid());
            auto inner_pat = annotatePattern<ReferencePattern>(
                std::make_unique<Pattern>(Pattern{std::move(inner_ref)}),
                inner_ref.span);
            ReferencePattern outer{std::move(inner_pat), false};
            auto sp = merge_span_pair(tok.span, outer.subpattern ? outer.subpattern->span : span::Span::invalid());
            return annotatePattern<ReferencePattern>(
                std::make_unique<Pattern>(Pattern{std::move(outer)}),
                sp);
        }

        auto amp = expect(TOKEN_OPERATOR, "&");
        bool is_mut = match(TOKEN_KEYWORD, "mut");
        auto inner = parsePattern();
        auto inner_span = inner ? inner->span : span::Span::invalid();
        return annotatePattern<ReferencePattern>(
            std::make_unique<Pattern>(Pattern{ReferencePattern{std::move(inner), is_mut}}),
            merge_span_pair(amp.span, inner_span));
    }

    StmtPtr parseLetStmt() {
        auto let_tok = expect(TOKEN_KEYWORD, "let");
        auto pattern = parsePattern();
        std::optional<TypePtr> type_annotation;
        if (match(TOKEN_SEPARATOR, ":")) {
            type_annotation = parseType();
        }
        std::optional<ExprPtr> initializer;
        if (match(TOKEN_OPERATOR, "=")) {
            initializer = parseExpr();
        }
        auto semi = expect(TOKEN_SEPARATOR, ";");
        std::vector<span::Span> spans{let_tok.span, semi.span};
        if (pattern) spans.push_back(pattern->span);
        if (type_annotation && *type_annotation) spans.push_back((*type_annotation)->span);
        if (initializer && *initializer) spans.push_back((*initializer)->span);
        return annotateStmt<LetStmt>(
            std::make_unique<Statement>(
                Statement{LetStmt{std::move(pattern), std::move(type_annotation), std::move(initializer)}}),
            merge_span_list(spans));
    }

    FunctionItem::SelfParamPtr parseSelfParam() {
        std::optional<Token> amp;
        if (is(TOKEN_OPERATOR, "&")) {
            amp = advance();
        }
        std::optional<Token> mut;
        if (is(TOKEN_KEYWORD, "mut")) {
            mut = advance();
        }
        auto self_tok = expect(TOKEN_KEYWORD, "self");
        auto sp = self_tok.span;
        if (amp) sp = merge_span_pair(sp, amp->span);
        if (mut) sp = merge_span_pair(sp, mut->span);
        return std::make_unique<FunctionItem::SelfParam>(amp.has_value(), mut.has_value(), sp);
    }

    bool startsSelfParam() const {
        if (is(TOKEN_KEYWORD, "self")) {
            return true;
        }
        if (is(TOKEN_KEYWORD, "mut")) {
            return lookaheadIs(1, TOKEN_KEYWORD, "self");
        }
        if (!is(TOKEN_OPERATOR, "&")) {
            return false;
        }
        return lookaheadIs(1, TOKEN_KEYWORD, "self") ||
               (lookaheadIs(1, TOKEN_KEYWORD, "mut") &&
                lookaheadIs(2, TOKEN_KEYWORD, "self"));
    }

    std::pair<PatternPtr, TypePtr> parseFunctionParam() {
        auto pattern = parsePattern();
        expect(TOKEN_SEPARATOR, ":");
        auto type = parseType();
        return {std::move(pattern), std::move(type)};
    }

    ItemPtr parseFunctionItem() {
        auto fn_tok = expect(TOKEN_KEYWORD, "fn");
        auto name = expectIdentifier();
        std::optional<FunctionItem::SelfParamPtr> self_param;
        std::vector<std::pair<PatternPtr, TypePtr>> params;

        expect(TOKEN_DELIMITER, "(");
        if (!is(TOKEN_DELIMITER, ")")) {
            if (startsSelfParam()) {
                self_param = parseSelfParam();
                if (match(TOKEN_SEPARATOR, ",") && !is(TOKEN_DELIMITER, ")")) {
                    while (true) {
                        params.push_back(parseFunctionParam());
                        if (!match(TOKEN_SEPARATOR, ",")) {
                            break;
                        }
                        if (is(TOKEN_DELIMITER, ")")) {
                            break;
                        }
                    }
                }
            } else {
                while (true) {
                    params.push_back(parseFunctionParam());
                    if (!match(TOKEN_SEPARATOR, ",")) {
                        break;
                    }
                    if (is(TOKEN_DELIMITER, ")")) {
                        break;
                    }
                }
            }
        }
        expect(TOKEN_DELIMITER, ")");

        std::optional<TypePtr> return_type;
        if (match(TOKEN_OPERATOR, "->")) {
            return_type = parseType();
        }
        std::optional<BlockExprPtr> body;
        if (match(TOKEN_SEPARATOR, ";")) {
            body = std::nullopt;
        } else {
            body = parseBlock();
        }

        std::vector<span::Span> spans{fn_tok.span, name ? name->span : span::Span::invalid()};
        if (return_type && *return_type) spans.push_back((*return_type)->span);
        if (body && *body) spans.push_back((*body)->span);
        auto item = std::make_unique<Item>(Item{FunctionItem{
            std::move(name), std::move(self_param), std::move(params),
            std::move(return_type), std::move(body)}});
        return annotateItem(std::move(item), merge_span_list(spans));
    }

    ItemPtr parseStructItem() {
        auto struct_tok = expect(TOKEN_KEYWORD, "struct");
        auto name = expectIdentifier();
        std::vector<std::pair<IdPtr, TypePtr>> fields;
        if (match(TOKEN_SEPARATOR, ";")) {
            auto item = std::make_unique<Item>(Item{StructItem{std::move(name), std::move(fields)}});
            return annotateItem(std::move(item), struct_tok.span);
        }
        expect(TOKEN_DELIMITER, "{");
        while (!is(TOKEN_DELIMITER, "}")) {
            auto field_name = expectIdentifier();
            expect(TOKEN_SEPARATOR, ":");
            auto field_type = parseType();
            fields.emplace_back(std::move(field_name), std::move(field_type));
            if (!match(TOKEN_SEPARATOR, ",")) {
                break;
            }
        }
        auto right = expect(TOKEN_DELIMITER, "}");
        auto item = std::make_unique<Item>(Item{StructItem{std::move(name), std::move(fields)}});
        return annotateItem(std::move(item), merge_span_pair(struct_tok.span, right.span));
    }

    ItemPtr parseEnumItem() {
        auto enum_tok = expect(TOKEN_KEYWORD, "enum");
        auto name = expectIdentifier();
        std::vector<IdPtr> variants;
        expect(TOKEN_DELIMITER, "{");
        while (!is(TOKEN_DELIMITER, "}")) {
            variants.push_back(expectIdentifier());
            if (!match(TOKEN_SEPARATOR, ",")) {
                break;
            }
        }
        auto right = expect(TOKEN_DELIMITER, "}");
        auto item = std::make_unique<Item>(Item{EnumItem{std::move(name), std::move(variants)}});
        return annotateItem(std::move(item), merge_span_pair(enum_tok.span, right.span));
    }

    ItemPtr parseConstItem() {
        auto const_tok = expect(TOKEN_KEYWORD, "const");
        auto name = expectIdentifier();
        expect(TOKEN_SEPARATOR, ":");
        auto type = parseType();
        expect(TOKEN_OPERATOR, "=");
        auto value = parseExpr();
        auto semi = expect(TOKEN_SEPARATOR, ";");
        auto item = std::make_unique<Item>(Item{ConstItem{std::move(name), std::move(type), std::move(value)}});
        return annotateItem(std::move(item), merge_span_pair(const_tok.span, semi.span));
    }

    ItemPtr parseTraitItem() {
        auto trait_tok = expect(TOKEN_KEYWORD, "trait");
        auto name = expectIdentifier();
        auto items = parseItemBlock();
        auto item = std::make_unique<Item>(Item{TraitItem{std::move(name), std::move(items.first)}});
        return annotateItem(std::move(item), merge_span_pair(trait_tok.span, items.second));
    }

    ItemPtr parseImplItem() {
        auto impl_tok = expect(TOKEN_KEYWORD, "impl");
        std::optional<IdPtr> trait_name;
        if (is(TOKEN_IDENTIFIER) && lookaheadIs(1, TOKEN_KEYWORD, "for")) {
            trait_name = expectIdentifier();
            expect(TOKEN_KEYWORD, "for");
        }
        auto for_type = parseType();
        auto items = parseItemBlock();
        if (trait_name) {
            auto item = std::make_unique<Item>(Item{TraitImplItem{
                std::move(*trait_name), std::move(for_type), std::move(items.first)}});
            return annotateItem(std::move(item), merge_span_pair(impl_tok.span, items.second));
        }
        auto item = std::make_unique<Item>(Item{InherentImplItem{std::move(for_type), std::move(items.first)}});
        return annotateItem(std::move(item), merge_span_pair(impl_tok.span, items.second));
    }

    std::pair<std::vector<ItemPtr>, span::Span> parseItemBlock() {
        auto left = expect(TOKEN_DELIMITER, "{");
        std::vector<ItemPtr> items;
        while (!is(TOKEN_DELIMITER, "}")) {
            if (ctx.isEOF()) {
                fail("'}'");
            }
            items.push_back(parseItem());
        }
        auto right = expect(TOKEN_DELIMITER, "}");
        return {std::move(items), merge_span_pair(left.span, right.span)};
    }
};

template<typename T, typename Fn>
parsec::Parser<T, Token> parserFrom(Fn fn) {
    return parsec::Parser<T, Token>(
        [fn = std::move(fn)](parsec::ParseContext<Token>& context) -> parsec::ParseResult<T> {
            try {
                HandwrittenParser parser(context);
                return fn(parser);
            } catch (const ParseFailure& failure) {
                return failure.error;
            }
        });
}

} // namespace

void initHandwrittenParserRegistry(ParserRegistry& registry) {
    registry.path = parserFrom<PathPtr>([](HandwrittenParser& parser) {
        return parser.parsePath();
    });
    registry.expr = parserFrom<ExprPtr>([](HandwrittenParser& parser) {
        return parser.parseExpr();
    });
    registry.exprWithBlock = parserFrom<ExprPtr>([](HandwrittenParser& parser) {
        return parser.parseWithBlockExpr();
    });
    registry.literalExpr = parserFrom<ExprPtr>([](HandwrittenParser& parser) {
        return parser.parseLiteralExpr();
    });
    registry.type = parserFrom<TypePtr>([](HandwrittenParser& parser) {
        return parser.parseType();
    });
    registry.pattern = parserFrom<PatternPtr>([](HandwrittenParser& parser) {
        return parser.parsePattern();
    });
    registry.stmt = parserFrom<StmtPtr>([](HandwrittenParser& parser) {
        return parser.parseStmt();
    });
    registry.item = parserFrom<ItemPtr>([](HandwrittenParser& parser) {
        return parser.parseItem();
    });
}
