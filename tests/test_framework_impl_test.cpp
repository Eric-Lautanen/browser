#include "test_framework.hpp"
#include <string>

TEST(register_and_run, {
    auto& reg = browser::test::TestRegistry::instance();
    auto& r = browser::test::TestRegistry::instance();
    ASSERT(&reg == &r);
})

TEST(stdout_capture, {
    auto& reg = browser::test::TestRegistry::instance();
    reg.reset_capture();
    reg.set_capture_stdout(true);
    std::cout << "captured output";
    reg.set_capture_stdout(false);
    std::string out = reg.get_captured_stdout();
    ASSERT(out.find("captured output") != std::string::npos);
    reg.reset_capture();
})

TEST(assert_works, {
    int x = 42;
    ASSERT(x == 42);
    ASSERT(true);
})

TEST(assert_eq_works, {
    int a = 10;
    int b = 10;
    ASSERT_EQ(a, b);
    ASSERT_EQ(1 + 1, 2);
})

TEST(run_filtered_no_match, {
    auto& reg = browser::test::TestRegistry::instance();
    int result = reg.run_filtered("__no_test_has_this_name__");
    ASSERT_EQ(result, 0);
})

TEST(run_filtered_matches, {
    auto& reg = browser::test::TestRegistry::instance();
    int result = reg.run_filtered("assert_eq_works");
    ASSERT_EQ(result, 0);
})
