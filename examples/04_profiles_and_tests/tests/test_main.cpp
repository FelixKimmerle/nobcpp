#include "test.hpp"
#include <cstdio>

int main()
{
    int failed = run_all_tests();
    std::printf("%s\n", failed == 0 ? "all tests passed" : "tests failed");
    return failed;
}
