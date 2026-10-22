// A shared library that bundles a static one:
//
//     app ──> libplugin.so ──> libcore.a
//
// nobcpp compiles `core` with -fPIC because it ends up inside a shared library, gives
// the .so a soname and the executable an rpath of $ORIGIN, so `app` runs from anywhere.
// On Windows (MSVC) you get plugin.dll + plugin.lib, symbols have to be exported with
// __declspec(dllexport), see src/plugin/plugin.hpp.
#include "../../nobcpp.hpp"

int main(int argc, char** argv)
{
    nob::rebuild_self(__FILE__, argc, argv, {"../../nobcpp.hpp"});

    nob::Build b(argc, argv);
    b.flags.std = 20;

    auto core = b.static_lib("core", "src/core");

    auto plugin = b.shared_lib("plugin", "src/plugin");
    plugin->define("PLUGIN_BUILD"); // private: only while compiling the plugin itself
    plugin->link(core);

    auto app = b.executable("app", "src/app");
    app->link(plugin);

    return b.run();
}
