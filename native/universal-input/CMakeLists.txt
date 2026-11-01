cmake_minimum_required(VERSION 3.20)
project(gh3_universal_input LANGUAGES CXX)

# GH3.exe is a 32-bit process; a 64-bit DLL would silently fail to load.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "GH3 Universal Input must be built 32-bit: cmake -A Win32 ...")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT so players don't need a specific Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(xinput1_3 SHARED
    src/proxy.cpp
    src/config.cpp
    src/log.cpp
    src/real_xinput.cpp
    src/exports.def
)
target_compile_definitions(xinput1_3 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(xinput1_3 PRIVATE /W4 /permissive-)
set_target_properties(xinput1_3 PROPERTIES PREFIX "" OUTPUT_NAME "xinput1_3")
add_custom_command(TARGET xinput1_3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/gh3_universal_input.ini $<TARGET_FILE_DIR:xinput1_3>)

add_executable(uitest tools/uitest.cpp)
target_compile_definitions(uitest PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
