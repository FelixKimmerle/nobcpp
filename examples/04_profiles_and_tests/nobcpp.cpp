// Several executables, custom profiles and compiler specific flags.
//
//     ./nobcpp run app                 debug build of the app
//     ./nobcpp run unit_tests          exit code = number of failed tests
//     ./nobcpp release lto run app     profiles combine: build/release-lto/
//     ./nobcpp asan run unit_tests     built in sanitizer profile
//     ./nobcpp --cxx clang++ stats     any compiler, compile time per file
#include "../../nobcpp.hpp"

int main(int argc, char** argv)
{
    nob::rebuild_self(__FILE__, argc, argv, {"../../nobcpp.hpp"});

    nob::Build b(argc, argv);
    b.flags.std = 20;
    b.flags.warnings = nob::Warnings::Extra;

    // Built in: debug (default), release, relwithdebinfo, asan, tsan.
    // A profile only sets what it cares about, so it can be combined with others.
    b.profile("lto", {.lto = true});
    b.profile("strict", {.werror = true});
    // Overriding a built in profile:
    b.profile("release", {.opt = nob::Opt::O2, .debug_info = false, .defines = {"NDEBUG"}});

    // The command line is parsed in the constructor, so the configuration can depend on
    // the selected profiles and compiler.
    b.flags.defines.push_back(b.profile_active("release") ? "BUILD_KIND=\"release\""
                                                          : "BUILD_KIND=\"debug\"");
    if (b.toolchain().family == nob::Family::Gcc)
    {
        b.flags.compile_flags.push_back("-fconcepts-diagnostics-depth=2");
    }

    auto mathlib = b.static_lib("mathlib", "src/mathlib");

    auto app = b.executable("app", "src/app");
    app->link(mathlib);

    // Tests live next to src/, not inside it.
    auto tests = b.executable("unit_tests", "tests");
    tests->link(mathlib);

    return b.run();
}
