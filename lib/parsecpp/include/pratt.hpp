#pragma once

#include "parsec.hpp"
#include <vector>
#include <stdexcept>

namespace parsec {

template <typename ResultType, typename Token>
class PrattParserBuilder {
public:
    using BinaryOp = std::function<ResultType(ResultType, ResultType)>;

    PrattParserBuilder() = default;

    PrattParserBuilder& withAtomParser(const Parser<ResultType, Token>& atom_parser) {
        atom_parser_ = atom_parser;
        return *this;
    }
    PrattParserBuilder& addInfixLeft(const Token& op_token, int precedence, BinaryOp op) {
        OpInfo info{precedence, true, std::move(op)};
        add_or_replace(op_token, std::move(info));
        return *this;
    }
    PrattParserBuilder& addInfixRight(const Token& op_token, int precedence, BinaryOp op) {
        OpInfo info{precedence, false, std::move(op)};
        add_or_replace(op_token, std::move(info));
        return *this;
    }
    Parser<ResultType, Token> build() const {
        if (!atom_parser_) {
            throw std::logic_error("Pratt parser cannot be built without an atom parser. Use withAtomParser().");
        }

        struct State {
            Parser<ResultType, Token> atom;
            std::vector<OpEntry> ops;
        };

        auto st = std::make_shared<State>();
        st->atom = *atom_parser_;
        st->ops = op_entries_;

        auto parse_fn = [st](ParseContext<Token>& context) -> ParseResult<ResultType> {
            auto parse_expr = [&](auto&& self, int min_precedence) -> ParseResult<ResultType> {
                auto left_res = st->atom.parse(context);
                if (std::holds_alternative<ParseError>(left_res)) {
                    return left_res;
                }
                ResultType left = std::move(std::get<ResultType>(left_res));

                while (true) {
                    if (context.isEOF()) {
                        break;
                    }

                    const Token& op_token = context.tokens[context.position];
                    const OpInfo* op_info = find_op(st->ops, op_token);
                    if (!op_info || op_info->precedence < min_precedence) {
                        break;
                    }

                    context.position++;
                    int next_min_precedence =
                        op_info->is_left_assoc ? op_info->precedence + 1 : op_info->precedence;

                    auto right_res = self(self, next_min_precedence);
                    if (std::holds_alternative<ParseError>(right_res)) {
                        return right_res;
                    }
                    left = op_info->op_func(
                        std::move(left), std::move(std::get<ResultType>(right_res)));
                }

                return left;
            };

            return parse_expr(parse_expr, 0);
        };

        return Parser<ResultType, Token>(parse_fn);
    }

private:
    struct OpInfo {
        int precedence;
        bool is_left_assoc;
        BinaryOp op_func;
    };

    std::optional<Parser<ResultType, Token>> atom_parser_;
    struct OpEntry {
        Token token;
        OpInfo info;
    };
    std::vector<OpEntry> op_entries_;

    void add_or_replace(const Token& token, OpInfo info) {
        for (auto& entry : op_entries_) {
            if (entry.token == token) {
                entry.info = std::move(info);
                return;
            }
        }
        op_entries_.push_back(OpEntry{token, std::move(info)});
    }

    static const OpInfo* find_op(const std::vector<OpEntry>& ops, const Token& token) {
        for (const auto& entry : ops) {
            if (entry.token == token) {
                return &entry.info;
            }
        }
        return nullptr;
    }

    Parser<ResultType, Token> parse_expression(int) const = delete; // no longer used
};

} // namespace parsec