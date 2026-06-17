#pragma once
#include <string>
#include <vector>
#include <memory>
#include <variant>
#include "../tests/utility.hpp"

namespace browser::css {

struct Length {
    enum class Unit { PX, EM, REM, PERCENT, VW, VH, NONE, DEG, S, MS };
    f32 value = 0;
    Unit unit = Unit::PX;
};

struct Color {
    u8 r = 0, g = 0, b = 0, a = 255;
    static Color from_hex(const std::string& hex);
    static Color from_name(const std::string& name);
    static Color from_rgba(u8 r, u8 g, u8 b, u8 a);
};

struct TransformFunc {
    enum class Type { MATRIX, TRANSLATE, TRANSLATE_X, TRANSLATE_Y, ROTATE, SCALE, SCALE_X, SCALE_Y, SKEW, SKEW_X, SKEW_Y };
    Type type;
    std::vector<f32> args;
};

struct CSSGradientStop {
    Color color;
    f32 position = -1.0f; // -1 means not specified
};

struct CSSGradient {
    enum class Type { LINEAR, RADIAL, CONIC, REPEATING_LINEAR, REPEATING_RADIAL, REPEATING_CONIC };
    Type type;
    f32 angle = 0; // for linear/repeating-linear
    std::vector<CSSGradientStop> stops;
    // radial: cx, cy, radius in % of box
    f32 cx = 0.5f, cy = 0.5f, radius = 0.5f;
};

struct CalcExpr {
    enum class Op { NONE, ADD, SUB, MUL, DIV };
    struct Node {
        enum Type { NUMBER, LENGTH, PERCENTAGE, NEGATE, OP };
        Type type = NUMBER;
        f32 number_value = 0;
        Length length_value;
        Op op = Op::NONE;
        std::unique_ptr<Node> left, right;
    };
    std::unique_ptr<Node> root;
};

struct CSSValue {
    enum class Type { KEYWORD, LENGTH, COLOR, STRING, NUMBER, PERCENTAGE, URL, FUNCTION, GRADIENT, TRANSFORM };
    Type type;
    std::string keyword;
    Length length;
    Color color;
    f32 number = 0;
    std::string string_value;
    CSSGradient gradient;
    std::vector<TransformFunc> transforms;
};

struct Selector;

struct SimpleSelector {
    enum class Type { TAG, CLASS, ID, UNIVERSAL, ATTRIBUTE, PSEUDO_CLASS, PSEUDO_ELEMENT };
    Type type;
    std::string name, value;
    char match_operator = 0;

    // For :not(), :is(), :where() — parsed selector list
    std::vector<Selector> argument_selectors;

    // For :nth-child(), :nth-last-child()
    struct NthArgs {
        i32 a = 0;
        i32 b = 0;
        bool is_odd = false;
        bool is_even = false;
    };
    NthArgs nth_args;
};

enum class Combinator { DESCENDANT, CHILD, ADJACENT_SIBLING, GENERAL_SIBLING };

struct CompoundSelector {
    std::vector<SimpleSelector> simples;
};

struct Selector {
    std::vector<CompoundSelector> compounds;
    std::vector<Combinator> combinators;
};

struct Declaration {
    std::string property;
    std::vector<CSSValue> values;
    bool important = false;
};

struct KeyframeBlock {
    std::vector<f32> positions; // percentages 0-100 for each selector
    std::vector<Declaration> declarations;
};

struct KeyframesRule {
    std::string name;
    std::vector<KeyframeBlock> blocks;
};

struct Rule {
    std::vector<Selector> selectors;
    std::vector<Declaration> declarations;
};

struct AtRule {
    std::string name;
    std::string prelude;
    std::vector<Rule> rules;
    std::vector<AtRule> at_rules;
    std::vector<Declaration> declarations;
    KeyframesRule keyframes; // for @keyframes
};

struct StyleSheet {
    std::vector<Rule> rules;
    std::vector<AtRule> at_rules;
};

}
