#include "core.hpp"

// a global with a relocation, this would fail to link into a .so without -fPIC
static int answer = 40;

int core_answer()
{
    return answer;
}
