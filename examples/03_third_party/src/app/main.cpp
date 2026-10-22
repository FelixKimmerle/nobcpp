#include <noisy_math.hpp>
#include <cstdio>

int main()
{
    std::printf("%s: hypotenuse(3, 4.5) = %g\n", APP_NAME, noisy_math::hypotenuse(3, 4.5));
}
