#pragma once
#include <string>
#include <functional>
#include <vector>
#include <iostream>
#include <chrono>
#include <sstream>

namespace browser::test {

namespace detail {
    template<typename T> std::string to_string_impl(const T& v) {
        if constexpr (std::is_arithmetic_v<T>) return std::to_string(v);
        else return std::string("<?>");
    }
}

constexpr const char* COLOR_GREEN = "\033[32m";
constexpr const char* COLOR_RED   = "\033[31m";
constexpr const char* COLOR_RESET = "\033[0m";

struct TestResult {
    std::string name;
    bool passed;
    std::string failure_message;
    double duration_ms;
};

class TestRegistry {
public:
    static TestRegistry& instance();
    void register_test(const std::string& name, std::function<bool(std::string&)> fn);
    int run_all();
    int run_filtered(const std::string& filter);
    void set_capture_stdout(bool capture);
    std::string get_captured_stdout() const;
    void reset_capture();

private:
    struct TestCase {
        std::string name;
        std::function<bool(std::string&)> fn;
    };
    static int run_tests_impl(const std::vector<TestCase>& tests,
                              std::vector<TestResult>& results,
                              const std::string* filter);
    std::vector<TestCase> tests_;
    std::vector<TestResult> results_;
    bool capture_stdout_ = false;
    std::stringstream captured_output_;
    std::streambuf* old_cout_buf_ = nullptr;
};

#define TEST(name, ...) \
    static bool _test_##name = []{ \
        ::browser::test::TestRegistry::instance().register_test(#name, \
            [](std::string& _err) -> bool { (void)_err; __VA_ARGS__; return true; }); \
        return true; \
    }();

#define ASSERT(cond) \
    if (!(cond)) { \
        _err = "ASSERT failed at line " + std::to_string(__LINE__) + ": " #cond; \
        return false; \
    }

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) { \
        _err = "ASSERT_EQ failed at line " + std::to_string(__LINE__) + ": " \
            + #a + " (" + ::browser::test::detail::to_string_impl(a) + ") != " \
            + #b + " (" + ::browser::test::detail::to_string_impl(b) + ")"; \
        return false; \
    }

} // namespace browser::test
