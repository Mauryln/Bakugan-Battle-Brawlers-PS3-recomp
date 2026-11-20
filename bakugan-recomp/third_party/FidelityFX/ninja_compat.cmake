function(fidelityfx_replace path before after)
    set(path "${fidelityfx_sdk}/${path}")
    file(READ "${path}" contents)
    string(REPLACE "${before}" "${after}" updated "${contents}")
    if(NOT updated STREQUAL contents)
        file(WRITE "${path}" "${updated}")
    endif()
endfunction()

# Derive the SDK and PIX architectures from the loaded compiler under Ninja.
fidelityfx_replace(CMakeLists.txt
    "elseif(CMAKE_GENERATOR_PLATFORM STREQUAL \"x64\" OR CMAKE_EXE_LINKER_FLAGS STREQUAL \"/machine:x64\")"
    "elseif(CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL \"x64\")")
fidelityfx_replace(libs/pix/CMakeLists.txt [[${CMAKE_GENERATOR_PLATFORM}]] [[${FFX_PLATFORM_NAME}]])

# AMD 1.1.4 seeds the output list with the argument's name, which Ninja treats
# as a missing file. Use distinct names for the output variable and the list.
fidelityfx_replace(include/FidelityFX/gpu/CMakeCompileShaders.txt
    "PERMUTATION_OUTPUTS)" "OUTPUT_VARIABLE)")
fidelityfx_replace(include/FidelityFX/gpu/CMakeCompileShaders.txt
    [[set(${PERMUTATION_OUTPUTS} ${PERMUTATION_OUTPUTS} PARENT_SCOPE)]]
    [[set(${OUTPUT_VARIABLE} ${PERMUTATION_OUTPUTS} PARENT_SCOPE)]])

# ffxCountBitsSet uses std::popcount in C++20 builds.
fidelityfx_replace(include/FidelityFX/host/ffx_util.h
    "#pragma once\n\n#include <FidelityFX/host/ffx_types.h>"
    "#pragma once\n\n#if __cplusplus >= 202002L\n#include <bit>\n#endif\n#include <FidelityFX/host/ffx_types.h>")
