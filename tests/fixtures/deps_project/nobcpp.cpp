// Test project for fetch(), cmake() and pkg_config(). The tests point
// $NOBCPP_TEST_REPO at a local git repository made from tests/fixtures/greetlib and
// $PKG_CONFIG_PATH at a fake package, so nothing here needs the network.
#include "../../nobcpp.hpp"
#include <cstdlib>

int main(int argc, char** argv)
{
    nob::rebuild_self(__FILE__, argc, argv, {"../../nobcpp.hpp"});

    nob::Build b(argc, argv);
    b.flags.std = 20;
    b.flags.warnings = nob::Warnings::Extra;

    const char* repo = std::getenv("NOBCPP_TEST_REPO");
    auto src = b.fetch("greetlib", {.git = repo ? repo : "", .ref = "main"});
    auto greetlib = b.cmake(src, {.targets = {"greetlib::greetlib"}});
    auto extra = b.static_lib("extra", src->dir() / "extra");
    auto fake = b.pkg_config("nobcpp-fake");

    auto app = b.executable("app", "src/app");
    app->link(greetlib).link(extra).link(fake);

    return b.run();
}
