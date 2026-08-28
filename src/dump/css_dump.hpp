#pragma once
#include "../../css/parser.hpp"
#include <string>

std::string css_val_type_str(browser::css::CSSValue::Type t);
std::string dump_declaration(const browser::css::Declaration &decl);
std::string dump_rule(const browser::css::Rule &rule, int idx);
std::string dump_stylesheet(const browser::css::StyleSheet &sheet);
