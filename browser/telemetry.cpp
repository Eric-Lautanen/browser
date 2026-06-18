#include "telemetry.hpp"

#include "../async/memory.hpp"
#include "perf_counter.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace {

    std::string html_escape(const std::string &s) {
        std::string r;
        for (char c : s) {
            if (c == '&')
                r += "&amp;";
            else if (c == '<')
                r += "&lt;";
            else if (c == '>')
                r += "&gt;";
            else if (c == '"')
                r += "&quot;";
            else
                r += c;
        }
        return r;
    }

}  // namespace

namespace browser {

    Telemetry::Telemetry() = default;

    void Telemetry::record(const TelemetryEvent &e) {
        events_.push_back(e);
    }

    std::string Telemetry::generate_report() const {
        auto &pc = PerfCounters::instance();
        std::ostringstream html;
        html << "<!DOCTYPE html><html><head><style>"
             << "body{font-family:sans-serif;margin:20px;background:#f5f5f5}"
             << "h1{color:#333;border-bottom:2px solid #555;padding-bottom:6px}"
             << "h2{color:#444;margin-top:24px}"
             << "table{border-collapse:collapse;width:100%;margin:8px 0}"
             << "th,td{border:1px solid #ccc;padding:6px 8px;text-align:left;font-size:13px}"
             << "th{background:#e0e0e0}.count{font-size:18px;margin:10px 0}"
             << ".stat{display:inline-block;margin:8px 16px 8px 0;padding:8px 12px;"
             << "background:#fff;border:1px solid #ddd;border-radius:4px;min-width:120px}"
             << ".stat b{display:block;font-size:22px;color:#222}"
             << ".stat span{font-size:11px;color:#888}"
             << ".type-html{background:#e8f5e9}.type-css{background:#e3f2fd}"
             << ".type-js{background:#fff3e0}.type-image{background:#fce4ec}"
             << ".type-font{background:#f3e5f5}"
             << "</style></head><body>"
             << "<h1>Performance Report</h1>"
             << "<p class='count'>Trackers blocked: " << trackers_blocked_ << "</p>";

        // --- FPS Section ---
        html << "<h2>FPS</h2>";
        f32 fps = pc.current_fps.load();
        f32 min_fps = pc.min_fps.load();
        f32 max_fps = pc.max_fps.load();
        f32 avg_fps = pc.avg_fps.load();
        html << "<div>"
             << "<div class='stat'><b>" << std::fixed << std::setprecision(1) << fps
             << "</b><span>Current FPS</span></div>"
             << "<div class='stat'><b>" << std::fixed << std::setprecision(1) << min_fps
             << "</b><span>Min FPS</span></div>"
             << "<div class='stat'><b>" << std::fixed << std::setprecision(1) << max_fps
             << "</b><span>Max FPS</span></div>"
             << "<div class='stat'><b>" << std::fixed << std::setprecision(1) << avg_fps
             << "</b><span>Avg FPS</span></div>"
             << "</div>";

        // Sparkline graph of last 120 frames
        html << "<svg width='600' height='60' style='background:#fff;border:1px solid #ddd;margin:8px 0'>";
        f32 max_fps_graph = 120.0f;
        html << "<polyline points='";
        for (u32 i = 0; i < 120; i++) {
            f32 val = pc.fps_history[i];
            if (val <= 0)
                val = 0;
            f32 x = (f32)i * 5.0f;
            f32 y = 60.0f - (val / max_fps_graph) * 55.0f;
            if (y > 60)
                y = 60;
            html << (int)x << "," << (int)y;
            if (i < 119)
                html << " ";
        }
        html << "' fill='none' stroke='#2196f3' stroke-width='1.5'/></svg>";

        // --- Frame Time Breakdown ---
        html << "<h2>Frame Time Breakdown</h2>";
        f32 frm = pc.frame_time_ms.load();
        f32 evt = pc.events_time_ms.load();
        f32 lay = pc.layout_time_ms.load();
        f32 pnt = pc.paint_time_ms.load();
        f32 cmp = pc.composite_time_ms.load();
        f32 gpu = pc.gpu_time_ms.load();
        f32 total_breakdown = evt + lay + pnt + cmp + gpu;
        if (total_breakdown < 0.01f)
            total_breakdown = 1.0f;

        auto bar = [&](const char *label, f32 val, const char *color) {
            f32 pct = (val / total_breakdown) * 100.0f;
            if (pct > 100.0f)
                pct = 100.0f;
            html << "<div style='margin:2px 0;font-size:12px'>"
                 << "<span style='display:inline-block;width:90px'>" << label << "</span>"
                 << "<span style='display:inline-block;width:300px;height:16px;background:#eee;vertical-align:middle'>"
                 << "<span style='display:inline-block;height:16px;width:" << (int)pct << "%;background:" << color
                 << "'></span>"
                 << "</span>"
                 << "<span style='margin-left:8px'>" << std::fixed << std::setprecision(2) << val << " ms</span>"
                 << "</div>";
        };
        bar("Events", evt, "#4caf50");
        bar("Layout", lay, "#ff9800");
        bar("Paint", pnt, "#2196f3");
        bar("Composite", cmp, "#9c27b0");
        bar("GPU", gpu, "#f44336");
        html << "<div style='margin-top:4px;font-size:12px;color:#888'>Total frame: " << std::fixed
             << std::setprecision(2) << frm << " ms</div>";

        // --- Memory Section ---
        html << "<h2>Memory</h2>";
        f64 ws_mb = (f64)pc.working_set_bytes.load() / (1024.0 * 1024.0);
        f64 heap_mb = (f64)pc.heap_allocated_bytes.load() / (1024.0 * 1024.0);
        html << "<div>"
             << "<div class='stat'><b>" << std::fixed << std::setprecision(1) << ws_mb
             << " MB</b><span>Working Set</span></div>"
             << "<div class='stat'><b>" << std::fixed << std::setprecision(1) << heap_mb
             << " MB</b><span>Heap Allocated</span></div>"
             << "<div class='stat'><b>" << pc.heap_alloc_count.load() << "</b><span>Live Allocations</span></div>"
             << "</div>";

        // --- Allocation Breakdown ---
        html << "<h2>Allocation by Subsystem</h2>";
        auto &alloc = async::AllocationBreakdown::instance();
        auto add_alloc_stat = [&](const char *label, u64 bytes) {
            f64 mb = (f64)bytes / (1024.0 * 1024.0);
            html << "<div class='stat'><b>" << std::fixed << std::setprecision(2) << mb << " MB</b><span>" << label
                 << "</span></div>";
        };
        html << "<div>";
        add_alloc_stat("HTML", alloc.html);
        add_alloc_stat("CSS", alloc.css);
        add_alloc_stat("JS", alloc.js);
        add_alloc_stat("Net", alloc.net);
        add_alloc_stat("Render", alloc.render);
        add_alloc_stat("Image", alloc.image);
        add_alloc_stat("Platform", alloc.platform);
        add_alloc_stat("Async", alloc.async);
        add_alloc_stat("Browser", alloc.browser);
        html << "</div>";

        // --- GC Section ---
        html << "<h2>Garbage Collection</h2>";
        html << "<div>"
             << "<div class='stat'><b>" << pc.gc_cycle_count.load() << "</b><span>Cycles</span></div>"
             << "<div class='stat'><b>" << pc.gc_live_objects.load() << "</b><span>Live Objects</span></div>"
             << "<div class='stat'><b>" << pc.gc_live_functions.load() << "</b><span>Live Functions</span></div>"
             << "<div class='stat'><b>" << pc.gc_heap_size.load() << " b</b><span>Heap Size</span></div>"
             << "<div class='stat'><b>" << pc.gc_collected_last.load() << "</b><span>Collected (Last)</span></div>"
             << "<div class='stat'><b>" << std::fixed << std::setprecision(2) << pc.gc_pause_time_ms.load()
             << " ms</b><span>Pause Time</span></div>"
             << "</div>";

        // --- Cache Section ---
        html << "<h2>Cache</h2>";
        u64 hits = pc.cache_hits.load();
        u64 misses = pc.cache_misses.load();
        u64 total_accesses = hits + misses;
        f64 hit_rate = (total_accesses > 0) ? (f64)hits / (f64)total_accesses * 100.0 : 0.0;
        f64 cache_mb = (f64)pc.cache_size_bytes.load() / (1024.0 * 1024.0);
        html << "<div>"
             << "<div class='stat'><b>" << std::fixed << std::setprecision(1) << hit_rate
             << "%</b><span>Hit Rate</span></div>"
             << "<div class='stat'><b>" << hits << " / " << total_accesses << "</b><span>Hits / Total</span></div>"
             << "<div class='stat'><b>" << std::fixed << std::setprecision(1) << cache_mb
             << " MB</b><span>Cache Size</span></div>"
             << "<div class='stat'><b>" << pc.cache_entry_count.load() << "</b><span>Entries</span></div>"
             << "</div>";

        // --- Resource Load Waterfall ---
        html << "<h2>Resource Load Waterfall</h2>";
        html << "<table><tr><th>URL</th><th>Type</th><th>Start (ms)</th><th>End (ms)</th><th>Duration "
                "(ms)</th><th>Bytes</th></tr>";
        auto sorted = pc.resources;
        std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.start_ms < b.start_ms; });
        for (const auto &r : sorted) {
            std::string css_class;
            if (r.type == "html")
                css_class = "type-html";
            else if (r.type == "css")
                css_class = "type-css";
            else if (r.type == "js")
                css_class = "type-js";
            else if (r.type == "image")
                css_class = "type-image";
            else if (r.type == "font")
                css_class = "type-font";
            html << "<tr class='" << css_class << "'>"
                 << "<td>" << html_escape(r.url) << "</td>"
                 << "<td>" << html_escape(r.type) << "</td>"
                 << "<td>" << r.start_ms << "</td>"
                 << "<td>" << r.end_ms << "</td>"
                 << "<td>" << (r.end_ms - r.start_ms) << "</td>"
                 << "<td>" << r.byte_size << "</td>"
                 << "</tr>";
        }
        html << "</table>";

        // --- Events Table (existing) ---
        html << "<h2>Events</h2>";
        html << "<table><tr><th>Type</th><th>URL</th><th>Duration (ms)</th></tr>";

        for (const auto &e : events_) {
            const char *type_name = "UNKNOWN";
            switch (e.type) {
                case TelemetryEvent::PAGE_LOAD:
                    type_name = "PAGE_LOAD";
                    break;
                case TelemetryEvent::DNS_LOOKUP:
                    type_name = "DNS_LOOKUP";
                    break;
                case TelemetryEvent::TLS_HANDSHAKE:
                    type_name = "TLS_HANDSHAKE";
                    break;
                case TelemetryEvent::FIRST_PAINT:
                    type_name = "FIRST_PAINT";
                    break;
                case TelemetryEvent::TRACKER_BLOCKED:
                    type_name = "TRACKER_BLOCKED";
                    break;
                case TelemetryEvent::GC_CYCLE:
                    type_name = "GC_CYCLE";
                    break;
                case TelemetryEvent::CACHE_HIT:
                    type_name = "CACHE_HIT";
                    break;
                case TelemetryEvent::CACHE_MISS:
                    type_name = "CACHE_MISS";
                    break;
                case TelemetryEvent::RESOURCE_LOAD:
                    type_name = "RESOURCE_LOAD";
                    break;
            }
            html << "<tr>"
                 << "<td>" << type_name << "</td>"
                 << "<td>" << html_escape(e.url) << "</td>"
                 << "<td>" << e.duration_ms << "</td>"
                 << "</tr>";
        }

        html << "</table></body></html>";
        return html.str();
    }

}  // namespace browser
