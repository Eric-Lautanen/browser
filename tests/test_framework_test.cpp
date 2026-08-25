#include "test_framework.hpp"

TEST(trivial_pass, { return true; })

// The runner contract is that a test callback reports failure via
// {return false, _err}. Verify the convention directly instead of shipping an
// intentionally-failing TEST that would make this binary exit non-zero.
TEST(failure_reporting_convention, {
    std::string err = "unset";
    auto failing = [](std::string &e) -> bool {
        e = "intentional failure";
        return false;
    };
    ASSERT(!failing(err));
    ASSERT_EQ(err, "intentional failure");
})
