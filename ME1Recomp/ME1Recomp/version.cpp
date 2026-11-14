#include "version.h"

// Only this file sees ME1_VERSION (and it skips the precompiled header), so a new
// version rebuilds nothing else.
#ifndef ME1_VERSION
#define ME1_VERSION "dev"
#endif

namespace me1
{
    const char* Version()
    {
        return ME1_VERSION;
    }
}
