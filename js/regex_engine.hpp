#pragma once
#include "../core/utility.hpp"

#include <memory>
#include <string>
#include <vector>

namespace browser::js {

    // A small backtracking regular-expression engine covering the subset of
    // ECMAScript RegExp that real sites use: literals, classes, anchors,
    // quantifiers (greedy/lazy/bounded), groups, non-capturing groups,
    // alternation, backreferences, lookahead/lookbehind, and the i/g/m/s/y
    // flag semantics that affect matching (u is accepted and ignored).
    // Operates on UTF-8 strings; ASCII case folding for /i.

    struct RegexFlags {
        bool global = false;
        bool ignore_case = false;
        bool multiline = false;
        bool dot_all = false;
        bool unicode = false;
        bool sticky = false;
    };

    struct RegexMatch {
        static constexpr u32 NOPOS = 0xFFFFFFFFu;
        bool matched = false;
        u32 start = 0;  // byte offsets into the input
        u32 end = 0;
        // Capture groups 1..N as byte offsets; NOPOS when unmatched.
        std::vector<u32> cap_start;
        std::vector<u32> cap_end;
    };

    using cp_range = std::pair<char32_t, char32_t>;

    // One class member: a set of ranges, possibly complemented (\D, \W, \S).
    struct RegexClassItem {
        bool negate = false;
        std::vector<cp_range> ranges;
    };

    struct RegexClassSpec {
        bool negate = false;  // leading ^ on [...]
        std::vector<RegexClassItem> items;
    };

    struct RegexProg;

    struct RegexInst {
        enum class Op : u8 {
            CHAR, ANY, CLASS, MATCH, JMP, SPLIT, SAVE, BOL, EOL, WORDB, BACKREF, LOOK
        };
        Op op{};
        u32 x = 0, y = 0;
        char32_t cp = 0;
        std::shared_ptr<RegexClassSpec> cls;
        std::shared_ptr<RegexProg> sub;  // LOOK
        bool simple_loop = false;        // SPLIT: single-instruction loop
        bool lazy_loop = false;          // SPLIT: iterate shortest-first
    };

    struct RegexProg {
        std::vector<RegexInst> insts;
        u32 group_count = 0;
        u32 nslots = 2;
        RegexFlags flags;
        std::string source;
    };

    // Parses and compiles `source`. Returns nullptr and sets `err` on an
    // invalid pattern.
    std::unique_ptr<RegexProg> regex_compile(const std::string &source, const RegexFlags &flags, std::string &err);
    u32 regex_group_count(const RegexProg &prog);
    const std::string &regex_source(const RegexProg &prog);
    const RegexFlags &regex_flags(const RegexProg &prog);

    // Leftmost match starting at or after byte offset `from`.
    bool regex_search(const RegexProg &prog, const std::string &input, u32 from, RegexMatch &out);
    // Anchored attempt at exactly byte offset `at`.
    bool regex_try_match(const RegexProg &prog, const std::string &input, u32 at, RegexMatch &out);

}  // namespace browser::js
