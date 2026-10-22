#include "mathlib.hpp"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv)
{
    unsigned n = argc > 1 ? static_cast<unsigned>(std::atoi(argv[1])) : 10;
    std::printf("[%s] %u! = %llu, gcd(84, 36) = %d, 97 is %sprime\n", BUILD_KIND, n,
                static_cast<unsigned long long>(mathlib::factorial(n)), mathlib::gcd(84, 36),
                mathlib::is_prime(97) ? "" : "not ");
}
