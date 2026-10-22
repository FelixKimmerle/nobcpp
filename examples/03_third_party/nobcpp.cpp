// Using a vendored header only library without drowning in its warnings.
//
// vendor/noisy_math is included as a *system* header (-isystem / /external:I), so the
// compiler stays quiet about it while your own code keeps -Wall -Wextra -Wpedantic.
// Change `true` to `false` below to see what that saves you. Editing the vendored
// header still triggers a rebuild of everything that includes it.
//
// Warnings of files that were not recompiled are only counted at the end of a build,
// `./nobcpp warnings` shows them again.
#include "../../nobcpp.hpp"

int main(int argc, char** argv)
{
    nob::rebuild_self(__FILE__, argc, argv, {"../../nobcpp.hpp"});

    nob::Build b(argc, argv);
    b.flags.std = 20;
    b.flags.warnings = nob::Warnings::Extra;

    auto noisy_math = b.header_lib("noisy_math", "vendor/noisy_math", /*system=*/true);
    // Public flags reach everything that links the library.
    noisy_math->public_flags.defines.push_back("NOISY_MATH_PRECISION=3");
    if (!nob::target_windows)
    {
        noisy_math->link_lib("m"); // -lm, would be m.lib with msvc
    }

    auto app = b.executable("app", "src/app");
    app->link(noisy_math);
    app->define("APP_NAME=\"third party demo\"");

    return b.run();
}
