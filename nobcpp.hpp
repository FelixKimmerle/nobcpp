#pragma once
// nobcpp - a single header C++ build tool in the spirit of tsoding's nob.h
//
// Bootstrap once with any C++20 compiler, afterwards the build script rebuilds itself:
//     c++ -std=c++20 -o nobcpp nobcpp.cpp        (Linux / MSYS2)
//     cl /nologo /std:c++20 /EHsc nobcpp.cpp     (MSVC developer prompt)
//
// Run `./nobcpp help` for the command line.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <io.h>
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <spawn.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace nob
{

namespace fs = std::filesystem;
using SteadyClock = std::chrono::steady_clock;

// ----------------------------------------------------------------------------------
// Utils
// ----------------------------------------------------------------------------------

inline double seconds_since(SteadyClock::time_point start)
{
    return std::chrono::duration<double>(SteadyClock::now() - start).count();
}

inline std::string format_duration(double seconds)
{
    char buf[32];
    if (seconds < 0.001)
    {
        std::snprintf(buf, sizeof(buf), "%.0fus", seconds * 1e6);
    }
    else if (seconds < 1.0)
    {
        std::snprintf(buf, sizeof(buf), "%.0fms", seconds * 1e3);
    }
    else if (seconds < 60.0)
    {
        std::snprintf(buf, sizeof(buf), "%.2fs", seconds);
    }
    else
    {
        int total = static_cast<int>(seconds + 0.5);
        std::snprintf(buf, sizeof(buf), "%dm%02ds", total / 60, total % 60);
    }
    return buf;
}

inline std::string str_lower(std::string s)
{
    for (char& c : s)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

inline std::vector<std::string> split(const std::string& s, char delim)
{
    std::vector<std::string> parts;
    std::string item;
    std::istringstream ss(s);
    while (std::getline(ss, item, delim))
    {
        if (!item.empty())
        {
            parts.push_back(item);
        }
    }
    return parts;
}

inline std::string join(const std::vector<std::string>& parts, const std::string& sep)
{
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (i != 0)
        {
            out += sep;
        }
        out += parts[i];
    }
    return out;
}

template <typename T>
void append(std::vector<T>& dst, const std::vector<T>& src)
{
    dst.insert(dst.end(), src.begin(), src.end());
}

// Stable across runs and platforms (unlike std::hash), used to detect flag changes.
inline uint64_t hash_args(const std::vector<std::string>& args)
{
    uint64_t h = 1469598103934665603ull;
    for (const auto& arg : args)
    {
        for (unsigned char c : arg)
        {
            h = (h ^ c) * 1099511628211ull;
        }
        h = (h ^ 0xffu) * 1099511628211ull;
    }
    return h;
}

// Removes CSI (colors) and OSC (hyperlinks) escape sequences.
inline std::string strip_ansi(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[')
        {
            i += 2;
            while (i < s.size() && !(s[i] >= 0x40 && s[i] <= 0x7e))
            {
                ++i;
            }
        }
        else if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == ']')
        {
            i += 2;
            while (i < s.size() && s[i] != '\a' && !(s[i] == '\x1b' && i + 1 < s.size()))
            {
                ++i;
            }
            if (i < s.size() && s[i] == '\x1b')
            {
                ++i;
            }
        }
        else
        {
            out += s[i];
        }
    }
    return out;
}

inline std::string json_escape(std::string_view s)
{
    std::string out;
    for (char c : s)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            }
            else
            {
                out += c;
            }
        }
    }
    return out;
}

inline std::string display_path(const fs::path& p)
{
    return p.lexically_normal().generic_string();
}

// last_write_time without exceptions: a missing file simply has no time.
inline std::optional<fs::file_time_type> file_mtime(const fs::path& p)
{
    std::error_code ec;
    auto t = fs::last_write_time(p, ec);
    if (ec)
    {
        return std::nullopt;
    }
    return t;
}

inline int64_t file_clock_now()
{
    return fs::file_time_type::clock::now().time_since_epoch().count();
}

// ----------------------------------------------------------------------------------
// Console: every line of output goes through here, so nothing interleaves and the
// progress bar can be redrawn below whatever was printed.
// ----------------------------------------------------------------------------------

enum class ColorMode
{
    Auto,
    Always,
    Never
};

class Console
{
  public:
    static Console& get()
    {
        static Console console;
        return console;
    }

    void configure(ColorMode mode)
    {
        if (mode == ColorMode::Always)
        {
            color_ = true;
        }
        else if (mode == ColorMode::Never)
        {
            color_ = false;
        }
        else
        {
            const char* no_color = std::getenv("NO_COLOR");
            const char* force = std::getenv("CLICOLOR_FORCE");
            const char* term = std::getenv("TERM");
            color_ = (tty_ && !(no_color && *no_color) &&
                      !(term && std::string_view(term) == "dumb")) ||
                     (force && std::string_view(force) != "0");
        }
    }

    bool tty() const
    {
        return tty_;
    }

    bool color() const
    {
        return color_;
    }

    int width() const
    {
#ifdef _WIN32
        CONSOLE_SCREEN_BUFFER_INFO info;
        if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
        {
            return info.srWindow.Right - info.srWindow.Left + 1;
        }
#else
        winsize ws{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        {
            return ws.ws_col;
        }
#endif
        return 80;
    }

    std::string paint(std::string_view codes, std::string_view text) const
    {
        if (!color_)
        {
            return std::string(text);
        }
        return "\x1b[" + std::string(codes) + "m" + std::string(text) + "\x1b[0m";
    }
    std::string bold(std::string_view t) const { return paint("1", t); }
    std::string dim(std::string_view t) const { return paint("2", t); }
    std::string red(std::string_view t) const { return paint("1;31", t); }
    std::string green(std::string_view t) const { return paint("1;32", t); }
    std::string yellow(std::string_view t) const { return paint("1;33", t); }
    std::string cyan(std::string_view t) const { return paint("36", t); }

    // Atomically prints `text` above the status line.
    void print(std::string_view text)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_shown_)
        {
            write_raw("\r\x1b[2K");
        }
        write_raw(text);
        status_shown_ = false;
        draw_status();
        std::fflush(stdout);
    }

    void println(std::string_view text)
    {
        print(std::string(text) + "\n");
    }

    void error(std::string_view text)
    {
        println(red("error: ") + std::string(text));
    }

    void warning(std::string_view text)
    {
        println(yellow("warning: ") + std::string(text));
    }

    void set_status(std::string status)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = std::move(status);
        draw_status();
        std::fflush(stdout);
    }

    void clear_status()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_shown_)
        {
            write_raw("\r\x1b[2K");
            std::fflush(stdout);
        }
        status_.clear();
        status_shown_ = false;
    }

    // Cuts a string to `width` visible characters, skipping escape codes and
    // counting UTF-8 code points.
    static std::string truncate_visible(std::string_view s, int width)
    {
        std::string out;
        int visible = 0;
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '\x1b')
            {
                size_t j = i + 1;
                while (j < s.size() && !(s[j] >= 0x40 && s[j] <= 0x7e && j > i + 1))
                {
                    ++j;
                }
                out.append(s.substr(i, j - i + 1));
                i = j;
                continue;
            }
            bool continuation = (static_cast<unsigned char>(s[i]) & 0xc0) == 0x80;
            if (!continuation)
            {
                if (visible == width)
                {
                    break;
                }
                ++visible;
            }
            out += s[i];
        }
        return out;
    }

  private:
    Console()
    {
#ifdef _WIN32
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (_isatty(_fileno(stdout)) && GetConsoleMode(out, &mode))
        {
            tty_ = SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
            SetConsoleOutputCP(CP_UTF8);
        }
#else
        tty_ = isatty(STDOUT_FILENO) != 0;
#endif
        configure(ColorMode::Auto);
    }

    void write_raw(std::string_view text)
    {
        std::fwrite(text.data(), 1, text.size(), stdout);
    }

    void draw_status()
    {
        if (!tty_ || status_.empty())
        {
            return;
        }
        write_raw("\r\x1b[2K");
        write_raw(truncate_visible(status_, width() - 1));
        if (color_)
        {
            write_raw("\x1b[0m");
        }
        status_shown_ = true;
    }

    std::mutex mutex_;
    std::string status_;
    bool status_shown_ = false;
    bool tty_ = false;
    bool color_ = false;
};

inline Console& console()
{
    return Console::get();
}

// ----------------------------------------------------------------------------------
// Processes
// ----------------------------------------------------------------------------------

struct ProcessResult
{
    int exit_code = -1;
    std::string output; // stdout and stderr merged, in the order they were written
    bool started = false;
};

// Pipes must not leak into children spawned concurrently by other threads, otherwise
// a job's output pipe only reaches EOF once unrelated jobs exit. Pipe creation and
// spawning are therefore serialized.
inline std::mutex& spawn_mutex()
{
    static std::mutex m;
    return m;
}

#ifdef _WIN32

inline std::string quote_arg_windows(const std::string& arg)
{
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos)
    {
        return arg;
    }
    std::string out = "\"";
    for (size_t i = 0;; ++i)
    {
        size_t backslashes = 0;
        while (i < arg.size() && arg[i] == '\\')
        {
            ++i;
            ++backslashes;
        }
        if (i == arg.size())
        {
            out.append(backslashes * 2, '\\');
            break;
        }
        if (arg[i] == '"')
        {
            out.append(backslashes * 2 + 1, '\\');
        }
        else
        {
            out.append(backslashes, '\\');
        }
        out += arg[i];
    }
    out += '"';
    return out;
}

inline std::string windows_command_line(const std::vector<std::string>& args)
{
    std::string cmdline;
    for (size_t i = 0; i < args.size(); ++i)
    {
        if (i != 0)
        {
            cmdline += ' ';
        }
        cmdline += quote_arg_windows(args[i]);
    }
    return cmdline;
}

inline std::string last_error_message()
{
    char buf[512] = {0};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   GetLastError(), 0, buf, sizeof(buf), nullptr);
    return buf;
}

inline ProcessResult run_capture(const std::vector<std::string>& args)
{
    std::string cmdline = windows_command_line(args);
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE read_end = nullptr, write_end = nullptr;
    PROCESS_INFORMATION pi{};
    BOOL ok = FALSE;
    {
        std::lock_guard<std::mutex> lock(spawn_mutex());
        if (!CreatePipe(&read_end, &write_end, &sa, 0))
        {
            return {-1, "CreatePipe failed: " + last_error_message(), false};
        }
        SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
        HANDLE nul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 &sa, OPEN_EXISTING, 0, nullptr);
        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = nul;
        si.hStdOutput = write_end;
        si.hStdError = write_end;
        ok = CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, TRUE, 0, nullptr,
                            nullptr, &si, &pi);
        CloseHandle(write_end);
        if (nul != INVALID_HANDLE_VALUE)
        {
            CloseHandle(nul);
        }
    }
    if (!ok)
    {
        std::string msg = "failed to start '" + args[0] + "': " + last_error_message();
        CloseHandle(read_end);
        return {127, msg, false};
    }

    ProcessResult result;
    result.started = true;
    char buffer[4096];
    DWORD count = 0;
    while (ReadFile(read_end, buffer, sizeof(buffer), &count, nullptr) && count > 0)
    {
        result.output.append(buffer, count);
    }
    CloseHandle(read_end);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    result.exit_code = static_cast<int>(code);
    return result;
}

// Runs with inherited stdin/stdout/stderr, for running the built program.
inline int run_interactive(const std::vector<std::string>& args)
{
    std::string cmdline = windows_command_line(args);
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        nullptr, &si, &pi))
    {
        console().error("failed to start '" + args[0] + "': " + last_error_message());
        return 127;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(code);
}

#else

inline std::vector<char*> make_argv(const std::vector<std::string>& args)
{
    std::vector<char*> argv;
    for (const auto& arg : args)
    {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    return argv;
}

inline int decode_wait_status(int status, std::string* output)
{
    if (WIFEXITED(status))
    {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status))
    {
        if (output)
        {
            *output += "\nprocess killed by signal " + std::to_string(WTERMSIG(status)) +
                       " (" + strsignal(WTERMSIG(status)) + ")\n";
        }
        return 128 + WTERMSIG(status);
    }
    return 1;
}

inline ProcessResult run_capture(const std::vector<std::string>& args)
{
    std::vector<char*> argv = make_argv(args);
    int fds[2];
    pid_t pid = 0;
    int err = 0;
    {
        std::lock_guard<std::mutex> lock(spawn_mutex());
        if (pipe(fds) != 0)
        {
            return {-1, std::string("pipe failed: ") + std::strerror(errno), false};
        }
        fcntl(fds[0], F_SETFD, FD_CLOEXEC);
        fcntl(fds[1], F_SETFD, FD_CLOEXEC);

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, fds[1], STDERR_FILENO);
        err = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        close(fds[1]);
    }
    if (err != 0)
    {
        close(fds[0]);
        return {127, "failed to start '" + args[0] + "': " + std::strerror(err), false};
    }

    ProcessResult result;
    result.started = true;
    char buffer[4096];
    while (true)
    {
        ssize_t count = read(fds[0], buffer, sizeof(buffer));
        if (count > 0)
        {
            result.output.append(buffer, static_cast<size_t>(count));
        }
        else if (count == -1 && errno == EINTR)
        {
            continue;
        }
        else
        {
            break;
        }
    }
    close(fds[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) == -1 && errno == EINTR)
    {
    }
    result.exit_code = decode_wait_status(status, &result.output);
    return result;
}

inline int run_interactive(const std::vector<std::string>& args)
{
    std::vector<char*> argv = make_argv(args);
    pid_t pid = 0;
    int err = posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ);
    if (err != 0)
    {
        console().error("failed to start '" + args[0] + "': " + std::strerror(err));
        return 127;
    }
    int status = 0;
    while (waitpid(pid, &status, 0) == -1 && errno == EINTR)
    {
    }
    std::string msg;
    int code = decode_wait_status(status, &msg);
    if (!msg.empty())
    {
        console().print(msg);
    }
    return code;
}

#endif

// ----------------------------------------------------------------------------------
// Rebuild self
// ----------------------------------------------------------------------------------

inline std::string& self_source_path()
{
    static std::string path;
    return path;
}

inline fs::path self_exe_path(const char* argv0)
{
    std::error_code ec;
#ifdef _WIN32
    char buf[MAX_PATH * 4];
    DWORD len = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (len > 0 && len < sizeof(buf))
    {
        return fs::path(std::string(buf, len));
    }
#elif defined(__linux__)
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    if (!ec)
    {
        return p;
    }
#endif
    fs::path p2 = fs::absolute(argv0, ec);
    return ec ? fs::path(argv0) : p2;
}

// The compiler used to rebuild the build script itself. Defaults to the compiler
// family that built the currently running binary. Override with -DNOBCPP_SELF_CXX=...
inline std::vector<std::string> self_compile_command(const std::string& src,
                                                     const std::string& out)
{
#if defined(_MSC_VER)
#if defined(NOBCPP_SELF_CXX)
    std::string cxx = NOBCPP_SELF_CXX;
#elif defined(__clang__)
    std::string cxx = "clang-cl";
#else
    std::string cxx = "cl";
#endif
    std::string obj = (fs::path(out).parent_path() / fs::path(out).stem()).string() + ".obj";
    return {cxx, "/nologo", "/std:c++20", "/EHsc", "/Fo" + obj, "/Fe" + out, src};
#else
#if defined(NOBCPP_SELF_CXX)
    std::string cxx = NOBCPP_SELF_CXX;
#elif defined(__clang__)
    std::string cxx = "clang++";
#else
    std::string cxx = "g++";
#endif
    // unoptimized: it only starts processes, and every edit of the script is a rebuild
    return {cxx, "-std=c++20", "-O0", "-Wall", "-Wextra", "-pthread", "-o", out, src};
#endif
}

