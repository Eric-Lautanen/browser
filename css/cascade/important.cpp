#include "engine.hpp"

namespace browser::css {

    u8 important_origin_priority(u8 origin, bool important) {
        if (important) {
            return static_cast<u8>(2 - origin);
        }
        return origin;
    }

}  // namespace browser::css
