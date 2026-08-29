#include "regex_engine.hpp"

#include "../html/utf8.hpp"

#include <cstring>

namespace browser::js {

namespace {

        constexpr u32 kMaxSteps = 1000000;    // backtracking budget per attempt
        constexpr size_t kMaxProgram = 65536; // instruction cap (bounds {n,m} expansion)
        constexpr u32 kNoCapture = 0xFFFFFFFFu;
        constexpr u32 kMaxDepth = 4000;

        using cp_range = std::pair<char32_t, char32_t>;

        using ClassItem = RegexClassItem;
        using ClassSpec = RegexClassSpec;
        using Inst = RegexInst;
        using Op = RegexInst::Op;
        using Prog = RegexProg;


    }  // namespace


    namespace {

        Inst make_split() {
            Inst i;
            i.op = Op::SPLIT;
            return i;
        }
        Inst make_jmp(u32 x) {
            Inst i;
            i.op = Op::JMP;
            i.x = x;
            return i;
        }


        bool is_word_cp(char32_t cp) {
            return (cp >= U'a' && cp <= U'z') || (cp >= U'A' && cp <= U'Z') || (cp >= U'0' && cp <= U'9') ||
                   cp == U'_';
        }

        bool is_line_terminator(char32_t cp) {
            return cp == U'\n' || cp == U'\r' || cp == U'\u2028' || cp == U'\u2029';
        }

        char32_t fold_cp(char32_t cp) {
            if (cp >= U'A' && cp <= U'Z')
                return cp + 32;
            if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7)
                return cp + 32;
            return cp;
        }

        bool char_eq(char32_t a, char32_t b, bool ignore_case) {
            if (a == b)
                return true;
            return ignore_case && fold_cp(a) == fold_cp(b);
        }

        bool range_hit(const std::vector<cp_range> &ranges, char32_t c) {
            for (const auto &r : ranges) {
                if (c >= r.first && c <= r.second)
                    return true;
            }
            return false;
        }

        bool class_matches(const ClassSpec &cls, char32_t cp, bool ignore_case) {
            auto in_item = [&](const ClassItem &item, char32_t c) {
                bool hit = range_hit(item.ranges, c);
                if (!hit && ignore_case) {
                    char32_t f = fold_cp(c);
                    if (f != c)
                        hit = range_hit(item.ranges, f);
                    else if (c >= U'a' && c <= U'z')
                        hit = range_hit(item.ranges, c - 32);
                    else if (c >= 0xE0 && c <= 0xFE && c != 0xF7)
                        hit = range_hit(item.ranges, c - 32);
                }
                return hit != item.negate;
            };
            bool hit = false;
            for (const auto &item : cls.items) {
                if (in_item(item, cp)) {
                    hit = true;
                    break;
                }
            }
            return cls.negate ? !hit : hit;
        }

        char32_t decode_at(const std::string &s, size_t &pos) {
            if (pos >= s.size())
                return 0;
            auto len = s.size() - pos;
            auto dr = browser::html::decode_utf8(reinterpret_cast<const u8 *>(s.data()) + pos, len);
            if (dr.bytes_consumed == 0) {
                pos += 1;
                return 0xFFFD;
            }
            pos += dr.bytes_consumed;
            return static_cast<char32_t>(dr.codepoint);
        }

        char32_t decode_at(const std::string &s, u32 &pos) {
            size_t p = pos;
            char32_t c = decode_at(s, p);
            pos = static_cast<u32>(p);
            return c;
        }

        u32 cp_len_at(const std::string &s, u32 pos) {
            if (pos >= s.size())
                return 0;
            size_t p = pos;
            decode_at(s, p);
            return static_cast<u32>(p - pos);
        }


        // ------------------------------------------------------------------
        // Parser -> AST
        // ------------------------------------------------------------------
        struct AstNode;
        using AstP = std::unique_ptr<AstNode>;

        struct AstNode {
            enum Kind { EMPTY, CHAR, ANY, CLASS, ALT, CAT, REPEAT, GROUP, BACKREF, LOOK, BOL, EOL, WORDB } kind;
            char32_t cp = 0;
            std::shared_ptr<ClassSpec> cls;
            std::vector<AstP> kids;
            AstP child;  // REPEAT / GROUP body
            u32 lo = 0, hi = 0;
            bool lazy = false;
            u32 group = 0;
            AstP look;
            bool look_neg = false;
            bool look_behind = false;
            u32 backref = 0;
            bool word_b = false;
        };

