#pragma once
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

enum class TargetType
{
    EXECUTABLE,
    STATIC_LIB,
    DYNAMIC_LIB,
    OBJECT,
    NONE
};

inline void rebuild_self(const std::string& source_filename, int argc, char** argv,
                         const std::vector<std::string>& deps = {})
{
    namespace fs = std::filesystem;
    fs::path src = fs::canonical(source_filename);
    fs::path bin = fs::canonical(argv[0]);

    bool needs_recompile = !fs::exists(bin) || fs::last_write_time(src) > fs::last_write_time(bin);
    if (!needs_recompile)
    {
        for (const auto& dep : deps)
        {
            needs_recompile |= !fs::exists(dep) || fs::last_write_time(dep) > fs::last_write_time(bin);
        }
    }

    if (needs_recompile)
    {

        std::cout << "Rebuilding: " << bin << "...\n";
        std::string temp_bin = bin.string() + ".new";
        std::string cmd = "c++ -std=c++23 -Wall -Wextra -Wpedantic -O3 -o " + bin.string() + " " + src.string();
        int ret = std::system(cmd.c_str());
        if (ret != 0)
        {
            std::cerr << "Compilation failed (exit = " << ret << ")\n";
            exit(ret);
        }

        std::rename(temp_bin.c_str(), bin.string().c_str());
        std::vector<char*> new_argv;
        new_argv.push_back(argv[0]);
        std::string flag = "--recompiled";
        new_argv.push_back(const_cast<char*>(flag.c_str()));
        for (int i = 1; i < argc; ++i)
        {
            new_argv.push_back(argv[i]);
        }
        new_argv.push_back(nullptr);
        execv(bin.c_str(), new_argv.data());
        perror("execv");
        exit(1);
    }
    std::cout << "nothing todo!" << std::endl;
}

class CompileCommand
{
  private:
    std::string command;
    std::vector<std::string> args;
    bool enabled;

  public:
    friend std::ostream& operator<<(std::ostream& os, const CompileCommand& cc)
    {
        os << cc.command << " ";
        for (const auto& arg : cc.args)
        {
            os << arg << " ";
        }
        os << " enabled: " << cc.enabled;
        return os;
    }
    CompileCommand(const std::string& command, const std::vector<std::string> args, bool enabled)
        : command(command), args(args), enabled(enabled)
    {
    }

    bool is_enabled() const
    {
        return enabled;
    }
    int execute() const
    {
        if (!enabled)
        {
            return 0;
        }
        std::stringstream ss;
        ss << command << " ";
        for (const auto& arg : args)
        {
            ss << arg << " ";
        }
        return std::system(ss.str().c_str());
    }
};

class CompileCommands
{
  private:
    std::vector<std::vector<CompileCommand>> commands;

  public:
    void add_cmd(size_t depth, const CompileCommand& compile_command)
    {
        // fill to the insertion depth (all depth - 1 should already be populated)
        while (commands.size() <= depth)
        {
            commands.emplace_back();
        }

        commands[depth].push_back(compile_command);
    }

    friend std::ostream& operator<<(std::ostream& os, CompileCommands compile_commands)
    {
        size_t level = 0;
        for (const auto& cc : compile_commands.commands)
        {
            std::cout << "Level: " << level++ << std::endl;
            for (const auto& c : cc)
            {
                std::cout << c << std::endl;
            }
        }

        return os;
    }

    void execute() const
    {
        for (const auto& compile_level : std::views::reverse(commands))
        {
            std::vector<std::future<int>> jobs;
            for (const auto& cmd : compile_level)
            {
                if (cmd.is_enabled())
                {
                    std::cout << "Running: " << cmd << "\n";

                    jobs.push_back(std::async(std::launch::async, [cmd]() { return cmd.execute(); }));
                }
            }
            for (auto& job : jobs)
            {
                if (job.get() != 0)
                {
                    std::cerr << "Command failed.\n";
                    std::exit(1);
                }
            }
        }
    }
};

class Unit
{
  private:
    std::vector<std::unique_ptr<Unit>> deps;
    std::optional<std::string> source_path;
    std::optional<std::string> target_path;
    std::vector<std::string> compile_flags;
    std::vector<std::string> link_flags;
    TargetType target_type;

