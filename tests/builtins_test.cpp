// End-to-end tests for JS builtin behavior that was previously broken:
// Date instance state, Promise chaining/statics, Error subclass constructors,
// function-object statics, and global URI functions.
#include "../js/ast.hpp"
#include "../js/compiler.hpp"
#include "../js/gc.hpp"
#include "../js/parser.hpp"
#include "../js/vm.hpp"
#include "test_framework.hpp"
#include "utility.hpp"

using namespace browser::js;

namespace {
    struct BuiltinFixture {
        std::unique_ptr<VM> vm;

        BuiltinFixture() {
            vm = std::make_unique<VM>();
            vm->register_builtins();
        }

        JSValue eval(const std::string &source) {
            Parser parser(source);
            auto program = parser.parse_program();
            if (!parser.errors().empty())
                return JSValue::undefined();
            Compiler compiler;
            auto bc = compiler.compile(*program);
            if (!bc)
                return JSValue::undefined();
            return vm->execute(bc.get());
        }

        // Evaluates `source` and returns a global variable's value.
        JSValue eval_global(const std::string &source, const char *global_name) {
            eval(source);
            return vm->global_object()->get(global_name);
        }
    };
}  // namespace

// ---------------------------------------------------------------------------
// Date
// ---------------------------------------------------------------------------

TEST(date_epoch_zero, {
    BuiltinFixture f;
    JSValue t = f.eval("new Date(0).getTime();");
    ASSERT(t.type == JSValue::Type::NUMBER);
    ASSERT_EQ(t.number_val, 0.0);
})

TEST(date_epoch_iso_string, {
    BuiltinFixture f;
    JSValue s = f.eval("new Date(0).toISOString();");
    ASSERT(s.type == JSValue::Type::STRING);
    ASSERT(s.string_val == "1970-01-01T00:00:00.000Z");
})

TEST(date_known_timestamp_fields, {
    BuiltinFixture f;
    // 2020-01-02T03:04:05Z = 1577934245000 ms
    JSValue y = f.eval("new Date(1577934245000).getFullYear();");
    ASSERT(y.type == JSValue::Type::NUMBER);
    ASSERT_EQ(y.number_val, 2020.0);
    JSValue mo = f.eval("new Date(1577934245000).getMonth();");
    ASSERT_EQ(mo.number_val, 0.0);  // January
    JSValue d = f.eval("new Date(1577934245000).getDate();");
    ASSERT_EQ(d.number_val, 2.0);
    JSValue h = f.eval("new Date(1577934245000).getHours();");
    ASSERT_EQ(h.number_val, 3.0);
})

TEST(date_constructor_components_utc, {
    BuiltinFixture f;
    // new Date(2020, 0, 2, 3, 4, 5) -> 2020-01-02T03:04:05Z
    JSValue t = f.eval("new Date(2020, 0, 2, 3, 4, 5).getTime();");
    ASSERT(t.type == JSValue::Type::NUMBER);
    ASSERT(t.number_val == 1577934245000.0 || t.number_val == 1577934245001.0);
})

TEST(date_parse_iso_matches_ctor, {
    BuiltinFixture f;
    JSValue parsed = f.eval("Date.parse('2020-01-02');");  // midnight UTC
    ASSERT(parsed.type == JSValue::Type::NUMBER);
    JSValue direct = f.eval("new Date(2020, 0, 2).getTime();");
    ASSERT(parsed.number_val == direct.number_val);
})

TEST(date_now_is_epoch_ms_number, {
    BuiltinFixture f;
    JSValue now = f.eval("Date.now();");
    ASSERT(now.type == JSValue::Type::NUMBER);
    // Any plausible current epoch (post-2020, pre-2100)
    ASSERT(now.number_val > 1577836800000.0);
    ASSERT(now.number_val < 4102444800000.0);
})

TEST(date_instance_not_stuck_at_current_time, {
    BuiltinFixture f;
    // Regression: getters used to ignore the stored time value entirely.
    JSValue diff = f.eval("Math.abs(new Date(86400000).getTime() - Date.now());");
    ASSERT(diff.number_val > 1000000000.0);
})

TEST(date_to_utc_string_format, {
    BuiltinFixture f;
    JSValue s = f.eval("new Date(0).toUTCString();");
    ASSERT(s.type == JSValue::Type::STRING);
    ASSERT(s.string_val == "Thu, 01 Jan 1970 00:00:00 GMT");
})

// ---------------------------------------------------------------------------
// Error constructors
// ---------------------------------------------------------------------------

TEST(error_new_typeerror_message_and_name, {
    BuiltinFixture f;
    JSValue m = f.eval("var e = new TypeError('bad op'); e.message;");
    ASSERT(m.type == JSValue::Type::STRING);
    ASSERT(m.string_val == "bad op");
    JSValue n = f.eval("e.name;");
    ASSERT(n.string_val == "TypeError");
})

TEST(error_instanceof_chain, {
    BuiltinFixture f;
    // Regression: TypeError and friends used to be plain objects — `new` and
    // instanceof were both broken.
    JSValue r = f.eval("var e = new RangeError('r'); e instanceof RangeError && e instanceof Error;");
    ASSERT(r.bool_val);
})

