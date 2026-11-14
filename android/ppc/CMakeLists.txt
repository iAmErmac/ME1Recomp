# (SoD) State of Decay's generated game code (ppc/, XenonRecomp output) for ARM64 Android, as
# a static library for the runtime. ME1RecompLib's flags in GCC style (clang-cl's
# /fp:strict -> -ffp-model=strict, /FI mmio_hook.h -> -include), as the ARM64 instruction tests
# (tests/insn) build it. Replaces DAO's android/ppc-probe (its harness isn't needed here).
cmake_minimum_required(VERSION 3.22)

if (NOT EXISTS ${ME1_PPC_DIR}/ppc_func_mapping.cpp)
    message(FATAL_ERROR "No generated code in ${ME1_PPC_DIR} (PHASE1_RESULT.md: How to regenerate).")
endif()

set(ME1_PPC_FLAGS -fno-strict-aliasing -ffp-model=strict -ffp-contract=off
    -include ${RECOMP}/ME1RecompLib/mmio_hook.h
    -Wno-unused-label -Wno-unused-variable -Wno-unused-but-set-variable)

file(GLOB ME1_PPC_SOURCES CONFIGURE_DEPENDS
    ${ME1_PPC_DIR}/ppc_recomp.*.cpp
    ${ME1_PPC_DIR}/ppc_func_mapping.cpp)
list(LENGTH ME1_PPC_SOURCES _n)
message(STATUS "Generated units: ${_n}")

add_library(daoppc STATIC ${ME1_PPC_SOURCES})
target_include_directories(daoppc PRIVATE ${ME1_PPC_DIR} ${XENON_TP}/simde)
target_compile_options(daoppc PRIVATE ${ME1_PPC_FLAGS})
target_precompile_headers(daoppc PRIVATE ${ME1_PPC_DIR}/ppc_recomp_shared.h)
