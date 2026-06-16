#include "test_framework.hpp"

TEST(trivial_pass, { return true; })

TEST(trivial_fail, { _err = "intentional failure"; return false; })
