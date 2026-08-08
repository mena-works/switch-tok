cmake_minimum_required(VERSION 3.15)

# Build with:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake
#   cmake --build build
# See README.md for the package prerequisites.

project(switch-tok VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# NOTE: build with -G Ninja on Windows/MSYS. devkitA64 is a native Windows
# binary and writes "C:/..." into its depfiles, which the MSYS make reads as
# relative paths -- producing targets like "lib/borealis/C:/Users/..." and
# dying on the drive colon with "multiple target patterns". Ninja consumes
# those depfiles itself and is unaffected.

if(NOT NINTENDO_SWITCH)
    message(FATAL_ERROR "This project only targets the Nintendo Switch. Pass -DCMAKE_TOOLCHAIN_FILE=$ENV{DEVKITPRO}/cmake/Switch.cmake")
endif()

# ---------------------------------------------------------------------------
# borealis
# ---------------------------------------------------------------------------
set(PLATFORM_SWITCH ON CACHE BOOL "" FORCE)
set(USE_LIBROMFS OFF CACHE BOOL "" FORCE)

# Pin the render backend. MpvPlayer hands mpv an FBO through glfwGetProcAddress
# and wraps the result with nvglCreateImageFromHandleGL3, so both the deko3d and
# the SDL2 paths would break it silently at link time.
set(BOREALIS_USE_DEKO3D OFF CACHE BOOL "" FORCE)
set(USE_SDL2 OFF CACHE BOOL "" FORCE)

# borealis feeds this straight into set_target_properties(... UNITY_BUILD ${...}),
# so leaving it undefined produces an odd argument list and a confusing
# "incorrect number of arguments" error rather than a missing-option one.
# Off keeps compile errors pointing at the file that actually caused them.
set(BRLS_UNITY_BUILD OFF CACHE BOOL "" FORCE)

# Switch.cmake puts portlibs on CMAKE_FIND_ROOT_PATH so find_package() works,
# but never adds it to the default include path. borealis links glfw3 without
# declaring an include directory for it, so glfw3.h has to be findable before
# borealis is configured.
include_directories(SYSTEM ${DEVKITPRO}/portlibs/switch/include)

add_subdirectory(lib/borealis/library)

# borealis loads its fonts, styles and translations from romfs at runtime, so
# they have to be staged before the .nro is packed.
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/lib/borealis/resources/
     DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/romfs)

# borealis ships en-US but consoles commonly report en-GB, and the miss is
# logged at ERROR even though it falls back cleanly. A red line that means
# nothing is worse than no line: it costs attention during every real
# investigation.
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/lib/borealis/resources/i18n/en-US/
     DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/romfs/i18n/en-GB)

# ---------------------------------------------------------------------------
# libmpv + curl (custom devkitPro packages, see README.md)
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)

# ---------------------------------------------------------------------------
# application
# ---------------------------------------------------------------------------
file(GLOB SOURCES source/*.cpp)

# libnx calls userAppInit/userAppExit from crt0, and borealis ships the Switch
# implementation as a source file the application must compile in -- it is not
# part of the borealis library target. It initialises pl:u (system shared font),
# set/setsys (locale and theme), romfs, sockets, psm, nifm and lbl.
#
# Leaving it out does not fail to link. It fails at runtime: borealis asks
# plGetSharedFontByType for a font it never initialised, hands the result to
# nanovg, and stb_truetype faults parsing the garbage buffer.
list(APPEND SOURCES lib/borealis/library/lib/platforms/switch/switch_wrapper.c)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE source)

target_link_libraries(${PROJECT_NAME} PRIVATE
    borealis
    PkgConfig::MPV
    PkgConfig::CURL
)

# Tegra X1. Homebrew is scheduled on cores 0-2, which is what the decoder
# thread count in mpv_player.cpp assumes.
target_compile_options(${PROJECT_NAME} PRIVATE -mtune=cortex-a57)

nx_generate_nacp(${PROJECT_NAME}.nacp
    NAME    "switch-tok"
    AUTHOR  "menaworks"
    VERSION ${PROJECT_VERSION}
)

# nx_create_nro rejects a missing icon, and a 256x256 JPEG is not worth
# committing just to get a first build going.
set(NRO_ICON_ARGS "")
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.jpg)
    set(NRO_ICON_ARGS ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.jpg)
endif()

nx_create_nro(${PROJECT_NAME}
    NACP  ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.nacp
    ${NRO_ICON_ARGS}
    ROMFS ${CMAKE_CURRENT_SOURCE_DIR}/romfs
)
