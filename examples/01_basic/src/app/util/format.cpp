#include "format.hpp"

std::string format_product(int a, int b)
{
    return std::to_string(a) + " * " + std::to_string(b) + " = " + std::to_string(a * b);
}
