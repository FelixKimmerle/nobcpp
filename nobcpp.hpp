#pragma once
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

// ----------------------------------------------------------------------------------
// Utils
// ----------------------------------------------------------------------------------

class Semaphore
{
  public:
    explicit Semaphore(int count) : count(count)
    {
    }

    void acquire()
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return count > 0; });
        count--;
    }

    void release()
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++count;
        cv.notify_one();
    }

  private:
    std::mutex mutex;
    std::condition_variable cv;
    int count;
};

class Timer
{
  public:
    Timer() : start_time(std::chrono::high_resolution_clock::now())
    {
    }

    void reset()
    {
        start_time = std::chrono::high_resolution_clock::now();
    }

    std::chrono::high_resolution_clock::duration elapsed_duration() const
    {
        return std::chrono::high_resolution_clock::now() - start_time;
    }

  private:
    std::chrono::high_resolution_clock::time_point start_time;
};

inline std::ostream& operator<<(std::ostream& os, const Timer& timer)
{
    using namespace std::chrono;

    auto dur = timer.elapsed_duration();

    struct Unit
    {
        double value;
        const char* suffix;
    };

    std::array<Unit, 6> units = {{{duration_cast<duration<double, std::ratio<3600>>>(dur).count(), "h"},
                                  {duration_cast<duration<double, std::ratio<60>>>(dur).count(), "m"},
                                  {duration_cast<duration<double>>(dur).count(), "s"},
                                  {duration_cast<duration<double, std::milli>>(dur).count(), "ms"},
                                  {duration_cast<duration<double, std::micro>>(dur).count(), "us"},
                                  {duration_cast<duration<double, std::nano>>(dur).count(), "ns"}}};

    for (const auto& u : units)
    {
        if (u.value >= 1.0)
        {
            os << std::fixed << std::setprecision(2) << u.value << u.suffix;
            return os;
        }
    }
    // If all are less than 1, show nanoseconds with decimals
    os << std::fixed << std::setprecision(2) << duration_cast<duration<double, std::nano>>(dur).count() << "ns";
    return os;
}

// Command line parser

#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

// Context for build actions
struct BuildContext
{
    std::vector<std::string> flags;
    std::string build_folder = "build";
    std::string binary_name = "mybinary";
    // Add more fields as needed
};

// Result of argument parsing
struct ParsedArgs
{
    std::set<std::string> configs_used;       // Sorted, for deterministic order
    std::vector<std::string> commands_to_run; // In order of appearance
};

// Type aliases for actions
using ConfigAction = std::function<void(BuildContext&)>;
using CommandAction = std::function<void(BuildContext&)>;

// Parse command line arguments
template <typename ConfigMap, typename CommandMap>
inline ParsedArgs parse_args(int argc, char* argv[], const ConfigMap& configs, const CommandMap& commands)
{
    std::set<std::string> known_configs;
    for (const auto& kv : configs)
        known_configs.insert(kv.first);

    std::set<std::string> known_commands;
    for (const auto& kv : commands)
        known_commands.insert(kv.first);

    ParsedArgs result;
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (known_configs.count(arg))
        {
            result.configs_used.insert(arg);
        }
        else if (known_commands.count(arg))
        {
            result.commands_to_run.push_back(arg);
        }
        else
        {
            std::cout << "Unknown argument: " << arg << "\n";
        }
    }
    return result;
}

// Compose build folder name from sorted configs
inline std::string compose_build_folder(const std::set<std::string>& configs_used)
{
    std::string build_folder = "build";
    if (!configs_used.empty())
    {
        build_folder += "/";
        bool first = true;
        for (const auto& cfg : configs_used)
        {
            if (!first)
                build_folder += "-";
            build_folder += cfg;
            first = false;
        }
    }
    else
    {
        build_folder += "/default";
    }
    return build_folder;
}

// Apply config actions in sorted order
inline void apply_configs(const std::set<std::string>& configs_used, const std::map<std::string, ConfigAction>& configs,
                          BuildContext& ctx)
{
    for (const auto& cfg : configs_used)
    {
        configs.at(cfg)(ctx);
    }
}

// Execute commands in order
inline void execute_commands(const std::vector<std::string>& commands_to_run,
                             const std::map<std::string, CommandAction>& commands, BuildContext& ctx)
{
    for (const auto& cmd : commands_to_run)
    {
        commands.at(cmd)(ctx);
    }
}

// ----------------------------------------------------------------------------------
// Rebuild
// ----------------------------------------------------------------------------------

