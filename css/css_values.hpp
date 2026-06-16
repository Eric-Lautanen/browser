#pragma once
#include <string>
#include <vector>
#include "../tests/utility.hpp"

namespace browser::css {

struct Length {
    enum class Unit { PX, EM, REM, PERCENT, VW, VH, NONE };
    f32 value = 0;
    Unit unit = Unit::PX;
};

struct Color {
    u8 r = 0, g = 0, b = 0, a = 255;
    static Color from_hex(const std::string& hex);
    static Color from_name(const std::string& name);
    static Color from_rgba(u8 r, u8 g, u8 b, u8 a);
};

struct CSSValue {
    enum class Type { KEYWORD, LENGTH, COLOR, STRING, NUMBER, PERCENTAGE, URL, FUNCTION };
    Type type;
    std::string keyword;
    Length length;
    Color color;
    f32 number = 0;
    std::string string_value;
};

struct SimpleSelector {
    enum class Type { TAG, CLASS, ID, UNIVERSAL, ATTRIBUTE, PSEUDO_CLASS, PSEUDO_ELEMENT };
    Type type;
    std::string name, value;
    char match_operator = 0;
};

enum class Combinator { DESCENDANT, CHILD, ADJACENT_SIBLING, GENERAL_SIBLING };

struct CompoundSelector {
    std::vector<SimpleSelector> simples;
};

struct Selector {
    std::vector<CompoundSelector> compounds;
    std::vector<Combinator> combinators; // always compounds.size()-1 for valid selectors
};

struct Declaration {
    std::string property;
    std::vector<CSSValue> values;
    bool important = false;
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
    std::vector<Declaration> declarations; // for @font-face etc
};

struct StyleSheet {
    std::vector<Rule> rules;
    std::vector<AtRule> at_rules;
};

}
