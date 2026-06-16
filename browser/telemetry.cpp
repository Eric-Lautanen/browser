#include "telemetry.hpp"
#include <sstream>

namespace browser {

Telemetry::Telemetry() = default;

void Telemetry::record(const TelemetryEvent& e) {
    events_.push_back(e);
}

std::string Telemetry::generate_report() const {
    std::ostringstream html;
    html << "<!DOCTYPE html><html><head><style>"
         << "body{font-family:sans-serif;margin:20px;background:#f5f5f5}"
         << "h1{color:#333}table{border-collapse:collapse;width:100%}"
         << "th,td{border:1px solid #ccc;padding:8px;text-align:left}"
         << "th{background:#e0e0e0}.count{font-size:18px;margin:10px 0}"
         << "</style></head><body>"
         << "<h1>Performance Report</h1>"
         << "<p class='count'>Trackers blocked: " << trackers_blocked_ << "</p>"
         << "<table><tr><th>Type</th><th>URL</th><th>Duration (ms)</th></tr>";

    for (const auto& e : events_) {
        const char* type_name = "UNKNOWN";
        switch (e.type) {
            case TelemetryEvent::PAGE_LOAD: type_name = "PAGE_LOAD"; break;
            case TelemetryEvent::DNS_LOOKUP: type_name = "DNS_LOOKUP"; break;
            case TelemetryEvent::TLS_HANDSHAKE: type_name = "TLS_HANDSHAKE"; break;
            case TelemetryEvent::FIRST_PAINT: type_name = "FIRST_PAINT"; break;
            case TelemetryEvent::TRACKER_BLOCKED: type_name = "TRACKER_BLOCKED"; break;
            case TelemetryEvent::GC_CYCLE: type_name = "GC_CYCLE"; break;
        }
        html << "<tr>"
             << "<td>" << type_name << "</td>"
             << "<td>" << e.url << "</td>"
             << "<td>" << e.duration_ms << "</td>"
             << "</tr>";
    }

    html << "</table></body></html>";
    return html.str();
}

}
