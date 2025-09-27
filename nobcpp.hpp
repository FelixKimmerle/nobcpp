#pragma once
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>
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

    std::array<Unit, 6> units = {
        {{duration_cast<duration<double, std::ratio<3600>>>(dur).count(), "h"},
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
    os << std::fixed << std::setprecision(2)
       << duration_cast<duration<double, std::nano>>(dur).count() << "ns";
    return os;
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

inline ProcessResult run_process(const std::string& cmd,
                                 const std::vector<std::string>& args)
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
        const std::string enable_color_flag = "-fdiagnostics-color=always";

        // Build argv
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(cmd.c_str()));
        for (const auto& arg : args)
        {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }

        if (cmd == "gcc" || cmd == "g++" || cmd == "c++" || cmd == "clang" ||
            cmd == "clang++")
        {
            argv.push_back(const_cast<char*>(enable_color_flag.c_str()));
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

    bool needs_recompile =
        !fs::exists(bin) || fs::last_write_time(src) > fs::last_write_time(bin);
    if (!needs_recompile)
    {
        for (const auto& dep : deps)
        {
            needs_recompile |=
                !fs::exists(dep) || fs::last_write_time(dep) > fs::last_write_time(bin);
        }
    }

    if (needs_recompile)
    {
        std::cout << "Rebuilding: " << bin << "...\n";
        std::string temp_bin = bin.string() + ".new";
        std::string cmd = "c++ -std=c++20 -Wall -Wextra -Wpedantic -O3 -o " +
                          bin.string() + " " + src.string();
        int ret = std::system(cmd.c_str());
        if (ret != 0)
        {
            std::cerr << "Compilation failed (exit = " << ret << ")\n";
            exit(ret);
        }

        std::rename(temp_bin.c_str(), bin.string().c_str());
        std::vector<char*> new_argv;
        new_argv.push_back(argv[0]);
        std::string flag = "nob_rebuild";
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
    CompileCommand(const std::string& command, const std::vector<std::string> args,
                   bool enabled, bool compile);
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
    std::vector<CompileCommand> cmds;
    std::vector<std::vector<int>> outs;
    std::vector<int> in_degree;

  public:
    int add_cmd(const CompileCommand& compile_command);
    bool add_edge(int src, int dst);
    void execute(int max_parallel = 0) const;
    void write() const;
    friend std::ostream& operator<<(std::ostream& os, CompileCommands compile_commands);
};

inline CompileCommand::CompileCommand(const std::string& command,
                                      const std::vector<std::string> args, bool enabled,
                                      bool compile)
    : command(command), args(args), enabled(enabled), compile(compile)
{
}

class Profile
{
  private:
    std::vector<std::string> compile_flags;
    std::vector<std::string> link_flags;

  public:
    Profile(const std::vector<std::string>& compile_flags = {},
            const std::vector<std::string>& link_flags = {})
        : compile_flags(compile_flags), link_flags(link_flags)
    {
    }

    const std::vector<std::string>& get_compile_flags() const
    {
        return compile_flags;
    }

    const std::vector<std::string>& get_link_flags() const
    {
        return link_flags;
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
    std::set<std::string> active_profiles;
    TargetType target_type;
    std::string compiler;
    mutable std::optional<int> node_id;

    void print_depth_impl(int depth) const;

    bool compile_impl(CompileCommands& compile_commands, TargetType target_type_parent,
                      const bool full_rebuild,
                      const std::vector<std::string>& inherited_compile_flags) const;
    void clean_impl(CompileCommands& compile_commands) const;
    void apply_profile(const std::string& name, const Profile& profile);

  public:
    Unit(const std::optional<std::string>& source_path,
         const std::optional<std::string>& target_path = std::nullopt);

    void add_dep(std::unique_ptr<Unit> unit);
    void add_link_flag(const std::string& flag);
    void add_link_flags(const std::vector<std::string>& flags);
    void add_compile_flag(const std::string& flag);
    void add_compile_flags(const std::vector<std::string>& flags);
    void print_depth();
    void set_compiler(const std::string& compiler);
    CompileCommands compile(bool rebuild) const;
    CompileCommands clean(bool remove_dir) const;
    std::string get_target() const;
    void parse(int argc, char** argv,
               const std::unordered_map<std::string, Profile>& profiles = {});
    friend std::ostream& operator<<(std::ostream& os, const Unit& unit);
};

// ----------------------------------------------------------------------------------
// Parse command line args
// ----------------------------------------------------------------------------------

using CmdCommand = std::function<void(const Unit*)>;
static std::unordered_map<std::string, CmdCommand> commands = {
    {"build",
     [](const Unit* unit) {
         std::cout << "build" << std::endl;
         CompileCommands cc = unit->compile(false);
         cc.execute();
         cc.write();
     }},
    {"clean",
     [](const Unit* unit) {
         std::cout << "clean" << std::endl;
         CompileCommands cc = unit->clean(false);
         std::cout << cc << std::endl;
         cc.execute();
     }},
    {"cleanall",
     [](const Unit* unit) {
         std::cout << "clean all" << std::endl;
         CompileCommands cc = unit->clean(true);
         std::cout << cc << std::endl;
         cc.execute();
     }},
    {"run",
     [](const Unit* unit) {
         std::cout << "run" << std::endl;
         auto [output, error_output, exit_code] = run_process(unit->get_target(), {});
         std::system(unit->get_target().c_str());
     }},
    {"rebuild", [](const Unit* unit) {
         std::cout << "rebuild" << std::endl;
         CompileCommands cc = unit->compile(true);
         std::cout << cc << std::endl;
         cc.execute();
         cc.write();
     }}};

inline void Unit::parse(int argc, char** argv,
                        const std::unordered_map<std::string, Profile>& profiles)
{
    std::vector<std::string> cmd_flags;
    cmd_flags.reserve(argc - 1);

    bool nob_rebuild_present = false;
    bool rebuild_present = false;
    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg == "nob_rebuild")
        {
            nob_rebuild_present = true;
            continue;
        }
        if (arg == "rebuild")
        {
            rebuild_present = true;
        }
        cmd_flags.push_back(arg);
    }

    if (nob_rebuild_present && !rebuild_present)
    {
        cmd_flags.insert(cmd_flags.begin(), "rebuild");
    }

    if (cmd_flags.size() == 0)
    {
        std::cout << "No flags specified!" << std::endl;
    }

    for (const std::string& cmd_flag : cmd_flags)
    {

        if (commands.contains(cmd_flag))
        {
            commands[cmd_flag](this);
        }
        else if (profiles.contains(cmd_flag))
        {
            const Profile& profile = profiles.at(cmd_flag);
            apply_profile(cmd_flag, profile);
        }
        else
        {
            std::cout << "Flag: " << cmd_flag << " unknown!" << std::endl;
        }
    }
}

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

    Timer timer;
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
    std::cout << "Took: " << timer << std::endl;
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

inline int CompileCommands::add_cmd(const CompileCommand& compile_command)
{
    int idx = static_cast<int>(cmds.size());
    cmds.push_back(compile_command);
    outs.emplace_back();
    in_degree.push_back(0);
    return idx;
}

inline bool CompileCommands::add_edge(int src, int dst)
{
    if (src < 0 || dst < 0 || src >= (int)cmds.size() || dst >= (int)cmds.size())
    {
        return false;
    }
    outs[src].push_back(dst);
    in_degree[dst]++;
    return true;
}

inline void CompileCommands::execute(int max_parallel) const
{

    int P = max_parallel;
    if (P <= 0)
    {
        unsigned hc = std::thread::hardware_concurrency();
        P = hc == 0 ? 1 : static_cast<int>(hc);
    }
    if (P < 1)
        P = 1;

    const int n = static_cast<int>(cmds.size());
    if (n == 0)
    {
        std::cout << "Compilation finished in: 0.00ms\n";
        return;
    }

    // Working indegrees (atomic for concurrency)
    std::vector<std::atomic<int>> indeg(n);
    for (int i = 0; i < n; ++i)
    {
        int d = cmds[i].is_enabled() ? in_degree[i] : 0; // disabled = already done
        indeg[i].store(d, std::memory_order_relaxed);
    }

    // Ready queue (MPMC via mutex+condvar)
    struct Ready
    {
        mutable std::mutex m;
        std::condition_variable cv;
        std::queue<int> q;
        void push(int t)
        {
            {
                std::lock_guard<std::mutex> lk(m);
                q.push(t);
            }
            cv.notify_one();
        }

        void notify_all()
        {
            cv.notify_all();
        }
        bool try_pop(int& t)
        {
            std::lock_guard<std::mutex> lk(m);
            if (q.empty())
                return false;
            t = q.front();
            q.pop();
            return true;
        }
        bool wait_pop(int& t, std::atomic<int>& remaining, std::atomic<bool>& stop)
        {
            std::unique_lock<std::mutex> lk(m);
            cv.wait(lk, [&] {
                return !q.empty() || remaining.load(std::memory_order_acquire) == 0 ||
                       stop.load(std::memory_order_relaxed);
            });
            if (!q.empty())
            {
                t = q.front();
                q.pop();
                return true;
            }
            return false;
        }

        bool empty() const
        {
            std::lock_guard<std::mutex> lk(m);
            return q.empty();
        }
    } ready;

    std::atomic<int> remaining{0};

    // Seed: enabled with indegree 0; disabled propagate immediately
    for (int i = 0; i < n; ++i)
    {
        if (cmds[i].is_enabled())
        {
            remaining.fetch_add(1, std::memory_order_relaxed);
            if (indeg[i].load(std::memory_order_relaxed) == 0)
            {
                ready.push(i);
            }
        }
        else
        {
            for (int d : outs[i])
            {
                int newdeg = indeg[d].fetch_sub(1, std::memory_order_acq_rel) - 1;
                if (newdeg == 0 && cmds[d].is_enabled())
                    ready.push(d);
            }
        }
    }

    std::atomic<bool> stop{false};
    std::atomic<int> failures{0};
    Timer timer;

    auto worker = [&]() {
        while (true)
        {

            if (stop.load(std::memory_order_relaxed))
                break;
            if (remaining.load(std::memory_order_acquire) == 0 && ready.empty())
                break;
            int t = -1;
            if (!ready.try_pop(t))
            {

                if (stop.load(std::memory_order_relaxed))
                    break;
                if (remaining.load(std::memory_order_acquire) == 0 && ready.empty())
                    break;
                if (!ready.wait_pop(t, remaining, stop))
                {
                    if (remaining.load(std::memory_order_acquire) == 0)
                        break;
                    if (stop.load(std::memory_order_relaxed))
                        break;
                    continue;
                }
            }

            int code = 0;
            try
            {
                std::cout << "Running: " << cmds[t] << "\n";
                code = cmds[t].execute();
            }
            catch (...)
            {
                code = -1;
            }

            if (code != 0)
            {
                failures.fetch_add(1, std::memory_order_acq_rel);
                stop.store(true, std::memory_order_release); // fail-fast
                ready.notify_all();
            }

            for (int d : outs[t])
            {
                int newdeg = indeg[d].fetch_sub(1, std::memory_order_acq_rel) - 1;
                if (newdeg == 0 && cmds[d].is_enabled())
                {
                    ready.push(d);
                }
            }
            // remaining.fetch_sub(1, std::memory_order_acq_rel);

            int prev = remaining.fetch_sub(1, std::memory_order_acq_rel);
            if (prev == 1)
            {
                // Just reached zero: wake all waiters so they can observe termination
                ready.notify_all();
            }
        }
    };

    std::vector<std::thread> pool;
    pool.reserve(static_cast<size_t>(P));
    for (int i = 0; i < P; ++i)
        pool.emplace_back(worker);
    for (auto& th : pool)
        th.join();

    if (failures.load(std::memory_order_relaxed) != 0)
    {
        std::cerr << "One or more commands failed.\n";
        std::exit(1);
    }
    std::cout << "Compilation finished in: " << timer << std::endl;
}

inline void CompileCommands::write() const
{

    std::vector<CompileCommand> filtered;
    filtered.reserve(cmds.size());
    for (const auto& c : cmds)
    {
        if (c.is_compile())
        {
            filtered.push_back(c);
        }
    }

    std::ofstream out_file("compile_commands.json");
    if (!out_file)
    {
        return;
    }

    out_file << "[\n";
    for (size_t i = 0; i < filtered.size(); i++)
    {
        out_file << "\t{\n";
        out_file << "\t\t\"directory\": \".\",\n";
        out_file << "\t\t\"command\": \"";
        filtered[i].print(out_file);
        out_file << "\",\n";
        out_file << "\t\t\"file\": \"" << filtered[i].get_abs_file() << "\"\n";
        out_file << "\t}";
        if (i + 1 != filtered.size())
        {
            out_file << ",\n";
        }
    }
    out_file << "\n]\n";
}

inline std::ostream& operator<<(std::ostream& os, CompileCommands compile_commands)
{
    for (size_t i = 0; i < compile_commands.cmds.size(); ++i)
    {
        os << "#" << i << " " << compile_commands.cmds[i] << "\n";
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

inline bool Unit::compile_impl(
    CompileCommands& compile_commands, TargetType target_type_parent,
    const bool full_rebuild,
    const std::vector<std::string>& inherited_compile_flags) const
{
    std::vector<std::string> local_compile_flags;
    local_compile_flags.insert(local_compile_flags.end(), inherited_compile_flags.begin(),
                               inherited_compile_flags.end());

    local_compile_flags.insert(local_compile_flags.end(), compile_flags.begin(),
                               compile_flags.end());
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
        bool rebuild = dep->compile_impl(compile_commands, target_type_parent,
                                         full_rebuild, local_compile_flags);
        parent_rebuild |= rebuild;
    }

    if (target_path)
    {
        std::filesystem::create_directories(
            std::filesystem::path(*target_path).parent_path());
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

            args.insert(args.end(), local_compile_flags.begin(),
                        local_compile_flags.end());

            args.insert(args.end(), {"-MMD", "-c", "-o", *target_path, *source_path});
            // .cpp -> .o compiling
            int node = compile_commands.add_cmd(
                CompileCommand(compiler, args, rebuild || full_rebuild, true));
            node_id = node;
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

            if (target_type == TargetType::DYNAMIC_LIB ||
                target_type == TargetType::EXECUTABLE)
            {
                args.insert(args.end(), link_flags.begin(), link_flags.end());
            }

            args.push_back("-o");
            args.push_back(*target_path);

            for (const auto& target : dep_target_objects)
            {
                args.push_back(target);
                rebuild = rebuild || std::filesystem::last_write_time(target) >
                                         std::filesystem::last_write_time(*target_path);
            }

            int link_node = compile_commands.add_cmd(
                CompileCommand(compiler, args, rebuild || full_rebuild, false));
            node_id = link_node;

            // Wire edges from each direct child’s node to this link/archive node
            for (const auto& dep : deps)
            {
                if (dep->node_id.has_value())
                {
                    compile_commands.add_edge(dep->node_id.value(), link_node);
                }
            }
        }
        return rebuild;
    }
    node_id.reset();
    return false;
}

inline void Unit::clean_impl(CompileCommands& compile_commands) const
{
    for (const auto& dep : deps)
    {
        dep->clean_impl(compile_commands);
    }
    if (target_path)
    {
        compile_commands.add_cmd(CompileCommand(
            "rm", {*target_path}, std::filesystem::exists(*target_path), false));

        if (target_type == TargetType::OBJECT)
        {

            const auto dfile =
                std::filesystem::path(*target_path).parent_path().string() + "/" +
                std::filesystem::path(*target_path).stem().string() + ".d";
            compile_commands.add_cmd(
                CompileCommand("rm", {dfile}, std::filesystem::exists(dfile), false));
        }
    }
}

inline void Unit::apply_profile(const std::string& name, const Profile& profile)
{
    active_profiles.insert(name);
    add_compile_flags(profile.get_compile_flags());
    add_link_flags(profile.get_link_flags());
}

inline Unit::Unit(const std::optional<std::string>& source_path,
                  const std::optional<std::string>& target_path)
    : source_path(source_path), target_path(target_path), target_type(TargetType::NONE),
      compiler("c++")
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

inline CompileCommands Unit::compile(bool rebuild) const
{
    CompileCommands compile_commands;
    compile_impl(compile_commands, target_type, rebuild, {});
    return compile_commands;
}

inline CompileCommands Unit::clean(bool remove_dir = false) const
{
    CompileCommands compile_commands;
    if (!remove_dir)
    {
        clean_impl(compile_commands);
    }
    else
    {
        compile_commands.add_cmd(CompileCommand("rm", {"-r", "build"},
                                                std::filesystem::exists("build"), false));
    }
    return compile_commands;
}

inline std::string Unit::get_target() const
{
    return *target_path;
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

inline std::vector<std::string> parse_dependency_file(
    const std::filesystem::path& d_file_path)
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

inline std::unique_ptr<Unit> build_tree_from_cpp_files(
    const std::filesystem::path& root_dir, const std::filesystem::path& target)
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
