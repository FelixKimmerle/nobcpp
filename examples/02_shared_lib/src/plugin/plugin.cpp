#include "plugin.hpp"
#include "core.hpp"

int plugin_answer()
{
    return core_answer() + 2;
}
