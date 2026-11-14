# The recompiled game code (generated into ME1_PPC_DIR by install.py or scripts/recomp.cmd),
# and Title Update 6's (ME1_PPC_TU6_DIR, scripts/recomp.cmd tu6) when it was generated.
project(ME1RecompLib CXX)

# One library per build. Each sees only its own generated headers; build_info.cpp
# describes the build to the runtime (build_info.h).
function(me1_recomp_library target dir info mappings)
    file(GLOB sources CONFIGURE_DEPENDS
        ${dir}/ppc_recomp.*.cpp
        ${dir}/ppc_func_mapping.cpp)

    add_library(${target} STATIC ${sources} ${CMAKE_CURRENT_SOURCE_DIR}/build_info.cpp)

    target_include_directories(${target} PRIVATE
        ${dir}
        ${XENON_ROOT}/thirdparty/simde)

    target_compile_definitions(${target} PRIVATE
        ME1_BUILD_INFO=${info}
        ME1_BUILD_MAPPINGS=${mappings})

    target_compile_options(${target} PRIVATE
        -fno-strict-aliasing
        /fp:strict
        -Wno-unused-label
        -Wno-unused-variable
        -Wno-unused-but-set-variable
        /FI${CMAKE_CURRENT_SOURCE_DIR}/mmio_hook.h)

    target_precompile_headers(${target} PRIVATE ${dir}/ppc_recomp_shared.h)
endfunction()

me1_recomp_library(ME1RecompLib ${ME1_PPC_DIR} Me1BuildDisc PPCFuncMappings)

# The runtime builds on the disc's generated headers (ppc_context.h; the
# per-build values come from build_info.h).
target_include_directories(ME1RecompLib PUBLIC
    ${ME1_PPC_DIR}
    ${XENON_ROOT}/thirdparty/simde
    ${CMAKE_CURRENT_SOURCE_DIR})

if (EXISTS ${ME1_PPC_TU6_DIR}/ppc_func_mapping.cpp)
    me1_recomp_library(ME1RecompLibTU6 ${ME1_PPC_TU6_DIR} Me1BuildTu6 PPCFuncMappings_tu6)
    target_link_libraries(ME1RecompLib INTERFACE ME1RecompLibTU6)
    target_compile_definitions(ME1RecompLib INTERFACE ME1_HAS_TU6=1)
    message(STATUS "Title Update 6 build: ${ME1_PPC_TU6_DIR}")
else()
    message(STATUS "Title Update 6 build: not generated (scripts/recomp.cmd tu6)")
endif()