// Recompiles and restarts the build script if its source (or `deps`, relative to the
// source's directory) is newer than the running binary.
inline void rebuild_self(const std::string& source_filename, int argc, char** argv,
                         const std::vector<std::string>& deps = {})
{
    std::error_code ec;
    fs::path src = fs::absolute(source_filename, ec);
    self_source_path() = src.string();
    fs::path bin = self_exe_path(argv[0]);
    fs::path old_bin = bin.string() + ".old";
    fs::remove(old_bin, ec); // leftover from the previous rebuild (locked on Windows)

    // Guards against endless rebuild loops, e.g. caused by timestamps in the future.
    if (const char* rebuilt = std::getenv("NOBCPP_REBUILT"); rebuilt && *rebuilt)
    {
#ifdef _WIN32
        _putenv_s("NOBCPP_REBUILT", "");
#else
        unsetenv("NOBCPP_REBUILT");
#endif
        return;
    }

    auto bin_time = file_mtime(bin);
    auto src_time = file_mtime(src);
    if (!src_time)
    {
        console().warning("build script source '" + src.string() +
                          "' not found, skipping self-rebuild");
        return;
    }
    if (*src_time > fs::file_time_type::clock::now())
    {
        console().warning("'" + src.string() + "' has a modification time in the future, " +
                          "the build script will be rebuilt on every run");
    }
    bool needs_rebuild = !bin_time || *src_time > *bin_time;
    for (const auto& dep : deps)
    {
        fs::path dep_path = src.parent_path() / dep;
        auto dep_time = file_mtime(dep_path);
        if (!dep_time)
        {
            console().warning("build script dependency '" + dep_path.string() +
                              "' not found");
        }
        else if (bin_time && *dep_time > *bin_time)
        {
            needs_rebuild = true;
        }
    }
    if (!needs_rebuild)
    {
        return;
    }

    console().println(console().cyan("nobcpp:") + " build script changed, rebuilding " +
                      display_path(bin.filename()) + "...");
    // Renaming works even while the binary is running (also on Windows), and lets us
    // restore it if compilation fails.
    fs::rename(bin, old_bin, ec);
    bool moved = !ec;
    auto cmd = self_compile_command(src.string(), bin.string());
    ProcessResult result = run_capture(cmd);
    if (!result.output.empty())
    {
        console().print(result.output);
    }
    if (result.exit_code != 0)
    {
        if (moved)
        {
            fs::remove(bin, ec);
            fs::rename(old_bin, bin, ec);
        }
        console().error("rebuilding the build script failed: " + join(cmd, " "));
        std::exit(1);
    }

    std::vector<std::string> args = {bin.string()};
    for (int i = 1; i < argc; ++i)
    {
        args.push_back(argv[i]);
    }
#ifdef _WIN32
    _putenv_s("NOBCPP_REBUILT", "1");
    std::exit(run_interactive(args));
#else
    setenv("NOBCPP_REBUILT", "1", 1);
    std::vector<char*> new_argv = make_argv(args);
    execv(new_argv[0], new_argv.data());
    console().error(std::string("execv failed: ") + std::strerror(errno));
    std::exit(1);
#endif
}

// ----------------------------------------------------------------------------------
// Flags and profiles (compiler independent)
// ----------------------------------------------------------------------------------

enum class Opt
{
    O0,
    Og,
    O1,
    O2,
    O3,
    Os
};

enum class Warnings
{
    Off,
    On,   // -Wall      | /W3
    Extra // -Wall -Wextra -Wpedantic | /W4
};

// Compiler independent build settings. Unset optionals mean "compiler default".
// Raw `compile_flags`/`link_flags` are passed through unchanged, check
// Build::toolchain().family if you need compiler specific ones.
// Members are declared in the order designated initializers have to use.
struct Flags
{
    std::optional<int> std{};        // 17, 20, 23, ...
    std::optional<Opt> opt{};
    std::optional<bool> debug_info{};
    std::optional<Warnings> warnings{};
    std::optional<bool> werror{};
    std::optional<bool> lto{};
    std::vector<std::string> defines{};      // "NDEBUG", "VERSION=3"
    std::vector<std::string> include_dirs{};
    std::vector<std::string> system_include_dirs{}; // warnings in these headers are hidden
    std::vector<std::string> sanitizers{};   // "address", "undefined", "thread", ...
    std::vector<std::string> libs{};         // system libraries: "m" -> -lm / m.lib
    std::vector<std::string> compile_flags{};
    std::vector<std::string> link_flags{};

    // Values of `other` win, lists are appended.
    void merge(const Flags& other)
    {
        if (other.std) std = other.std;
        if (other.opt) opt = other.opt;
        if (other.debug_info) debug_info = other.debug_info;
        if (other.warnings) warnings = other.warnings;
        if (other.werror) werror = other.werror;
        if (other.lto) lto = other.lto;
        append(defines, other.defines);
        append(include_dirs, other.include_dirs);
        append(system_include_dirs, other.system_include_dirs);
        append(sanitizers, other.sanitizers);
        append(libs, other.libs);
        append(compile_flags, other.compile_flags);
        append(link_flags, other.link_flags);
    }
};

using Profile = Flags;

// ----------------------------------------------------------------------------------
// Toolchain
// ----------------------------------------------------------------------------------

enum class Kind
{
    Executable,
    StaticLib,
    SharedLib,
    HeaderOnly // nothing to build, only passes its public flags to dependents
};

enum class Family
{
    Gcc,
    Clang,
    Msvc,   // cl.exe
    ClangCl // clang-cl.exe, msvc compatible command line
};

inline const char* family_name(Family family)
{
    switch (family)
    {
    case Family::Gcc: return "gcc";
    case Family::Clang: return "clang";
    case Family::Msvc: return "msvc";
    case Family::ClangCl: return "clang-cl";
    }
    return "?";
}

#if defined(_WIN32) || defined(__CYGWIN__)
inline constexpr bool target_windows = true;
#else
inline constexpr bool target_windows = false;
#endif
#if defined(__APPLE__)
inline constexpr bool target_apple = true;
#else
inline constexpr bool target_apple = false;
#endif

inline void warn_once(const std::string& message)
{
    static std::mutex m;
    static std::set<std::string> seen;
    std::lock_guard<std::mutex> lock(m);
    if (seen.insert(message).second)
    {
        console().warning(message);
    }
}

struct Toolchain
{
    std::vector<std::string> cxx; // optional launcher (e.g. ccache) + compiler
    std::string ar;
    Family family = Family::Gcc;
    std::string version;

    bool msvc_like() const
    {
        return family == Family::Msvc || family == Family::ClangCl;
    }

    std::string obj_ext() const
    {
        return msvc_like() ? ".obj" : ".o";
    }

    std::string output_name(const std::string& name, Kind kind) const
    {
        switch (kind)
        {
        case Kind::Executable: return name + (target_windows ? ".exe" : "");
        case Kind::StaticLib: return msvc_like() ? name + ".lib" : "lib" + name + ".a";
        case Kind::SharedLib:
            if (msvc_like()) return name + ".dll";
            if (target_windows) return "lib" + name + ".dll";
            if (target_apple) return "lib" + name + ".dylib";
            return "lib" + name + ".so";
        case Kind::HeaderOnly: return "";
        }
        return name;
    }

    // Not part of the command hash or compile_commands.json.
    std::vector<std::string> color_flags() const
    {
        switch (family)
        {
        case Family::Gcc: return {"-fdiagnostics-color=always"};
        case Family::Clang:
            if (target_windows) return {"-fcolor-diagnostics", "-fansi-escape-codes"};
            return {"-fcolor-diagnostics"};
        case Family::ClangCl: return {"-fcolor-diagnostics", "-fansi-escape-codes"};
        case Family::Msvc: return {};
        }
        return {};
    }

    std::vector<std::string> sanitizers(const Flags& f) const
    {
        if (f.sanitizers.empty())
        {
            return {};
        }
        if (family == Family::Msvc)
        {
            std::vector<std::string> out;
            for (const auto& s : f.sanitizers)
            {
                if (s == "address")
                    out.push_back("/fsanitize=address");
                else
                    warn_once("sanitizer '" + s + "' is not supported by msvc, ignored");
            }
            return out;
        }
        return {"-fsanitize=" + join(f.sanitizers, ","), "-fno-omit-frame-pointer"};
    }

    std::vector<std::string> compile_flags(const Flags& f, bool pic) const
    {
        std::vector<std::string> out;
        if (msvc_like())
        {
            out = {"/nologo", "/EHsc"};
            if (f.std) out.push_back(*f.std > 20 ? "/std:c++latest"
                                                 : "/std:c++" + std::to_string(*f.std));
            if (f.opt)
            {
                switch (*f.opt)
                {
                case Opt::O0:
                case Opt::Og: out.push_back("/Od"); break;
                case Opt::O1:
                case Opt::Os: out.push_back("/O1"); break;
                case Opt::O2:
                case Opt::O3: out.push_back("/O2"); break;
                }
            }
            if (f.debug_info.value_or(false)) out.push_back("/Z7");
            if (f.warnings)
            {
                const char* w[] = {"/W0", "/W3", "/W4"};
                out.push_back(w[static_cast<int>(*f.warnings)]);
            }
            if (f.werror.value_or(false)) out.push_back("/WX");
            if (f.lto.value_or(false)) out.push_back(family == Family::Msvc ? "/GL" : "-flto");
            for (const auto& d : f.defines) out.push_back("/D" + d);
            for (const auto& i : f.include_dirs) out.push_back("/I" + i);
            for (const auto& i : f.system_include_dirs) append(out, {"/external:I", i});
            if (!f.system_include_dirs.empty()) out.push_back("/external:W0");
        }
        else
        {
            if (f.std) out.push_back("-std=c++" + std::to_string(*f.std));
            if (f.opt)
            {
                const char* o[] = {"-O0", "-Og", "-O1", "-O2", "-O3", "-Os"};
                out.push_back(o[static_cast<int>(*f.opt)]);
            }
            if (f.debug_info.value_or(false)) out.push_back("-g");
            if (f.warnings == Warnings::Off) out.push_back("-w");
            if (f.warnings == Warnings::On) out.push_back("-Wall");
            if (f.warnings == Warnings::Extra) append(out, {"-Wall", "-Wextra", "-Wpedantic"});
            if (f.werror.value_or(false)) out.push_back("-Werror");
            if (f.lto.value_or(false)) out.push_back("-flto");
            if (pic && !target_windows) out.push_back("-fPIC");
            for (const auto& d : f.defines) out.push_back("-D" + d);
            for (const auto& i : f.include_dirs) out.push_back("-I" + i);
            for (const auto& i : f.system_include_dirs) append(out, {"-isystem", i});
        }
        append(out, sanitizers(f));
        append(out, f.compile_flags);
        return out;
    }

    std::vector<std::string> compile_command(const Flags& f, const std::string& src,
                                             const std::string& obj,
                                             const std::string& depfile, bool pic) const
    {
        std::vector<std::string> cmd = cxx;
        append(cmd, compile_flags(f, pic));
        if (msvc_like())
        {
            append(cmd, {"/showIncludes", "/c", src, "/Fo" + obj});
        }
        else
        {
            // -MD instead of -MMD: -MMD would also drop headers from -isystem directories
            append(cmd, {"-MD", "-MF", depfile, "-c", src, "-o", obj});
        }
        return cmd;
    }

    // `inputs` are object files followed by libraries built by us, in link order.
    std::vector<std::string> link_command(const Flags& f, Kind kind,
                                          const std::vector<std::string>& inputs,
                                          const std::string& out,
                                          bool needs_rpath) const
    {
        std::vector<std::string> cmd = cxx;
        if (msvc_like())
        {
            cmd.push_back("/nologo");
            append(cmd, inputs);
            for (const auto& lib : f.libs) cmd.push_back(lib + ".lib");
            if (kind == Kind::SharedLib) cmd.push_back("/LD");
            append(cmd, sanitizers(f));
            if (f.lto.value_or(false) && family == Family::ClangCl)
                append(cmd, {"-flto", "-fuse-ld=lld"});
            cmd.push_back("/Fe" + out);
            cmd.push_back("/link");
            if (f.debug_info.value_or(false)) cmd.push_back("/DEBUG");
            if (f.lto.value_or(false) && family == Family::Msvc) cmd.push_back("/LTCG");
            append(cmd, f.link_flags);
            return cmd;
        }
        append(cmd, inputs);
        if (kind == Kind::SharedLib)
        {
            cmd.push_back(target_apple ? "-dynamiclib" : "-shared");
            std::string file = fs::path(out).filename().string();
            if (target_apple)
                cmd.push_back("-Wl,-install_name,@rpath/" + file);
            else if (!target_windows)
                cmd.push_back("-Wl,-soname," + file);
        }
        if (needs_rpath && !target_windows)
        {
            cmd.push_back(target_apple ? "-Wl,-rpath,@loader_path" : "-Wl,-rpath,$ORIGIN");
        }
        if (f.lto.value_or(false))
        {
            cmd.push_back("-flto");
            if (f.opt)
            {
                const char* o[] = {"-O0", "-Og", "-O1", "-O2", "-O3", "-Os"};
                cmd.push_back(o[static_cast<int>(*f.opt)]);
            }
        }
        append(cmd, sanitizers(f));
        append(cmd, f.link_flags);
        for (const auto& lib : f.libs) cmd.push_back("-l" + lib);
        append(cmd, {"-o", out});
        return cmd;
    }

    std::vector<std::string> archive_command(const std::vector<std::string>& objs,
                                             const std::string& out) const
    {
        std::vector<std::string> cmd;
        if (msvc_like())
        {
            cmd = {ar, "/nologo", "/OUT:" + out};
        }
        else
        {
            cmd = {ar, "rcs", out};
        }
        append(cmd, objs);
        return cmd;
    }
};

// `spec` is the compiler command, optionally prefixed by a launcher ("ccache g++").
// The family is found by running `<compiler> --version` instead of guessing from the
// name, so g++-14, x86_64-w64-mingw32-g++, c++ or icpx all work.
inline std::optional<Toolchain> detect_toolchain(const std::string& spec)
{
    Toolchain tc;
    tc.cxx = split(spec, ' ');
    if (tc.cxx.empty())
    {
        console().error("no compiler given");
        return std::nullopt;
    }
    std::string stem = str_lower(fs::path(tc.cxx.back()).stem().string());

    ProcessResult probe = run_capture({tc.cxx.back(), "--version"});
    if (!probe.started)
    {
        console().error("compiler '" + tc.cxx.back() + "' not found (" + probe.output + ")");
        return std::nullopt;
    }
    std::string text = str_lower(probe.output);
    tc.version = probe.output.substr(0, probe.output.find_first_of("\r\n"));

    if (stem.find("clang-cl") != std::string::npos)
        tc.family = Family::ClangCl;
    else if (text.find("microsoft") != std::string::npos || stem == "cl")
        tc.family = Family::Msvc;
    else if (text.find("clang") != std::string::npos)
        tc.family = Family::Clang;
    else
    {
        tc.family = Family::Gcc;
        if (text.find("free software foundation") == std::string::npos &&
            text.find("gcc") == std::string::npos && text.find("g++") == std::string::npos)
        {
            console().warning("unknown compiler '" + tc.cxx.back() +
                              "', assuming a gcc compatible command line");
        }
    }
    if (tc.family == Family::Msvc)
    {
        // cl prints its banner on stderr when called without sources
        tc.version = probe.output.substr(0, probe.output.find_first_of("\r\n"));
    }

    if (const char* ar = std::getenv("AR"); ar && *ar)
        tc.ar = ar;
    else if (tc.family == Family::Msvc)
        tc.ar = "lib";
    else if (tc.family == Family::ClangCl)
        tc.ar = "llvm-lib";
    else
    {
        // cross compilers: x86_64-w64-mingw32-g++ -> x86_64-w64-mingw32-ar
        std::string name = fs::path(tc.cxx.back()).filename().string();
        size_t dash = name.rfind('-');
        bool triple = dash != std::string::npos && name.find('-') < dash &&
                      (name.ends_with("g++") || name.ends_with("gcc") || name.ends_with("c++"));
        tc.ar = triple ? name.substr(0, dash + 1) + "ar" : "ar";
    }
    return tc;
}

inline std::string default_compiler()
{
    if (const char* cxx = std::getenv("CXX"); cxx && *cxx)
    {
        return cxx;
    }
#if defined(_MSC_VER) && defined(__clang__)
    return "clang-cl";
#elif defined(_MSC_VER)
    return "cl";
#else
    return "c++";
#endif
}

// ----------------------------------------------------------------------------------
// Targets
// ----------------------------------------------------------------------------------

inline bool is_cpp_source(const fs::path& p)
{
    static const std::set<std::string> exts = {".cpp", ".cc", ".cxx", ".c++"};
    return exts.contains(p.extension().string());
}

class Target
{
  public:
    Target(std::string name, Kind kind) : name_(std::move(name)), kind_(kind)
    {
    }

    // Applied when compiling and linking this target.
    Flags flags;
    // For imported targets: where they come from (shown by 'tree').
    std::string origin;
    // Also applied to every target that (transitively) links this one:
    // include directories, defines and system libraries a user of this target needs.
    Flags public_flags;

    // Adds all C++ sources below `dir` (sorted, so the link order is deterministic) and
    // makes `dir` a public include directory.
    Target& add_sources(const fs::path& dir, bool recursive = true)
    {
        std::vector<fs::path> found;
        std::error_code ec;
        if (recursive)
        {
            for (const auto& e : fs::recursive_directory_iterator(dir, ec))
                if (e.is_regular_file() && is_cpp_source(e.path())) found.push_back(e.path());
        }
        else
        {
            for (const auto& e : fs::directory_iterator(dir, ec))
                if (e.is_regular_file() && is_cpp_source(e.path())) found.push_back(e.path());
        }
        if (ec)
        {
            console().error("cannot read source directory '" + dir.string() + "': " +
                            ec.message());
        }
        std::sort(found.begin(), found.end());
        for (const auto& f : found)
        {
            add_source(f);
        }
        include_dir(dir);
        return *this;
    }

