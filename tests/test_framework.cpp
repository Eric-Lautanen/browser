#include "test_framework.hpp"

namespace browser::test {

TestRegistry& TestRegistry::instance() {
    static TestRegistry registry;
    return registry;
}

void TestRegistry::register_test(const std::string& name, std::function<bool(std::string&)> fn) {
    tests_.push_back({name, std::move(fn)});
}

int TestRegistry::run_tests_impl(const std::vector<TestCase>& tests,
                                 std::vector<TestResult>& results,
                                 const std::string* filter) {
    int passed = 0, failed = 0;
    for (const auto& test : tests) {
        if (filter && test.name.find(*filter) == std::string::npos) continue;
        std::string error;
        auto start = std::chrono::high_resolution_clock::now();
        bool ok = test.fn(error);
        auto end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start).count();
        results.push_back({test.name, ok, error, ms});
        if (ok) {
            std::cout << COLOR_GREEN << "[PASS] " << COLOR_RESET << test.name << " (" << ms << "ms)" << std::endl;
            passed++;
        } else {
            std::cout << COLOR_RED << "[FAIL] " << COLOR_RESET << test.name << ": " << error << " (" << ms << "ms)" << std::endl;
            failed++;
        }
    }
    std::cout << passed << "/" << (passed + failed) << " tests passed, " << failed << " failed" << std::endl;
    return failed;
}

int TestRegistry::run_all() {
    results_.clear();
    return run_tests_impl(tests_, results_, nullptr);
}

int TestRegistry::run_filtered(const std::string& filter) {
    results_.clear();
    return run_tests_impl(tests_, results_, &filter);
}

void TestRegistry::set_capture_stdout(bool capture) {
    if (capture == capture_stdout_) return;
    capture_stdout_ = capture;
    if (capture) {
        old_cout_buf_ = std::cout.rdbuf();
        std::cout.rdbuf(captured_output_.rdbuf());
    } else {
        std::cout.rdbuf(old_cout_buf_);
        old_cout_buf_ = nullptr;
    }
}

std::string TestRegistry::get_captured_stdout() const {
    return captured_output_.str();
}

void TestRegistry::reset_capture() {
    captured_output_.str("");
    captured_output_.clear();
}

} // namespace browser::test
