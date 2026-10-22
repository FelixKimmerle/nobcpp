#include "extra.hpp"
#include <cstdio>
#include <greetlib.hpp>

int main()
{
    std::printf("%s, %s, fake=%d, imported=%d\n", greetlib_greet().c_str(), extra_name(),
                NOBCPP_FAKE, GREETLIB_IMPORTED);
}
