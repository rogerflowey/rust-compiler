#pragma once

#include "lib/parsecpp/include/parsec.hpp"
#include "parser/ast/common.hpp"
#include "parser/common.hpp"
#include "parser/utils.hpp"
class PathParserBuilder{
    PathParser p_path;
    parsec::Parser<PathSegment,Token> p_segment;
public:
    PathParserBuilder(){
        init_segment();
        init_path_parser();
    };
    void init_segment(){
        p_segment = parsec::satisfy<Token>([](const Token &t) {
            return t.type == TokenType::TOKEN_IDENTIFIER ||
                   (t.type == TokenType::TOKEN_KEYWORD &&
                    (t.value == "self" || t.value == "Self"));
        }).map([](Token t) -> PathSegment {
            PathSegment segment;
            if (t.type == TokenType::TOKEN_IDENTIFIER) {
                segment.type = PathSegType::IDENTIFIER;
            } else if (t.value == "self") {
                segment.type = PathSegType::self;
            } else {
                segment.type = PathSegType::SELF;
            }
            segment.id = std::make_unique<Identifier>(t.value);
            return segment;
        });
    }
    void init_path_parser(){
        p_path = p_segment.andThen((equal({TOKEN_SEPARATOR, "::"}) > p_segment).optional())
            .map([](std::tuple<PathSegment, std::optional<PathSegment>> &&segments) -> PathPtr {
                std::vector<PathSegment> path_segments;
                path_segments.push_back(std::move(std::get<0>(segments)));
                if (std::get<1>(segments)) {
                    path_segments.push_back(std::move(*std::get<1>(segments)));
                }
                return std::make_unique<Path>(std::move(path_segments));
            });
    }
    auto get_parser(){
        return p_path;
    }
};