struct ProcessResult
{
    std::string out;
    std::string err;
    int exit_code;
};

inline ProcessResult run_process(const std::string& cmd, const std::vector<std::string>& args)
{
    int out_pipe[2], err_pipe[2];
    if (pipe(out_pipe) == -1 || pipe(err_pipe) == -1)
    {
        perror("pipe");
        return {"", "", -1};
    }

    pid_t pid = fork();
    if (pid == -1)
    {
        perror("fork");
        return {"", "", -1};
    }

    if (pid == 0)
    {
        // Child
        close(out_pipe[0]);
        close(err_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);
        close(err_pipe[1]);
        const std::string placeholder = "-fdiagnostics-color=always";

        // Build argv
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(cmd.c_str()));
        for (const auto& arg : args)
            argv.push_back(const_cast<char*>(arg.c_str()));

        if (cmd == "gcc" || cmd == "g++" || cmd == "c++" || cmd == "clang" || cmd == "clang++")
        {
            argv.push_back(const_cast<char*>(placeholder.c_str()));
        }
        argv.push_back(nullptr);

        // Pass through PATH from parent
        const char* path = std::getenv("PATH");
        std::string path_var = path ? std::string("PATH=") + path : "PATH=/usr/bin:/bin";
        char* envp[] = {const_cast<char*>(path_var.c_str()), nullptr};

        // Use execvpe to search PATH
        execvpe(cmd.c_str(), argv.data(), envp);

        // If execvpe fails
        perror("execvpe");
        _exit(127);
    }
    else
    {
        // Parent
        close(out_pipe[1]);
        close(err_pipe[1]);
        std::string out, err;
        char buffer[4096];
        ssize_t count;
        fd_set fds;
        int maxfd = std::max(out_pipe[0], err_pipe[0]);
        bool out_open = true, err_open = true;

        while (out_open || err_open)
        {
            FD_ZERO(&fds);
            if (out_open)
                FD_SET(out_pipe[0], &fds);
            if (err_open)
                FD_SET(err_pipe[0], &fds);

            int ready = select(maxfd + 1, &fds, nullptr, nullptr, nullptr);
            if (ready == -1)
            {
                perror("select");
                break;
            }

            if (out_open && FD_ISSET(out_pipe[0], &fds))
            {
                count = read(out_pipe[0], buffer, sizeof(buffer));
                if (count > 0)
                    out.append(buffer, count);
                else
                    out_open = false;
            }

            if (err_open && FD_ISSET(err_pipe[0], &fds))
            {
                count = read(err_pipe[0], buffer, sizeof(buffer));
                if (count > 0)
                    err.append(buffer, count);
                else
                    err_open = false;
            }
        }

        close(out_pipe[0]);
        close(err_pipe[0]);
        int status;
        waitpid(pid, &status, 0);
        int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        return {out, err, exit_code};
    }
}

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
        std::string cmd = "c++ -std=c++20 -Wall -Wextra -Wpedantic -O3 -o " + bin.string() + " " + src.string();
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

// ----------------------------------------------------------------------------------
// Type definitions
// ----------------------------------------------------------------------------------

enum class TargetType
{
    EXECUTABLE,
    STATIC_LIB,
    DYNAMIC_LIB,
    OBJECT,
    NONE
};

class CompileCommand
{
  private:
    std::string command;
    std::vector<std::string> args;
    bool enabled;
    bool compile;

  public:
    CompileCommand(const std::string& command, const std::vector<std::string> args, bool enabled, bool compile);
    bool is_enabled() const;
    bool is_compile() const;
    int execute() const;
    const std::string get_abs_file() const;
    void print(std::ostream& os) const;
    friend std::ostream& operator<<(std::ostream& os, const CompileCommand& cc);
};

class CompileCommands
{
  private:
    std::vector<std::vector<CompileCommand>> commands;

  public:
    void add_cmd(size_t depth, const CompileCommand& compile_command);
    void execute() const;
    void write() const;
    friend std::ostream& operator<<(std::ostream& os, CompileCommands compile_commands);
};

inline CompileCommand::CompileCommand(const std::string& command, const std::vector<std::string> args, bool enabled,
                                      bool compile)
    : command(command), args(args), enabled(enabled), compile(compile)
{
}

class Unit
{
  private:
    std::vector<std::unique_ptr<Unit>> deps;
    std::optional<std::string> source_path;
    std::optional<std::string> target_path;
    std::vector<std::string> compile_flags;
    std::vector<std::string> link_flags;
    TargetType target_type;
    std::string compiler;

