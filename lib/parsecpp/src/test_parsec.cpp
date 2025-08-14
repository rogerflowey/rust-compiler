#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <numeric> // For std::accumulate
#include "parsec.hpp"

// 1. SETUP: A move-only AST node and a simple token (char)
struct MyNode {
    int value;
};
using NodePtr = std::unique_ptr<MyNode>;


// 2. THE PARSER
namespace Grammar {
    using namespace parsec;

    auto node_parser() {
        return satisfy<char>([](char c){ return std::isdigit(c); })
            .map([](char c) {
                return std::make_unique<MyNode>(MyNode{c - '0'});
            });
    }

    // A parser for a list of numbers like "1,2,3"
    auto list_parser_that_fails() {
        auto comma = satisfy<char>([](char c){ return c == ','; });
        auto optional_bang = optional(satisfy<char>([](char c){ return c == '!'; }));

        // This chain now mirrors your failing code's structure
        return (node_parser() >> (comma > node_parser()).many()) // 1. Returns Parser<tuple<NodePtr, vector<NodePtr>>>
            .keepLeft(optional_bang)                              // 2. This is the problematic step
            .map([](std::tuple<NodePtr, std::vector<NodePtr>>&& pair) { // 3. This lambda is now incorrect
                auto&& [first_node, rest_nodes] = pair;
                int sum = first_node->value;
                for (const auto& node : rest_nodes) {
                    sum += node->value;
                }
                return sum;
            });
    }
}

int main() {    
    std::cout << "The code above demonstrates the compilation failure." << std::endl;
    std::cout << "The explanation for the failure is below." << std::endl;
    return 0;
}