    Target& add_source(const fs::path& file)
    {
        sources_.push_back(file.lexically_normal());
        return *this;
    }

    Target& link(std::shared_ptr<Target> dep)
    {
        deps_.push_back(std::move(dep));
        return *this;
    }

    // `system` include dirs are meant for third party headers: the compiler does not
    // report warnings inside them (-isystem / /external:I).
    Target& include_dir(const fs::path& dir, bool is_public = true, bool system = false)
    {
        std::string d = display_path(dir);
        if (!d.empty() && d.back() == '/' && d.size() > 1)
        {
            d.pop_back();
        }
        Flags& f = is_public ? public_flags : flags;
        (system ? f.system_include_dirs : f.include_dirs).push_back(d);
        return *this;
    }

    Target& system_include_dir(const fs::path& dir, bool is_public = true)
    {
        return include_dir(dir, is_public, true);
    }

    Target& define(const std::string& define, bool is_public = false)
    {
        (is_public ? public_flags : flags).defines.push_back(define);
        return *this;
    }

    Target& link_lib(const std::string& lib)
    {
        public_flags.libs.push_back(lib);
        return *this;
    }

    const std::string& name() const
    {
        return name_;
    }

    Kind kind() const
    {
        return kind_;
    }

    const std::vector<fs::path>& sources() const
    {
        return sources_;
    }

    const std::vector<std::shared_ptr<Target>>& deps() const
    {
        return deps_;
    }

  private:
    std::string name_;
    Kind kind_;
    std::vector<fs::path> sources_;
    std::vector<std::shared_ptr<Target>> deps_;
};

// ----------------------------------------------------------------------------------
// Build cache: per output file the command hash, the time the command was started,
// the last duration and the discovered header dependencies.
// ----------------------------------------------------------------------------------

struct CacheEntry
{
    uint64_t hash = 0;
    int64_t stamp = 0; // file_time_type ticks when the job started
    double seconds = 0;
    std::string display;
    std::vector<std::string> deps;
    std::string diagnostics; // compiler output of the last successful run (warnings)
};

// Keeps multi-line text on a single cache line.
inline std::string escape_line(std::string_view s)
{
    std::string out;
    for (char c : s)
    {
        switch (c)
        {
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c;
        }
    }
    return out;
}

inline std::string unescape_line(std::string_view s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] != '\\' || i + 1 == s.size())
        {
            out += s[i];
            continue;
        }
        switch (s[++i])
        {
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        default: out += s[i];
        }
    }
    return out;
}

inline bool has_text(std::string_view output)
{
    return strip_ansi(output).find_first_not_of(" \r\n\t") != std::string::npos;
}

// Wall clock time of a build, for `stats`.
struct BuildRecord
{
    double wall = 0;
    double cpu = 0; // sum of the durations of the jobs that ran
    int jobs_run = 0;
    int jobs_total = 0;
    int threads = 0;
};

class Cache
{
  public:
    std::unordered_map<std::string, CacheEntry> entries;
    std::optional<BuildRecord> last_build;
    std::optional<BuildRecord> last_full_build; // every job ran

    void load(const fs::path& path)
    {
        std::ifstream in(path);
        std::string line;
        if (!std::getline(in, line) || line != header)
        {
            return;
        }
        CacheEntry* current = nullptr;
        while (std::getline(in, line))
        {
            if (line.starts_with("E\t"))
            {
                std::istringstream ss(line.substr(2));
                std::string hash, stamp, seconds, output, display;
                if (!std::getline(ss, hash, '\t') || !std::getline(ss, stamp, '\t') ||
                    !std::getline(ss, seconds, '\t') || !std::getline(ss, output, '\t') ||
                    !std::getline(ss, display, '\t'))
                {
                    current = nullptr;
                    continue;
                }
                CacheEntry& e = entries[output];
                e = {};
                e.hash = std::stoull(hash, nullptr, 16);
                e.stamp = std::stoll(stamp);
                e.seconds = std::stod(seconds);
                e.display = display;
                current = &e;
            }
            else if (line.starts_with("D\t") && current)
            {
                current->deps.push_back(line.substr(2));
            }
            else if (line.starts_with("L\t") || line.starts_with("F\t"))
            {
                BuildRecord r;
                std::istringstream ss(line.substr(2));
                if (ss >> r.wall >> r.cpu >> r.jobs_run >> r.jobs_total >> r.threads)
                {
                    (line[0] == 'L' ? last_build : last_full_build) = r;
                }
            }
            else if (line.starts_with("O\t") && current)
            {
                current->diagnostics = unescape_line(std::string_view(line).substr(2));
            }
        }
    }

    void save(const fs::path& path) const
    {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        fs::path tmp = path.string() + ".tmp";
        {
            std::ofstream out(tmp, std::ios::trunc);
            out << header << "\n";
            auto record = [&](char kind, const std::optional<BuildRecord>& r) {
                if (r)
                    out << kind << "\t" << r->wall << " " << r->cpu << " " << r->jobs_run << " "
                        << r->jobs_total << " " << r->threads << "\n";
            };
            record('L', last_build);
            record('F', last_full_build);
            char hash[32];
            for (const auto& [output, e] : entries)
            {
                std::snprintf(hash, sizeof(hash), "%016llx",
                              static_cast<unsigned long long>(e.hash));
                out << "E\t" << hash << "\t" << e.stamp << "\t" << e.seconds << "\t"
                    << output << "\t" << e.display << "\n";
                for (const auto& d : e.deps)
                {
                    out << "D\t" << d << "\n";
                }
                if (!e.diagnostics.empty())
                {
                    out << "O\t" << escape_line(e.diagnostics) << "\n";
                }
            }
        }
        fs::rename(tmp, path, ec); // atomic: a crash never leaves a half written cache
    }

  private:
    static constexpr const char* header = "nobcpp-cache 2";
};

// ----------------------------------------------------------------------------------
// Jobs
// ----------------------------------------------------------------------------------

enum class JobType
{
    Compile,
    Link,
    Archive
};

enum class JobState
{
    UpToDate,
    Pending,
    Running,
    Done,
    Failed,
    Skipped
};

struct Job
{
    JobType type = JobType::Compile;
    std::string display; // source file for compiles, output file otherwise
    std::string output;
    std::string source;
    std::string depfile;
    std::vector<std::string> args;  // hashed, written to compile_commands.json
    std::vector<std::string> extra; // color flags, not hashed
    std::vector<std::string> inputs;
    std::vector<int> deps;
    std::vector<int> dependents;

    bool dirty = false;
    bool has_history = false;
    double estimate = 1.0;
    double priority = 0.0; // estimate + longest chain of dependents (critical path)

    JobState state = JobState::UpToDate;
    int pending_deps = 0;
    SteadyClock::time_point started;
    double seconds = 0;
    int64_t stamp = 0;
    ProcessResult result;
    std::string diagnostics; // output with /showIncludes lines removed
    std::vector<std::string> discovered_deps;
    int errors = 0;
    int warnings = 0;
};

