#include "path_parse.hpp"

#include "parser_registry.hpp"
#include "utils.hpp"
#include <vector>
#include <string>

using namespace parsec;
using namespace ast;

void PathParserBuilder::finalize(const ParserRegistry& /*registry*/, std::function<void(PathParser)> set_path_parser) {
    auto segmentParser = buildSegmentParser();
    auto pathParser = segmentParser.andThen((equal({TOKEN_SEPARATOR, "::"}) > segmentParser).many())
        .map([](auto &&tup) -> PathPtr {
            auto first = std::move(std::get<0>(tup));
            auto rest_vec = std::move(std::get<1>(tup));
            std::vector<PathSegment> segments;
            segments.reserve(1 + rest_vec.size());
            segments.push_back(std::move(first));
            for (auto &seg : rest_vec) segments.push_back(std::move(seg));
            return std::make_unique<Path>(std::move(segments));
        }).label("a path");
    
    set_path_parser(pathParser);
}

parsec::Parser<PathSegment, Token> PathParserBuilder::buildSegmentParser() const {
    return parsec::satisfy<Token>([](const Token &t) {
        if (t.type == TokenType::TOKEN_IDENTIFIER) return true;
        if (t.type == TokenType::TOKEN_KEYWORD && (t.value == "self" || t.value == "Self")) return true;
        return false;
    }, "an identifier or self keyword").map([](Token t) -> PathSegment {
        PathSegment segment;
        if (t.type == TokenType::TOKEN_IDENTIFIER) {
            segment.type = PathSegType::IDENTIFIER;
            segment.id = std::make_unique<Identifier>(t.value);
        } else if (t.value == "self") {
            segment.type = PathSegType::self;
            segment.id = std::make_unique<Identifier>(t.value);
        } else { // Self
            segment.type = PathSegType::SELF;
            segment.id = std::make_unique<Identifier>(t.value);
        }
        return segment;
    });
}