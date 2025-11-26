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
            auto path = std::make_unique<Path>(std::move(segments));
            std::vector<span::Span> spans;
            spans.reserve(path->segments.size());
            for (const auto &seg : path->segments) {
                spans.push_back(seg.span);
            }
            path->span = merge_span_list(spans);
            return path;
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
            segment.id.value().get()->span = t.span;
        } else if (t.value == "self") {
            segment.type = PathSegType::self;
            segment.id = std::make_unique<Identifier>(t.value);
        } else { // Self
            segment.type = PathSegType::SELF;
            segment.id = std::make_unique<Identifier>(t.value);
        }
        if (segment.id.has_value() && segment.id.value() != nullptr) {
            segment.id.value().get()->span = t.span;
        }
        segment.span = t.span;
        return segment;
    });
}