// Parses a make style .d file as written by -MMD.
inline std::vector<std::string> parse_depfile(const std::string& content)
{
    std::vector<std::string> deps;
    size_t start = std::string::npos;
    for (size_t i = 0; i + 1 <= content.size(); ++i)
    {
        // the target ends at the first ':' followed by whitespace (not "C:/...")
        if (content[i] == ':' &&
            (i + 1 == content.size() || content[i + 1] == ' ' || content[i + 1] == '\n' ||
             content[i + 1] == '\r' || content[i + 1] == '\t'))
        {
            start = i + 1;
            break;
        }
    }
    if (start == std::string::npos)
    {
        return deps;
    }
    std::string current;
    for (size_t i = start; i < content.size(); ++i)
    {
        char c = content[i];
        if (c == '\\' && i + 1 < content.size() &&
            (content[i + 1] == '\n' || content[i + 1] == '\r'))
        {
            ++i; // line continuation
            if (content[i] == '\r' && i + 1 < content.size() && content[i + 1] == '\n')
                ++i;
            c = ' ';
        }
        else if (c == '\\' && i + 1 < content.size() && content[i + 1] == ' ')
        {
            current += ' ';
            ++i;
            continue;
        }
        else if (c == '$' && i + 1 < content.size() && content[i + 1] == '$')
        {
            current += '$';
            ++i;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        {
            if (!current.empty())
            {
                deps.push_back(current);
                current.clear();
            }
            if (c == '\n')
            {
                break; // -MP style phony targets follow, we don't need them
            }
            continue;
        }
        current += c;
    }
    if (!current.empty())
    {
        deps.push_back(current);
    }
    return deps;
}

// Counts diagnostics in gcc/clang ("file:1:2: error:") and msvc
// ("file(1): error C1234:", "error LNK2019:") output.
inline void count_diagnostics(const std::string& output, int& errors, int& warnings)
{
    std::istringstream ss(strip_ansi(output));
    std::string line;
    while (std::getline(ss, line))
    {
        if (line.find(": error") != std::string::npos ||
            line.find(": fatal error") != std::string::npos ||
            line.find("error LNK") != std::string::npos)
        {
            ++errors;
        }
        else if (line.find(": warning") != std::string::npos ||
                 line.find("warning LNK") != std::string::npos)
        {
            ++warnings;
        }
    }
}

// ----------------------------------------------------------------------------------
// JSON (just enough to read CMake's file API replies)
// ----------------------------------------------------------------------------------

struct Json
{
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<Json> array;
    // object members; std::pair<std::string, Json> can't be used while Json is incomplete
    std::vector<std::string> keys;
    std::vector<Json> values;

    // Missing keys give a null value, so lookups can be chained.
    const Json& operator[](std::string_view key) const;

    static std::optional<Json> parse(std::string_view text)
    {
        size_t pos = 0;
        Json value;
        if (!parse_value(text, pos, value, 0))
        {
            return std::nullopt;
        }
        return value;
    }

  private:
    static void skip_space(std::string_view t, size_t& p)
    {
        while (p < t.size() && std::isspace(static_cast<unsigned char>(t[p]))) ++p;
    }

    static void append_utf8(std::string& out, unsigned cp)
    {
        if (cp < 0x80)
        {
            out += static_cast<char>(cp);
        }
        else if (cp < 0x800)
        {
            out += static_cast<char>(0xc0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3f));
        }
        else if (cp < 0x10000)
        {
            out += static_cast<char>(0xe0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (cp & 0x3f));
        }
        else
        {
            out += static_cast<char>(0xf0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (cp & 0x3f));
        }
    }

    static bool parse_hex4(std::string_view t, size_t p, unsigned& out)
    {
        if (p + 4 > t.size()) return false;
        out = 0;
        for (size_t i = p; i < p + 4; ++i)
        {
            char c = t[i];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= c - '0';
            else if (c >= 'a' && c <= 'f') out |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') out |= c - 'A' + 10;
            else return false;
        }
        return true;
    }

    static bool parse_string(std::string_view t, size_t& p, std::string& out)
    {
        if (p >= t.size() || t[p] != '"') return false;
        ++p;
        while (p < t.size())
        {
            char c = t[p++];
            if (c == '"') return true;
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (p >= t.size()) return false;
            char e = t[p++];
            switch (e)
            {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u':
            {
                unsigned cp = 0;
                if (!parse_hex4(t, p, cp)) return false;
                p += 4;
                unsigned low = 0;
                if (cp >= 0xd800 && cp <= 0xdbff && p + 6 <= t.size() && t[p] == '\\' &&
                    t[p + 1] == 'u' && parse_hex4(t, p + 2, low) && low >= 0xdc00 && low <= 0xdfff)
                {
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                    p += 6;
                }
                append_utf8(out, cp);
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    static bool parse_value(std::string_view t, size_t& p, Json& v, int depth)
    {
        skip_space(t, p);
        if (p >= t.size() || depth > 256) return false;
        char c = t[p];
        if (c == '{' || c == '[')
        {
            bool is_object = c == '{';
            v.type = is_object ? Type::Object : Type::Array;
            ++p;
            skip_space(t, p);
            if (p < t.size() && t[p] == (is_object ? '}' : ']'))
            {
                ++p;
                return true;
            }
            while (true)
            {
                std::string key;
                if (is_object)
                {
                    skip_space(t, p);
                    if (!parse_string(t, p, key)) return false;
                    skip_space(t, p);
                    if (p >= t.size() || t[p] != ':') return false;
                    ++p;
                }
                Json item;
                if (!parse_value(t, p, item, depth + 1)) return false;
                if (is_object)
                {
                    v.keys.push_back(std::move(key));
                    v.values.push_back(std::move(item));
                }
                else
                {
                    v.array.push_back(std::move(item));
                }
                skip_space(t, p);
                if (p < t.size() && t[p] == ',')
                {
                    ++p;
                    continue;
                }
                if (p < t.size() && t[p] == (is_object ? '}' : ']'))
                {
                    ++p;
                    return true;
                }
                return false;
            }
        }
        if (c == '"')
        {
            v.type = Type::String;
            return parse_string(t, p, v.string);
        }
        for (auto [word, type, value] : {std::tuple{"true", Type::Bool, true},
                                         std::tuple{"false", Type::Bool, false},
                                         std::tuple{"null", Type::Null, false}})
        {
            std::string_view w = word;
            if (t.substr(p, w.size()) == w)
            {
                v.type = type;
                v.boolean = value;
                p += w.size();
                return true;
            }
        }
        size_t start = p;
        while (p < t.size() && std::strchr("+-0123456789.eE", t[p])) ++p;
        if (p == start) return false;
        v.type = Type::Number;
        v.number = std::strtod(std::string(t.substr(start, p - start)).c_str(), nullptr);
        return true;
    }
};

inline const Json& Json::operator[](std::string_view key) const
{
    static const Json null;
    for (size_t i = 0; i < keys.size(); ++i)
    {
        if (keys[i] == key) return values[i];
    }
    return null;
}

inline std::optional<std::string> read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return std::nullopt;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

inline void set_env(const char* name, const char* value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

// ----------------------------------------------------------------------------------
// Dependencies: fetching sources
// ----------------------------------------------------------------------------------

// Where to get a dependency from. Give either `git` (+ `ref` or `commit`) or `url`.
struct Source
{
    std::string git{};    // repository url (or local path)
    std::string ref{};    // tag or branch, resolved once and pinned in nobcpp.lock
    std::string commit{}; // exact commit, no lookup needed at all
    std::string url{};    // alternatively an archive: .tar.gz, .tar.xz, .zip
    bool submodules = false;
};

// Fetched sources are shared by all projects of the user, they never change once
// fetched (one directory per commit). Override with $NOBCPP_CACHE.
inline fs::path source_cache_dir()
{
    if (const char* d = std::getenv("NOBCPP_CACHE"); d && *d) return fs::path(d) / "src";
#ifdef _WIN32
    if (const char* d = std::getenv("LOCALAPPDATA"); d && *d) return fs::path(d) / "nobcpp" / "src";
#else
    if (const char* d = std::getenv("XDG_CACHE_HOME"); d && *d) return fs::path(d) / "nobcpp" / "src";
    if (const char* d = std::getenv("HOME"); d && *d) return fs::path(d) / ".cache" / "nobcpp" / "src";
#endif
    return fs::temp_directory_path() / "nobcpp" / "src";
}

// Fetched files are shown as "imgui/backends/imgui_impl_sdl3.cpp" instead of their
// full path in the cache.
inline std::string source_display_path(const fs::path& p)
{
    static const std::string cache = source_cache_dir().lexically_normal().generic_string() + "/";
    std::string s = display_path(p);
    if (!s.starts_with(cache))
    {
        return s;
    }
    std::string rest = s.substr(cache.size()); // "imgui-d88f238d1d36/imgui.cpp"
    size_t slash = rest.find('/');
    std::string dir = rest.substr(0, slash);
    size_t dash = dir.rfind('-');
    if (dir.ends_with("-sm"))
    {
        dash = dir.rfind('-', dash - 1);
    }
    return dir.substr(0, dash) + (slash == std::string::npos ? "" : rest.substr(slash));
}

inline std::string unique_suffix()
{
    return std::to_string(SteadyClock::now().time_since_epoch().count());
}

inline bool is_full_commit(const std::string& s)
{
    return s.size() == 40 && std::all_of(s.begin(), s.end(), [](char c) {
               return std::isxdigit(static_cast<unsigned char>(c));
           });
}

// Moves a finished download into the cache. Another nobcpp process may have been
// faster, then its copy is used.
inline bool publish_dir(const fs::path& tmp, const fs::path& dest)
{
    std::error_code ec;
    fs::rename(tmp, dest, ec);
    if (ec)
    {
        fs::remove_all(tmp, ec);
        return fs::exists(dest);
    }
    return true;
}

// nobcpp.lock pins tags and branches to the commit they pointed to when first used.
class Lockfile
{
  public:
    struct Entry
    {
        std::string url;
        std::string ref;
        std::string commit;
    };
    std::map<std::string, Entry> entries;

    explicit Lockfile(fs::path path) : path_(std::move(path))
    {
        std::ifstream in(path_);
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ss(line);
            std::string name;
            Entry e;
            if (std::getline(ss, name, '\t') && std::getline(ss, e.url, '\t') &&
                std::getline(ss, e.ref, '\t') && std::getline(ss, e.commit, '\t'))
            {
                entries[name] = e;
            }
        }
    }

    void save() const
    {
        std::ofstream out(path_, std::ios::trunc);
        out << "# nobcpp.lock: the exact commits of fetched dependencies, commit this file.\n"
               "# './nobcpp update' moves tags and branches to their current commit.\n";
        for (const auto& [name, e] : entries)
        {
            out << name << "\t" << e.url << "\t" << e.ref << "\t" << e.commit << "\n";
        }
    }

  private:
    fs::path path_;
};

inline std::string tail_lines(const std::string& text, int count)
{
    size_t pos = text.size();
    if (pos > 0 && text[pos - 1] == '\n') --pos;
    while (count-- > 0)
    {
        size_t nl = pos == 0 ? std::string::npos : text.rfind('\n', pos - 1);
        if (nl == std::string::npos) return text;
        pos = nl;
    }
    return text.substr(pos + 1);
}

// Runs a command, exits with a readable error if it fails.
inline ProcessResult run_or_explain(const std::vector<std::string>& cmd, const std::string& what)
{
    ProcessResult r = run_capture(cmd);
    if (r.exit_code != 0)
    {
        console().error(what + " failed:\n" + console().dim("$ " + join(cmd, " ")) + "\n" +
                        tail_lines(r.output, 30));
    }
    return r;
}

// Resolves a tag or branch to a commit with `git ls-remote`.
inline std::optional<std::string> resolve_git_ref(const std::string& url, const std::string& ref)
{
    if (is_full_commit(ref))
    {
        return ref;
    }
    ProcessResult r = run_or_explain({"git", "ls-remote", url, ref, ref + "^{}"},
                                     "looking up '" + ref + "' in " + url);
    if (r.exit_code != 0)
    {
        return std::nullopt;
    }
    // annotated tags: the peeled entry (^{}) is the commit, the plain one the tag object
    std::map<std::string, std::string> found;
    std::istringstream ss(r.output);
    std::string line;
    while (std::getline(ss, line))
    {
        size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        found[line.substr(tab + 1)] = line.substr(0, tab);
    }
    for (const std::string& name : {"refs/tags/" + ref + "^{}", "refs/tags/" + ref,
                                    "refs/heads/" + ref, ref})
    {
        if (auto it = found.find(name); it != found.end())
        {
            return it->second;
        }
    }
    console().error("'" + ref + "' is neither a tag nor a branch of " + url);
    return std::nullopt;
}

inline bool fetch_git(const std::string& url, const std::string& commit, bool submodules,
                      const fs::path& dest)
{
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    fs::path tmp = dest.string() + ".tmp-" + unique_suffix();
    std::string t = tmp.string();
    auto git = [&](std::vector<std::string> args) {
        std::vector<std::string> cmd = {"git", "-C", t, "-c", "advice.detachedHead=false"};
        append(cmd, args);
        return run_capture(cmd);
    };
    bool ok = run_capture({"git", "init", "-q", t}).exit_code == 0;
    // a shallow fetch of exactly the commit is fast, but not every server allows it
    if (ok && git({"fetch", "-q", "--depth", "1", url, commit}).exit_code == 0)
    {
        ok = git({"checkout", "-q", "--detach", "FETCH_HEAD"}).exit_code == 0;
    }
    else
    {
        fs::remove_all(tmp, ec);
        ok = run_or_explain({"git", "clone", "-q", url, t}, "cloning " + url).exit_code == 0 &&
             run_or_explain({"git", "-C", t, "-c", "advice.detachedHead=false", "checkout",
                             "-q", "--detach", commit},
                            "checking out " + commit)
                     .exit_code == 0;
    }
    if (ok && submodules)
    {
        ok = run_or_explain({"git", "-C", t, "submodule", "update", "-q", "--init", "--recursive",
                             "--depth", "1"},
                            "fetching submodules of " + url)
                 .exit_code == 0;
    }
    if (!ok)
    {
        fs::remove_all(tmp, ec);
        return false;
    }
    return publish_dir(tmp, dest);
}

inline bool fetch_archive(const std::string& url, const fs::path& dest)
{
    std::error_code ec;
    fs::path tmp = dest.string() + ".tmp-" + unique_suffix();
    fs::create_directories(tmp / "x", ec);
    std::string file = url.substr(0, url.find_first_of("?#"));
    file = (tmp / fs::path(file).filename()).string();
    bool ok = run_or_explain({"curl", "-fsSL", "-o", file, url}, "downloading " + url).exit_code == 0;
    if (ok)
    {
        // bsdtar (Windows, macOS) also unpacks zips, GNU tar needs unzip for them
        ok = run_capture({"tar", "-xf", file, "-C", (tmp / "x").string()}).exit_code == 0 ||
             (file.ends_with(".zip") &&
              run_or_explain({"unzip", "-q", file, "-d", (tmp / "x").string()},
                             "unpacking " + file)
                      .exit_code == 0);
        if (!ok) console().error("could not unpack " + file);
    }
    if (!ok)
    {
        fs::remove_all(tmp, ec);
        return false;
    }
    // most archives contain a single top level directory, use that as the root
    fs::path root = tmp / "x";
    std::vector<fs::path> entries;
    for (const auto& e : fs::directory_iterator(root, ec)) entries.push_back(e.path());
    if (entries.size() == 1 && fs::is_directory(entries[0])) root = entries[0];
    bool published = publish_dir(root, dest);
    fs::remove_all(tmp, ec);
    return published;
}

// A fetched source tree, see Build::fetch.
class Fetched
{
  public:
    Fetched(std::string name, Source source, std::string version, fs::path dir)
        : name_(std::move(name)), source_(std::move(source)), version_(std::move(version)),
          dir_(std::move(dir))
    {
    }

    const std::string& name() const { return name_; }
    const Source& source() const { return source_; }
    // commit (or a hash of the archive url)
    const std::string& version() const { return version_; }
    const fs::path& dir() const { return dir_; }
    fs::path operator/(const fs::path& sub) const { return dir_ / sub; }

  private:
    std::string name_;
    Source source_;
    std::string version_;
    fs::path dir_;
};

// How to build a fetched CMake project and what to use from it, see Build::cmake.
struct CMakeOptions
{
    std::vector<std::string> targets{};  // CMake targets to link: "SDL3::SDL3-static"
    std::vector<std::string> options{};  // configure options: "SDL_SHARED=OFF"
    std::string package{};               // find_package name, default: namespace of targets[0]
    std::vector<std::string> components{};      // find_package(... COMPONENTS ...)
    std::vector<std::string> probe_variables{}; // set before find_package: "SFML_STATIC_LIBRARIES=TRUE"
    std::string build_type{};            // default: Debug/Release/... from the active profiles
    std::string source_subdir{};         // if CMakeLists.txt is not at the root
    std::vector<std::shared_ptr<Target>> depends{}; // other cmake dependencies it needs
};

// The C compiler that belongs to a C++ compiler: g++-14 -> gcc-14, clang++ -> clang.
inline std::string c_compiler_for(const std::string& cxx)
{
    if (const char* cc = std::getenv("CC"); cc && *cc)
    {
        return cc;
    }
    fs::path p(cxx);
    std::string name = p.filename().string();
    for (auto [from, to] : {std::pair{"clang++", "clang"}, std::pair{"g++", "gcc"},
                            std::pair{"c++", "cc"}, std::pair{"icpx", "icx"}})
    {
        size_t pos = name.rfind(from);
        if (pos != std::string::npos)
        {
            name.replace(pos, std::strlen(from), to);
            return p.has_parent_path() ? (p.parent_path() / name).string() : name;
        }
    }
    return cxx; // cl and clang-cl compile C as well
}

// Splits a command line fragment ("-I/a -DX", "path\ with\ space") into arguments.
inline std::vector<std::string> split_args(const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    bool in_token = false;
    char quote = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        char c = s[i];
        if (quote)
        {
            if (c == quote) quote = 0;
            else cur += c;
        }
        else if (c == '"' || c == '\'')
        {
            quote = c;
            in_token = true;
        }
        else if (c == '\\' && i + 1 < s.size() && s[i + 1] == ' ')
        {
            cur += ' ';
            ++i;
            in_token = true;
        }
        else if (std::isspace(static_cast<unsigned char>(c)))
        {
            if (in_token) out.push_back(cur);
            cur.clear();
            in_token = false;
        }
        else
        {
            cur += c;
            in_token = true;
        }
    }
    if (in_token) out.push_back(cur);
    return out;
}

// Compile and link flags a CMake target hands to its users, as reported by CMake.
struct ImportedFlags
{
    std::vector<std::string> includes;
    std::vector<std::string> defines;
    std::vector<std::string> compile;
    std::vector<std::string> link;

    void save(const fs::path& path) const
    {
        std::ofstream out(path, std::ios::trunc);
        for (const auto& [kind, values] : {std::pair{'I', &includes}, std::pair{'D', &defines},
                                           std::pair{'C', &compile}, std::pair{'L', &link}})
        {
            for (const auto& v : *values) out << kind << "\t" << v << "\n";
        }
    }

    static std::optional<ImportedFlags> load(const fs::path& path)
    {
        std::ifstream in(path);
        if (!in) return std::nullopt;
        ImportedFlags f;
        std::string line;
        while (std::getline(in, line))
        {
            if (line.size() < 2) continue;
            std::string v = line.substr(2);
            switch (line[0])
            {
            case 'I': f.includes.push_back(v); break;
            case 'D': f.defines.push_back(v); break;
            case 'C': f.compile.push_back(v); break;
            case 'L': f.link.push_back(v); break;
            }
        }
        return f;
    }

    // Reads the reply of CMake's file API for the probe executable.
    static std::optional<ImportedFlags> from_file_api(const fs::path& build_dir, std::string& error)
    {
        fs::path reply = build_dir / ".cmake" / "api" / "v1" / "reply";
        std::vector<fs::path> indexes;
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(reply, ec))
        {
            if (e.path().filename().string().starts_with("index-")) indexes.push_back(e.path());
        }
        if (indexes.empty())
        {
            error = "CMake wrote no file API reply (CMake 3.14 or newer is needed)";
            return std::nullopt;
        }
        std::sort(indexes.begin(), indexes.end());
        auto load = [&](const fs::path& p) {
            auto text = read_file(p);
            return text ? Json::parse(*text) : std::nullopt;
        };
        auto index = load(indexes.back());
        if (!index) return std::nullopt;
        auto model = load(reply / (*index)["reply"]["codemodel-v2"]["jsonFile"].string);
        if (!model || (*model)["configurations"].array.empty())
        {
            error = "unreadable CMake codemodel";
            return std::nullopt;
        }
        std::string target_file;
        for (const auto& t : (*model)["configurations"].array[0]["targets"].array)
        {
            if (t["name"].string == "nobcpp_probe") target_file = t["jsonFile"].string;
        }
        auto target = load(reply / target_file);
        if (target_file.empty() || !target)
        {
            error = "the probe target is missing from the CMake codemodel";
            return std::nullopt;
        }

        ImportedFlags f;
        for (const auto& group : (*target)["compileGroups"].array)
        {
            if (group["language"].string != "CXX") continue;
            for (const auto& inc : group["includes"].array) f.includes.push_back(inc["path"].string);
            for (const auto& def : group["defines"].array) f.defines.push_back(def["define"].string);
            for (const auto& frag : group["compileCommandFragments"].array)
            {
                for (const auto& a : split_args(frag["fragment"].string))
                {
                    // the language standard is ours to choose
                    if (!a.starts_with("-std=") && !a.starts_with("/std:") && !a.starts_with("-std:"))
                        f.compile.push_back(a);
                }
            }
        }
        for (const auto& frag : (*target)["link"]["commandFragments"].array)
        {
            append(f.link, split_args(frag["fragment"].string));
        }
        return f;
    }
};

// ----------------------------------------------------------------------------------
// Build
// ----------------------------------------------------------------------------------

struct Options
{
    std::vector<std::string> positionals; // commands, profiles, run target
    std::vector<std::string> profiles;    // from -p
    std::vector<std::string> run_args;    // after --
    int jobs = 0;
    bool verbose = false;
    bool fail_fast = false;
    bool offline = false;
    ColorMode color = ColorMode::Auto;
    std::string cxx;
};

class Build
{
  public:
    // Flags for every target. Profiles are applied on top.
    Flags flags;
    std::string build_dir = "build";
    std::string default_profile = "debug";

    Build(int argc, char** argv)
    {
        // hints print the command the way it was typed ("./nobcpp"), unless that was a
        // long absolute path
        program_ = argc > 0 ? argv[0] : "nobcpp";
        if (fs::path(program_).is_absolute())
        {
            program_ = fs::path(program_).filename().string();
        }
        parse_options(argc, argv);
        console().configure(options_.color);
        updating_ = std::find(options_.positionals.begin(), options_.positionals.end(),
                              "update") != options_.positionals.end();

        profile("debug", {.opt = Opt::O0, .debug_info = true});
        profile("release", {.opt = Opt::O3, .debug_info = false, .defines = {"NDEBUG"}});
        profile("relwithdebinfo", {.opt = Opt::O2, .debug_info = true, .defines = {"NDEBUG"}});
        profile("asan", {.debug_info = true, .sanitizers = {"address", "undefined"}});
        profile("tsan", {.debug_info = true, .sanitizers = {"thread"}});
    }

    // Registers (or replaces) a profile. Several profiles can be active at once and are
    // merged in command line order: `./nobcpp release asan` or `-p release,asan`.
    void profile(const std::string& name, const Profile& p)
    {
        if (!profiles_.contains(name))
        {
            profile_order_.push_back(name);
        }
        profiles_[name] = p;
    }

    // True if `name` was selected on the command line (or is the default profile).
    // Usable while defining targets, before run().
    bool profile_active(const std::string& name) const
    {
        auto active = requested_profiles();
        return std::find(active.begin(), active.end(), name) != active.end();
    }

    std::shared_ptr<Target> executable(const std::string& name, const fs::path& src_dir = {})
    {
        return add_target(name, Kind::Executable, src_dir);
    }

    std::shared_ptr<Target> static_lib(const std::string& name, const fs::path& src_dir = {})
    {
        return add_target(name, Kind::StaticLib, src_dir);
    }

    std::shared_ptr<Target> shared_lib(const std::string& name, const fs::path& src_dir = {})
    {
        return add_target(name, Kind::SharedLib, src_dir);
    }

    // A header only library, e.g. third party code: linking it only adds its include
    // directory (and any public flags you give it). With `system` its warnings are hidden.
    std::shared_ptr<Target> header_lib(const std::string& name, const fs::path& include_dir = {},
                                       bool system = false)
    {
        auto target = add_target(name, Kind::HeaderOnly, {});
        if (!include_dir.empty())
        {
            target->include_dir(include_dir, true, system);
        }
        return target;
    }

    // Downloads sources into a cache shared by all projects (once per commit) and
    // returns where they are. Tags and branches are pinned to a commit in nobcpp.lock,
    // './nobcpp update' moves them. Use the sources directly (header only libraries,
    // or compile them with static_lib()) or build them with cmake().
    std::shared_ptr<Fetched> fetch(const std::string& name, const Source& source);

    // Builds and installs a fetched CMake project into build/_deps (once per build
    // type and set of options), then imports `options.targets` with everything they
    // need: include directories (as system headers), defines and transitive libraries.
    std::shared_ptr<Target> cmake(std::shared_ptr<Fetched> source, const CMakeOptions& options);

    // Imports a library installed on the system via pkg-config (gcc/clang only).
    std::shared_ptr<Target> pkg_config(const std::string& package, bool static_libs = false);

    // The compiler selected via --cxx, $CXX or the platform default. Exits if it
    // cannot be found.
    const Toolchain& toolchain()
    {
        if (!toolchain_)
        {
            toolchain_ = detect_toolchain(options_.cxx.empty() ? default_compiler()
                                                               : options_.cxx);
            if (!toolchain_)
            {
                std::exit(1);
            }
        }
        return *toolchain_;
    }

    // Executes the commands given on the command line. Returns the exit code for main.
    int run();

  private:
    struct Action
    {
        std::string command;
        std::string target{};               // for run
        std::vector<std::string> filters{}; // for warnings
    };

    struct CMakeDep
    {
        std::shared_ptr<Target> target;
        std::shared_ptr<Fetched> source;
        CMakeOptions options;
        // resolved by prepare_dependencies()
        std::vector<CMakeDep*> depends;
        std::string build_type;
        std::vector<std::string> common; // generator and compiler settings
        std::vector<std::string> configure;
        std::string prefix_path;
        fs::path dir, build_dir, install_dir, probe_dir, import_file, log_file;
        enum class State
        {
            Waiting,
            Running,
            Done,
            Failed
        } state = State::Waiting;
        bool built = false; // false: everything was there from an earlier run
        std::string phase;
        SteadyClock::time_point phase_start;
        double seconds = 0;
        std::string error;
    };

    std::string program_;
    Options options_;
    std::vector<std::shared_ptr<CMakeDep>> cmake_deps_;
    bool deps_prepared_ = false;
    bool updating_ = false; // 'update': re-resolve tags and branches
    std::unique_ptr<Lockfile> lockfile_;

    std::string dependency_build_type() const;
    bool build_cmake_dep(CMakeDep& d, std::mutex& m);
    void apply_import(CMakeDep& d, const ImportedFlags& imported);
    bool prepare_dependencies();
    std::map<std::string, Profile> profiles_;
    std::vector<std::string> profile_order_;
    std::vector<std::shared_ptr<Target>> targets_;
    std::optional<Toolchain> toolchain_;

    // resolved in run()
    std::vector<std::string> active_profiles_;
    Flags profile_flags_;
    fs::path out_dir_;
    Cache cache_;
    std::vector<Job> jobs_;
    std::unordered_map<std::string, int> job_by_output_;
    std::unordered_map<const Target*, int> job_by_target_;
    std::set<const Target*> pic_targets_; // shared libs and everything linked into them
    int threads_used_ = 0;
    std::unordered_map<std::string, std::optional<fs::file_time_type>> mtimes_;

    static const std::vector<std::pair<std::string, std::string>>& command_help()
    {
        static const std::vector<std::pair<std::string, std::string>> cmds = {
            {"build", "build all targets (default)"},
            {"rebuild", "clean the active profile, then build"},
            {"run", "build, then run an executable: run [target] [-- args...]"},
            {"clean", "remove the active profile's build directory"},
            {"cleanall", "remove the whole build directory"},
            {"compdb", "write compile_commands.json without building"},
            {"stats", "show how long each translation unit took to compile"},
            {"warnings", "replay stored warnings of all files: warnings [path filter...]"},
            {"tree", "show targets, sources and dependencies"},
            {"update", "move fetched tags and branches to their latest commit (nobcpp.lock)"},
            {"help", "show this help"},
        };
        return cmds;
    }

    static bool is_command(const std::string& s)
    {
        for (const auto& [name, help] : command_help())
        {
            if (name == s) return true;
        }
        return false;
    }

    std::shared_ptr<Target> add_target(const std::string& name, Kind kind,
                                       const fs::path& src_dir)
    {
        for (const auto& t : targets_)
        {
            if (t->name() == name)
            {
                console().error("duplicate target name '" + name + "'");
                std::exit(1);
            }
        }
        auto target = std::make_shared<Target>(name, kind);
        if (!src_dir.empty())
        {
            target->add_sources(src_dir);
        }
        targets_.push_back(target);
        return target;
    }

    std::vector<std::string> requested_profiles() const
    {
        std::vector<std::string> out = options_.profiles;
        for (const auto& p : options_.positionals)
        {
            if (!is_command(p) && profiles_.contains(p))
            {
                out.push_back(p);
            }
        }
        if (out.empty())
        {
            out.push_back(default_profile);
        }
        std::vector<std::string> unique;
        for (const auto& p : out)
        {
            if (std::find(unique.begin(), unique.end(), p) == unique.end())
            {
                unique.push_back(p);
            }
        }
        return unique;
    }

    void usage_error(const std::string& message)
    {
        console().error(message);
        console().println("run '" + program_ + " help' for usage");
        std::exit(2);
    }

    void parse_options(int argc, char** argv)
    {
        auto value = [&](int& i, const std::string& arg, const std::string& name) {
            if (arg.size() > name.size() && arg.starts_with(name))
            {
                std::string rest = arg.substr(name.size());
                return rest[0] == '=' ? rest.substr(1) : rest;
            }
            if (i + 1 >= argc)
            {
                usage_error("missing value for " + name);
            }
            return std::string(argv[++i]);
        };
        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            if (arg == "--")
            {
                for (++i; i < argc; ++i)
                {
                    options_.run_args.push_back(argv[i]);
                }
            }
            else if (arg == "-h" || arg == "--help")
            {
                options_.positionals.push_back("help");
            }
            else if (arg == "-v" || arg == "--verbose")
            {
                options_.verbose = true;
            }
            else if (arg == "--fail-fast")
            {
                options_.fail_fast = true;
            }
            else if (arg == "--offline")
            {
                options_.offline = true;
            }
            else if (arg == "--no-color")
            {
                options_.color = ColorMode::Never;
            }
            else if (arg.starts_with("--color"))
            {
                std::string v = value(i, arg, "--color");
                if (v == "always") options_.color = ColorMode::Always;
                else if (v == "never") options_.color = ColorMode::Never;
                else if (v == "auto") options_.color = ColorMode::Auto;
                else usage_error("--color must be auto, always or never");
            }
            else if (arg.starts_with("--jobs") || arg.starts_with("-j"))
            {
                std::string v = value(i, arg, arg.starts_with("-j") ? "-j" : "--jobs");
                try
                {
                    options_.jobs = std::stoi(v);
                }
                catch (...)
                {
                    usage_error("invalid job count '" + v + "'");
                }
            }
            else if (arg.starts_with("--profile") || arg.starts_with("-p"))
            {
                std::string v = value(i, arg, arg.starts_with("-p") ? "-p" : "--profile");
                append(options_.profiles, split(v, ','));
            }
            else if (arg.starts_with("--cxx") || arg.starts_with("--cc"))
            {
                options_.cxx = value(i, arg, arg.starts_with("--cxx") ? "--cxx" : "--cc");
            }
            else if (arg.size() > 1 && arg[0] == '-')
            {
                usage_error("unknown option '" + arg + "'");
            }
            else
            {
                options_.positionals.push_back(arg);
            }
        }
    }

    void print_help() const
    {
        auto& c = console();
        std::string s;
        s += c.bold("usage: ") + program_ + " [commands...] [profiles...] [options] [-- run args]\n\n";
        s += c.bold("commands:\n");
        for (const auto& [name, help] : command_help())
        {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "  %-10s %s\n", name.c_str(), help.c_str());
            s += buf;
        }
        s += c.bold("\nprofiles:") + " (combine them: '" + program_ + " release asan', default: " +
             default_profile + ")\n  ";
        s += join(profile_order_, ", ") + "\n";
        s += c.bold("\noptions:\n");
        s += "  -p, --profile a,b  select profiles (same as naming them)\n";
        s += "  -j, --jobs N       parallel jobs (default: number of cores)\n";
        s += "  -v, --verbose      print every command\n";
        s += "      --fail-fast    stop starting new jobs after the first failure\n";
        s += "      --offline      never download, use fetched dependencies from the cache\n";
        s += "      --cxx CMD      compiler to use (default: $CXX or " + default_compiler() + ")\n";
        s += "      --color WHEN   auto, always or never (also: --no-color, $NO_COLOR)\n";
        s += c.bold("\nexamples:\n");
        s += "  " + program_ + "                    debug build\n";
        s += "  " + program_ + " release run -- x   release build, run with argument x\n";
        s += "  " + program_ + " -p release,asan    release build with sanitizers\n";
        s += "  " + program_ + " --cxx clang++ -j8  build with clang using 8 jobs\n";
        c.print(s);
    }

    bool resolve(std::vector<Action>& actions)
    {
        for (size_t i = 0; i < options_.positionals.size(); ++i)
        {
            const std::string& p = options_.positionals[i];
            if (is_command(p))
            {
                actions.push_back({p, ""});
            }
            else if (profiles_.contains(p))
            {
                continue;
            }
            else if (!actions.empty() && actions.back().command == "run" &&
                     actions.back().target.empty() && find_target(p))
            {
                actions.back().target = p;
            }
            else if (!actions.empty() && actions.back().command == "warnings")
            {
                actions.back().filters.push_back(p);
            }
            else
            {
                usage_error("unknown command, profile or target '" + p + "'");
            }
        }
        for (const auto& p : options_.profiles)
        {
            if (!profiles_.contains(p))
            {
                usage_error("unknown profile '" + p + "' (available: " +
                            join(profile_order_, ", ") + ")");
            }
        }
        if (!options_.run_args.empty() &&
            std::none_of(actions.begin(), actions.end(),
                         [](const Action& a) { return a.command == "run"; }))
        {
            usage_error("arguments after '--' are only used by 'run'");
        }
        if (actions.empty())
        {
            actions.push_back({"build", ""});
        }
        if (!profiles_.contains(default_profile))
        {
            usage_error("default profile '" + default_profile + "' does not exist");
        }

        active_profiles_ = requested_profiles();
        profile_flags_ = {};
        for (const auto& p : active_profiles_)
        {
            profile_flags_.merge(profiles_.at(p));
        }
        out_dir_ = fs::path(build_dir) / join(active_profiles_, "-");
        return true;
    }

    std::shared_ptr<Target> find_target(const std::string& name) const
    {
        for (const auto& t : targets_)
        {
            if (t->name() == name) return t;
        }
        return nullptr;
    }

    fs::path cache_path() const
    {
        return out_dir_ / ".nobcpp_cache";
    }

    fs::path output_path(const Target& t)
    {
        return out_dir_ / toolchain().output_name(t.name(), t.kind());
    }

    // ---------------------------------------------------------------- planning

    std::optional<fs::file_time_type> cached_mtime(const std::string& path)
    {
        auto it = mtimes_.find(path);
        if (it != mtimes_.end())
        {
            return it->second;
        }
        return mtimes_[path] = file_mtime(path);
    }

    bool is_dirty(Job& job)
    {
        for (int d : job.deps)
        {
            if (jobs_[d].dirty) return true;
        }
        auto it = cache_.entries.find(job.output);
        if (it == cache_.entries.end() || it->second.hash != hash_args(job.args))
        {
            return true;
        }
        if (!cached_mtime(job.output))
        {
            return true;
        }
        const CacheEntry& entry = it->second;
        auto newer = [&](const std::string& input) {
            auto t = cached_mtime(input);
            // a missing input (deleted header) forces a recompile instead of crashing
            return !t || t->time_since_epoch().count() > entry.stamp;
        };
        return std::any_of(job.inputs.begin(), job.inputs.end(), newer) ||
               std::any_of(entry.deps.begin(), entry.deps.end(), newer);
    }

    int add_job(Job job)
    {
        if (auto it = cache_.entries.find(job.output); it != cache_.entries.end())
        {
            job.estimate = it->second.seconds;
            job.has_history = true;
        }
        job.dirty = is_dirty(job);
        int id = static_cast<int>(jobs_.size());
        for (int d : job.deps)
        {
            jobs_[d].dependents.push_back(id);
        }
        job_by_output_[job.output] = id;
        jobs_.push_back(std::move(job));
        return id;
    }

    // Targets this one links against, dependents before their dependencies.
    void collect_link_deps(const Target& t, std::vector<const Target*>& order,
                           std::set<const Target*>& seen)
    {
        for (const auto& dep : t.deps())
        {
            if (seen.insert(dep.get()).second)
            {
                // a shared library already contains its static dependencies
                if (dep->kind() != Kind::SharedLib)
                {
                    collect_link_deps(*dep, order, seen);
                }
                order.push_back(dep.get());
            }
        }
    }

    int plan_target(const Target& t)
    {
        if (auto it = job_by_target_.find(&t); it != job_by_target_.end())
        {
            return it->second;
        }
        const Toolchain& tc = toolchain();

        std::vector<const Target*> link_deps;
        std::set<const Target*> seen;
        collect_link_deps(t, link_deps, seen);
        std::reverse(link_deps.begin(), link_deps.end());

        // global -> public flags of dependencies -> own flags -> profile
        Flags f = flags;
        for (auto it = link_deps.rbegin(); it != link_deps.rend(); ++it)
        {
            f.merge((*it)->public_flags);
        }
        f.merge(t.public_flags);
        f.merge(t.flags);
        f.merge(profile_flags_);

        if (t.kind() == Kind::HeaderOnly)
        {
            if (!t.sources().empty())
            {
                console().warning("sources of header only target '" + t.name() +
                                  "' are ignored");
            }
            job_by_target_[&t] = -1;
            return -1;
        }

        std::vector<int> deps;
        std::vector<std::string> inputs;
        for (const Target* dep : link_deps)
        {
            if (int id = plan_target(*dep); id >= 0)
            {
                deps.push_back(id);
            }
        }

        // objects mirror the source tree below the deepest directory common to all sources
        fs::path common;
        for (const auto& src : t.sources())
        {
            fs::path dir = src.parent_path();
            if (&src == &t.sources().front())
            {
                common = dir;
                continue;
            }
            fs::path prefix;
            auto a = common.begin();
            for (auto b = dir.begin(); a != common.end() && b != dir.end() && *a == *b; ++a, ++b)
            {
                prefix /= *a;
            }
            common = prefix;
        }

        bool pic = pic_targets_.contains(&t);
        for (const auto& src : t.sources())
        {
            fs::path rel = src.lexically_relative(common);
            if (rel.empty())
            {
                rel = src.is_absolute() ? src.relative_path() : src;
            }
            std::string mangled;
            for (const auto& part : rel)
            {
                std::string s = part.string();
                if (!mangled.empty()) mangled += "/";
                mangled += s == ".." ? "__" : s;
            }
            fs::path obj = out_dir_ / "obj" / t.name() / (mangled + tc.obj_ext());
            Job job;
            job.type = JobType::Compile;
            job.display = source_display_path(src);
            job.source = src.generic_string();
            job.output = obj.generic_string();
            job.depfile = job.output + ".d";
            job.args = tc.compile_command(f, job.source, job.output, job.depfile, pic);
            job.inputs = {job.source};
            if (job_by_output_.contains(job.output))
            {
                console().error("source '" + job.display + "' added twice to target '" +
                                t.name() + "'");
                std::exit(1);
            }
            int id = add_job(std::move(job));
            deps.push_back(id);
            inputs.push_back(jobs_[id].output);
        }
        if (t.sources().empty())
        {
            console().warning("target '" + t.name() + "' has no sources");
        }

        Job job;
        job.output = output_path(t).generic_string();
        job.display = job.output;
        if (t.kind() == Kind::StaticLib)
        {
            job.type = JobType::Archive;
            job.args = tc.archive_command(inputs, job.output);
        }
        else
        {
            bool needs_rpath = false;
            for (const Target* dep : link_deps)
            {
                if (dep->kind() == Kind::HeaderOnly)
                {
                    continue;
                }
                std::string out = output_path(*dep).generic_string();
                // msvc links against the import library of a dll
                if (dep->kind() == Kind::SharedLib && tc.msvc_like())
                    out = (out_dir_ / (dep->name() + ".lib")).generic_string();
                inputs.push_back(out);
                needs_rpath |= dep->kind() == Kind::SharedLib;
            }
            job.type = JobType::Link;
            job.args = tc.link_command(f, t.kind(), inputs, job.output, needs_rpath);
        }
        job.inputs = inputs;
        job.deps = deps;
        int id = add_job(std::move(job));
        job_by_target_[&t] = id;
        return id;
    }

    void plan()
    {
        jobs_.clear();
        job_by_output_.clear();
        job_by_target_.clear();
        mtimes_.clear();
        cache_ = {};
        cache_.load(cache_path());
        pic_targets_.clear();
        std::function<void(const Target&)> mark_pic = [&](const Target& t) {
            if (pic_targets_.insert(&t).second)
                for (const auto& dep : t.deps()) mark_pic(*dep);
        };
        for (const auto& t : targets_)
        {
            if (t->kind() == Kind::SharedLib) mark_pic(*t);
        }
        for (const auto& t : targets_)
        {
            plan_target(*t);
        }

        // unknown durations: average of the known ones of the same kind
        double sums[2] = {0, 0};
        int counts[2] = {0, 0};
        for (const auto& j : jobs_)
        {
            int k = j.type == JobType::Compile ? 0 : 1;
            if (j.has_history)
            {
                sums[k] += j.estimate;
                counts[k]++;
            }
        }
        for (auto& j : jobs_)
        {
            int k = j.type == JobType::Compile ? 0 : 1;
            if (!j.has_history)
                j.estimate = counts[k] ? sums[k] / counts[k] : (k == 0 ? 1.0 : 0.3);
        }
        // jobs are created in dependency order, so walk backwards for critical paths
        for (int i = static_cast<int>(jobs_.size()) - 1; i >= 0; --i)
        {
            double longest = 0;
            for (int d : jobs_[i].dependents)
            {
                if (jobs_[d].dirty) longest = std::max(longest, jobs_[d].priority);
            }
            jobs_[i].priority = jobs_[i].estimate + longest;
        }
    }

    // ---------------------------------------------------------------- execution

    std::vector<std::string> final_args(const Job& job) const
    {
        std::vector<std::string> args = job.args;
        // only the compiler driver understands them, `ar`/`lib` would choke on them
        if (console().color() && job.type != JobType::Archive)
        {
            append(args, toolchain_->color_flags());
        }
#ifdef _WIN32
        // CreateProcess is limited to 32k characters, use a response file
        if (windows_command_line(args).size() > 30000)
        {
            size_t prefix = toolchain_->cxx.size();
            if (job.type == JobType::Archive) prefix = 1;
            std::string rsp = job.output + ".rsp";
            std::ofstream out(rsp, std::ios::trunc);
            for (size_t i = prefix; i < args.size(); ++i)
            {
                if (toolchain_->msvc_like())
                {
                    out << quote_arg_windows(args[i]) << "\n";
                }
                else
                {
                    std::string quoted = "\"";
                    for (char ch : args[i])
                    {
                        if (ch == '\\' || ch == '"') quoted += '\\';
                        quoted += ch;
                    }
                    out << quoted << "\"\n";
                }
            }
            args.resize(prefix);
            args.push_back("@" + rsp);
        }
#endif
        return args;
    }

    void run_job(Job& job)
    {
        std::error_code ec;
        fs::create_directories(fs::path(job.output).parent_path(), ec);
        if (job.type == JobType::Archive)
        {
            fs::remove(job.output, ec); // `ar rcs` would keep members of deleted sources
        }
        job.stamp = file_clock_now();
        auto start = SteadyClock::now();
        job.result = run_capture(final_args(job));
        job.seconds = seconds_since(start);

        if (job.type == JobType::Compile && toolchain_->msvc_like())
        {
            const std::string prefix = "Note: including file:";
            std::istringstream ss(job.result.output);
            std::string line;
            bool first = true;
            std::string filename = fs::path(job.source).filename().string();
            while (std::getline(ss, line))
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.starts_with(prefix))
                {
                    size_t p = line.find_first_not_of(' ', prefix.size());
                    if (p != std::string::npos) job.discovered_deps.push_back(line.substr(p));
                }
                else if (!(first && line == filename)) // cl echoes the source name
                {
                    job.diagnostics += line + "\n";
                }
                first = false;
            }
        }
        else
        {
            job.diagnostics = job.result.output;
            if (job.type == JobType::Compile && job.result.exit_code == 0)
            {
                std::ifstream in(job.depfile);
                std::stringstream content;
                content << in.rdbuf();
                job.discovered_deps = parse_depfile(content.str());
                in.close();
                fs::remove(job.depfile, ec);
            }
        }
        count_diagnostics(job.diagnostics, job.errors, job.warnings);
        if (job.result.exit_code != 0 && job.errors == 0)
        {
            job.errors = 1;
        }
    }

    std::string short_name(const Job& job) const
    {
        return fs::path(job.display).filename().string();
    }

    std::string counts_text(int errors, int warnings) const
    {
        auto& c = console();
        std::vector<std::string> parts;
        if (errors)
            parts.push_back(c.red(std::to_string(errors) + (errors == 1 ? " error" : " errors")));
        if (warnings)
            parts.push_back(c.yellow(std::to_string(warnings) +
                                     (warnings == 1 ? " warning" : " warnings")));
        return join(parts, ", ");
    }

    // Runs all dirty jobs. Returns false if any failed.
    bool execute(double& wall_seconds)
    {
        auto& con = console();
        std::vector<int> todo;
        for (int i = 0; i < static_cast<int>(jobs_.size()); ++i)
        {
            Job& j = jobs_[i];
            j.state = j.dirty ? JobState::Pending : JobState::UpToDate;
            if (j.dirty) todo.push_back(i);
        }
        if (todo.empty())
        {
            wall_seconds = 0;
            return true;
        }

        int parallel = options_.jobs;
        if (parallel <= 0)
        {
            parallel = std::max(1u, std::thread::hardware_concurrency());
        }
        parallel = std::min<int>(parallel, static_cast<int>(todo.size()));
        threads_used_ = parallel;

        auto cmp = [&](int a, int b) { return jobs_[a].priority < jobs_[b].priority; };
        std::priority_queue<int, std::vector<int>, decltype(cmp)> ready(cmp);
        for (int i : todo)
        {
            Job& j = jobs_[i];
            j.pending_deps = 0;
            for (int d : j.deps)
            {
                if (jobs_[d].dirty) j.pending_deps++;
            }
            if (j.pending_deps == 0) ready.push(i);
        }

        std::mutex m;
        std::condition_variable cv;
        int outstanding = static_cast<int>(todo.size());
        int finished = 0;
        int running = 0;
        bool stop = false;
        bool any_failed = false;
        double total_estimate = 0;
        int with_history = 0;
        for (int i : todo)
        {
            total_estimate += jobs_[i].estimate;
            with_history += jobs_[i].has_history;
        }
        const bool use_history = with_history * 2 >= static_cast<int>(todo.size());
        double done_estimate = 0;
        const int total = static_cast<int>(todo.size());
        const int width = static_cast<int>(std::to_string(total).size());
        auto start = SteadyClock::now();

        // Called with `m` held.
        std::function<void(int)> skip = [&](int id) {
            for (int d : jobs_[id].dependents)
            {
                Job& dep = jobs_[d];
                if (dep.state == JobState::Pending)
                {
                    dep.state = JobState::Skipped;
                    --outstanding;
                    skip(d);
                }
            }
        };

        auto progress_prefix = [&](int n) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "[%*d/%d] ", width, n, total);
            return std::string(buf);
        };

        auto worker = [&]() {
            std::unique_lock<std::mutex> lock(m);
            while (true)
            {
                cv.wait(lock, [&] { return !ready.empty() || outstanding == 0 || stop; });
                if (stop || ready.empty())
                {
                    return;
                }
                int id = ready.top();
                ready.pop();
                Job& job = jobs_[id];
                job.state = JobState::Running;
                job.started = SteadyClock::now();
                ++running;
                lock.unlock();

                run_job(job);

                lock.lock();
                bool ok = job.result.exit_code == 0;
                job.state = ok ? JobState::Done : JobState::Failed;
                --outstanding;
                --running;
                ++finished;
                done_estimate += job.estimate;
                if (ok)
                {
                    for (int d : job.dependents)
                    {
                        Job& dep = jobs_[d];
                        if (dep.state == JobState::Pending && --dep.pending_deps == 0)
                            ready.push(d);
                    }
                }
                else
                {
                    any_failed = true;
                    skip(id);
                    if (options_.fail_fast) stop = true;
                }

                // One line per job when not on a terminal (CI logs) or when verbose,
                // on a terminal only failures are announced, details come at the end.
                std::string line;
                if (!con.tty() || options_.verbose || !ok)
                {
                    line = con.dim(progress_prefix(finished));
                    line += ok ? "" : con.red("FAILED ");
                    line += job.display + con.dim("  " + format_duration(job.seconds));
                    if (job.errors || job.warnings)
                        line += "  " + counts_text(ok ? 0 : job.errors, job.warnings);
                    line += "\n";
                    if (options_.verbose) line += con.dim(join(job.args, " ")) + "\n";
                }
                if (!line.empty()) con.print(line);
                cv.notify_all();
            }
        };

        std::vector<std::thread> pool;
        for (int i = 0; i < parallel; ++i)
        {
            pool.emplace_back(worker);
        }

        // progress bar
        {
            std::unique_lock<std::mutex> lock(m);
            while (!(outstanding == 0 || (stop && running == 0)))
            {
                if (con.tty())
                {
                    con.set_status(render_status(todo, finished, total, done_estimate,
                                                 total_estimate, use_history, parallel,
                                                 start, any_failed));
                }
                cv.wait_for(lock, std::chrono::milliseconds(100));
            }
            stop = true;
        }
        cv.notify_all();
        for (auto& t : pool)
        {
            t.join();
        }
        con.clear_status();
        wall_seconds = seconds_since(start);

        for (int i : todo)
        {
            if (jobs_[i].state == JobState::Pending) jobs_[i].state = JobState::Skipped;
        }
        return !any_failed;
    }

    // Called with the executor mutex held.
    std::string render_status(const std::vector<int>& todo, int finished, int total,
                              double done_estimate, double total_estimate,
                              bool use_history, int parallel,
                              SteadyClock::time_point start, bool any_failed) const
    {
        auto& c = console();
        auto now = SteadyClock::now();
        double done = done_estimate;
        double critical = 0;
        std::vector<std::pair<SteadyClock::time_point, std::string>> running;
        int remaining = 0;
        for (int i : todo)
        {
            const Job& j = jobs_[i];
            if (j.state == JobState::Running)
            {
                double elapsed = std::chrono::duration<double>(now - j.started).count();
                done += std::min(elapsed, j.estimate * 0.95);
                critical = std::max(critical, j.priority - elapsed);
                running.emplace_back(j.started, short_name(j));
                ++remaining;
            }
            else if (j.state == JobState::Pending)
            {
                critical = std::max(critical, j.priority);
                ++remaining;
            }
        }
        double fraction = use_history ? done / std::max(total_estimate, 1e-9)
                                      : static_cast<double>(finished) / total;
        fraction = std::clamp(fraction, 0.0, 1.0);

        const int bar_width = 24;
        int filled = static_cast<int>(fraction * bar_width + 0.5);
        std::string bar;
        for (int i = 0; i < bar_width; ++i) bar += i < filled ? "█" : "░";

        char pct[64];
        std::snprintf(pct, sizeof(pct), "%3d%%", static_cast<int>(fraction * 100));
        std::string status = c.dim("[" + std::to_string(finished) + "/" +
                                   std::to_string(total) + "] ") +
                             (any_failed ? c.red(bar) : c.green(bar)) + " " + pct;
        if (use_history)
        {
            double eta = std::max((total_estimate - done) /
                                      std::max(1, std::min(parallel, remaining)),
                                  critical);
            status += c.dim("  ~" + format_duration(std::max(eta, 0.0)) + " left");
        }
        else
        {
            status += c.dim("  " + format_duration(seconds_since(start)));
        }
        std::sort(running.begin(), running.end());
        std::vector<std::string> names;
        for (const auto& r : running) names.push_back(r.second);
        status += "  " + c.cyan(join(names, " "));
        return status;
    }

    // ---------------------------------------------------------------- reporting

    // Headers outside the project (standard library, SDKs) are not tracked, like -MMD.
    bool inside_project(const std::string& path) const
    {
        fs::path p(path);
        if (p.is_relative() && !p.has_root_name())
        {
            return true;
        }
        static const std::string root = str_lower_if_windows(fs::current_path().generic_string() + "/");
        return str_lower_if_windows(p.lexically_normal().generic_string()).starts_with(root);
    }

    static std::string str_lower_if_windows(std::string s)
    {
        return target_windows ? str_lower(std::move(s)) : s;
    }

    void update_cache()
    {
        // forget outputs that are no longer part of the build (renamed sources, ...)
        for (auto it = cache_.entries.begin(); it != cache_.entries.end();)
        {
            it = job_by_output_.contains(it->first) ? std::next(it) : cache_.entries.erase(it);
        }
        for (const auto& j : jobs_)
        {
            if (j.state == JobState::Done)
            {
                CacheEntry& e = cache_.entries[j.output];
                e.hash = hash_args(j.args);
                e.stamp = j.stamp;
                e.seconds = j.seconds;
                e.display = j.display;
                e.diagnostics = has_text(j.diagnostics) ? j.diagnostics : "";
                if (j.type == JobType::Compile)
                {
                    e.deps.clear();
                    for (const auto& d : j.discovered_deps)
                    {
                        if (inside_project(d))
                            e.deps.push_back(fs::path(d).lexically_normal().generic_string());
                    }
                }
            }
            else if (j.state == JobState::Failed)
            {
                cache_.entries.erase(j.output); // never treat a failed output as current
            }
        }
        cache_.save(cache_path());
    }

    // Diagnostics of every job, one atomic block per file, sorted by file.
    // One file's diagnostics: a header line with the counts, then the compiler output.
    std::string diagnostic_block(const std::string& display, std::string text, int errors,
                                 int warnings, const std::string& command) const
    {
        auto& c = console();
        int width = std::min(c.width(), 100);
        std::string counts = counts_text(errors, warnings);
        std::string title = "── " + display + " ";
        int rule = width - static_cast<int>(title.size()) -
                   static_cast<int>(strip_ansi(counts).size()) - 2;
        std::string line;
        for (int i = 0; i < std::max(rule, 3); ++i) line += "─";
        std::string block = "\n" + (errors ? c.red(title) : c.yellow(title)) + c.dim(line) +
                            " " + counts + "\n";
        if (!command.empty())
        {
            block += c.dim("$ " + command) + "\n";
        }
        if (!c.color())
        {
            text = strip_ansi(text); // replayed output may have been captured in color
        }
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.pop_back();
        if (!text.empty()) block += text + "\n";
        return block;
    }

    // Diagnostics of every job that ran, one atomic block per file, sorted by file.
    void report_diagnostics()
    {
        auto& c = console();
        std::vector<const Job*> noisy;
        for (const auto& j : jobs_)
        {
            bool failed = j.state == JobState::Failed;
            if (failed || ((j.state == JobState::Done) && has_text(j.diagnostics)))
                noisy.push_back(&j);
        }
        std::sort(noisy.begin(), noisy.end(),
                  [](const Job* a, const Job* b) { return a->display < b->display; });
        for (const Job* j : noisy)
        {
            bool failed = j->state == JobState::Failed;
            // the command only helps when the output alone doesn't explain the failure
            bool show_cmd = failed && (options_.verbose || j->type != JobType::Compile);
            std::string block = diagnostic_block(j->display, j->diagnostics,
                                                 failed ? j->errors : 0, j->warnings,
                                                 show_cmd ? join(j->args, " ") : "");
            if (failed && !j->result.started)
                block += c.red("could not start the command") + "\n";
            c.print(block);
        }
    }

    // Warnings of files that were not recompiled are only summarized, they are often
    // from third party headers. `warnings` replays them.
    void report_hidden_warnings()
    {
        auto& c = console();
        std::vector<std::pair<int, std::string>> files;
        int total = 0;
        for (const auto& j : jobs_)
        {
            if (j.state != JobState::UpToDate) continue;
            auto it = cache_.entries.find(j.output);
            if (it == cache_.entries.end() || it->second.diagnostics.empty()) continue;
            int errors = 0, warnings = 0;
            count_diagnostics(it->second.diagnostics, errors, warnings);
            if (warnings == 0) continue;
            files.emplace_back(warnings, j.display);
            total += warnings;
        }
        if (files.empty())
        {
            return;
        }
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first > b.first : a.second < b.second;
        });
        std::string s = c.yellow("! ") + std::to_string(total) +
                        (total == 1 ? " warning" : " warnings") + " hidden in " +
                        std::to_string(files.size()) +
                        (files.size() == 1 ? " unchanged file" : " unchanged files") +
                        c.dim(" (show with '" + program_ + " warnings [filter]')") + "\n";
        const size_t max_lines = 5;
        for (size_t i = 0; i < std::min(files.size(), max_lines); ++i)
        {
            char count[16];
            std::snprintf(count, sizeof(count), "%6d  ", files[i].first);
            s += c.yellow(count) + files[i].second + "\n";
        }
        if (files.size() > max_lines)
        {
            s += c.dim("        ... and " + std::to_string(files.size() - max_lines) +
                       " more files") + "\n";
        }
        c.print(s);
    }

    void report_summary(bool ok, double wall)
    {
        auto& c = console();
        int built = 0, skipped = 0, up_to_date = 0, errors = 0, warnings = 0;
        double cpu = 0;
        std::vector<const Job*> done;
        std::vector<const Job*> failed_jobs;
        for (const auto& j : jobs_)
        {
            switch (j.state)
            {
            case JobState::Done: built++; done.push_back(&j); break;
            case JobState::Failed: failed_jobs.push_back(&j); break;
            case JobState::Skipped: skipped++; break;
            case JobState::UpToDate: up_to_date++; break;
            default: break;
            }
            if (j.state == JobState::Done || j.state == JobState::Failed)
            {
                cpu += j.seconds;
                warnings += j.warnings;
                if (j.state == JobState::Failed) errors += j.errors;
            }
        }
        std::string profile = join(active_profiles_, "+");
        std::string s = "\n";
        if (!ok)
        {
            std::sort(failed_jobs.begin(), failed_jobs.end(),
                      [](const Job* a, const Job* b) { return a->display < b->display; });
            s += c.red("✗ build failed") + " (" + profile + ", " + format_duration(wall) +
                 "): " + counts_text(errors, warnings) + "\n";
            for (const Job* j : failed_jobs)
            {
                s += "    " + c.red("✗ ") + j->display + "  " + counts_text(j->errors, 0) + "\n";
            }
            if (skipped)
            {
                s += c.dim("    " + std::to_string(skipped) +
                           " job(s) not run because of the failure") + "\n";
            }
            c.print(s);
            return;
        }
        if (built == 0)
        {
            c.println(c.green("✓ ") + profile + ": everything up to date (" +
                      std::to_string(up_to_date) + " jobs)");
            return;
        }
        s += c.green("✓ ") + profile + ": built " + std::to_string(built) + "/" +
             std::to_string(jobs_.size()) + " jobs in " + c.bold(format_duration(wall));
        if (wall > 0.05)
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), " (cpu %s, %.1fx parallel)",
                          format_duration(cpu).c_str(), cpu / wall);
            s += c.dim(buf);
        }
        if (warnings) s += ", " + counts_text(0, warnings);
        s += "\n";
        std::sort(done.begin(), done.end(),
                  [](const Job* a, const Job* b) { return a->seconds > b->seconds; });
        if (done.size() > 2)
        {
            std::vector<std::string> slow;
            for (size_t i = 0; i < std::min<size_t>(3, done.size()); ++i)
                slow.push_back(done[i]->display + " " + format_duration(done[i]->seconds));
            s += c.dim("  slowest: " + join(slow, " · ") + "  (see '" + program_ + " stats')") +
                 "\n";
        }
        c.print(s);
    }

    void write_compdb()
    {
        std::string cwd = fs::current_path().generic_string();
        std::string json = "[\n";
        bool first = true;
        auto entry = [&](const std::vector<std::string>& args, const std::string& file,
                         const std::string& output) {
            json += first ? "" : ",\n";
            first = false;
            json += "  {\n    \"directory\": \"" + json_escape(cwd) + "\",\n";
            json += "    \"file\": \"" + json_escape(fs::absolute(file).generic_string()) + "\",\n";
            if (!output.empty())
                json += "    \"output\": \"" + json_escape(output) + "\",\n";
            json += "    \"arguments\": [";
            for (size_t i = 0; i < args.size(); ++i)
                json += (i ? ", \"" : "\"") + json_escape(args[i]) + "\"";
            json += "]\n  }";
        };
        for (const auto& j : jobs_)
        {
            if (j.type == JobType::Compile) entry(j.args, j.source, j.output);
        }
        if (!self_source_path().empty())
        {
            entry(self_compile_command(self_source_path(), "nobcpp"), self_source_path(), "");
        }
        json += "\n]\n";

        // only touch the file when it changed, clangd re-indexes on every write
        std::ifstream in("compile_commands.json");
        std::stringstream old;
        old << in.rdbuf();
        in.close();
        if (old.str() != json)
        {
            std::ofstream("compile_commands.json", std::ios::trunc) << json;
        }
    }

    // ---------------------------------------------------------------- commands

    bool cmd_build()
    {
        plan();
        write_compdb();
        if (options_.verbose)
        {
            console().println(console().dim("compiler: " + join(toolchain().cxx, " ") + " (" +
                                            family_name(toolchain().family) + ", " +
                                            toolchain().version + ")"));
        }
        double wall = 0;
        bool ok = execute(wall);
        BuildRecord record{wall, 0, 0, static_cast<int>(jobs_.size()), threads_used_};
        for (const auto& j : jobs_)
        {
            if (j.state == JobState::Done)
            {
                record.cpu += j.seconds;
                record.jobs_run++;
            }
        }
        if (ok && record.jobs_run > 0)
        {
            cache_.last_build = record;
            if (record.jobs_run == record.jobs_total)
            {
                cache_.last_full_build = record;
            }
        }
        update_cache();
        report_diagnostics();
        report_summary(ok, wall);
        report_hidden_warnings();
        return ok;
    }

    bool cmd_clean(bool all)
    {
        fs::path dir = all ? fs::path(build_dir) : out_dir_;
        std::error_code ec;
        auto removed = fs::remove_all(dir, ec);
        if (ec)
        {
            console().error("could not remove '" + display_path(dir) + "': " + ec.message());
            return false;
        }
        console().println(console().green("✓ ") + "removed " + display_path(dir) + " (" +
                          std::to_string(removed) + " files)");
        return true;
    }

    int cmd_run(const std::string& name)
    {
        std::shared_ptr<Target> target;
        if (!name.empty())
        {
            target = find_target(name);
        }
        else
        {
            std::vector<std::shared_ptr<Target>> exes;
            for (const auto& t : targets_)
                if (t->kind() == Kind::Executable) exes.push_back(t);
            if (exes.size() != 1)
            {
                std::vector<std::string> names;
                for (const auto& t : exes) names.push_back(t->name());
                console().error(exes.empty() ? "no executable target to run"
                                             : "several executables, choose one: run " +
                                                   join(names, "|"));
                return 2;
            }
            target = exes[0];
        }
        if (!target || target->kind() != Kind::Executable)
        {
            console().error("'" + name + "' is not an executable target");
            return 2;
        }
        if (!cmd_build())
        {
            return 1;
        }
        fs::path exe = fs::absolute(output_path(*target));
        std::vector<std::string> args = {exe.string()};
        append(args, options_.run_args);
        console().println(console().dim("› " + display_path(output_path(*target)) +
                                        (options_.run_args.empty()
                                             ? ""
                                             : " " + join(options_.run_args, " "))));
        std::fflush(stdout);
        return run_interactive(args);
    }

    // Duration of a full build with `parallel` jobs, scheduled like execute() does
    // (longest remaining chain first), using the recorded durations.
    double simulate_full_build(int parallel, const std::vector<double>& priority) const
    {
        const int n = static_cast<int>(jobs_.size());
        std::vector<int> pending(n);
        auto by_priority = [&](int a, int b) { return priority[a] < priority[b]; };
        std::priority_queue<int, std::vector<int>, decltype(by_priority)> ready(by_priority);
        for (int i = 0; i < n; ++i)
        {
            pending[i] = static_cast<int>(jobs_[i].deps.size());
            if (pending[i] == 0) ready.push(i);
        }
        using Running = std::pair<double, int>; // finish time, job
        std::priority_queue<Running, std::vector<Running>, std::greater<Running>> running;
        double now = 0;
        while (!ready.empty() || !running.empty())
        {
            while (static_cast<int>(running.size()) < parallel && !ready.empty())
            {
                int id = ready.top();
                ready.pop();
                running.emplace(now + jobs_[id].estimate, id);
            }
            auto [finish, id] = running.top();
            running.pop();
            now = finish;
            for (int d : jobs_[id].dependents)
            {
                if (--pending[d] == 0) ready.push(d);
            }
        }
        return now;
    }

    void cmd_stats()
    {
        auto& c = console();
        plan(); // the dependency graph, with the recorded durations as estimates
        std::vector<const Job*> recorded;
        double sum = 0;
        for (const auto& j : jobs_)
        {
            sum += j.estimate;
            if (j.has_history) recorded.push_back(&j);
        }
        std::string profile = join(active_profiles_, "+");
        if (recorded.empty())
        {
            c.println("no timing data for profile '" + profile + "' yet, build first");
            return;
        }
        std::sort(recorded.begin(), recorded.end(),
                  [](const Job* a, const Job* b) { return a->estimate > b->estimate; });

        std::string s = c.bold("Job times") +
                        c.dim(" (" + profile + ", the most recent run of each job)") + "\n";
        const int bar_width = 30;
        double max = recorded[0]->estimate;
        for (const Job* j : recorded)
        {
            int filled = static_cast<int>(j->estimate / std::max(max, 1e-9) * bar_width + 0.5);
            std::string bar;
            for (int i = 0; i < bar_width; ++i) bar += i < filled ? "█" : " ";
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%9s %5.1f%% ", format_duration(j->estimate).c_str(),
                          100.0 * j->estimate / std::max(sum, 1e-9));
            s += buf + c.cyan(bar) + " " + j->display + "\n";
        }

        // Longest chain of jobs that depend on each other (compile -> archive -> link):
        // no build can be faster than this, however many threads it uses.
        std::vector<double> chain(jobs_.size(), 0.0);
        std::vector<int> next(jobs_.size(), -1);
        for (int i = static_cast<int>(jobs_.size()) - 1; i >= 0; --i)
        {
            chain[i] = jobs_[i].estimate;
            for (int d : jobs_[i].dependents)
            {
                if (jobs_[i].estimate + chain[d] > chain[i])
                {
                    chain[i] = jobs_[i].estimate + chain[d];
                    next[i] = d;
                }
            }
        }
        int first = static_cast<int>(std::max_element(chain.begin(), chain.end()) - chain.begin());
        std::vector<std::string> path;
        for (int i = first; i >= 0; i = next[i])
        {
            path.push_back(short_name(jobs_[i]));
        }
        double critical = chain[first];

        // Best case: the build simulated with 1..N threads, never more threads than jobs.
        int cores = std::max(1u, std::thread::hardware_concurrency());
        int max_threads = std::min(options_.jobs > 0 ? options_.jobs : cores,
                                   static_cast<int>(jobs_.size()));
        double fastest = simulate_full_build(max_threads, chain);
        int useful = max_threads;
        for (int p = 1; p < max_threads; ++p)
        {
            if (simulate_full_build(p, chain) <= fastest * 1.01)
            {
                useful = p;
                break;
            }
        }

        auto row = [&](const std::string& label, double seconds, const std::string& how) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%-16s %9s  ", label.c_str(),
                          format_duration(seconds).c_str());
            return c.bold(buf) + c.dim(how) + "\n";
        };
        auto threads_text = [](int n) { return std::to_string(n) + (n == 1 ? " thread" : " threads"); };
        const std::string indent(28, ' ');

        s += "\n";
        s += row("sum of job times", sum,
                 "all " + std::to_string(jobs_.size()) +
                     " jobs one after another, what a build with -j1 takes");
        if (recorded.size() < jobs_.size())
        {
            s += indent + c.dim(std::to_string(jobs_.size() - recorded.size()) +
                                " jobs have no recorded time yet and are estimated") + "\n";
        }
        if (const auto& r = cache_.last_full_build)
        {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "wall clock, %d jobs on %s, %.1fx parallel",
                          r->jobs_run, threads_text(r->threads).c_str(),
                          r->cpu / std::max(r->wall, 1e-9));
            s += row("last full build", r->wall, buf);
        }
        if (const auto& r = cache_.last_build; r && r->jobs_run != r->jobs_total)
        {
            s += row("last build", r->wall,
                     "wall clock, " + std::to_string(r->jobs_run) + " of " +
                         std::to_string(r->jobs_total) + " jobs on " + threads_text(r->threads));
        }
        s += row("critical path", critical,
                 join(path, " → ") + ", these depend on each other and run in sequence");
        s += row("best case", fastest,
                 "with -j" + std::to_string(useful) +
                     ", simulated from the job times and their dependencies");
        if (fastest <= critical * 1.01)
        {
            s += indent + c.dim("reaches the critical path, more threads can't make it faster") +
                 "\n";
        }
        else if (useful < max_threads)
        {
            s += indent + c.dim("more threads don't help, the dependencies don't allow more "
                                "jobs to run at the same time") + "\n";
        }
        else if (max_threads == static_cast<int>(jobs_.size()))
        {
            s += indent + c.dim("every job gets its own thread") + "\n";
        }
        else
        {
            s += indent + c.dim("limited by " + threads_text(max_threads) +
                                (options_.jobs > 0 ? " (-j)" : " (cores)") +
                                ", more would still help") + "\n";
        }
        c.print(s);
    }

    // Replays the stored output of every file, filtered by substrings of its path.
    void cmd_warnings(const std::vector<std::string>& filters)
    {
        auto& c = console();
        Cache cache;
        cache.load(cache_path());
        std::vector<const CacheEntry*> entries;
        for (const auto& [output, e] : cache.entries)
        {
            bool match = filters.empty() ||
                         std::any_of(filters.begin(), filters.end(), [&](const auto& f) {
                             return e.display.find(f) != std::string::npos;
                         });
            if (match && !e.diagnostics.empty()) entries.push_back(&e);
        }
        std::sort(entries.begin(), entries.end(),
                  [](const auto* a, const auto* b) { return a->display < b->display; });
        int total = 0;
        for (const CacheEntry* e : entries)
        {
            int errors = 0, warnings = 0;
            count_diagnostics(e->diagnostics, errors, warnings);
            total += warnings;
            c.print(diagnostic_block(e->display, e->diagnostics, 0, warnings, ""));
        }
        std::string profile = join(active_profiles_, "+");
        if (entries.empty())
        {
            c.println(c.green("✓ ") + profile + ": no warnings stored" +
                      (filters.empty() ? "" : " for files matching '" + join(filters, "', '") + "'"));
            return;
        }
        c.println("\n" + c.yellow("! ") + profile + ": " + std::to_string(total) +
                  (total == 1 ? " warning" : " warnings") + " in " +
                  std::to_string(entries.size()) + (entries.size() == 1 ? " file" : " files") +
                  c.dim(" (from the last compile of each file)"));
    }

    void cmd_tree()
    {
        auto& c = console();
        std::string s;
        std::function<void(const Target&, int)> print = [&](const Target& t, int depth) {
            const char* kinds[] = {"executable", "static lib", "shared lib",
                                   t.origin.empty() ? "header only" : "imported"};
            std::string indent(depth * 2, ' ');
            std::string out = t.kind() == Kind::HeaderOnly
                                  ? join(t.public_flags.system_include_dirs, ", ")
                                  : display_path(output_path(t));
            if (!t.origin.empty())
            {
                out = t.origin;
            }
            if (t.kind() == Kind::HeaderOnly && !t.public_flags.include_dirs.empty())
            {
                out += (out.empty() ? "" : ", ") + join(t.public_flags.include_dirs, ", ");
            }
            s += indent + c.bold(t.name()) +
                 c.dim(" (" + std::string(kinds[(int)t.kind()]) + " → " + out + ")") + "\n";
            for (const auto& src : t.sources())
            {
                s += indent + "  " + display_path(src) + "\n";
            }
            for (const auto& dep : t.deps())
            {
                print(*dep, depth + 1);
            }
        };
        std::set<const Target*> used;
        for (const auto& t : targets_)
            for (const auto& d : t->deps()) used.insert(d.get());
        for (const auto& t : targets_)
            if (!used.contains(t.get())) print(*t, 0);
        c.print(s);
    }
};

