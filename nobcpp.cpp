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

    // Define configs and their actions
    std::map<std::string, ConfigAction> configs = {
        {"asan", [](BuildContext& c) { c.flags.push_back("-fsanitize=address"); }},
        {"debug",
         [](BuildContext& c) {
             c.flags.push_back("-g");
             c.flags.push_back("-O0");
         }},
        {"release", [](BuildContext& c) { c.flags.push_back("-O3"); }},
        {"tsan", [](BuildContext& c) { c.flags.push_back("-fsanitize=thread"); }},
        {"ubsan", [](BuildContext& c) { c.flags.push_back("-fsanitize=undefined"); }},
    };

    // bool rebuild = argc >= 2;

    auto tree_2 = build_tree_from_cpp_files("src/project_2/", "build/project_2/target.a");

    const auto tree_1 = build_tree_from_cpp_files("src/project_1/", "build/project_1/target");

    // tree_1->set_compiler("g++");
    // tree_2->set_compiler("clang++");
    tree_1->add_dep(std::move(tree_2));
    tree_1->add_compile_flags({"-Isrc/project_2"});

    // Define commands and their actions
    std::map<std::string, CommandAction> commands = {
        {"build",
         [&](BuildContext& c) {
             std::cout << "Building with flags:" << std::endl;
             CompileCommands cc_1 = tree_1->compile(false);
             std::cout << cc_1 << std::endl;
             cc_1.execute();
             cc_1.write();

             // Here you would invoke your build logic
         }},
        {"rebuild",
         [&](BuildContext& c) {
             std::cout << "Rebuilding" << std::endl;
             CompileCommands cc_1 = tree_1->compile(true);
             std::cout << cc_1 << std::endl;
             cc_1.execute();
             cc_1.write();
         }},

        {"run",
         [](BuildContext& c) {
             std::string binary = c.build_folder + "/" + c.binary_name;
             if (std::filesystem::exists(binary) &&
                 (std::filesystem::status(binary).permissions() & std::filesystem::perms::owner_exec) !=
                     std::filesystem::perms::none)
             {
                 std::cout << "Running: " << binary << "\n";
                 std::system(binary.c_str());
             }
             else
             {
                 std::cerr << "Binary not found or not executable: " << binary << "\n";
             }
         }},
        {"clean",
         [](BuildContext& c) {
             std::cout << "Cleaning build folder: " << c.build_folder << "\n";
             // Here you would remove build artifacts
         }},
    };

    BuildContext ctx;

    // Parse arguments and get result
    ParsedArgs parsed = parse_args(argc, argv, configs, commands);

    // Compose build folder and update context
    ctx.build_folder = compose_build_folder(parsed.configs_used);

    // Apply configs in sorted order
    apply_configs(parsed.configs_used, configs, ctx);
    tree_1->add_compile_flags(ctx.flags);

    // Execute commands in order
    execute_commands(parsed.commands_to_run, commands, ctx);

    if (parsed.commands_to_run.empty())
    {
        std::cout << "No command given. Exiting.\n";
    }

    // std::cout << "Tree 1" << std::endl;
    // tree_1->print_depth();

    // CompileCommands cc_1 = tree_1->compile(rebuild);
    // std::cout << "Compile commands 1" << std::endl;
    // std::cout << cc_1 << std::endl;
    // cc_1.execute();
    // cc_1.write();
}
