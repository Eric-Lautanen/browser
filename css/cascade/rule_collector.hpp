#pragma once
#include "engine.hpp"
#include "selector_index.hpp"
namespace browser::css { struct RuleCollector { RuleIndex index; void collect(const html::Element* el); }; }