inline int Build::run()
{
    std::vector<Action> actions;
    resolve(actions);
    bool needs_dependencies = std::any_of(actions.begin(), actions.end(), [](const Action& a) {
        return a.command == "build" || a.command == "rebuild" || a.command == "run" ||
               a.command == "compdb";
    });
    if (needs_dependencies && !prepare_dependencies())
    {
        return 1;
    }
    for (const auto& action : actions)
    {
        const std::string& cmd = action.command;
        bool ok = true;
        if (cmd == "help")
        {
            print_help();
        }
        else if (cmd == "build")
        {
            ok = cmd_build();
        }
        else if (cmd == "rebuild")
        {
            ok = cmd_clean(false) && cmd_build();
        }
        else if (cmd == "clean")
        {
            ok = cmd_clean(false);
        }
        else if (cmd == "cleanall")
        {
            ok = cmd_clean(true);
        }
        else if (cmd == "compdb")
        {
            plan();
            write_compdb();
            console().println(console().green("✓ ") + "wrote compile_commands.json");
        }
        else if (cmd == "stats")
        {
            cmd_stats();
        }
        else if (cmd == "tree")
        {
            cmd_tree();
        }
        else if (cmd == "warnings")
        {
            cmd_warnings(action.filters);
        }
        else if (cmd == "update")
        {
            console().println(console().green("✓ ") + "fetched dependencies are at the latest "
                              "commit of their tag or branch (nobcpp.lock)");
        }
        else if (cmd == "run")
        {
            return cmd_run(action.target);
        }
        if (!ok)
        {
            return 1;
        }
    }
    return 0;
}