    void print_depth_impl(int depth) const;

    bool compile_impl(CompileCommands& compile_commands, int depth, TargetType target_type_parent,
                      const bool full_rebuild, const std::vector<std::string>& inherited_compile_flags) const;

  public:
    Unit(const std::optional<std::string>& source_path, const std::optional<std::string>& target_path = std::nullopt);

    void add_dep(std::unique_ptr<Unit> unit);
    void add_link_flag(const std::string& flag);
    void add_link_flags(const std::vector<std::string>& flags);
    void add_compile_flag(const std::string& flag);
    void add_compile_flags(const std::vector<std::string>& flags);
    void print_depth();
    void set_compiler(const std::string& compiler);
    CompileCommands compile(bool rebuild, int depth = 0);
    friend std::ostream& operator<<(std::ostream& os, const Unit& unit);
};

// ----------------------------------------------------------------------------------
// CompileCommand
// ----------------------------------------------------------------------------------

inline bool CompileCommand::is_enabled() const
{
    return enabled;
}

inline bool CompileCommand::is_compile() const
{
    return compile;
}

inline int CompileCommand::execute() const
{
    if (!enabled)
    {
        return 0;
    }
    auto [output, error_output, exit_code] = run_process(command, args);
    if (exit_code != 0)
    {
        std::cout << "Exit code: " << exit_code << "\n";
    }
    if (output.size() > 0)
    {
        std::cout << "stdout: \n" << output << std::endl;
    }
    if (error_output.size() > 0)
    {
        std::cout << "stderr: \n" << error_output << std::endl;
    }
    return exit_code;
}

inline const std::string CompileCommand::get_abs_file() const
{
    std::filesystem::path rel_path(args.back());
    std::filesystem::path abs_path = std::filesystem::absolute(rel_path);
    return abs_path;
}

inline void CompileCommand::print(std::ostream& os) const
{
    os << command << " ";
    size_t arg_count = 0;
    for (const auto& arg : args)
    {
        os << arg;
        if (arg_count + 1 != args.size())
        {
            os << " ";
        }
        arg_count++;
    }
}

inline std::ostream& operator<<(std::ostream& os, const CompileCommand& cc)
{
    cc.print(os);
    os << " enabled: " << cc.is_enabled();
    return os;
}

// ----------------------------------------------------------------------------------
// CompileCommands
// ----------------------------------------------------------------------------------

inline void CompileCommands::add_cmd(size_t depth, const CompileCommand& compile_command)
{
    // fill to the insertion depth (all depth - 1 should already be populated)
    while (commands.size() <= depth)
    {
        commands.emplace_back();
    }

    commands[depth].push_back(compile_command);
}

inline void CompileCommands::execute() const
{
    const int max_concurrent_jobs = std::thread::hardware_concurrency();
    Semaphore sem(max_concurrent_jobs);
    std::vector<std::future<int>> jobs;
    Timer timer;

    for (const auto& compile_level : std::views::reverse(commands))
    {
        jobs.clear();
        for (const auto& cmd : compile_level)
        {
            if (cmd.is_enabled())
            {
                sem.acquire(); // Wait for a slot

                jobs.push_back(std::async(std::launch::async, [&, cmd]() {
                    std::cout << "Running: " << cmd << "\n";
                    int result = cmd.execute();
                    sem.release(); // Release slot when done
                    return result;
                }));
            }
        }
        // Wait for all jobs in this level to finish
        for (auto& job : jobs)
        {
            if (job.get() != 0)
            {
                std::cerr << "Command failed.\n";
                std::exit(1);
            }
        }
    }

    std::cout << "Compilation finished in: " << timer << std::endl;
}

inline void CompileCommands::write() const
{
    std::vector<CompileCommand> filtered_commands;
    for (const auto& cc : commands)
    {
        for (const auto& c : cc)
        {
            if (c.is_compile())
            {
                filtered_commands.push_back(c);
            }
        }
    }

    std::ofstream out_file("compile_commands.json");
    if (out_file)
    {
        out_file << "[\n";
        for (size_t i = 0; i < filtered_commands.size(); i++)
        {
            out_file << "\t{\n";
            // std::cout << c << std::endl;
            out_file << "\t\t\"directory\": \".\",\n";
            out_file << "\t\t\"command\": \"";
            filtered_commands[i].print(out_file);
            out_file << "\",\n";
            out_file << "\t\t\"file\":";
            out_file << "\"" << filtered_commands[i].get_abs_file() << "\"";
            out_file << "\n\t}";
            if (i + 1 != filtered_commands.size())
            {
                out_file << ",\n";
            }
        }
        out_file << "\n]\n";
    }
}