        std::shared_ptr<ClassSpec> shorthand_class(char e) {
            auto cls = std::make_shared<ClassSpec>();
            ClassItem item;
            switch (e) {
                case 'd': item.ranges.push_back({'0', '9'}); break;
                case 'w':
                    item.ranges.push_back({'a', 'z'});
                    item.ranges.push_back({'A', 'Z'});
                    item.ranges.push_back({'0', '9'});
                    item.ranges.push_back({'_', '_'});
                    break;
                case 's':
                    item.ranges.push_back({' ', ' '});
                    item.ranges.push_back({'\t', '\r'});
                    item.ranges.push_back({0x00A0, 0x00A0});
                    break;
                case 'D': item.negate = true; item.ranges.push_back({'0', '9'}); break;
                case 'W':
                    item.negate = true;
                    item.ranges.push_back({'a', 'z'});
                    item.ranges.push_back({'A', 'Z'});
                    item.ranges.push_back({'0', '9'});
                    item.ranges.push_back({'_', '_'});
                    break;
                case 'S':
                    item.negate = true;
                    item.ranges.push_back({' ', ' '});
                    item.ranges.push_back({'\t', '\r'});
                    item.ranges.push_back({0x00A0, 0x00A0});
                    break;
                default: break;
            }
            cls->items.push_back(std::move(item));
            return cls;
        }

        struct Parser {
            const std::string &src;
            size_t pos = 0;
            u32 ngroups = 0;
            std::string err;

            explicit Parser(const std::string &s) : src(s) {}

            bool eof() const { return pos >= src.size(); }
            char peek() const { return eof() ? '\0' : src[pos]; }

            AstP parse_alt() {
                auto node = std::make_unique<AstNode>();
                node->kind = AstNode::Kind::ALT;
                for (;;) {
                    auto branch = parse_concat();
                    if (!branch)
                        return nullptr;
                    node->kids.push_back(std::move(branch));
                    if (peek() == '|') {
                        pos++;
                        continue;
                    }
                    break;
                }
                if (node->kids.size() == 1)
                    return std::move(node->kids[0]);
                return node;
            }

            AstP parse_concat() {
                auto node = std::make_unique<AstNode>();
                node->kind = AstNode::Kind::CAT;
                for (;;) {
                    char c = peek();
                    if (eof() || c == '|' || c == ')')
                        break;
                    auto piece = parse_repeat();
                    if (!piece)
                        return nullptr;
                    node->kids.push_back(std::move(piece));
                }
                if (node->kids.empty()) {
                    node->kind = AstNode::Kind::EMPTY;
                    return node;
                }
                if (node->kids.size() == 1)
                    return std::move(node->kids[0]);
                return node;
            }

            AstP parse_repeat() {
                auto atom = parse_atom();
                if (!atom)
                    return nullptr;
                char c = peek();
                u32 lo = 0, hi = 0;
                bool has_quant = false;
                if (c == '*') {
                    lo = 0, hi = UINT32_MAX, has_quant = true;
                    pos++;
                } else if (c == '+') {
                    lo = 1, hi = UINT32_MAX, has_quant = true;
                    pos++;
                } else if (c == '?') {
                    lo = 0, hi = 1, has_quant = true;
                    pos++;
                } else if (c == '{') {
                    size_t save = pos;
                    pos++;
                    u32 n = 0;
                    bool any = false;
                    while (isdigit(static_cast<unsigned char>(peek()))) {
                        n = n * 10 + (peek() - '0');
                        pos++;
                        any = true;
                    }
                    if (!any) {
                        pos = save;
                        return atom;
                    }
                    lo = n;
                    hi = n;
                    if (peek() == ',') {
                        pos++;
                        if (isdigit(static_cast<unsigned char>(peek()))) {
                            u32 m = 0;
                            while (isdigit(static_cast<unsigned char>(peek()))) {
                                m = m * 10 + (peek() - '0');
                                pos++;
                            }
                            hi = m;
                        } else {
                            hi = UINT32_MAX;
                        }
                    }
                    if (peek() != '}') {
                        pos = save;
                        return atom;
                    }
                    pos++;
                    has_quant = true;
                }
                if (!has_quant)
                    return atom;
                if (atom->kind == AstNode::Kind::BOL || atom->kind == AstNode::Kind::EOL)
                    return atom;
                bool lazy = false;
                if (peek() == '?') {
                    lazy = true;
                    pos++;
                }
                auto rep = std::make_unique<AstNode>();
                rep->kind = AstNode::Kind::REPEAT;
                rep->child = std::move(atom);
                rep->lo = lo;
                rep->hi = hi;
                rep->lazy = lazy;
                return rep;
            }

