// End to end tests: every example is copied to tests/tmp/examples/, bootstrapped and
// built from scratch, then the scenarios that broke in the past are replayed against it.
//
//     c++ -std=c++20 -o tests/run tests/run.cpp     (from the repository root)
//     ./tests/run [filter]
#include "../nobcpp.hpp"

namespace fs = std::filesystem;

struct Result
{
    int exit_code;
    std::string output;
};

class Tests
{
  public:
    explicit Tests(std::string filter) : filter_(std::move(filter))
    {
    }

    // Starts a test on a fresh copy of a project (examples/..., tests/fixtures/...).
    // Returns false if filtered out.
    bool begin(const std::string& name, const std::string& project)
    {
        name_ = name;
        if (!filter_.empty() && name.find(filter_) == std::string::npos)
        {
            return false;
        }
        // tests/tmp mirrors the repository layout, so the examples' include of
        // ../../nobcpp.hpp finds the copy at tests/tmp/nobcpp.hpp
        fs::path root = fs::absolute("tests/tmp");
        dir_ = root / "examples" / name;
        std::error_code ec;
        fs::remove_all(dir_, ec);
        fs::create_directories(dir_);
        fs::copy_file("nobcpp.hpp", root / "nobcpp.hpp", fs::copy_options::overwrite_existing);
        fs::copy(project, dir_,
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        // drop build artifacts copied from a manual build of the example
        fs::remove_all(dir_ / "build", ec);
        fs::remove(dir_ / exe_name(), ec);

        auto cmd = nob::self_compile_command((dir_ / "nobcpp.cpp").string(),
                                             (dir_ / exe_name()).string());
        auto r = in_dir([&] { return nob::run_capture(cmd); });
        check(r.exit_code == 0, "bootstrap", r.output);
        return r.exit_code == 0;
    }

    Result nob(const std::vector<std::string>& args)
    {
        std::vector<std::string> cmd = {(dir_ / exe_name()).string(), "--no-color"};
        nob::append(cmd, args);
        auto r = in_dir([&] { return nob::run_capture(cmd); });
        return {r.exit_code, r.output};
    }

    void check(bool ok, const std::string& what, const std::string& output = "")
    {
        auto& c = nob::console();
        if (ok)
        {
            ++passed_;
            c.println(c.green("  ✓ ") + name_ + ": " + what);
            return;
        }
        ++failed_;
        c.println(c.red("  ✗ ") + name_ + ": " + what);
        if (!output.empty())
        {
            std::string indented;
            std::istringstream ss(output);
            std::string line;
            while (std::getline(ss, line)) indented += "      " + line + "\n";
            c.print(c.dim(indented));
        }
    }

    void expect(const Result& r, int exit_code, const std::vector<std::string>& contains,
                const std::vector<std::string>& not_contains, const std::string& what)
    {
        bool ok = r.exit_code == exit_code;
        for (const auto& s : contains) ok &= r.output.find(s) != std::string::npos;
        for (const auto& s : not_contains) ok &= r.output.find(s) == std::string::npos;
        check(ok, what, ok ? "" : "exit code " + std::to_string(r.exit_code) + "\n" + r.output);
    }

    // Sets the modification time to now, i.e. newer than every build so far.
    void touch(const std::string& file)
    {
        fs::last_write_time(dir_ / file, fs::file_time_type::clock::now());
    }

    void write(const std::string& file, const std::string& content)
    {
        std::ofstream(dir_ / file, std::ios::trunc) << content;
        touch(file);
    }

    void replace(const std::string& file, const std::string& from, const std::string& to)
    {
        std::ifstream in(dir_ / file);
        std::stringstream ss;
        ss << in.rdbuf();
        in.close();
        std::string text = ss.str();
        size_t pos = text.find(from);
        if (pos == std::string::npos)
        {
            check(false, "replace '" + from + "' in " + file);
            return;
        }
        write(file, text.replace(pos, from.size(), to));
    }

    void remove(const std::string& file)
    {
        fs::remove(dir_ / file);
    }

    int summary()
    {
        auto& c = nob::console();
        c.println("\n" + (failed_ ? c.red("✗ ") : c.green("✓ ")) + std::to_string(passed_) +
                  " passed, " + std::to_string(failed_) + " failed");
        return failed_ ? 1 : 0;
    }

  private:
    std::string filter_;
    std::string name_;
    fs::path dir_;
    int passed_ = 0;

  public:
    const fs::path& dir() const
    {
        return dir_;
    }

    // Continues the current test in another directory (e.g. a copy of the project).
    void set_dir(const fs::path& dir)
    {
        dir_ = dir;
    }

  private:
    int failed_ = 0;

    static std::string exe_name()
    {
        return nob::target_windows ? "nobcpp.exe" : "nobcpp";
    }

    template <typename F>
    nob::ProcessResult in_dir(F f)
    {
        fs::path old = fs::current_path();
        fs::current_path(dir_);
        auto r = f();
        fs::current_path(old);
        return r;
    }
};

int main(int argc, char** argv)
{
    nob::rebuild_self(__FILE__, argc, argv, {"../nobcpp.hpp"});
    if (!fs::exists("examples"))
    {
        nob::console().error("run the tests from the repository root");
        return 2;
    }
    Tests t(argc > 1 ? argv[1] : "");

    if (t.begin("basic", "examples/01_basic"))
    {
        t.expect(t.nob({"run"}), 0, {"Hello, nobcpp!", "6 * 7 = 42"}, {}, "builds and runs");
        t.expect(t.nob({}), 0, {"everything up to date"}, {}, "second build does nothing");
        // colors are on in a terminal: compiler color flags must not reach `ar`
        t.expect(t.nob({"rebuild", "--color=always"}), 0, {"built 5/5"}, {},
                 "colored build (as in a terminal) works");

        t.touch("src/greeter/greeter.hpp");
        t.expect(t.nob({}), 0, {"src/greeter/greeter.cpp", "src/app/main.cpp", "built 4/5"},
                 {"format.cpp"}, "touching a header rebuilds exactly its includers");

        t.expect(t.nob({"release"}), 0, {"release: built 5/5"}, {},
                 "switching profiles builds into a separate directory");
        t.expect(t.nob({}), 0, {"debug: everything up to date"}, {},
                 "switching back reuses the debug build");

        // used to crash with an uncaught filesystem_error on every following build
        t.remove("src/app/util/format.hpp");
        t.expect(t.nob({}), 1, {"build failed", "format.hpp"}, {"filesystem_error"},
                 "a deleted header gives a compiler error, not a crash");
        t.write("src/app/util/format.hpp",
                "#pragma once\n#include <string>\nstd::string format_product(int a, int b);\n");
        t.expect(t.nob({}), 0, {"debug: built"}, {}, "restoring the header recovers");

        t.write("src/greeter/greeter.cpp", "#include \"greeter.hpp\"\nstd::string greet(\n");
        t.write("src/app/util/format.cpp", "int broken = ;\n");
        t.expect(t.nob({}), 1,
                 {"2 errors", "✗ src/app/util/format.cpp", "✗ src/greeter/greeter.cpp"}, {},
                 "errors of all files are reported");

        t.expect(t.nob({"relase"}), 2, {"unknown command, profile or target 'relase'"}, {},
                 "typos are rejected");

        t.touch("nobcpp.cpp");
        t.expect(t.nob({"help"}), 0, {"rebuilding", "usage:"}, {},
                 "the build script rebuilds itself");
        t.replace("nobcpp.cpp", "b.flags.std = 20;", "b.flags.std = 20 +;");
        t.expect(t.nob({}), 1, {"rebuilding the build script failed"}, {},
                 "a broken build script fails with exit code 1");
        t.replace("nobcpp.cpp", "b.flags.std = 20 +;", "b.flags.std = 20;");
        t.expect(t.nob({"help"}), 0, {"usage:"}, {}, "and recovers once fixed");
    }

    if (t.begin("commands", "examples/01_basic"))
    {
        t.replace("nobcpp.cpp", "    return b.run();",
                  "    auto assets = b.custom(\"assets\");\n"
                  "    assets->add_copy(\"src/greeter\", b.output_dir() / \"assets\");\n"
                  "    assets->add_command({\"cmake\", \"-E\", \"copy\", \"src/app/main.cpp\",\n"
                  "                         (b.output_dir() / \"main.copy\").string()},\n"
                  "                        {\"src/app/main.cpp\"}, {b.output_dir() / \"main.copy\"});\n"
                  "    app->link(assets);\n"
                  "    greeter->flags.opt = nob::Opt::O2;\n"
                  "    return b.run();");
        t.expect(t.nob({"-v", "run"}), 0,
                 {"Hello, nobcpp!", "-O2 -g -Wall", "-O0 -g -Wall"}, {},
                 "target flags override the profile (-O2 for greeter, -O0 elsewhere)");
        bool copied = fs::exists(t.dir() / "build/debug/assets/greeter.hpp") &&
                      fs::exists(t.dir() / "build/debug/assets/greeter.cpp") &&
                      fs::exists(t.dir() / "build/debug/main.copy");
        t.check(copied, "copies and commands produce their outputs");
        t.expect(t.nob({}), 0, {"everything up to date"}, {}, "and are not rerun");
        t.touch("src/greeter/greeter.hpp");
        t.expect(t.nob({}), 0, {"src/greeter/greeter.hpp", "src/greeter/greeter.cpp"},
                 {"main.copy"}, "a changed input reruns only its copy");
        t.touch("src/app/main.cpp");
        t.expect(t.nob({}), 0, {"src/app/main.cpp", "built 3/"}, {},
                 "a command reruns without forcing extra relinks");
    }

    if (t.begin("timing", "examples/01_basic"))
    {
        t.expect(t.nob({}), 0, {"built 5/5"}, {}, "first build records the job times");
        t.expect(t.nob({"clean"}), 0, {"removed build/debug"}, {}, "clean");
        t.expect(t.nob({"stats"}), 0, {"src/app/main.cpp", "sum of job times"},
                 {"no timing data", "no recorded time"}, "the timing history survives 'clean'");
        t.expect(t.nob({"rebuild"}), 0, {"built 5/5"}, {}, "and 'rebuild' builds everything");

        t.write("src/app/main.cpp", "int main() { return undefined_thing; }\n");
        t.expect(t.nob({}), 1, {"build failed"}, {}, "a failing compile");
        t.expect(t.nob({"stats"}), 0, {"src/app/main.cpp", "sum of job times"}, {"no recorded time"},
                 "keeps the last successful time of the file");
        t.write("src/app/main.cpp", "#include \"greeter.hpp\"\n#include <iostream>\n"
                                    "int main() { std::cout << greet(\"again\") << \"\\n\"; }\n");

        t.write("src/extra.cpp", "int extra() { return 1; }\n");
        t.replace("nobcpp.cpp", "    app->link(greeter);", "    app->link(greeter);\n    app->add_source(\"src/extra.cpp\");");
        t.expect(t.nob({"stats"}), 0, {"1 jobs have no recorded time yet and are estimated from their file size (extra.cpp)"},
                 {}, "a new file is estimated from its size, the other objects keep their place and time");

        t.expect(t.nob({}), 0, {"src/extra.cpp"}, {}, "the new file is built");
        t.replace("nobcpp.cpp", "\n    app->add_source(\"src/extra.cpp\");", "");
        t.expect(t.nob({}), 0, {"debug: built"}, {}, "and removed from the build again");
        t.check(!fs::exists(t.dir() / "build/debug/obj/app/src/extra.cpp.o") &&
                    fs::exists(t.dir() / "build/debug/obj/app/src/app/main.cpp.o"),
                "the object of a removed source is deleted, the others stay");
        t.expect(t.nob({"cleanall"}), 0, {"removed build"}, {}, "cleanall");
        t.expect(t.nob({"stats"}), 0, {"no timing data"}, {}, "removes the history too");
    }

    if (t.begin("copied", "examples/01_basic"))
    {
        // the bootstrap compiles with an absolute path, like a self-rebuild does, so
        // __FILE__ in the binary points at this directory
        t.expect(t.nob({}), 0, {"built 5/5"}, {}, "original builds");
        fs::path original = t.dir();
        fs::path copy = original.parent_path() / "copied_moved";
        std::error_code ec;
        fs::remove_all(copy, ec);
        fs::copy(original, copy, fs::copy_options::recursive);
        t.set_dir(copy);
        t.replace("nobcpp.cpp", "    return b.run();", "    // edited in the copy\n    return b.run();");
        t.expect(t.nob({"help"}), 0, {"rebuilding", "usage:"}, {},
                 "a copied checkout rebuilds when its own build script changes");
        t.set_dir(original);
        t.expect(t.nob({}), 0, {"everything up to date"}, {"rebuilding"},
                 "and the original is not affected");
    }

    if (t.begin("shared_lib", "examples/02_shared_lib"))
    {
        t.expect(t.nob({"run"}), 0, {"plugin says 42"}, {}, "static lib inside a shared lib");
        t.touch("src/core/core.cpp");
        t.expect(t.nob({"run"}), 0, {"src/core/core.cpp", "plugin says 42"}, {"src/app/main.cpp"},
                 "changing the static lib relinks without recompiling the app");
    }

    if (t.begin("third_party", "examples/03_third_party"))
    {
        t.expect(t.nob({"run"}), 0, {"third party demo: hypotenuse(3, 4.5) = 5.408"},
                 {"warning"}, "system headers produce no warnings");
        t.touch("vendor/noisy_math/noisy_math.hpp");
        t.expect(t.nob({}), 0, {"src/app/main.cpp", "built 2/2"}, {},
                 "editing a system header still rebuilds");

        t.replace("nobcpp.cpp", "/*system=*/true", "/*system=*/false");
        t.expect(t.nob({}), 0, {"unused_counter", "2 warnings"}, {},
                 "without system the warnings appear");
        t.expect(t.nob({}), 0, {"2 warnings hidden in 1 unchanged file", "src/app/main.cpp"},
                 {"unused_counter"}, "unchanged files only show a warning count");
        t.expect(t.nob({"warnings"}), 0, {"unused_counter", "2 warnings in 1 file"}, {},
                 "'warnings' replays them");
        t.expect(t.nob({"warnings", "nomatch"}), 0, {"no warnings stored"}, {},
                 "'warnings' filters by path");
    }

    if (t.begin("profiles", "examples/04_profiles_and_tests"))
    {
        t.expect(t.nob({"run", "unit_tests"}), 0, {"all tests passed"}, {}, "unit tests pass");
        t.expect(t.nob({"run", "app", "--", "12"}), 0, {"[debug] 12! = 479001600"}, {},
                 "run passes arguments");
        t.expect(t.nob({"run"}), 2, {"several executables"}, {},
                 "run asks which executable");
        t.expect(t.nob({"release", "lto", "run", "app"}), 0, {"[release]", "release+lto: built"},
                 {}, "profiles combine");
        t.expect(t.nob({"-p", "strict,release", "stats"}), 0, {"no timing data"}, {},
                 "stats per profile");
        t.expect(t.nob({"release", "lto", "stats"}), 0,
                 {"src/mathlib/mathlib.cpp", "sum of job times", "last full build",
                  "critical path", "best case"},
                 {}, "stats lists jobs and build times");
        // only a handful of jobs: the best case must not claim to use 64 threads
        t.expect(t.nob({"-j64", "release", "lto", "stats"}), 0, {"best case"}, {"-j64"},
                 "the best case never uses more threads than there are jobs");
        t.replace("tests/test_mathlib.cpp", "factorial(5) == 120", "factorial(5) == 121");
        t.expect(t.nob({"run", "unit_tests"}), 1, {"CHECK failed"}, {},
                 "run returns the program's exit code");
    }

    if (t.begin("deps", "tests/fixtures/deps_project"))
    {
        // a local git repository with a tiny CMake library, a fake pkg-config package
        // and an empty source cache: the dependency features without network access
        fs::path tmp = fs::absolute("tests/tmp");
        fs::path repo = tmp / "repos" / "greetlib";
        fs::path cache = tmp / "cache";
        std::error_code ec;
        fs::remove_all(repo, ec);
        fs::remove_all(cache, ec);
        fs::create_directories(repo.parent_path());
        fs::copy("tests/fixtures/greetlib", repo, fs::copy_options::recursive);
        auto git = [&](std::vector<std::string> args) {
            std::vector<std::string> cmd = {"git", "-C", repo.string(), "-c", "user.name=nobcpp",
                                            "-c", "user.email=nobcpp@test", "-c",
                                            "commit.gpgsign=false"};
            nob::append(cmd, args);
            auto r = nob::run_capture(cmd);
            return r.output.substr(0, r.output.find('\n'));
        };
        nob::run_capture({"git", "init", "-q", "-b", "main", repo.string()});
        git({"add", "."});
        git({"commit", "-q", "-m", "v1"});
        std::string v1 = git({"rev-parse", "HEAD"});

        fs::create_directories(tmp / "pkgconfig");
        std::ofstream(tmp / "pkgconfig" / "nobcpp-fake.pc")
            << "Name: nobcpp-fake\nDescription: test\nVersion: 1.2.3\n"
               "Cflags: -I/nonexistent/fake/include -DNOBCPP_FAKE=7\nLibs: -lm\n";
        nob::set_env("NOBCPP_TEST_REPO", repo.string().c_str());
        nob::set_env("NOBCPP_CACHE", cache.string().c_str());
        nob::set_env("PKG_CONFIG_PATH", (tmp / "pkgconfig").string().c_str());

        t.expect(t.nob({"run"}), 0,
                 {"fetching greetlib", "greetlib (cmake, main", "Debug) ready in",
                  "hello from greetlib v1, extra, fake=7, imported=1"},
                 {}, "fetches, builds with cmake, imports flags, pkg-config, nob-built sources");
        auto lock = nob::read_file(t.dir() / "nobcpp.lock");
        t.check(lock && lock->find("greetlib\t" + repo.string() + "\tmain\t" + v1) != std::string::npos,
                "nobcpp.lock pins the branch to its commit", lock.value_or("no nobcpp.lock"));
        t.expect(t.nob({"run"}), 0, {"everything up to date", "greetlib v1"},
                 {"fetching", "ready in"}, "second build: no fetch, no cmake, no compile");

        t.write("../../repos/greetlib/src/version.hpp", "#define GREETLIB_VERSION \"v2\"\n");
        git({"commit", "-q", "-am", "v2"});
        t.expect(t.nob({"run"}), 0, {"greetlib v1"}, {"fetching"},
                 "a new upstream commit is ignored, the lockfile wins");
        t.expect(t.nob({"update", "run"}), 0,
                 {"nobcpp.lock: greetlib main", "(was " + v1.substr(0, 12) + ")", "fetching greetlib",
                  "greetlib v2"},
                 {}, "'update' moves the branch to its new commit");
        t.expect(t.nob({"--offline", "run"}), 0, {"greetlib v2"}, {"fetching"},
                 "--offline works with everything in the cache");
        t.expect(t.nob({"release", "run"}), 0, {"Release) ready in", "greetlib v2"}, {"fetching"},
                 "another build type builds the dependency again, from the cached sources");

        fs::remove_all(cache, ec);
        t.expect(t.nob({"--offline", "run"}), 1, {"not downloaded yet and --offline is set"}, {},
                 "--offline fails cleanly when sources are missing");

        t.replace("nobcpp.cpp", ".targets = {\"greetlib::greetlib\"}",
                  ".targets = {\"greetlib::greetlib\"}, .options = {\"GREETLIB_LOUD=ON\"}");
        t.expect(t.nob({"run"}), 0, {"fetching greetlib", "ready in", "HELLO FROM GREETLIB v2"}, {},
                 "changing a cmake option rebuilds the dependency");

        // updates by name, stale lockfile entries, cache cleanup
        {
            std::ofstream(t.dir() / "nobcpp.lock", std::ios::app) << "ghost\thttps://example.invalid/ghost\tmain\t"
                                                                    << std::string(40, 'a') << "\n";
        }
        git({"commit", "-q", "--allow-empty", "-m", "v3"});
        std::string v3 = git({"rev-parse", "HEAD"});
        t.expect(t.nob({"update", "greetlib"}), 0,
                 {"nobcpp.lock: greetlib main → " + v3.substr(0, 12), "greetlib at the latest commit"}, {},
                 "'update <name>' updates that dependency");
        auto lock2 = nob::read_file(t.dir() / "nobcpp.lock");
        t.check(lock2 && lock2->find("ghost") != std::string::npos,
                "'update <name>' keeps other entries", lock2.value_or(""));
        t.expect(t.nob({"update", "nope"}), 2, {"'nope' is not a fetched dependency (fetched: greetlib)"}, {},
                 "'update' rejects unknown names");
        t.expect(t.nob({"update"}), 0, {"nobcpp.lock: removed ghost", "all fetched dependencies"}, {},
                 "'update' removes entries of dependencies that are not fetched anymore");

        fs::path old_src = cache / "src" / "oldlib-0123456789ab";
        fs::create_directories(old_src);
        std::ofstream(old_src / "big.txt") << std::string(10000, 'x');
        fs::last_write_time(old_src, fs::file_time_type::clock::now() - std::chrono::hours(24 * 40));
        // the earlier greetlib commit was used minutes ago: kept by default, deleted by
        // 'cleancache 0', the current commit's sources are always kept
        fs::path current_src = cache / "src" / ("greetlib-" + v3.substr(0, 12));
        t.expect(t.nob({"cleancache"}), 0, {"removed oldlib-0123456789ab", "removed 1 cached source", "kept 2"}, {},
                 "'cleancache' deletes sources unused for 30 days");
        t.check(!fs::exists(old_src) && fs::exists(current_src), "the old source is gone, the current one stays");
        t.expect(t.nob({"cleancache", "0"}), 0, {"removed 1 cached source", "kept 1"}, {"removed greetlib-" + v3.substr(0, 12)},
                 "'cleancache 0' deletes everything this project doesn't use");
        t.check(fs::exists(current_src), "sources of the current project are never deleted");

        t.replace("nobcpp.cpp", "greetlib::greetlib\"}", "greetlib::nope\"}");
        t.expect(t.nob({}), 1, {"dependency greetlib", "cmake import failed", "full log:"}, {},
                 "a broken dependency is reported with its log");
    }

    return t.summary();
}
