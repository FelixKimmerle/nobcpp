#include "greetlib.hpp"
#include "version.hpp"

std::string greetlib_greet()
{
#ifdef GREETLIB_LOUD
    return "HELLO FROM GREETLIB " GREETLIB_VERSION;
#else
    return "hello from greetlib " GREETLIB_VERSION;
#endif
}
