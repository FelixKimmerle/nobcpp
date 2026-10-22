#pragma once
#include <cstdio>

// A deliberately tiny test framework: CHECK records failures, tests register themselves.
inline int& failures()
{
    static int count = 0;
    return count;
}

#define CHECK(expr)                                                                \
    do                                                                             \
    {                                                                              \
        if (!(expr))                                                               \
        {                                                                          \
            std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr);   \
            ++failures();                                                          \
        }                                                                          \
    } while (0)

void test_factorial();
void test_primes();
void test_gcd();

inline int run_all_tests()
{
    test_factorial();
    test_primes();
    test_gcd();
    return failures();
}