            char32_t parse_class_escape(char e, bool &ok) {
                ok = true;
                switch (e) {
                    case 'n': return U'\n';
                    case 't': return U'\t';
                    case 'r': return U'\r';
                    case 'f': return U'\f';
                    case 'v': return U'\v';
                    case '0': return U'\0';
                    case 'b': return U'\b';
                    default: return static_cast<char32_t>(static_cast<unsigned char>(e));
                }
            }

            bool parse_hex(u32 digits, u32 &out) {
                u32 v = 0;
                for (int i = 0; i < static_cast<int>(digits) && pos < src.size() &&
                                isxdigit(static_cast<unsigned char>(src[pos]));
                     i++) {
                    char c = src[pos++];
                    v = v * 16 + (c <= '9' ? c - '0' : (c <= 'F' ? c - 'A' : c - 'a') + 10);
                }
                out = v;
                return true;
            }

            bool parse_class_spec(ClassSpec &out) {
                out.negate = false;
                if (peek() == '^') {
                    out.negate = true;
                    pos++;
                }
                while (!eof() && peek() != ']') {
                    char32_t lo = 0;
                    if (peek() == '\\' && pos + 1 < src.size()) {
                        pos++;
                        char e = src[pos++];
                        if (e == 'd' || e == 'D' || e == 'w' || e == 'W' || e == 's' || e == 'S') {
                            // \d etc. inside a class: attach as its own item so
                            // negated shorthands complement correctly.
                            auto inner = shorthand_class(e);
                            out.items.push_back(std::move(inner->items[0]));
                            continue;
                        }
                        if (e == 'x' || e == 'u') {
                            u32 v = 0;
                            parse_hex(e == 'x' ? 2 : 4, v);
                            lo = v;
                        } else {
                            bool ok = true;
                            lo = parse_class_escape(e, ok);
                            if (!ok)
                                return false;
                        }
                    } else {
                        lo = decode_at(src, pos);
                    }
                    char32_t hi = lo;
                    if (peek() == '-' && pos + 1 < src.size() && src[pos + 1] != ']') {
                        pos++;
                        if (peek() == '\\' && pos + 1 < src.size()) {
                            pos++;
                            char e = src[pos++];
                            if (e == 'x' || e == 'u') {
                                u32 v = 0;
                                parse_hex(e == 'x' ? 2 : 4, v);
                                hi = v;
                            } else {
                                bool ok = true;
                                hi = parse_class_escape(e, ok);
                                if (!ok)
                                    return false;
                            }
                        } else {
                            hi = decode_at(src, pos);
                        }
                    }
                    if (hi < lo)
                        hi = lo;
                    ClassItem item;
                    item.ranges.push_back({lo, hi});
                    out.items.push_back(std::move(item));
                }
                if (eof())
                    return false;
                pos++;
                return true;
            }

