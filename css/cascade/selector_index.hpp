#pragma once
#include "../../css/parser.hpp"
#include "../../css/selector_match.hpp"
#include <unordered_map>
#include <vector>

namespace browser::css {

// CS-P2: RuleIndex buckets rules by rightmost compound id/class/tag to avoid
// O(elements * rules) matching. First step: structure and single-bucket lookup;
// full wiring into Cascade::collect_rules will follow.
struct RuleIndex {
    std::unordered_map<std::string, std::vector<const Rule*>> by_id;
    std::unordered_map<std::string, std::vector<const Rule*>> by_class;
    std::unordered_map<std::string, std::vector<const Rule*>> by_tag;
    std::vector<const Rule*> universal;
    std::unordered_map<const Rule*, u32> order;

    void build(const std::vector<Rule>& rules);
    std::vector<const Rule*> candidates_for(const html::Element* el) const;
};

} // namespace browser::css
