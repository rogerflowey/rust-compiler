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
        return parse_expression(0);
    }

private:
    struct OpInfo {
        int precedence;
        bool is_left_assoc;
        BinaryOp op_func;
    };

    std::optional<Parser<ResultType, Token>> atom_parser_;
    std::map<Token, OpInfo> op_info_map_;

    Parser<ResultType, Token> parse_expression(int min_precedence) const {
        // THE FIX: Capture `this` pointer instead of making a copy of the builder.
        // This ensures all recursive calls operate on the same builder instance,
        // which is crucial for the lazy parser to work correctly.
        auto parse_fn = [this, min_precedence](ParseContext<Token>& context) -> std::optional<ResultType> {
            // Use `this->` to access member variables.
            auto left_opt = this->atom_parser_->parse(context);
            if (!left_opt) {
                return std::nullopt;
            }
            ResultType left = *left_opt;

            while (true) {
                auto loop_start_pos = context.position;
                if (context.isEOF()) {
                    break;
                }

                const Token& op_token = context.tokens[context.position];
                // Use `this->` to access member variables.
                auto it = this->op_info_map_.find(op_token);

                if (it == this->op_info_map_.end() || it->second.precedence < min_precedence) {
                    break;
                }

                const auto& op_info = it->second;

                context.position++;

                int next_min_precedence = op_info.is_left_assoc ? op_info.precedence + 1 : op_info.precedence;
                
                // The recursive call now uses the captured `this` pointer, ensuring
                // it's part of the same logical "build" process.
                auto rhs_parser = this->parse_expression(next_min_precedence);
                auto right_opt = rhs_parser.parse(context);

                if (!right_opt) {
                    context.position = loop_start_pos;
                    break;
                }
                left = op_info.op_func(left, *right_opt);
            }

            return left;
        };

        return Parser<ResultType, Token>(parse_fn);
    }
};

} // namespace parsec