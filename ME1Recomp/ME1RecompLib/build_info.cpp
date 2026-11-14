// Compiled once per library, with ME1_BUILD_INFO (the descriptor's name) and
// ME1_BUILD_MAPPINGS (the build's function table) defined by CMakeLists.txt.
#include "build_info.h"

extern const Me1BuildInfo ME1_BUILD_INFO = {
    uint32_t(PPC_IMAGE_BASE),
    uint32_t(PPC_IMAGE_SIZE),
    uint32_t(PPC_CODE_BASE),
    uint32_t(PPC_CODE_SIZE),
    ME1_BUILD_MAPPINGS,
};
