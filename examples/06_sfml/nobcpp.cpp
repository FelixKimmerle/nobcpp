// SFML, fetched and built as a static library. Every project pins its own SFML version
// in nobcpp.lock, so an SFML 2 project and an SFML 3 project don't get in each other's way.
//
//     ./nobcpp run                 a window with a bouncing circle
//     ./nobcpp run -- --headless   just checks that everything links
//     ./nobcpp release run         SFML gets built a second time, as Release
#include "../../nobcpp.hpp"

int main(int argc, char** argv)
{
    nob::rebuild_self(__FILE__, argc, argv, {"../../nobcpp.hpp"});

    nob::Build b(argc, argv);
    b.flags.std = 20;
    b.flags.warnings = nob::Warnings::Extra;

    auto sfml = b.cmake(b.fetch("SFML", {.git = "https://github.com/SFML/SFML", .ref = "3.1.0"}),
                        {.targets = {"SFML::Graphics"},
                         .options = {"SFML_BUILD_AUDIO=OFF", "SFML_BUILD_NETWORK=OFF"},
                         .components = {"Graphics"},
                         // SFML's CMake package looks for the shared build unless told
                         .probe_variables = {"SFML_STATIC_LIBRARIES=ON"}});
    // For SFML 2.x it would be:
    //     .targets = {"sfml-graphics"}, .package = "SFML", .components = {"graphics"},
    //     .probe_variables = {"SFML_STATIC_LIBRARIES=ON"}

    auto app = b.executable("app", "src/app");
    app->link(sfml);

    return b.run();
}