inline std::shared_ptr<Fetched> Build::fetch(const std::string& name, const Source& source)
{
    auto& c = console();
    if (source.git.empty() == source.url.empty())
    {
        c.error("fetch(\"" + name + "\"): give either .git or .url");
        std::exit(1);
    }
    set_env("GIT_TERMINAL_PROMPT", "0"); // fail instead of hanging on a password prompt
    std::string version;
    std::string label;
    if (!source.url.empty())
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%016llx",
                      static_cast<unsigned long long>(hash_args({source.url})));
        version = buf;
        label = source.url;
    }
    else
    {
        std::string commit = source.commit;
        std::string ref = source.ref.empty() ? "HEAD" : source.ref;
        if (commit.empty())
        {
            if (!lockfile_)
            {
                lockfile_ = std::make_unique<Lockfile>("nobcpp.lock");
            }
            auto it = lockfile_->entries.find(name);
            bool locked = it != lockfile_->entries.end() && it->second.url == source.git &&
                          it->second.ref == ref;
            if (locked && !updating_)
            {
                commit = it->second.commit;
            }
            else
            {
                if (options_.offline)
                {
                    c.error("'" + name + "' is not in nobcpp.lock yet, '" + ref +
                            "' can't be looked up with --offline");
                    std::exit(1);
                }
                auto resolved = resolve_git_ref(source.git, ref);
                if (!resolved)
                {
                    std::exit(1);
                }
                commit = *resolved;
                if (!locked || it->second.commit != commit)
                {
                    std::string was = locked ? " (was " + it->second.commit.substr(0, 12) + ")" : "";
                    c.println(c.cyan("nobcpp.lock: ") + name + " " + ref + " → " +
                              commit.substr(0, 12) + c.dim(was));
                    lockfile_->entries[name] = {source.git, ref, commit};
                    lockfile_->save();
                }
            }
        }
        version = commit;
        label = (source.ref.empty() ? "" : source.ref + " ") + commit.substr(0, 12);
    }

    fs::path dir = source_cache_dir() /
                   (name + "-" + version.substr(0, 12) + (source.submodules ? "-sm" : ""));
    if (!fs::exists(dir))
    {
        if (options_.offline)
        {
            c.error("'" + name + "' (" + label + ") is not downloaded yet and --offline is set");
            std::exit(1);
        }
        c.println(c.cyan("fetching ") + name + " " + c.dim(label));
        auto start = SteadyClock::now();
        bool ok = source.url.empty() ? fetch_git(source.git, version, source.submodules, dir)
                                     : fetch_archive(source.url, dir);
        if (!ok)
        {
            c.error("fetching '" + name + "' failed");
            std::exit(1);
        }
        c.println(c.green("✓ ") + "fetched " + name +
                  c.dim(" in " + format_duration(seconds_since(start)) + " → " +
                        display_path(dir)));
    }
    return std::make_shared<Fetched>(name, source, version, dir);
}

