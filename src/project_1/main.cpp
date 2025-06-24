void foo();
void bar();
void test();
#include "test/test.hpp"
#include "test_lib.hpp"
#include <iostream>
int main()
{
    foo();
    bar();
    test();
    greet();
    std::cout << my_add(8, 4) << std::endl;
}
