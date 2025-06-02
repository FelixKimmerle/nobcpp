#include "nobcpp.hpp"

int main(int argc, char** argv)
{
    rebuild_self(__FILE__, argc, argv, {"nobcpp.hpp"});

    std::cout << __TIME__ << std::endl;

    // Unit root("src/main.cpp", "build/output.exe");
    // auto test = std::make_unique<Unit>("src/test.cpp", "build/test.o");
    // test->add_dep(std::make_unique<Unit>("src/test.hpp"));
    // root.add_dep(std::move(test));
    // root.print_depth();
    const auto tree = build_tree_from_cpp_files("src/", "build/target.exe");
    tree->print_depth();
}
