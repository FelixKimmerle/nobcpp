#include "nobcpp.hpp"

int main(int argc, char** argv /*, char** envp*/)
{
    rebuild_self(__FILE__, argc, argv, {"nobcpp.hpp"});
    std::cout << __TIME__ << std::endl;

    // for (char** env = envp; *env != nullptr; ++env)
    // {
    //     std::cout << *env << std::endl;
    // }

    std::unordered_map<std::string, Profile> profiles = {
        {"release", {{"-O3"}}},
        {"debug", {{"-O0", "-g"}}},
    };

    auto tree_2 = build_tree_from_cpp_files("src/project_2/", "build/project_2/target.a");

    const auto tree_1 =
        build_tree_from_cpp_files("src/project_1/", "build/project_1/target");

    tree_1->add_dep(std::move(tree_2));
    tree_1->add_compile_flags({"-Isrc/project_2"});

    std::cout << "Tree 1" << std::endl;
    tree_1->print_depth();

    // CompileCommands cc_1 = tree_1->compile(false);
    // std::cout << "Compile commands 1" << std::endl;
    // std::cout << cc_1 << std::endl;
    // cc_1.execute();
    // cc_1.write();

    tree_1->parse(argc, argv, profiles);
}