    void print_depth_impl(int depth) const
    {
        // Recurse into dependencies
        for (const auto& dep : deps)
        {
            dep->print_depth_impl(depth + 1);
        }

        // Indent based on depth
        for (int i = 0; i < depth; ++i)
        {
            std::cout << "  ";
        }

        if (source_path && target_path)
        {
            std::cout << "Compilation unit: ";
        }
        if (source_path && !target_path)
        {
            std::cout << "Header dep: ";
        }
        if (!source_path && target_path)
        {
            std::cout << "Target: ";
        }

        if (source_path)
        {
            std::cout << *source_path;
        }
        if (target_path)
        {
            std::cout << " -> " << *target_path;
        }
        std::cout << std::endl;
    }

    bool compile_impl(CompileCommands& compile_commands, int depth, TargetType target_type_parent,
                      const bool full_rebuild, const std::vector<std::string>& inherited_compile_flags) const
    {
        std::vector<std::string> local_compile_flags;
        local_compile_flags.insert(local_compile_flags.begin(), inherited_compile_flags.begin(),
                                   inherited_compile_flags.end());

        local_compile_flags.insert(local_compile_flags.end(), compile_flags.begin(), compile_flags.end());
        // Recurse into dependencies
        std::vector<std::string> dep_target_objects;
        std::vector<std::string> dep_target_libs;
        std::vector<std::string> header_deps;
        bool parent_rebuild = false;

        if (target_type == TargetType::EXECUTABLE || target_type == TargetType::DYNAMIC_LIB ||
            target_type == TargetType::STATIC_LIB)
        {
            target_type_parent = target_type;
        }

        for (const auto& dep : deps)
        {
            // if (dep->target_type == TargetType::DYNAMIC_LIB || dep->target_type == TargetType::STATIC_LIB)
            // {
            //     dep_target_libs.push_back(*dep->target_path);
            // }
            if (dep->target_path)
            {
                dep_target_objects.push_back(*dep->target_path);
            }
            else if (dep->source_path)
            {
                header_deps.push_back(*dep->source_path);
            }
            bool rebuild =
                dep->compile_impl(compile_commands, depth + 1, target_type_parent, full_rebuild, local_compile_flags);
            parent_rebuild |= rebuild;
        }

        if (target_path)
        {
            std::filesystem::create_directories(std::filesystem::path(*target_path).parent_path());
            bool rebuild = parent_rebuild || !std::filesystem::exists(*target_path);
            if (!header_deps.empty())
            {

                std::cout << *target_path << " has dependency on headers: ";
                for (const auto& header_dep : header_deps)
                {
                    std::cout << header_dep << ", ";
                    rebuild = rebuild || std::filesystem::last_write_time(header_dep) >
                                             std::filesystem::last_write_time(*target_path);
                }
                std::cout << std::endl;
            }
            if (source_path)
            {
                rebuild = rebuild || std::filesystem::last_write_time(*source_path) >
                                         std::filesystem::last_write_time(*target_path);

                std::vector<std::string> args;

                if (target_type_parent == TargetType::DYNAMIC_LIB)
                {
                    args.push_back("-fPIC");
                }

                args.insert(args.end(), local_compile_flags.begin(), local_compile_flags.end());

                args.insert(args.end(), {"-MMD", "-c", "-o", *target_path, *source_path});
                // .cpp -> .o compiling
                compile_commands.add_cmd(depth, CompileCommand("c++", args, rebuild || full_rebuild));
            }
            else
            {
                // .o -> .exe linking
                std::vector<std::string> args;

                std::string compiler = "c++";
                if (target_type == TargetType::DYNAMIC_LIB)
                {
                    args.push_back("-shared");
                }
                else if (target_type == TargetType::STATIC_LIB)
                {
                    compiler = "ar rcs";
                }

                if (target_type == TargetType::DYNAMIC_LIB || target_type == TargetType::EXECUTABLE)
                {
                    args.insert(args.end(), link_flags.begin(), link_flags.end());
                }

                args.push_back("-o");
                args.push_back(*target_path);

                for (const auto& target : dep_target_objects)
                {
                    args.push_back(target);
                    rebuild = rebuild ||
                              std::filesystem::last_write_time(target) > std::filesystem::last_write_time(*target_path);
                }

                compile_commands.add_cmd(depth, CompileCommand(compiler, args, rebuild || full_rebuild));
            }
            return rebuild;
        }
        return false;
    }

