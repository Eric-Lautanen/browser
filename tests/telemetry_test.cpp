#include "test_framework.hpp"
#include "../browser/telemetry.hpp"

namespace browser {

TEST(telemetry_empty_report, {
    Telemetry t;
    auto r = t.generate_report();
    ASSERT(r.find("<table>") != std::string::npos);
    ASSERT(r.find("Trackers blocked: 0") != std::string::npos);
})

TEST(telemetry_record, {
    Telemetry t;
    t.record({TelemetryEvent::PAGE_LOAD, "https://example.com", 150.0});
    t.record({TelemetryEvent::DNS_LOOKUP, "https://example.com", 20.0});
    auto r = t.generate_report();
    ASSERT(r.find("example.com") != std::string::npos);
    ASSERT(r.find("150") != std::string::npos);
    ASSERT(r.find("20") != std::string::npos);
})

TEST(telemetry_trackers_blocked, {
    Telemetry t;
    t.set_trackers_blocked(5);
    auto r = t.generate_report();
    ASSERT(r.find("Trackers blocked: 5") != std::string::npos);
})

TEST(telemetry_event_types, {
    Telemetry t;
    t.record({TelemetryEvent::PAGE_LOAD, "a", 1});
    t.record({TelemetryEvent::DNS_LOOKUP, "a", 2});
    t.record({TelemetryEvent::TLS_HANDSHAKE, "a", 3});
    t.record({TelemetryEvent::FIRST_PAINT, "a", 4});
    t.record({TelemetryEvent::TRACKER_BLOCKED, "a", 5});
    t.record({TelemetryEvent::GC_CYCLE, "a", 6});
    auto r = t.generate_report();
    for (int i = 0; i < 6; i++)
        ASSERT(r.find(std::to_string(i)) != std::string::npos);
})

}
