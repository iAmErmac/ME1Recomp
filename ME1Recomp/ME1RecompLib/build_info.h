#pragma once
// One recompiled build of the game, as the runtime sees it: where its image and
// code are, and its functions. build_info.cpp is compiled into each library
// (the disc's executable, and Title Update 6's when it was generated), so the
// values come from that build's own ppc_config.h.
#include <cstdint>

struct PPCFuncMapping;

struct Me1BuildInfo
{
    uint32_t imageBase;
    uint32_t imageSize;
    uint32_t codeBase;
    uint32_t codeSize;
    const PPCFuncMapping* mappings;   // Ends with guest 0.
};

extern const Me1BuildInfo Me1BuildDisc;
#if ME1_HAS_TU6
extern const Me1BuildInfo Me1BuildTu6;
#endif