            AstP parse_atom() {
                char c = peek();
                auto node = std::make_unique<AstNode>();
                if (c == '^') {
                    pos++;
                    node->kind = AstNode::Kind::BOL;
                    return node;
                }
                if (c == '$') {
                    pos++;
                    node->kind = AstNode::Kind::EOL;
                    return node;
                }
                if (c == '.') {
                    pos++;
                    node->kind = AstNode::Kind::ANY;
                    return node;
                }
                if (c == '(') {
                    pos++;
                    node->kind = AstNode::Kind::GROUP;
                    if (peek() == '?') {
                        pos++;
                        char k = peek();
                        if (k == ':') {
                            pos++;
                        } else if (k == '=' || k == '!') {
                            pos++;
                            node->kind = AstNode::Kind::LOOK;
                            node->look_neg = (k == '!');
                            node->look_behind = false;
                        } else if (k == '<') {
                            pos++;
                            char k2 = peek();
                            if (k2 != '=' && k2 != '!') {
                                err = "named groups unsupported";
                                return nullptr;
                            }
                            pos++;
                            node->kind = AstNode::Kind::LOOK;
                            node->look_neg = (k2 == '!');
                            node->look_behind = true;
                        } else {
                            err = "invalid group";
                            return nullptr;
                        }
                    } else {
                        node->group = ++ngroups;
                    }
                    auto body = parse_alt();
                    if (!body)
                        return nullptr;
                    if (peek() != ')') {
                        err = "missing ')'";
                        return nullptr;
                    }
                    pos++;
                    if (node->kind == AstNode::Kind::LOOK)
                        node->look = std::move(body);
                    else
                        node->child = std::move(body);
                    return node;
                }
                if (c == '[') {
                    pos++;
                    node->kind = AstNode::Kind::CLASS;
                    auto cls = std::make_shared<ClassSpec>();
                    if (!parse_class_spec(*cls)) {
                        err = err.empty() ? "missing ']'" : err;
                        return nullptr;
                    }
                    node->cls = std::move(cls);
                    return node;
                }
                if (c == '\\') {
                    pos++;
                    if (eof()) {
                        err = "trailing backslash";
                        return nullptr;
                    }
                    char e = src[pos++];
                    if (e == 'd' || e == 'D' || e == 'w' || e == 'W' || e == 's' || e == 'S') {
                        node->kind = AstNode::Kind::CLASS;
                        node->cls = shorthand_class(e);
                        return node;
                    }
                    switch (e) {
                        case 'b': node->kind = AstNode::Kind::WORDB; node->word_b = true; return node;
                        case 'B': node->kind = AstNode::Kind::WORDB; node->word_b = false; return node;
                        case '1': case '2': case '3': case '4': case '5':
                        case '6': case '7': case '8': case '9': {
                            u32 n = e - '0';
                            while (isdigit(static_cast<unsigned char>(peek())))
                                n = n * 10 + (peek() - '0');
                            if (n > ngroups) {
                                node->kind = AstNode::Kind::CHAR;
                                node->cp = e;
                                return node;
                            }
                            node->kind = AstNode::Kind::BACKREF;
                            node->backref = n;
                            return node;
                        }
                        case 'x': {
                            u32 v = 0;
                            parse_hex(2, v);
                            node->kind = AstNode::Kind::CHAR;
                            node->cp = v;
                            return node;
                        }
                        case 'u': {
                            u32 v = 0;
                            parse_hex(4, v);
                            node->kind = AstNode::Kind::CHAR;
                            node->cp = v;
                            return node;
                        }
                        case 'c': {
                            if (pos >= src.size()) {
                                err = "invalid \\c";
                                return nullptr;
                            }
                            char l = src[pos++];
                            char up = (l >= 'a' && l <= 'z') ? static_cast<char>(l - 32) : l;
                            node->kind = AstNode::Kind::CHAR;
                            node->cp = static_cast<char32_t>(up - '@');
                            return node;
                        }
                        default: {
                            bool ok = true;
                            char32_t cp = parse_class_escape(e, ok);
                            node->kind = AstNode::Kind::CHAR;
                            node->cp = cp;
                            return node;
                        }
                    }
                }
                node->kind = AstNode::Kind::CHAR;
                node->cp = decode_at(src, pos);
                return node;
            }
        };

        // ------------------------------------------------------------------
        // Compiler: AST -> instructions
        // ------------------------------------------------------------------
        struct Compiler {
            std::vector<Inst> &out;
            u32 total_groups = 0;

            u32 emit(Inst i) {
                out.push_back(std::move(i));
                return static_cast<u32>(out.size() - 1);
            }
            u32 here() const { return static_cast<u32>(out.size()); }
            bool over_budget() const { return out.size() > kMaxProgram; }

            static bool is_single_consuming(const AstNode &n) {
                return n.kind == AstNode::Kind::CHAR || n.kind == AstNode::Kind::ANY ||
                       n.kind == AstNode::Kind::CLASS;
            }

