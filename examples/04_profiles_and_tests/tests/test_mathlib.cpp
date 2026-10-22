#include "mathlib.hpp"
#include "test.hpp"

void test_factorial()
{
    CHECK(mathlib::factorial(0) == 1);
    CHECK(mathlib::factorial(5) == 120);
    CHECK(mathlib::factorial(20) == 2432902008176640000ull);
}

void test_primes()
{
    CHECK(!mathlib::is_prime(1));
    CHECK(mathlib::is_prime(2));
    CHECK(mathlib::is_prime(97));
    CHECK(!mathlib::is_prime(91));
}

void test_gcd()
{
    static_assert(mathlib::gcd(84, 36) == 12);
    CHECK(mathlib::gcd(17u, 5u) == 1u);
}
