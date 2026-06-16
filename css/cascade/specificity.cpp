#include "engine.hpp"

#include <algorithm>
#include <vector>

namespace browser::css {

    void sort_matched_decls(std::vector<MatchedDecl> &decls) {
        std::sort(decls.begin(), decls.end(), [](const MatchedDecl &a, const MatchedDecl &b) {
            bool a_imp = a.decl->important;
            bool b_imp = b.decl->important;
            if (a_imp != b_imp)
                return a_imp > b_imp;
            u8 a_prio = important_origin_priority(a.origin, a_imp);
            u8 b_prio = important_origin_priority(b.origin, b_imp);
            if (a_prio != b_prio)
                return a_prio < b_prio;
            if (a.specificity.bits != b.specificity.bits)
                return a.specificity.bits < b.specificity.bits;
            return a.source_order < b.source_order;
        });
    }

}  // namespace browser::css