            void compile(const AstNode &n) {
                if (over_budget())
                    return;
                switch (n.kind) {
                    case AstNode::Kind::EMPTY: break;
                    case AstNode::Kind::CHAR: {
                        Inst i;
                        i.op = Op::CHAR;
                        i.cp = n.cp;
                        emit(i);
                        break;
                    }
                    case AstNode::Kind::ANY: {
                        Inst i;
                        i.op = Op::ANY;
                        emit(i);
                        break;
                    }
                    case AstNode::Kind::CLASS: {
                        Inst i;
                        i.op = Op::CLASS;
                        i.cls = n.cls;
                        emit(i);
                        break;
                    }
                    case AstNode::Kind::BOL: {
                        Inst i;
                        i.op = Op::BOL;
                        emit(i);
                        break;
                    }
                    case AstNode::Kind::EOL: {
                        Inst i;
                        i.op = Op::EOL;
                        emit(i);
                        break;
                    }
                    case AstNode::Kind::WORDB: {
                        Inst i;
                        i.op = Op::WORDB;
                        i.y = n.word_b ? 1 : 0;
                        emit(i);
                        break;
                    }
                    case AstNode::Kind::BACKREF: {
                        Inst i;
                        i.op = Op::BACKREF;
                        i.x = n.backref;
                        emit(i);
                        break;
                    }
                    case AstNode::Kind::CAT:
                        for (const auto &k : n.kids)
                            compile(*k);
                        break;
                    case AstNode::Kind::ALT: {
                        std::vector<u32> jumps;
                        for (size_t k = 0; k + 1 < n.kids.size(); k++) {
                            Inst sp;
                            sp.op = Op::SPLIT;
                            u32 sp_idx = emit(sp);
                            u32 br = here();
                            compile(*n.kids[k]);
                            jumps.push_back(emit(make_jmp(0)));
                            u32 next = here();
                            out[sp_idx].x = br;
                            out[sp_idx].y = next;
                        }
                        compile(*n.kids.back());
                        u32 end = here();
                        for (auto j : jumps) out[j].x = end;
                        break;
                    }
                    case AstNode::Kind::GROUP: {
                        if (n.group > 0) {
                            Inst s;
                            s.op = Op::SAVE;
                            s.x = n.group * 2;
                            emit(s);
                        }
                        compile(*n.child);
                        if (n.group > 0) {
                            Inst e;
                            e.op = Op::SAVE;
                            e.x = n.group * 2 + 1;
                            emit(e);
                        }
                        break;
                    }
                    case AstNode::Kind::LOOK: {
                        Inst i;
                        i.op = Op::LOOK;
                        i.y = n.look_neg ? 0 : 1;
                        i.x = n.look_behind ? 1 : 0;
                        i.sub = std::make_shared<RegexProg>();
                        i.sub->nslots = 2 * (total_groups + 1);
                        Compiler sub{i.sub->insts, total_groups};
                        sub.compile(*n.look);
                        Inst m;
                        m.op = Op::MATCH;
                        sub.emit(m);
                        emit(i);
                        break;
                    }
                    case AstNode::Kind::REPEAT:
                        compile_repeat(n);
                        break;
                }
            }

