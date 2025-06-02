#pragma once
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unistd.h>
#include <vector>

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
};

// Helper to convert a .cpp path to a .o path (optional)
inline std::string to_object_path(const std::filesystem::path& source)
{
    std::filesystem::path obj = source;
    obj.replace_extension(".o");
    return obj.string();
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
            std::string obj_path = to_object_path(entry.path());

            auto child = std::make_unique<Unit>(src_path, obj_path);
            root->add_dep(std::move(child));
        }
    }

    return root;
}
