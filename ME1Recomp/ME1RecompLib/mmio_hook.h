#pragma once
// Force-included into the recompiled code (see CMakeLists.txt). XenonRecomp emits
// PPC_MM_STORE_U32 for stores preceded by eieio; the ones that hit the GPU register
// window (0x7FC80000) are forwarded to the runtime's fake command processor.
#include <cstdint>

void Me1GpuRegisterWrite(uint32_t address, uint32_t value);
// (SoD) The APU's XMA registers (0x7FEA0000), which State of Decay's XMA library writes.
void Me1ApuRegisterWrite(uint32_t address, uint32_t value);

#define PPC_MM_STORE_U32(x, y)                                    \
    do                                                            \
    {                                                             \
        const uint32_t me1MmAddress_ = (x);                       \
        const uint32_t me1MmValue_ = (y);                         \
        PPC_STORE_U32(me1MmAddress_, me1MmValue_);                \
        if ((me1MmAddress_ & 0xFFFF0000u) == 0x7FC80000u)         \
            Me1GpuRegisterWrite(me1MmAddress_, me1MmValue_);      \
        else if ((me1MmAddress_ & 0xFFFF0000u) == 0x7FEA0000u)    \
            Me1ApuRegisterWrite(me1MmAddress_, me1MmValue_);      \
    } while (0)
