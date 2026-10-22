#pragma once
#include <concepts>
#include <cstdint>

namespace mathlib
{
std::uint64_t factorial(unsigned n);
bool is_prime(std::uint64_t n);

template <std::integral T>
constexpr T gcd(T a, T b)
{
    while (b != 0)
    {
        T t = a % b;
        a = b;
        b = t;
    }
    return a;
}
} // namespace mathlib
