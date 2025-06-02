#pragma once
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>
#include <algorithm>

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
        std::string cmd = "c++ -std=c++23 -O3 -o " + bin.string() + " " + src.string();
        int ret = std::system(cmd.c_str());
        if (ret != 0)
        {
            std::cerr << "Compilation failed (exit = " << ret << ")\n";
            exit(ret);
        }

        std::rename(temp_bin.c_str(), bin.string().c_str());
        execv(bin.c_str(), argv);
        perror("execv");
        exit(1);
    }
    std::cout << "nothing todo!" << std::endl;
}

class Unit
{
  private:
    std::vector<std::unique_ptr<Unit>> deps;
    std::optional<std::string> source_path;
    std::optional<std::string> target_path;

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

    void compile_impl() const
    {
        // Recurse into dependencies
        std::vector<std::string> dep_targets;
        std::vector<std::string> header_deps;
        for (const auto& dep : deps)
        {
            if (dep->target_path)
            {
                dep_targets.push_back(*dep->target_path);
            }
            else if (dep->source_path)
            {
                header_deps.push_back(*dep->source_path);
            }
            dep->compile_impl();
        }

        if (target_path)
        {
            std::filesystem::create_directories(std::filesystem::path(*target_path).parent_path());
            std::cout << *target_path << " has dependency on headers: " << std::endl;
            for (const auto& header_dep : header_deps)
            {
                std::cout << "\t" << header_dep << std::endl;
            }
            if (source_path)
            {
                std::string cmd = "c++ -MMD -c -std=c++23 -O3 -o " + *target_path + " " + *source_path;
                int ret = std::system(cmd.c_str());
                if (ret != 0)
                {
                    std::cerr << "Compilation failed (exit = " << ret << ")\n";
                    exit(ret);
                }
            }
            else
            {
                std::string cmd = "c++ -MMD -std=c++23 -O3 -o " + *target_path + " ";
                for (const auto target : dep_targets)
                {
                    cmd += target + " ";
                }
                int ret = std::system(cmd.c_str());
                if (ret != 0)
                {
                    std::cerr << "Compilation failed (exit = " << ret << ")\n";
                    exit(ret);
                }
            }
        }
    }

  public:
    Unit(const std::optional<std::string>& source_path, const std::optional<std::string>& target_path = std::nullopt)
        : source_path(source_path), target_path(target_path)
    {
    }

    void add_dep(std::unique_ptr<Unit> unit)
    {
        deps.push_back(std::move(unit));
    }

    void print_depth()
    {
        print_depth_impl(0);
    }

    void compile()
    {
        compile_impl();
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
                std::cout << "Found .d file: " << header_deps_path << std::endl;
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