inline std::ostream& operator<<(std::ostream& os, CompileCommands compile_commands)
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

// ----------------------------------------------------------------------------------
// Unit
// ----------------------------------------------------------------------------------

inline void Unit::print_depth_impl(int depth) const
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

inline bool Unit::compile_impl(CompileCommands& compile_commands, int depth, TargetType target_type_parent,
                               const bool full_rebuild, const std::vector<std::string>& inherited_compile_flags) const
{
    std::vector<std::string> local_compile_flags;
    local_compile_flags.insert(local_compile_flags.end(), inherited_compile_flags.begin(),
                               inherited_compile_flags.end());

    local_compile_flags.insert(local_compile_flags.end(), compile_flags.begin(), compile_flags.end());
    // Recurse into dependencies
    std::vector<std::string> dep_target_objects;
    std::vector<std::string> header_deps;
    bool parent_rebuild = false;

    if (target_type == TargetType::EXECUTABLE || target_type == TargetType::DYNAMIC_LIB ||
        target_type == TargetType::STATIC_LIB)
    {
        target_type_parent = target_type;
    }

    for (const auto& dep : deps)
    {
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
                rebuild = rebuild ||
                          std::filesystem::last_write_time(header_dep) > std::filesystem::last_write_time(*target_path);
            }
            std::cout << std::endl;
        }
        if (source_path)
        {
            rebuild = rebuild ||
                      std::filesystem::last_write_time(*source_path) > std::filesystem::last_write_time(*target_path);

            std::vector<std::string> args;

            if (target_type_parent == TargetType::DYNAMIC_LIB)
            {
                args.push_back("-fPIC");
            }

            args.insert(args.end(), local_compile_flags.begin(), local_compile_flags.end());

            args.insert(args.end(), {"-MMD", "-c", "-o", *target_path, *source_path});
            // .cpp -> .o compiling
            compile_commands.add_cmd(depth, CompileCommand(compiler, args, rebuild || full_rebuild, true));
        }
        else
        {
            // .o -> .exe linking
            std::vector<std::string> args;

            std::string compiler = this->compiler;
            if (target_type == TargetType::DYNAMIC_LIB)
            {
                args.push_back("-shared");
            }
            else if (target_type == TargetType::STATIC_LIB)
            {
                compiler = "ar";
                args.push_back("rcs");
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

            compile_commands.add_cmd(depth, CompileCommand(compiler, args, rebuild || full_rebuild, false));
        }
        return rebuild;
    }
    return false;
}

inline Unit::Unit(const std::optional<std::string>& source_path, const std::optional<std::string>& target_path)
    : source_path(source_path), target_path(target_path), target_type(TargetType::NONE), compiler("error")
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
        else if (extension == ".exe" || extension.empty())
        {
            target_type = TargetType::EXECUTABLE;
        }
    }
}

inline void Unit::add_dep(std::unique_ptr<Unit> unit)
{
    deps.push_back(std::move(unit));
}

inline void Unit::add_link_flag(const std::string& flag)
{
    link_flags.emplace_back(flag);
}

inline void Unit::add_link_flags(const std::vector<std::string>& flags)
{
    link_flags.insert(link_flags.end(), flags.begin(), flags.end());
}

inline void Unit::add_compile_flag(const std::string& flag)
{
    compile_flags.emplace_back(flag);
}

inline void Unit::add_compile_flags(const std::vector<std::string>& flags)
{
    compile_flags.insert(compile_flags.end(), flags.begin(), flags.end());
}

inline void Unit::print_depth()
{
    print_depth_impl(0);
}

inline void Unit::set_compiler(const std::string& compiler)
{
    this->compiler = compiler;
    for (auto& dep : deps)
    {
        dep->set_compiler(compiler);
    }
}

inline CompileCommands Unit::compile(bool rebuild, int depth)
{
    CompileCommands compile_commands;
    compile_impl(compile_commands, depth, target_type, rebuild, {});
    return compile_commands;
}

inline std::ostream& operator<<(std::ostream& os, const Unit& unit)
{
    return os;
}

// ----------------------------------------------------------------------------------
// Utils
// ----------------------------------------------------------------------------------

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
