#include "selector_index.hpp"
#include <algorithm>

namespace browser::css {

void RuleIndex::build(const std::vector<Rule>& rules) {
    for (u32 i = 0; i < rules.size(); ++i) {
        auto& r = rules[i];
        order[&r] = i;
        for (auto& sel : r.selectors) {
            if (sel.compounds.empty()) { universal.push_back(&r); continue; }
            auto& last = sel.compounds.back();
            bool placed = false;
            for (auto& s : last.simples) {
                if (s.type == SimpleSelector::Type::ID) { by_id[s.name].push_back(&r); placed = true; break; }
            }
            if (placed) continue;
            for (auto& s : last.simples) {
                if (s.type == SimpleSelector::Type::CLASS) { by_class[s.name].push_back(&r); placed = true; break; }
            }
            if (placed) continue;
            for (auto& s : last.simples) {
                if (s.type == SimpleSelector::Type::TAG) { by_tag[s.name].push_back(&r); placed = true; break; }
            }
            if (!placed) universal.push_back(&r);
        }
    }
}

std::vector<const Rule*> RuleIndex::candidates_for(const html::Element* el) const {
    std::vector<const Rule*> out;
    out.reserve(universal.size() + 4);
    for (auto* r : universal) out.push_back(r);
    auto it = by_id.find(el->id());
    if (it != by_id.end()) for (auto* r : it->second) out.push_back(r);
    for (auto& cls : el->class_list()) {
        auto cit = by_class.find(cls);
        if (cit != by_class.end()) for (auto* r : cit->second) out.push_back(r);
    }
    auto tt = by_tag.find(el->tag_name);
    if (tt != by_tag.end()) for (auto* r : tt->second) out.push_back(r);
    // Deduplicate and sort by source order to preserve cascade ordering (audit CS-P2)
    std::sort(out.begin(), out.end(), [&](const Rule* a, const Rule* b){
        auto ai = order.find(a); auto bi = order.find(b);
        u32 av = ai != order.end() ? ai->second : 0;
        u32 bv = bi != order.end() ? bi->second : 0;
        return av < bv;
    });
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

} // namespace browser::css