inline std::shared_ptr<Target> Build::cmake(std::shared_ptr<Fetched> source,
                                            const CMakeOptions& options)
{
    if (options.targets.empty())
    {
        console().error("cmake(" + source->name() +
                        "): name the CMake targets to link, e.g. .targets = {\"SDL3::SDL3\"}");
        std::exit(1);
    }
    if (options.package.empty() && options.targets[0].find("::") == std::string::npos)
    {
        console().error("cmake(" + source->name() + "): can't guess the find_package() name from '" +
                        options.targets[0] + "', set .package (e.g. .package = \"SFML\")");
        std::exit(1);
    }
    auto target = add_target(source->name(), Kind::HeaderOnly, {});
    const Source& s = source->source();
    target->origin = "cmake, " + (s.ref.empty() ? "" : s.ref + " ") +
                     (s.url.empty() ? source->version().substr(0, 12) : s.url);
    auto dep = std::make_shared<CMakeDep>();
    dep->target = target;
    dep->source = std::move(source);
    dep->options = options;
    cmake_deps_.push_back(dep);
    return target;
}

inline std::shared_ptr<Target> Build::pkg_config(const std::string& package, bool static_libs)
{
    if (toolchain().msvc_like())
    {
        console().error("pkg_config(\"" + package +
                        "\") needs a gcc or clang style compiler, use cmake() with msvc");
        std::exit(1);
    }
    const char* env = std::getenv("PKG_CONFIG");
    std::string exe = env && *env ? env : "pkg-config";
    std::vector<std::string> cmd = {exe, "--cflags", "--libs"};
    if (static_libs)
    {
        cmd.push_back("--static");
    }
    cmd.push_back(package);
    ProcessResult r = run_or_explain(cmd, "pkg-config " + package);
    if (r.exit_code != 0)
    {
        std::exit(1);
    }
    std::string version = run_capture({exe, "--modversion", package}).output;
    version = version.substr(0, version.find_first_of("\r\n"));

    auto target = add_target(package, Kind::HeaderOnly, {});
    target->origin = "pkg-config" + (version.empty() ? "" : ", " + version);
    Flags& f = target->public_flags;
    std::vector<std::string> args = split_args(r.output);
    for (size_t i = 0; i < args.size(); ++i)
    {
        const std::string& a = args[i];
        // "-Ipath" or "-I path"
        auto value = [&](size_t prefix) {
            return a.size() > prefix ? a.substr(prefix)
                                     : (i + 1 < args.size() ? args[++i] : std::string());
        };
        if (a.starts_with("-I"))
            f.system_include_dirs.push_back(value(2)); // third party: no warnings
        else if (a == "-isystem")
            f.system_include_dirs.push_back(value(8));
        else if (a.starts_with("-D"))
            f.defines.push_back(value(2));
        else if (a == "-pthread")
        {
            f.compile_flags.push_back(a);
            f.link_flags.push_back(a);
        }
        else if ((a == "-L" || a == "-l") && i + 1 < args.size())
            f.link_flags.push_back(a + args[++i]);
        else if (a == "-framework" && i + 1 < args.size())
            append(f.link_flags, {a, args[++i]});
        else if (a.starts_with("-L") || a.starts_with("-l") || a.starts_with("-Wl,") ||
                 a.starts_with("-framework") || a[0] != '-')
            f.link_flags.push_back(a);
        else
            f.compile_flags.push_back(a);
    }
    return target;
}

