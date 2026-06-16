#include "test_framework.hpp"
#include "utility.hpp"
#include "../js/ast.hpp"
#include "../js/parser.hpp"
#include "../js/compiler.hpp"
#include "../js/value.hpp"
#include "../js/vm.hpp"

using namespace browser;
using namespace browser::js;

TEST(jsvalue_truthy, {
    ASSERT(JSValue::number(1).is_truthy());
    ASSERT(!JSValue::number(0).is_truthy());
    ASSERT(!JSValue::undefined().is_truthy());
    ASSERT(!JSValue::null().is_truthy());
    ASSERT(JSValue::string("hello").is_truthy());
    ASSERT(!JSValue::string("").is_truthy());
})

TEST(jsvalue_to_number, {
    ASSERT_EQ(JSValue::string("42").to_number(), 42);
    auto nan_val = JSValue::undefined().to_number();
    ASSERT(std::isnan(nan_val));
})

TEST(vm_type_coerce, {
    VM vm;
    auto bc = Compiler{}.compile(*Parser("1 + \"2\";").parse_program());
    auto r = vm.execute(bc.get());
    ASSERT_EQ(r.string_val, "12");
})

TEST(vm_strict_eq, {
    auto bc = Compiler{}.compile(*Parser("1 === 1;").parse_program());
    auto r = VM{}.execute(bc.get());
    ASSERT(r.bool_val);
})

TEST(vm_loose_eq, {
    auto bc = Compiler{}.compile(*Parser("1 == \"1\";").parse_program());
    auto r = VM{}.execute(bc.get());
    ASSERT(r.bool_val);
})

TEST(vm_add, {
    auto bc = Compiler{}.compile(*Parser("1+2;").parse_program());
    VM vm; auto r = vm.execute(bc.get());
    ASSERT_EQ(r.number_val, 3);
})

TEST(vm_string_concat, {
    auto bc = Compiler{}.compile(*Parser("\"hello\" + \" world\";").parse_program());
    VM vm; auto r = vm.execute(bc.get());
    ASSERT_EQ(r.string_val, "hello world");
})

TEST(vm_comparison, {
    auto bc = Compiler{}.compile(*Parser("5 > 3;").parse_program());
    VM vm; auto r = vm.execute(bc.get());
    ASSERT(r.bool_val);
})

TEST(vm_function_call, {
    Parser p("function f(x){return x*2;} f(21);");
    auto bc = Compiler{}.compile(*p.parse_program());
    VM vm; auto r = vm.execute(bc.get());
    ASSERT_EQ(r.number_val, 42);
})

TEST(vm_nested_call, {
    Parser p("function a(x){return x+1;} function b(y){return a(y)*2;} b(5);");
    auto bc = Compiler{}.compile(*p.parse_program());
    VM vm; auto r = vm.execute(bc.get());
    ASSERT_EQ(r.number_val, 12);
})

TEST(vm_recursion, {
    Parser p("function f(n){if(n<=1)return 1;return n*f(n-1);} f(5);");
    auto bc = Compiler{}.compile(*p.parse_program());
    VM vm; auto r = vm.execute(bc.get());
    ASSERT_EQ(r.number_val, 120);
})

TEST(vm_property_access, {
    Parser p("var o = {a:1}; o.a;");
    auto bc = Compiler{}.compile(*p.parse_program());
    VM vm; auto r = vm.execute(bc.get());
    ASSERT_EQ(r.number_val, 1);
})

TEST(vm_builtin_parseInt, {
    auto bc = Compiler{}.compile(*Parser("parseInt(\"42\") + 1;").parse_program());
    VM vm; vm.register_builtins();
    auto r = vm.execute(bc.get());
    ASSERT_EQ(r.number_val, 43);
})

TEST(vm_console_log, {
    auto bc = Compiler{}.compile(*Parser("console.log(\"hello\");").parse_program());
    VM vm; vm.register_builtins();
    vm.execute(bc.get());
})
