#include "tracker_blocker.hpp"
#include "../net/url.hpp"
#include <algorithm>

namespace browser::net {

TrackerBlocker::TrackerBlocker() = default;

void TrackerBlocker::load_default_list() {
    static const char* default_trackers[] = {
        "doubleclick.net",
        "google-analytics.com",
        "googletagmanager.com",
        "googlesyndication.com",
        "googleadservices.com",
        "adservice.google.com",
        "facebook.net",
        "pixel.facebook.com",
        "amazon-adsystem.com",
        "scorecardresearch.com",
        "quantserve.com",
        "criteo.com",
        "outbrain.com",
        "taboola.com",
        "rubiconproject.com",
        "pubmatic.com",
        "openx.net",
        "appnexus.com",
        "adsrvr.org",
        "casalemedia.com",
        "moatads.com",
        "adnxs.com",
        "bluekai.com",
        "exelator.com",
        "demdex.net",
        "mathtag.com",
        "media.net",
        "adroll.com",
        "bidswitch.net",
        "adsafeprotected.com",
        "serving-sys.com",
        "turn.com",
        "agkn.com",
        "adzerk.net",
        "contextweb.com",
        "criteo.net",
        "adsymptotic.com",
        "sharethis.com",
        "addthis.com",
        "tribalfusion.com"
    };
    for (auto* d : default_trackers) {
        rules_.push_back({d});
    }
}

bool TrackerBlocker::should_block(const std::string& url) const {
    if (rules_.empty()) return false;

    auto parsed = URL::parse(url);
    if (parsed.is_err()) return false;
    auto& u = parsed.unwrap();
    std::string host = u.host;

    for (const auto& rule : rules_) {
        const std::string& tdom = rule.domain;
        if (host == tdom) {
            blocked_count_++;
            return true;
        }
        if (host.length() > tdom.length() &&
            host[host.length() - tdom.length() - 1] == '.' &&
            host.substr(host.length() - tdom.length()) == tdom) {
            blocked_count_++;
            return true;
        }
    }
    return false;
}

u32 TrackerBlocker::blocked_count() const {
    return blocked_count_;
}

void TrackerBlocker::reset_count() {
    blocked_count_ = 0;
}

}