            void compile_repeat(const AstNode &n) {
                if (n.lo == 0 && n.hi == 1) {
                    u32 sp = emit(make_split());
                    u32 body = here();
                    compile(*n.child);
                    u32 exit = here();
                    if (n.lazy) {
                        out[sp].x = exit;
                        out[sp].y = body;
                    } else {
                        out[sp].x = body;
                        out[sp].y = exit;
                    }
                    return;
                }
                if (n.lo == 1 && n.hi == UINT32_MAX) {
                    // mandatory first iteration, then a loop
                    u32 body = here();
                    compile(*n.child);
                    u32 sp = emit(make_split());
                    u32 exit = here();
                    if (is_single_consuming(*n.child)) {
                        out[sp].x = body;
                        out[sp].y = exit;
                        out[sp].simple_loop = true;
                        out[sp].lazy_loop = n.lazy;
                    } else if (n.lazy) {
                        out[sp].x = exit;
                        out[sp].y = body;
                    } else {
                        out[sp].x = body;
                        out[sp].y = exit;
                    }
                    return;
                }
                if (n.lo == 0 && n.hi == UINT32_MAX) {
                    u32 sp = emit(make_split());
                    u32 body = here();
                    compile(*n.child);
                    u32 exit = here();
                    if (is_single_consuming(*n.child)) {
                        out[sp].x = body;
                        out[sp].y = exit;
                        out[sp].simple_loop = true;
                        out[sp].lazy_loop = n.lazy;
                    } else {
                        emit(make_jmp(sp));
                        if (n.lazy) {
                            out[sp].x = exit;
                            out[sp].y = body;
                        } else {
                            out[sp].x = body;
                            out[sp].y = exit;
                        }
                    }
                    return;
                }
                for (u32 i = 0; i < n.lo && !over_budget(); i++)
                    compile(*n.child);
                if (n.hi != UINT32_MAX) {
                    std::vector<u32> splits;
                    for (u32 i = 0; i < n.hi - n.lo && !over_budget(); i++) {
                        u32 sp = emit(make_split());
                        splits.push_back(sp);
                        u32 body = here();
                        compile(*n.child);
                        if (n.lazy) {
                            out[sp].x = UINT32_MAX;  // patched to end below
                            out[sp].y = body;
                        } else {
                            out[sp].x = body;
                            out[sp].y = UINT32_MAX;
                        }
                    }
                    u32 end = here();
                    for (auto sp : splits) {
                        if (out[sp].y == UINT32_MAX)
                            out[sp].y = end;
                        else
                            out[sp].x = end;
                    }
                } else {
                    // {n,}: n copies then a loop
                    u32 sp = emit(make_split());
                    u32 body = here();
                    compile(*n.child);
                    u32 exit = here();
                    if (is_single_consuming(*n.child)) {
                        out[sp].x = body;
                        out[sp].y = exit;
                        out[sp].simple_loop = true;
                        out[sp].lazy_loop = n.lazy;
                    } else {
                        emit(make_jmp(sp));
                        if (n.lazy) {
                            out[sp].x = exit;
                            out[sp].y = body;
                        } else {
                            out[sp].x = body;
                            out[sp].y = exit;
                        }
                    }
                }
            }
        };

        // ------------------------------------------------------------------
        // Backtracking matcher
        // ------------------------------------------------------------------
        struct Frame {
            u32 pc, sp;
            std::vector<u32> caps;
        };

        bool consumes_at(const Inst &inst, const std::string &input, u32 pos, u32 &next, bool icase, bool dot_all) {
            if (pos >= input.size())
                return false;
            u32 p = pos;
            char32_t c = decode_at(input, p);
            switch (inst.op) {
                case Op::CHAR:
                    if (!char_eq(c, inst.cp, icase))
                        return false;
                    break;
                case Op::ANY:
                    if (!dot_all && is_line_terminator(c))
                        return false;
                    break;
                case Op::CLASS:
                    if (!class_matches(*inst.cls, c, icase))
                        return false;
                    break;
                default:
                    return false;
            }
            next = p;
            return true;
        }

        struct Exec {
            const RegexProg &prog;
            const std::string &input;
            u32 input_size;
            bool icase, multiline, dot_all;
            u32 steps = 0;
            bool budget_hit = false;

            bool run_look(RegexProg &sub, u32 sp, bool positive, bool behind) {
                if (behind) {
                    // Try starts from sp backwards; a match must end at sp.
                    for (u32 s = sp;;) {
                        Exec se{sub, input, input_size, icase, multiline, dot_all, 0, false};
                        std::vector<u32> caps(sub.nslots, kNoCapture);
                        if (sub.nslots >= 2)
                            caps[0] = s;
                        RegexMatch tmp;
                        tmp.cap_start.assign(sub.nslots / 2, kNoCapture);
                        tmp.cap_end.assign(sub.nslots / 2, kNoCapture);
                        bool ok = se.step(0, s, caps, tmp, 0);
                        steps += se.steps;
                        if (se.budget_hit)
                            budget_hit = true;
                        if (ok && tmp.end == sp)
                            return positive;
                        if (budget_hit)
                            return false;
                        if (s == 0)
                            break;
                        s -= cp_len_at(input, s - 1);
                        if (steps > kMaxSteps) {
                            budget_hit = true;
                            return false;
                        }
                    }
                    return !positive;
                }
                // Lookahead: sub-program anchored at sp.
                Exec se{sub, input, input_size, icase, multiline, dot_all, 0, false};
                std::vector<u32> caps(sub.nslots, kNoCapture);
                if (sub.nslots >= 2)
                    caps[0] = sp;
                RegexMatch tmp;
                tmp.cap_start.assign(sub.nslots / 2, kNoCapture);
                tmp.cap_end.assign(sub.nslots / 2, kNoCapture);
                bool ok = se.step(0, sp, caps, tmp, 0);
                steps += se.steps;
                if (se.budget_hit)
                    budget_hit = true;
                return ok == positive;
            }

