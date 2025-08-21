// pratt.h (Corrected)
#pragma once

#include "parsec.hpp"
#include <map>
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
        OpInfo info{precedence, true, op};
        op_info_map_[op_token] = info;
        return *this;
    }
    PrattParserBuilder& addInfixRight(const Token& op_token, int precedence, BinaryOp op) {
        OpInfo info{precedence, false, op};
        op_info_map_[op_token] = info;
        return *this;
    }
    Parser<ResultType, Token> build() const {
        if (!atom_parser_) {
            throw std::logic_error("Pratt parser cannot be built without an atom parser. Use withAtomParser().");
        }

        struct State {
            Parser<ResultType, Token> atom;
            std::map<Token, OpInfo> ops;
        };

        struct Recursor {
            std::shared_ptr<State> st;
            Parser<ResultType, Token> operator()(int min_precedence) const {
                auto st = this->st; // capture for lambdas
                auto self = *this;   // copyable for recursion
                auto parse_fn = [st, self, min_precedence](ParseContext<Token>& context) -> std::optional<ResultType> {
                    auto left_opt = st->atom.parse(context);
                    if (!left_opt) {
                        return std::nullopt;
                    }
                    ResultType left = std::move(*left_opt);

                    while (true) {
                        auto loop_start_pos = context.position;
                        if (context.isEOF()) {
                            break;
                        }

                        const Token& op_token = context.tokens[context.position];
                        auto it = st->ops.find(op_token);
                        if (it == st->ops.end() || it->second.precedence < min_precedence) {
                            break;
                        }

                        const auto& op_info = it->second;
                        context.position++;

                        int next_min_precedence = op_info.is_left_assoc ? op_info.precedence + 1 : op_info.precedence;

                        auto rhs_parser = self(next_min_precedence);
                        auto right_opt = rhs_parser.parse(context);
                        if (!right_opt) {
                            context.position = loop_start_pos;
                            break;
                        }
                        left = op_info.op_func(std::move(left), std::move(*right_opt));
                    }

                    return left;
                };

                return Parser<ResultType, Token>(parse_fn);
            }
        };

        auto st = std::make_shared<State>();
        st->atom = *atom_parser_;
        st->ops = op_info_map_;
        Recursor rec{st};
        return rec(0);
    }

private:
    struct OpInfo {
        int precedence;
        bool is_left_assoc;
        BinaryOp op_func;
    };

    std::optional<Parser<ResultType, Token>> atom_parser_;
    std::map<Token, OpInfo> op_info_map_;

    Parser<ResultType, Token> parse_expression(int) const = delete; // no longer used
};

} // namespace parsec