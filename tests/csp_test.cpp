#include "test_framework.hpp"
#include "../net/csp.hpp"
#include "../net/url.hpp"

using namespace browser;
using namespace browser::net;

// N-S4: 'self' must resolve against the policy's own origin.

TEST(csp_self_matches_own_origin_only, {
    auto p = CSPParser::parse("default-src 'self'", "https://example.com");
    ASSERT(p.allows("script-src", "https://example.com/x.js"));
    ASSERT(!p.allows("script-src", "https://evil.com/x.js"));
    ASSERT(!p.allows("script-src", "http://example.com/x.js"));  // scheme differs
})

TEST(csp_self_unknown_origin_fails_closed, {
    // No page origin supplied: 'self' must match nothing (pre-fix it matched
    // every origin in existence).
    auto p = CSPParser::parse("default-src 'self'");
    ASSERT(!p.allows("script-src", "https://evil.com/x.js"));
    ASSERT(!p.allows("script-src", "https://example.com/x.js"));
})

TEST(csp_wildcard_requires_dot_boundary, {
    auto p = CSPParser::parse("img-src *.example.com", "https://example.com");
    ASSERT(p.allows("img-src", "https://cdn.example.com/i.png"));
    ASSERT(!p.allows("img-src", "https://evilexample.com/i.png"));
    ASSERT(!p.allows("img-src", "https://example.com/i.png"));  // bare domain not covered by *.
})

TEST(csp_none_blocks_everything, {
    auto p = CSPParser::parse("default-src 'none'", "https://example.com");
    ASSERT(!p.allows("script-src", "https://example.com/x.js"));
    ASSERT(!p.allows("img-src", "https://example.com/i.png"));
})

TEST(csp_unsafe_inline_allows_inline_script, {
    auto strict = CSPParser::parse("script-src 'self'", "https://example.com");
    ASSERT(!strict.allows_inline_script());
    auto loose = CSPParser::parse("script-src 'self' 'unsafe-inline'", "https://example.com");
    ASSERT(loose.allows_inline_script());
})

TEST(csp_directive_overrides_default, {
    auto p = CSPParser::parse("default-src 'self'; img-src *", "https://example.com");
    ASSERT(p.allows("img-src", "https://anywhere.example.net/i.png"));
    ASSERT(!p.allows("script-src", "https://not-example.com/s.js"));
})
