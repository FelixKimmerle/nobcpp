# nobcpp

> **⚠️ WARNING: Pre-Alpha Software**  
> nobcpp is in a very early stage of development.  
> Expect bugs, missing features, and breaking changes.  
> **Do not use in production or for critical projects (yet)!**

**nobcpp** is a C++-only build tool loosely inspired by [nob](https://github.com/tsoding/nob.h), designed to simplify and accelerate the build process for large, modular C++ projects.

- **Self-contained:** Can be bootstrapped with only a C++20 compiler—no external dependencies required.
- **Self-rebuilding:** Automatically rebuilds itself if the build process definition changes.
- **Portable:** Linux, Windows (MSVC `cl`, `clang-cl`, MSYS2/MinGW) and gcc/clang compatible compilers. The compiler family is detected by probing it, not by its name. Windows support is implemented but still untested.

## Quick start

Copy `nobcpp.hpp` into your project and write a `nobcpp.cpp` next to it:

```cpp
#include "nobcpp.hpp"

int main(int argc, char** argv)
{
    nob::rebuild_self(__FILE__, argc, argv, {"nobcpp.hpp"});

    nob::Build b(argc, argv);
    b.flags.std = 20;

    auto lib = b.static_lib("greeter", "src/greeter");
    auto app = b.executable("app", "src/app");
    app->link(lib);

    return b.run();
}
```

```sh
c++ -std=c++20 -o nobcpp nobcpp.cpp      # Linux / MSYS2, once
cl /nologo /std:c++20 /EHsc nobcpp.cpp   # MSVC developer prompt, once
./nobcpp                                 # debug build
./nobcpp release run -- arg              # release build, then run
./nobcpp -p release,asan --cxx clang++   # profiles combine, any compiler
./nobcpp stats                           # compile time per translation unit
./nobcpp help
```

## Examples

Each example is a complete project, build it from inside its directory with
`c++ -std=c++20 -o nobcpp nobcpp.cpp && ./nobcpp run`.

| example | shows |
| --- | --- |
| [01_basic](examples/01_basic/nobcpp.cpp) | executable + static library, nested source directories |
| [02_shared_lib](examples/02_shared_lib/nobcpp.cpp) | shared library bundling a static one (`-fPIC`, soname, rpath), private defines, dll exports |
| [03_third_party](examples/03_third_party/nobcpp.cpp) | header only library as system headers, public defines, system libraries |
| [04_profiles_and_tests](examples/04_profiles_and_tests/nobcpp.cpp) | custom and overridden profiles, profile/compiler dependent flags, several executables, unit tests outside `src/` |
| [05_sdl3_imgui](examples/05_sdl3_imgui/nobcpp.cpp) | fetched SDL3 (built with CMake) and Dear ImGui docking (built by nobcpp) |
| [06_sfml](examples/06_sfml/nobcpp.cpp) | fetched SFML 3 as a static library, with the settings for SFML 2 |

## Dependencies

```cpp
// SDL3 is a CMake project: built once per build type, its targets are imported
auto sdl = b.cmake(b.fetch("SDL3", {.git = "https://github.com/libsdl-org/SDL", .ref = "release-3.4.18"}),
                   {.targets = {"SDL3::SDL3-static"}, .options = {"SDL_SHARED=OFF", "SDL_STATIC=ON"}});

// ImGui is a few .cpp files: compiled by nobcpp with your compiler and profile
auto imgui_src = b.fetch("imgui", {.git = "https://github.com/ocornut/imgui", .ref = "docking"});
auto imgui = b.static_lib("imgui");
imgui->add_sources(imgui_src->dir(), false).add_source(*imgui_src / "backends/imgui_impl_sdl3.cpp");
imgui->link(sdl);

// or what is installed on the system
auto zlib = b.pkg_config("zlib");
```

- **`fetch`** downloads a git repository (or a `.tar.gz`/`.zip` with `.url`) into a cache shared by all your projects (`~/.cache/nobcpp`, `%LOCALAPPDATA%\nobcpp`, or `$NOBCPP_CACHE`), once per commit.
- **`nobcpp.lock`** pins tags and branches to the commit they had when first used. Commit it. `./nobcpp update` moves them forward, `--offline` never touches the network.
- **`cmake`** configures, builds and installs the project into `build/_deps/<name>-<build type>-<hash>/` with your compiler (static, PIC, Debug/Release from the active profile). A tiny probe project then asks CMake, through its file API, what the targets need: include directories (used as system headers), defines and every transitive library. Nothing is rebuilt until the version, options, compiler or build type change.
- **Versions per project:** every project has its own lockfile and build directory, so one project on SFML 2 and another on SFML 3 never collide.
- Needs `git` (and `curl`/`tar` for archives), `cmake` 3.16+ for `cmake()`, and uses `ninja` when it is installed.


## Tests

```sh
c++ -std=c++20 -o tests/run tests/run.cpp   # from the repository root, once
./tests/run [filter]
```

Builds every example from scratch in `tests/tmp/` and checks incremental builds, profile switches, deleted headers, error reporting, warning replay, self-rebuilds and more. The dependency tests use a local git repository and need `git`, `cmake` and `pkg-config`, but no network. Examples 05 and 06 download SDL3, ImGui and SFML and are not part of the tests.

## Features

- **Parallel builds** scheduled by critical path, using the durations of previous builds.
- **Incremental builds** that also notice changed flags, deleted headers and profile switches.
- **Automatic header dependency tracking** (`-MMD` / `/showIncludes`).
- **Composable profiles:** `debug`, `release`, `relwithdebinfo`, `asan`, `tsan` built in, each combination gets its own build directory (`build/release-asan/`).
- **Readable output:** a progress bar with an ETA, and at the end one block of diagnostics per file, sorted by file, with an error/warning summary.
- **Warnings don't get lost:** files that weren't recompiled only show a count of their stored warnings, `./nobcpp warnings [filter]` replays them.
- **Quiet third party code:** `b.header_lib("json", "third_party/json", /*system=*/true)` or `target->system_include_dir(dir)` include headers as system headers (`-isystem` / `/external:I`), so their warnings don't drown yours. Changes to them still trigger rebuilds.
- **Dependencies:** fetch git repositories or archives, build CMake projects and import their targets, use pkg-config, all pinned in `nobcpp.lock`.
- **Build stats:** `./nobcpp stats` shows how long every translation unit took.
- **compile_commands.json** is written on every build.

## Goals

- Make building large, modular C++ projects straightforward and fast.
- Minimize external dependencies, nobcpp is implemented in pure C++.
- Provide a clean and scalable alternative to traditional build systems.

---

> There are n build tools out there, and they all suck.  
> When I'm finished with this project, there will be n+1 build tools and they will all suck.

 *Feedback, suggestions, and contributions are welcome!*
