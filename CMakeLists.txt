cmake_minimum_required(VERSION 3.20)
project(pop2_gamepad LANGUAGES CXX)

# -----------------------------------------------------------------------------
# IMPORTANT: this MUST build 32-bit (x86). pop2.exe is PE32 / Intel 80386.
# Configure with, e.g.:
#     cmake -B build -A Win32 -DSDL2_DIR=<path-to-SDL2-cmake>
#     cmake --build build --config Release
# -----------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "pop2_gamepad must be built as 32-bit (x86). "
                        "Re-run cmake with -A Win32.")
endif()

# --- SDL2 -------------------------------------------------------------------
# Point SDL2_DIR at the SDL2 development libraries (the x86 ones).
find_package(SDL2 CONFIG REQUIRED)

# --- the proxy DLL ----------------------------------------------------------
add_library(dinput8 SHARED
    src/dllmain.cpp
    src/hook_di.cpp
    src/input_sdl.cpp
    src/config.cpp
    src/log.cpp
    dinput8.def
)

target_include_directories(dinput8 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# We resolve DirectInput8Create dynamically via GetProcAddress (see dllmain),
# so we must NOT link the SDK's dinput8.lib (name clash with our own output +
# we load the real DLL at runtime). We only need dxguid for the IID_/GUID_
# symbols referenced by <dinput.h> (IID_IDirectInput8, etc.).
target_link_libraries(dinput8 PRIVATE
    SDL2::SDL2
    dxguid
)

# Force the output name regardless of platform decorations.
set_target_properties(dinput8 PROPERTIES
    OUTPUT_NAME "dinput8"
    PREFIX ""
)

# Copy SDL2.dll next to the built proxy for convenience.
if(TARGET SDL2::SDL2)
    add_custom_command(TARGET dinput8 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:dinput8>)
endif()
