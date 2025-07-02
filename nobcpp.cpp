#include "nobcpp.hpp"

int main(int argc, char** argv /*, char** envp*/)
{
    rebuild_self(__FILE__, argc, argv, {"nobcpp.hpp"});
    std::cout << __TIME__ << std::endl;

    // for (char** env = envp; *env != nullptr; ++env)
    // {
    //     std::cout << *env << std::endl;
    // }

    // auto [output, exit_code] = run_process("env", {});
    // std::cout << "exit code: " << exit_code << "\n";
    // std::cout << "output: \n" << output << std::endl;

    bool rebuild = argc >= 2;

    auto tree_2 = build_tree_from_cpp_files("src/project_2/", "build/project_2/target.a");

    const auto tree_1 = build_tree_from_cpp_files("src/project_1/", "build/project_1/target");
    tree_1->set_compiler("g++");
    tree_2->set_compiler("clang++");
    tree_1->add_dep(std::move(tree_2));
    tree_1->add_compile_flags({"-g", "-O0", "-Isrc/project_2"});
    tree_1->add_link_flags({"-lsfml-system", "-lsfml-window", "-lsfml-graphics"});

    std::cout << "Tree 1" << std::endl;
    tree_1->print_depth();

    CompileCommands cc_1 = tree_1->compile(rebuild);
    std::cout << "Compile commands 1" << std::endl;
    std::cout << cc_1 << std::endl;
    cc_1.execute();
    cc_1.write();
}