TEST(error_throw_catch_subclass, {
    BuiltinFixture f;
    JSValue out = f.eval_global(
        "var caught = '';\n"
        "try { throw new ReferenceError('nope'); }\n"
        "catch (err) { caught = err.name + ':' + err.message; }",
        "caught");
    ASSERT(out.string_val == "ReferenceError:nope");
})

TEST(error_to_string_format, {
    BuiltinFixture f;
    JSValue s = f.eval("String(new SyntaxError('parse fail'));");
    ASSERT(s.type == JSValue::Type::STRING);
    ASSERT(s.string_val == "SyntaxError: parse fail");
})

// ---------------------------------------------------------------------------
// Promise
// ---------------------------------------------------------------------------

TEST(promise_resolve_then_chain_transforms_value, {
    BuiltinFixture f;
    // Regression: .then() returned a promise that was never resolved, so any
    // chain longer than one link silently dropped its result.
    JSValue out = f.eval_global(
        "var out = 0;\n"
        "Promise.resolve(41).then(function(v) { return v + 1; })"
        ".then(function(v) { out = v; });",
        "out");
    ASSERT(out.type == JSValue::Type::NUMBER);
    ASSERT_EQ(out.number_val, 42.0);
})

TEST(promise_reject_caught_by_catch, {
    BuiltinFixture f;
    JSValue out = f.eval_global(
        "var msg = '';\n"
        "Promise.reject(new Error('boom')).then(function() { msg = 'wrong'; })"
        ".catch(function(e) { msg = e.message; });",
        "msg");
    ASSERT(out.string_val == "boom");
})

TEST(promise_pending_executor_resolves_later_in_same_tick, {
    BuiltinFixture f;
    JSValue out = f.eval_global(
        "var v = 0;\n"
        "new Promise(function(resolve) { resolve(7); }).then(function(x) { v = x; });",
        "v");
    ASSERT_EQ(out.number_val, 7.0);
})

TEST(promise_static_resolve_available_on_constructor, {
    BuiltinFixture f;
    JSValue r = f.eval("typeof Promise.resolve === 'function' && typeof Promise.reject === 'function';");
    ASSERT(r.bool_val);
})

TEST(promise_all_resolves_with_all_values, {
    BuiltinFixture f;
    JSValue sum = f.eval_global(
        "var sum = -1;\n"
        "Promise.all([Promise.resolve(10), Promise.resolve(20), Promise.resolve(32)])"
        ".then(function(vs) { sum = vs[0] + vs[1] + vs[2]; });",
        "sum");
    ASSERT(sum.type == JSValue::Type::NUMBER);
    ASSERT_EQ(sum.number_val, 62.0);
})

TEST(promise_all_rejects_on_first_rejection, {
    BuiltinFixture f;
    JSValue out = f.eval_global(
        "var reason = '';\n"
        "Promise.all([Promise.resolve(1), Promise.reject('bad')])"
        ".catch(function(e) { reason = String(e); });",
        "reason");
    ASSERT(out.string_val == "bad");
})

TEST(promise_race_first_settled_wins, {
    BuiltinFixture f;
    JSValue winner = f.eval_global(
        "var w = '';\n"
        "Promise.race([Promise.resolve('fast'), Promise.reject('slow')])"
        ".then(function(v) { w = 'ok:' + v; }, function(e) { w = 'err:' + e; });",
        "w");
    ASSERT(winner.string_val == "ok:fast");
})

TEST(promise_finally_runs_on_fulfillment, {
    BuiltinFixture f;
    JSValue out = f.eval_global(
        "var order = '';\n"
        "Promise.resolve('x').then(function(v) { order += v; })"
        ".finally(function() { order += '!'; });",
        "order");
    ASSERT(out.string_val == "x!");
})

TEST(promise_adopt_resolved_promise_result, {
    BuiltinFixture f;
    // resolve() with an inner promise must adopt the inner value.
    JSValue out = f.eval_global(
        "var v = 0;\n"
        "Promise.resolve(Promise.resolve(9)).then(function(x) { v = x; });",
        "v");
    ASSERT_EQ(out.number_val, 9.0);
})

TEST(fetch_promise_has_then_method, {
    BuiltinFixture f;
    // Engine-created promises (Promise.resolve etc.) must carry
    // Promise.prototype so .then resolves through normal property lookup —
    // the same wiring fetch() promises rely on.
    JSValue r = f.eval("typeof Promise.resolve(1).then === 'function';");
    ASSERT(r.bool_val);
})

// ---------------------------------------------------------------------------
// Function statics + globals
// ---------------------------------------------------------------------------

TEST(function_statics_date_now_callable, {
    BuiltinFixture f;
    // Regression: functions could not hold properties; statics had to be
    // registered as separate globals.
    JSValue r = f.eval("typeof Date.now === 'function';");
    ASSERT(r.bool_val);
})

TEST(global_encodeURIComponent, {
    BuiltinFixture f;
    JSValue r = f.eval("encodeURIComponent('a b&c=d/e?');");
    ASSERT(r.string_val == "a%20b%26c%3Dd%2Fe%3F");
})

TEST(global_decodeURIComponent_roundtrip, {
    BuiltinFixture f;
    JSValue r = f.eval("decodeURIComponent(encodeURIComponent('hello world & more'));");
    ASSERT(r.string_val == "hello world & more");
})