inline std::string Build::dependency_build_type() const
{
    bool debug_info = profile_flags_.debug_info.value_or(false);
    switch (profile_flags_.opt.value_or(Opt::O0))
    {
    case Opt::O2:
    case Opt::O3: return debug_info ? "RelWithDebInfo" : "Release";
    case Opt::Os: return "MinSizeRel";
    default: return "Debug";
    }
}

// Configures, builds and installs a CMake dependency (unless an earlier run did), then
// asks CMake which flags its targets need. Runs on its own thread.
inline bool Build::build_cmake_dep(CMakeDep& d, std::mutex& m)
{
    if (auto imported = ImportedFlags::load(d.import_file))
    {
        apply_import(d, *imported);
        return true;
    }
    auto& c = console();
    {
        std::lock_guard<std::mutex> lock(m);
        d.built = true;
    }
    std::error_code ec;
    fs::create_directories(d.dir, ec);
    std::ofstream log(d.log_file, std::ios::app);
    auto step = [&](const std::string& phase, const std::vector<std::string>& cmd) {
        {
            std::lock_guard<std::mutex> lock(m);
            d.phase = phase;
            d.phase_start = SteadyClock::now();
        }
        if (!c.tty() || options_.verbose)
        {
            c.println(c.dim("  " + d.target->name() + ": " + phase));
        }
        log << "$ " << join(cmd, " ") << "\n";
        ProcessResult r = run_capture(cmd);
        log << r.output << "\n";
        log.flush();
        if (r.exit_code != 0)
        {
            d.error = "cmake " + phase + " failed\n" + c.dim("$ " + join(cmd, " ")) + "\n" +
                      tail_lines(r.output, 25) + c.dim("full log: " + display_path(d.log_file));
            return false;
        }
        return true;
    };

    fs::path installed = d.dir / "installed";
    if (!fs::exists(installed))
    {
        std::string jobs = std::to_string(std::max(1u, std::thread::hardware_concurrency()));
        if (!step("configure", d.configure) ||
            !step("build", {"cmake", "--build", d.build_dir.string(), "--config", d.build_type,
                            "--parallel", jobs}) ||
            !step("install", {"cmake", "--install", d.build_dir.string(), "--config", d.build_type}))
        {
            return false;
        }
        std::ofstream(installed) << "ok\n";
    }

    // A tiny project that links the requested targets: CMake resolves everything they
    // need (includes, defines, transitive libraries) and reports it via its file API.
    const CMakeOptions& o = d.options;
    std::string package = o.package;
    if (package.empty())
    {
        package = o.targets[0].substr(0, o.targets[0].find("::"));
    }
    fs::remove_all(d.probe_dir, ec);
    fs::path probe_build = d.probe_dir / "build";
    fs::create_directories(probe_build / ".cmake" / "api" / "v1" / "query", ec);
    std::ofstream(probe_build / ".cmake" / "api" / "v1" / "query" / "codemodel-v2");
    std::ofstream(d.probe_dir / "probe.cpp") << "int main() { return 0; }\n";
    {
        std::ofstream cm(d.probe_dir / "CMakeLists.txt");
        cm << "cmake_minimum_required(VERSION 3.16)\nproject(nobcpp_probe C CXX)\n";
        for (const auto& v : o.probe_variables)
        {
            size_t eq = v.find('=');
            cm << "set(" << v.substr(0, eq) << " \"" << (eq == std::string::npos ? "" : v.substr(eq + 1))
               << "\")\n";
        }
        cm << "find_package(" << package << " REQUIRED";
        if (!o.components.empty()) cm << " COMPONENTS " << join(o.components, " ");
        cm << ")\nadd_executable(nobcpp_probe probe.cpp)\n";
        cm << "target_link_libraries(nobcpp_probe PRIVATE " << join(o.targets, " ") << ")\n";
    }
    std::vector<std::string> probe = {"cmake", "-S", d.probe_dir.string(), "-B", probe_build.string(),
                                      "-DCMAKE_BUILD_TYPE=" + d.build_type,
                                      "-DCMAKE_PREFIX_PATH=" + d.prefix_path};
    append(probe, d.common);
    // without this the probe would report the default flags of its build type
    for (const char* var : {"CMAKE_C_FLAGS", "CMAKE_CXX_FLAGS", "CMAKE_EXE_LINKER_FLAGS"})
    {
        for (const char* config : {"", "_DEBUG", "_RELEASE", "_RELWITHDEBINFO", "_MINSIZEREL"})
        {
            probe.push_back("-D" + std::string(var) + config + "=");
        }
    }
    if (!step("import", probe))
    {
        return false;
    }
    std::string error;
    auto imported = ImportedFlags::from_file_api(probe_build, error);
    if (!imported)
    {
        d.error = "reading what CMake reported failed: " + error;
        return false;
    }
    imported->save(d.import_file);
    apply_import(d, *imported);
    return true;
}

inline void Build::apply_import(CMakeDep& d, const ImportedFlags& imported)
{
    Flags& f = d.target->public_flags;
    append(f.system_include_dirs, imported.includes); // third party: no warnings
    append(f.defines, imported.defines);
    append(f.compile_flags, imported.compile);
    append(f.link_flags, imported.link);
}

// Builds all cmake() dependencies, independent ones in parallel.
inline bool Build::prepare_dependencies()
{
    if (deps_prepared_ || cmake_deps_.empty())
    {
        return true;
    }
    deps_prepared_ = true;
    auto& c = console();
    const Toolchain& tc = toolchain();

    std::vector<std::string> common;
    if (run_capture({"ninja", "--version"}).exit_code == 0)
    {
        append(common, {"-G", "Ninja"});
    }
    const std::string& cxx = tc.cxx.back();
    common.push_back("-DCMAKE_CXX_COMPILER=" + cxx);
    common.push_back("-DCMAKE_C_COMPILER=" + c_compiler_for(cxx));
    if (tc.cxx.size() > 1)
    {
        std::string launcher = join(std::vector<std::string>(tc.cxx.begin(), tc.cxx.end() - 1), ";");
        common.push_back("-DCMAKE_CXX_COMPILER_LAUNCHER=" + launcher);
        common.push_back("-DCMAKE_C_COMPILER_LAUNCHER=" + launcher);
    }
    if (tc.msvc_like())
    {
        // the static runtime (/MT), what cl uses by default, mixing them fails to link
        append(common, {"-DCMAKE_POLICY_DEFAULT_CMP0091=NEW",
                        "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"});
    }

    auto hex = [](uint64_t h) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
        return std::string(buf).substr(0, 10);
    };
    fs::path root = fs::absolute(fs::path(build_dir) / "_deps");
    for (auto& d : cmake_deps_)
    {
        const CMakeOptions& o = d->options;
        d->build_type = o.build_type.empty() ? dependency_build_type() : o.build_type;
        d->common = common;
        std::vector<std::string> prefix;
        for (const auto& dep_target : o.depends)
        {
            auto it = std::find_if(cmake_deps_.begin(), cmake_deps_.end(),
                                   [&](const auto& x) { return x->target == dep_target; });
            if (it == cmake_deps_.end() || it->get() == d.get() || (*it)->install_dir.empty())
            {
                c.error("cmake(" + d->target->name() + "): .depends must name cmake() targets "
                        "declared before it");
                return false;
            }
            d->depends.push_back(it->get());
            prefix.push_back((*it)->install_dir.generic_string());
        }
        // everything that influences the build decides its directory, so changing an
        // option or the profile never reuses a wrong build
        std::vector<std::string> key = {d->source->version(), o.source_subdir, d->build_type};
        append(key, common);
        append(key, prefix);
        append(key, o.options);
        uint64_t build_hash = hash_args(key);
        d->dir = root / (d->target->name() + "-" + str_lower(d->build_type) + "-" + hex(build_hash));
        d->build_dir = d->dir / "build";
        d->install_dir = d->dir / "install";
        d->log_file = d->dir / "nobcpp.log";
        prefix.insert(prefix.begin(), d->install_dir.generic_string());
        d->prefix_path = join(prefix, ";");

        std::vector<std::string> probe_key = {std::to_string(build_hash), o.package};
        append(probe_key, o.targets);
        append(probe_key, o.components);
        append(probe_key, o.probe_variables);
        std::string probe_id = hex(hash_args(probe_key));
        d->probe_dir = d->dir / ("probe-" + probe_id);
        d->import_file = d->dir / ("import-" + probe_id + ".txt");

        d->configure = {"cmake", "-S", (d->source->dir() / o.source_subdir).string(),
                        "-B", d->build_dir.string(),
                        "-DCMAKE_BUILD_TYPE=" + d->build_type,
                        "-DCMAKE_INSTALL_PREFIX=" + d->install_dir.string(),
                        "-DCMAKE_PREFIX_PATH=" + d->prefix_path,
                        "-DBUILD_SHARED_LIBS=OFF",
                        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"};
        append(d->configure, common);
        for (const auto& opt : o.options)
        {
            d->configure.push_back("-D" + opt); // after the defaults, so they can be overridden
        }
    }

    std::mutex m;
    std::condition_variable cv;
    auto run = [&](CMakeDep& d) {
        {
            std::unique_lock<std::mutex> lock(m);
            cv.wait(lock, [&] {
                return std::all_of(d.depends.begin(), d.depends.end(), [](const CMakeDep* x) {
                    return x->state == CMakeDep::State::Done || x->state == CMakeDep::State::Failed;
                });
            });
            if (std::any_of(d.depends.begin(), d.depends.end(),
                            [](const CMakeDep* x) { return x->state == CMakeDep::State::Failed; }))
            {
                d.error = "not built because a dependency it needs failed";
                d.state = CMakeDep::State::Failed;
                cv.notify_all();
                return;
            }
            d.state = CMakeDep::State::Running;
        }
        auto start = SteadyClock::now();
        bool ok = build_cmake_dep(d, m);
        std::lock_guard<std::mutex> lock(m);
        d.seconds = seconds_since(start);
        d.state = ok ? CMakeDep::State::Done : CMakeDep::State::Failed;
        cv.notify_all();
    };

    auto start = SteadyClock::now();
    std::vector<std::thread> threads;
    for (auto& d : cmake_deps_)
    {
        threads.emplace_back(run, std::ref(*d));
    }
    {
        std::unique_lock<std::mutex> lock(m);
        auto finished = [&] {
            return std::all_of(cmake_deps_.begin(), cmake_deps_.end(), [](const auto& d) {
                return d->state == CMakeDep::State::Done || d->state == CMakeDep::State::Failed;
            });
        };
        while (!finished())
        {
            if (c.tty())
            {
                std::vector<std::string> parts;
                for (const auto& d : cmake_deps_)
                {
                    if (d->state == CMakeDep::State::Running && d->built)
                    {
                        parts.push_back(c.cyan(d->target->name()) + " " + d->phase + " " +
                                        c.dim(format_duration(seconds_since(d->phase_start))));
                    }
                }
                if (!parts.empty())
                {
                    c.set_status(c.dim("dependencies (" + format_duration(seconds_since(start)) +
                                       ")  ") + join(parts, c.dim(" · ")));
                }
            }
            cv.wait_for(lock, std::chrono::milliseconds(150));
        }
    }
    for (auto& t : threads)
    {
        t.join();
    }
    c.clear_status();

    bool ok = true;
    for (const auto& d : cmake_deps_)
    {
        if (d->state == CMakeDep::State::Failed)
        {
            ok = false;
            c.print("\n" + c.red("── dependency " + d->target->name() + " ") + "\n" + d->error + "\n");
        }
        else if (d->built)
        {
            c.println(c.green("✓ ") + d->target->name() +
                      c.dim(" (" + d->target->origin + ", " + d->build_type + ") ready in " +
                            format_duration(d->seconds)));
        }
    }
    if (!ok)
    {
        c.println("\n" + c.red("✗ ") + "a dependency failed to build");
    }
    return ok;
}

} // namespace nob
