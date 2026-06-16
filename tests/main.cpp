#include "test_framework.hpp"

int main(int argc, char** argv) {
    auto& reg = browser::test::TestRegistry::instance();
    if (argc > 1) return reg.run_filtered(argv[1]);
    return reg.run_all();
}
