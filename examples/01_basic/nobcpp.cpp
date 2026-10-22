// The minimal setup: an executable that links a static library.
//
//     c++ -std=c++20 -o nobcpp nobcpp.cpp
//     ./nobcpp run
#include "../../nobcpp.hpp"

int main(int argc, char** argv)
{
    nob::rebuild_self(__FILE__, argc, argv, {"../../nobcpp.hpp"});

    nob::Build b(argc, argv);
    b.flags.std = 20;
    b.flags.warnings = nob::Warnings::Extra;

    // All .cpp files below the directory are compiled, the directory becomes a public
    // include directory: everything linking `greeter` can #include "greeter.hpp".
    auto greeter = b.static_lib("greeter", "src/greeter");

    auto app = b.executable("app", "src/app");
    app->link(greeter);

    return b.run();
}