            bool step(u32 pc, u32 sp, std::vector<u32> &caps, RegexMatch &out, u32 depth) {
                if (depth > kMaxDepth) {
                    budget_hit = true;
                    return false;
                }
                for (;;) {
                    if (++steps > kMaxSteps) {
                        budget_hit = true;
                        return false;
                    }
                    if (pc >= prog.insts.size())
                        return false;
                    const Inst &inst = prog.insts[pc];
                    switch (inst.op) {
                        case Op::MATCH: {
                            out.matched = true;
                            out.end = sp;
                            if (caps.size() >= 2) {
                                out.cap_start[0] = caps[0];
                                out.cap_end[0] = sp;
                            }
                            for (size_t g = 1; g < out.cap_start.size() && 2 * g + 1 < caps.size(); g++) {
                                if (caps[2 * g] != kNoCapture && caps[2 * g + 1] != kNoCapture) {
                                    out.cap_start[g] = caps[2 * g];
                                    out.cap_end[g] = caps[2 * g + 1];
                                }
                            }
                            return true;
                        }
                        case Op::CHAR: {
                            u32 next = 0;
                            if (!consumes_at(inst, input, sp, next, icase, dot_all))
                                return false;
                            sp = next;
                            pc++;
                            break;
                        }
                        case Op::ANY: {
                            u32 next = 0;
                            if (!consumes_at(inst, input, sp, next, icase, dot_all))
                                return false;
                            sp = next;
                            pc++;
                            break;
                        }
                        case Op::CLASS: {
                            u32 next = 0;
                            if (!consumes_at(inst, input, sp, next, icase, dot_all))
                                return false;
                            sp = next;
                            pc++;
                            break;
                        }
                        case Op::JMP:
                            pc = inst.x;
                            break;
                        case Op::SPLIT: {
                            if (inst.simple_loop) {
                                // Iterate the single consuming instruction at x
                                // without recursing per character.
                                const Inst &body = prog.insts[inst.x];
                                u32 end_pos = sp;
                                while (end_pos < input_size) {
                                    u32 next = 0;
                                    if (!consumes_at(body, input, end_pos, next, icase, dot_all))
                                        break;
                                    end_pos = next;
                                    if (++steps > kMaxSteps) {
                                        budget_hit = true;
                                        return false;
                                    }
                                }
                                // greedy: longest first; lazy: shortest first
                                if (out.cap_start.empty()) {
                                    return false;
                                }
                                if (inst.lazy_loop) {
                                    // Try 0 reps first, then 1, 2, ...
                                    for (u32 q = sp;;) {
                                        std::vector<u32> cc = caps;
                                        if (step(inst.y, q, cc, out, depth + 1))
                                            return true;
                                        if (budget_hit || q >= end_pos)
                                            break;
                                        u32 fwd = q;
                                        decode_at(input, fwd);
                                        q = fwd;
                                    }
                                } else {
                                    u32 q = end_pos;
                                    for (;;) {
                                        std::vector<u32> cc = caps;
                                        if (step(inst.y, q, cc, out, depth + 1))
                                            return true;
                                        if (budget_hit || q == sp)
                                            break;
                                        // step back one codepoint
                                        u32 back = q - 1;
                                        while (back > sp && (static_cast<u8>(input[back]) & 0xC0) == 0x80) back--;
                                        q = back;
                                    }
                                }
                                return false;
                            }
                            std::vector<u32> xcaps = caps;
                            if (step(inst.x, sp, xcaps, out, depth + 1))
                                return true;
                            if (budget_hit)
                                return false;
                            pc = inst.y;
                            break;
                        }
                        case Op::SAVE:
                            if (inst.x < caps.size())
                                caps[inst.x] = sp;
                            pc++;
                            break;
                        case Op::BOL: {
                            bool at_start = (sp == 0) || (multiline && is_line_terminator(prev_cp(sp)));
                            if (!at_start)
                                return false;
                            pc++;
                            break;
                        }
                        case Op::EOL: {
                            bool at_end = (sp >= input_size) ||
                                          (multiline && is_line_terminator(peek_cp(sp)));
                            if (!at_end)
                                return false;
                            pc++;
                            break;
                        }
                        case Op::WORDB: {
                            bool before = sp > 0 && is_word_cp(prev_cp(sp));
                            bool after = sp < input_size && is_word_cp(peek_cp(sp));
                            if ((before != after) != (inst.y == 1))
                                return false;
                            pc++;
                            break;
                        }
                        case Op::BACKREF: {
                            u32 g = inst.x;
                            if (2 * g + 1 >= caps.size())
                                return false;
                            u32 s = caps[2 * g], e = caps[2 * g + 1];
                            if (s == kNoCapture || e == kNoCapture) {
                                pc++;  // unmatched group matches empty
                                break;
                            }
                            if (e < s || e > input_size)
                                return false;
                            u32 len = e - s;
                            if (sp + len > input_size)
                                return false;
                            if (icase) {
                                u32 p1 = s, p2 = sp;
                                while (p1 < e) {
                                    char32_t c1 = decode_at(input, p1);
                                    char32_t c2 = decode_at(input, p2);
                                    if (!char_eq(c1, c2, true))
                                        return false;
                                }
                            } else if (std::memcmp(input.data() + sp, input.data() + s, len) != 0) {
                                return false;
                            }
                            sp += len;
                            pc++;
                            break;
                        }
                        case Op::LOOK: {
                            bool ok = run_look(*inst.sub, sp, inst.y == 1, inst.x == 1);
                            if (budget_hit)
                                return false;
                            if (!ok)
                                return false;
                            pc++;
                            break;
                        }
                        default:
                            return false;
                    }
                }
            }

