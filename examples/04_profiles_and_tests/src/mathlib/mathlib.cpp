#include "mathlib.hpp"

namespace mathlib
{
std::uint64_t factorial(unsigned n)
{
    std::uint64_t result = 1;
    for (unsigned i = 2; i <= n; ++i)
    {
        result *= i;
    }
    return result;
}

bool is_prime(std::uint64_t n)
{
    if (n < 2)
    {
        return false;
    }
    for (std::uint64_t d = 2; d * d <= n; ++d)
    {
        if (n % d == 0)
        {
            return false;
        }
    }
    return true;
}
} // namespace mathlib
