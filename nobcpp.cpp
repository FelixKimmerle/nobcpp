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
    const auto tree_1 = build_tree_from_cpp_files("src/project_1/", "build/project_1/target.exe");
    tree_1->print_depth();
    CompileCommands cc_1 = tree_1->compile();
    std::cout << cc_1 << std::endl;
    cc_1.execute();
    const auto tree_2 = build_tree_from_cpp_files("src/project_2/", "build/project_2/target.exe");
    tree_2->print_depth();
    CompileCommands cc_2 = tree_2->compile();
    std::cout << cc_2 << std::endl;
    cc_2.execute();
}
