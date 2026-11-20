include(FetchContent)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# AMD FidelityFX SDK 1.1.4. Keep the SDK and generated shader headers in the build tree.
FetchContent_Declare(fidelityfx
    URL https://codeload.github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/zip/c6efa6bf7f2027b3ec94f28578bb5965eabb9e55
    URL_HASH SHA256=b5bba71656e18f4c813fda4eb364dcb5e9b8491efd6abb66fdc8a2b530bfcedd
    TLS_VERIFY ON
    DOWNLOAD_DIR "${FETCHCONTENT_BASE_DIR}"
    DOWNLOAD_NAME FidelityFX-SDK-1.1.4.zip
    # Populate first so the compatibility fixes apply before configuring the SDK.
    SOURCE_SUBDIR bakugan-unused
)
FetchContent_MakeAvailable(fidelityfx)
set(fidelityfx_sdk "${fidelityfx_SOURCE_DIR}/sdk")
include(ninja_compat.cmake)
include(fsr3_compat.cmake)

# The standalone SDK toolchain forces -A x64 into the cache. The parent has
# already selected the compiler architecture, and Ninja rejects -A on reconfigure.
set(_TOOLCHAIN_ ON)
set(FFX_API_BACKEND DX12_X64)
set(FFX_API_DX12 ON)
set(FFX_FSR3 ON)
set(FFX_FSR3UPSCALER ON)
set(FFX_FI ON)
set(FFX_OF ON)
set(FFX_BUILD_AS_DLL OFF)
set(FFX_AUTO_COMPILE_SHADERS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
add_subdirectory("${fidelityfx_sdk}" "${fidelityfx_BINARY_DIR}" EXCLUDE_FROM_ALL)

set(_ffx_targets
    ffx_fsr3_x64
    ffx_fsr3upscaler_x64
    ffx_opticalflow_x64
    ffx_frameinterpolation_x64
    ffx_backend_dx12_x64
)
foreach(_target IN LISTS _ffx_targets)
    foreach(_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_property(TARGET ${_target} PROPERTY ARCHIVE_OUTPUT_DIRECTORY_${_config}
            "${fidelityfx_BINARY_DIR}/lib")
    endforeach()
endforeach()
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # AMD's backend uses MSVC's warning set with /WX.
    target_compile_options(ffx_backend_dx12_x64 PRIVATE /WX-)
endif()

# A private static dependency keeps SDK include paths off the generated PPU units.
add_library(bakugan_fidelityfx STATIC fidelityfx_boot_report.cpp ../../frame_generation.cpp)
target_include_directories(bakugan_fidelityfx PRIVATE
    "${PS3RECOMP_DIR}/include" "${PS3RECOMP_DIR}/libs/video")
target_link_libraries(bakugan_fidelityfx PRIVATE ffx_fsr3_x64 ffx_backend_dx12_x64)

set(_ffx_license "${fidelityfx_sdk}/LICENSE.txt")
configure_file("${_ffx_license}" "${CMAKE_BINARY_DIR}/licenses/FidelityFX-SDK.txt" COPYONLY)
install(FILES "${_ffx_license}" DESTINATION bin/licenses RENAME FidelityFX-SDK.txt)
