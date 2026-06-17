#include "engine.hpp"

namespace browser::css {

    u8 important_origin_priority(u8 origin, bool important) {
        if (important) {
            // For important: Author(1) < Inline(2) < UA(0)
            if (origin == 0) return 2;   // UA important → highest
            return origin - 1;           // Author(1)→0, Inline(2)→1
        }
        // For non-important: UA(0) < Author(1) < Inline(2)
        return origin;
    }

}  // namespace browser::css
