#include "greeter.hpp"
#include "util/format.hpp"
#include <iostream>

int main()
{
    std::cout << greet("nobcpp") << "\n" << format_product(6, 7) << "\n";
}
