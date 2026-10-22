// SDL3 + Dear ImGui (docking branch), both fetched, nothing installed on the system.
//
//     ./nobcpp run                 window with a dockspace and the ImGui demo
//     ./nobcpp run -- --headless   just checks that everything links
//
// SDL3 is a CMake project: nobcpp builds it once per build type into build/_deps and
// asks CMake what SDL3::SDL3-static needs (include dirs, defines, system libraries).
// ImGui is a handful of .cpp files: nobcpp compiles them itself, with the same compiler
// and profile as your code. Exact versions end up in nobcpp.lock.
#include "../../nobcpp.hpp"

int main(int argc, char** argv)
{
    nob::rebuild_self(__FILE__, argc, argv, {"../../nobcpp.hpp"});

    nob::Build b(argc, argv);
    b.flags.std = 20;
    b.flags.warnings = nob::Warnings::Extra;

    auto sdl = b.cmake(b.fetch("SDL3", {.git = "https://github.com/libsdl-org/SDL",
                                        .ref = "release-3.4.18"}),
                       {.targets = {"SDL3::SDL3-static"},
                        .options = {"SDL_SHARED=OFF", "SDL_STATIC=ON", "SDL_TEST_LIBRARY=OFF"}});

    auto imgui_src = b.fetch("imgui", {.git = "https://github.com/ocornut/imgui", .ref = "docking"});
    auto imgui = b.static_lib("imgui");
    imgui->add_sources(imgui_src->dir(), /*recursive=*/false)
        .add_source(*imgui_src / "backends/imgui_impl_sdl3.cpp")
        .add_source(*imgui_src / "backends/imgui_impl_sdlrenderer3.cpp")
        .system_include_dir(imgui_src->dir())
        .system_include_dir(*imgui_src / "backends");
    imgui->flags.warnings = nob::Warnings::Off; // not our code
    imgui->link(sdl);

    auto app = b.executable("app", "src/app");
    app->link(imgui);

    return b.run();
}
