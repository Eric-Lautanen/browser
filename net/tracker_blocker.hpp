#pragma once
#include "../core/utility.hpp"

#include <atomic>
#include <string>
#include <vector>

namespace browser::net {

    class TrackerBlocker {
    public:
        TrackerBlocker();
        void load_default_list();
        bool should_block(const std::string &url) const;
        u32 blocked_count() const;
        void reset_count();

    private:
        struct Rule {
            std::string domain;
        };
        std::vector<Rule> rules_;
        // BR-P5: should_block runs on concurrent fetch workers.
        mutable std::atomic<u32> blocked_count_{0};
    };

}  // namespace browser::net