  public:
    Unit(const std::optional<std::string>& source_path, const std::optional<std::string>& target_path = std::nullopt)
        : source_path(source_path), target_path(target_path), target_type(TargetType::NONE)
    {
        if (target_path)
        {
            std::string extension = std::filesystem::path(*target_path).extension();
            if (extension == ".a")
            {
                target_type = TargetType::STATIC_LIB;
            }
            else if (extension == ".so")
            {
                target_type = TargetType::DYNAMIC_LIB;
            }
            else if (extension == ".o")
            {
                target_type = TargetType::OBJECT;
            }
        }
    }

    void add_dep(std::unique_ptr<Unit> unit)
    {
        deps.push_back(std::move(unit));
    }

    void add_link_flag(const std::string& flag)
    {
        link_flags.emplace_back(flag);
    }

    void add_compile_flag(const std::string& flag)
    {
        compile_flags.emplace_back(flag);
    }

    void print_depth()
    {
        print_depth_impl(0);
    }

    CompileCommands compile(bool rebuild, int depth = 0)
    {
        CompileCommands compile_commands;
        // TargetType target_type = TargetType::EXECUTABLE;

        compile_impl(compile_commands, depth, target_type, rebuild, {});

        return compile_commands;
    }
};

inline std::filesystem::path to_object_path(const std::filesystem::path& source)
{
    std::filesystem::path relative = source.lexically_relative("src");
    std::filesystem::path obj_path = "build" / relative;
    obj_path.replace_extension(".o");
    return obj_path;
}

inline std::vector<std::string> parse_dependency_file(const std::filesystem::path& d_file_path)
{
    std::ifstream file(d_file_path);
    if (!file)
    {
        throw std::runtime_error("Failed to open file: " + d_file_path.string());
    }

    std::ostringstream oss;
    std::string line;

    // Handle line continuations with backslash
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\\')
        {
            line.pop_back(); // remove trailing backslash
            oss << line;
        }
        else
        {
            oss << line << ' '; // keep space for splitting
        }
    }

    std::istringstream dep_stream(oss.str());
    std::string token;

    std::vector<std::string> headers;
    bool after_colon = false;
    int cpp_seen = 0;

    while (dep_stream >> token)
    {
        if (!after_colon)
        {
            auto colon_pos = token.find(':');
            if (colon_pos != std::string::npos)
            {
                // Start reading dependencies after ':'
                token = token.substr(colon_pos + 1);
                after_colon = true;
                if (token.empty())
                    continue;
            }
            else
            {
                continue; // still in the target part
            }
        }

        // Skip the first non-target (assumed .cpp)
        if (cpp_seen == 0 && token.ends_with(".cpp"))
        {
            cpp_seen++;
            continue;
        }

        headers.push_back(token);
    }

    return headers;
}

inline std::unique_ptr<Unit> build_tree_from_cpp_files(const std::filesystem::path& root_dir,
                                                       const std::filesystem::path& target)
{
    auto root = std::make_unique<Unit>(std::nullopt, target.string());

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root_dir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".cpp")
        {
            std::string src_path = entry.path().string();
            std::filesystem::path obj_path = to_object_path(entry.path());

            auto child = std::make_unique<Unit>(src_path, obj_path.string());
            std::filesystem::path header_deps_path =
                obj_path.parent_path().string() + "/" + obj_path.stem().string() + ".d";
            if (std::filesystem::exists(header_deps_path))
            {
                const auto header_deps = parse_dependency_file(header_deps_path);
                for (const auto& header_dep : header_deps)
                {
                    child->add_dep(std::make_unique<Unit>(header_dep));
                }
            }
            root->add_dep(std::move(child));
        }
    }

    return root;
}