            char32_t prev_cp(u32 sp) {
                if (sp == 0)
                    return 0;
                u32 p = sp - 1;
                for (int k = 0; k < 4 && p > 0; k++) {
                    u8 b = static_cast<u8>(input[p]);
                    if ((b & 0xC0) != 0x80) {
                        u32 q = p;
                        return decode_at(input, q);
                    }
                    p--;
                }
                u32 q = p;
                return decode_at(input, q);
            }

            char32_t peek_cp(u32 sp) {
                u32 q = sp;
                return decode_at(input, q);
            }
        };

    }  // namespace

    std::unique_ptr<RegexProg> regex_compile(const std::string &source, const RegexFlags &flags, std::string &err) {
        auto prog = std::make_unique<RegexProg>();
        prog->source = source;
        prog->flags = flags;
        Parser p(source);
        auto ast = p.parse_alt();
        if (!ast) {
            err = p.err.empty() ? "invalid pattern" : p.err;
            return nullptr;
        }
        prog->group_count = p.ngroups;
        prog->nslots = 2 * (p.ngroups + 1);
        Compiler c{prog->insts, p.ngroups};
        c.compile(*ast);
        Inst m;
        m.op = Op::MATCH;
        c.emit(m);
        return prog;
    }

    u32 regex_group_count(const RegexProg &prog) {
        return prog.group_count;
    }

    const std::string &regex_source(const RegexProg &prog) {
        return prog.source;
    }

    const RegexFlags &regex_flags(const RegexProg &prog) {
        return prog.flags;
    }

    bool regex_try_match(const RegexProg &prog, const std::string &input, u32 at, RegexMatch &out) {
        out = RegexMatch{};
        out.start = at;
        out.cap_start.assign(prog.nslots / 2, kNoCapture);
        out.cap_end.assign(prog.nslots / 2, kNoCapture);
        Exec e{prog, input, static_cast<u32>(input.size()), prog.flags.ignore_case, prog.flags.multiline,
               prog.flags.dot_all, 0, false};
        std::vector<u32> caps(prog.nslots, kNoCapture);
        caps[0] = at;
        bool ok = e.step(0, at, caps, out, 0);
        if (ok && out.end < out.start)
            out.end = out.start;
        return ok;
    }

    bool regex_search(const RegexProg &prog, const std::string &input, u32 from, RegexMatch &out) {
        u32 size = static_cast<u32>(input.size());
        if (from > size)
            from = size;
        bool bol_anchored = !prog.insts.empty() && prog.insts[0].op == Op::BOL && !prog.flags.multiline;
        u32 limit = prog.flags.sticky ? from : size;
        for (u32 at = from;;) {
            if (regex_try_match(prog, input, at, out) && out.matched)
                return true;
            if (bol_anchored || at >= limit)
                break;
            at += cp_len_at(input, at);
        }
        return false;
    }

}  // namespace browser::js
