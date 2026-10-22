#pragma once
// Stand-in for a third party header that is not warning clean.
#include <cmath>

#ifndef NOISY_MATH_PRECISION
#define NOISY_MATH_PRECISION 2
#endif

namespace noisy_math
{
inline double hypotenuse(double a, double b)
{
    int unused_counter;               // -Wunused-variable
    unsigned precision = NOISY_MATH_PRECISION;
    if (precision >= 0)               // -Wtype-limits (always true)
    {
        double scale = std::pow(10.0, NOISY_MATH_PRECISION);
        return std::round(std::sqrt(a * a + b * b) * scale) / scale;
    }
    return 0;
}
} // namespace noisy_math
