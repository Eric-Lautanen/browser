#pragma once
#include "types.hpp"

#include <string>
#include <vector>

namespace browser {

    struct TelemetryEvent {
        enum Type : u32 {
            PAGE_LOAD = 0,
            DNS_LOOKUP = 1,
            TLS_HANDSHAKE = 2,
            FIRST_PAINT = 3,
            TRACKER_BLOCKED = 4,
            GC_CYCLE = 5,
            CACHE_HIT = 6,
            CACHE_MISS = 7,
            RESOURCE_LOAD = 8
        };
        Type type;
        std::string url;
        f64 duration_ms = 0;
    };

    class Telemetry {
    public:
        Telemetry();
        void record(const TelemetryEvent &e);
        std::string generate_report() const;
        std::string render_page() const { return generate_report(); }
        u32 trackers_blocked() const { return trackers_blocked_; }
        void set_trackers_blocked(u32 n) { trackers_blocked_ = n; }

    private:
        std::vector<TelemetryEvent> events_;
        u32 trackers_blocked_ = 0;
    };

}  // namespace browser
