# C ABI import library shared by clang-cl and MSVC. No global SDK install.
set(_sdl3_local "${CMAKE_CURRENT_SOURCE_DIR}/../diagnostics/dependencies/SDL3-3.4.20/SDL3-3.4.20")
if(EXISTS "${_sdl3_local}/include/SDL3/SDL.h")
    set(_sdl3_default "${_sdl3_local}")
else()
    set(_sdl3_default "${CMAKE_CURRENT_BINARY_DIR}/_deps/SDL3-3.4.20")
endif()
set(BAKUGAN_SDL3_ROOT "${_sdl3_default}" CACHE PATH "SDL3 official VC development package")
if(NOT EXISTS "${BAKUGAN_SDL3_ROOT}/include/SDL3/SDL.h")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/_deps")
    set(_sdl3_zip "${CMAKE_CURRENT_BINARY_DIR}/_deps/SDL3-devel-3.4.20-VC.zip")
    file(DOWNLOAD
        "https://github.com/libsdl-org/SDL/releases/download/release-3.4.20/SDL3-devel-3.4.20-VC.zip"
        "${_sdl3_zip}"
        EXPECTED_HASH SHA256=1ec3009655ee37b1f73e52b3b120acc5afd8abaf9532f3303b1bf1dfe91b5969
        TLS_VERIFY ON)
    file(ARCHIVE_EXTRACT INPUT "${_sdl3_zip}" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/_deps")
endif()
foreach(_sdl3_file include/SDL3/SDL.h lib/x64/SDL3.lib lib/x64/SDL3.dll)
    if(NOT EXISTS "${BAKUGAN_SDL3_ROOT}/${_sdl3_file}")
        message(FATAL_ERROR "SDL3 package missing ${_sdl3_file}; set BAKUGAN_SDL3_ROOT to the official x64 VC package root")
    endif()
endforeach()
set(BAKUGAN_SDL3_LIBRARY "${BAKUGAN_SDL3_ROOT}/lib/x64/SDL3.lib")
function(bakugan_copy_sdl3 target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BAKUGAN_SDL3_ROOT}/lib/x64/SDL3.dll" "$<TARGET_FILE_DIR:${target}>/SDL3.dll"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${BAKUGAN_SDL3_ROOT}/LICENSE.txt" "$<TARGET_FILE_DIR:${target}>/licenses/SDL3.txt")
endfunction()
