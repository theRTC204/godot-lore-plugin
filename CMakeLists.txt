cmake_minimum_required(VERSION 3.19)

# This extension only registers editor-only classes (EditorVCSInterface and
# friends), so it is built exclusively for the "editor" godot-cpp target,
# never template_debug/template_release export templates.
if(NOT DEFINED GODOTCPP_TARGET)
    set(GODOTCPP_TARGET "editor" CACHE STRING "" FORCE)
endif()

# Target the oldest Godot API godot-cpp still supports (see its README) to
# maximize which editor versions this plugin can load in. Every API surface
# used here (EditorVCSInterface's status/diff/stage/commit/branch/push/pull
# virtuals, FileAccess) is present as of 4.3; keep this in sync with
# compatibility_minimum in the .gdextension file.
if(NOT DEFINED GODOTCPP_API_VERSION)
    set(GODOTCPP_API_VERSION "4.3" CACHE STRING "" FORCE)
endif()

# This plugin only touches a handful of Godot classes, so compiling
# bindings for all ~1900 engine classes wastes most of the build — see
# godot-cpp-build-profile.json. Re-derive its list with
# `grep -rn "godot_cpp/classes/" src/` if src/ starts using another class.
if(NOT DEFINED GODOTCPP_BUILD_PROFILE)
    set(GODOTCPP_BUILD_PROFILE "${CMAKE_SOURCE_DIR}/godot-cpp-build-profile.json" CACHE PATH "" FORCE)
endif()

if(APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "")
endif()

# Which third_party/lore Cargo profile to build. Defaults to the real
# shippable one, so a plain `cmake -B build` still produces what README.md
# documents. CI overrides this to the vendored crate's own `[profile.release]`
# (no LTO) for routine validation builds, since `release-lto`'s whole-program
# LTO pass can't be cached/made incremental by cargo regardless of dependency
# changes, and is unnecessary when just confirming the code compiles.
if(NOT DEFINED LORE_CARGO_PROFILE)
    set(LORE_CARGO_PROFILE "release-lto" CACHE STRING "" FORCE)
endif()

project(godot-lore-plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(third_party/godot-cpp)

# Lore C API, built from the third_party/lore submodule via cargo. Linked
# dynamically: the staticlib archive cbindgen/cargo also produce is 100s of MB
# to several GB unlinked (no dead-code elimination happens until final link),
# whereas the cdylib is already linked+stripped down to ~30MB. Lore's shared
# library ships alongside the extension's own shared library at runtime.
find_program(CARGO_EXECUTABLE cargo REQUIRED)

# cargo's cdylib output naming is OS-specific; there's no cross-platform way
# to ask it for the name, so mirror its convention here. Only Windows uses a
# separate PE import library (.lib) for linking — ELF/Mach-O link directly
# against the shared object itself. GODOT_LORE_PLUGIN_PLATFORM_DIR matches
# the per-platform subdirectory layout the .gdextension file's relative
# library paths expect (see addons/godot-lore-plugin/godot-lore-plugin.gdextension).
if(WIN32)
    set(LORE_SHARED_LIB_NAME "lore.dll")
    set(GODOT_LORE_PLUGIN_PLATFORM_DIR "windows")
elseif(APPLE)
    set(LORE_SHARED_LIB_NAME "liblore.dylib")
    set(GODOT_LORE_PLUGIN_PLATFORM_DIR "macos")
else()
    set(LORE_SHARED_LIB_NAME "liblore.so")
    set(GODOT_LORE_PLUGIN_PLATFORM_DIR "linux")
endif()

set(LORE_SRC_DIR "${CMAKE_SOURCE_DIR}/third_party/lore")
set(LORE_STAGE_DIR "${CMAKE_BINARY_DIR}/lore")

# CMAKE_OSX_ARCHITECTURES containing both archs makes clang emit one true
# fat binary for the C++ side in a single invocation, but cargo has no
# equivalent — it only ever builds for the host's native arch. Build the
# crate once per triple and lipo them together, or the extension's linker
# can't resolve Lore's symbols for whichever slice cargo didn't build.
if(APPLE AND "x86_64" IN_LIST CMAKE_OSX_ARCHITECTURES AND "arm64" IN_LIST CMAKE_OSX_ARCHITECTURES)
    set(LORE_DUAL_ARCH ON)
else()
    set(LORE_DUAL_ARCH OFF)
endif()
if(WIN32)
    set(LORE_IS_WIN32 ON)
else()
    set(LORE_IS_WIN32 OFF)
endif()

# Delegated to a script rather than an inline COMMAND list: it skips the
# cargo invocation entirely when third_party/lore's current commit already
# matches what's staged in LORE_STAGE_DIR, letting CI restore a cache keyed
# on that commit and avoid rebuilding lore whenever the submodule pin
# hasn't moved (see the script for details).
add_custom_target(
    lore_cargo_build
    COMMAND ${CMAKE_COMMAND}
        -DLORE_SRC_DIR=${LORE_SRC_DIR}
        -DLORE_STAGE_DIR=${LORE_STAGE_DIR}
        -DLORE_CARGO_PROFILE=${LORE_CARGO_PROFILE}
        -DLORE_SHARED_LIB_NAME=${LORE_SHARED_LIB_NAME}
        -DCARGO_EXECUTABLE=${CARGO_EXECUTABLE}
        -DLORE_IS_WIN32=${LORE_IS_WIN32}
        -DLORE_DUAL_ARCH=${LORE_DUAL_ARCH}
        -P "${CMAKE_SOURCE_DIR}/cmake/build_lore_crate.cmake"
    VERBATIM
)

# INTERFACE_INCLUDE_DIRECTORIES on an imported target must exist at configure
# time even though lore_cargo_build doesn't populate it until the build step.
file(MAKE_DIRECTORY "${LORE_STAGE_DIR}/include")

add_library(lore SHARED IMPORTED)
set_target_properties(
    lore
    PROPERTIES
        IMPORTED_LOCATION "${LORE_STAGE_DIR}/bin/${LORE_SHARED_LIB_NAME}"
        INTERFACE_INCLUDE_DIRECTORIES "${LORE_STAGE_DIR}/include"
)
if(WIN32)
    set_target_properties(lore PROPERTIES IMPORTED_IMPLIB "${LORE_STAGE_DIR}/lib/lore.dll.lib")
endif()
add_dependencies(lore lore_cargo_build)

# Thin C++ wrapper adapting lore-capi's async/event-callback API to the
# synchronous calls EditorVCSInterface's virtual methods need. Its own
# library so both the extension and the standalone FFI test binary below can
# link it without depending on godot-cpp.
add_library(lore_ffi STATIC
    src/lore_ffi/lore_c_api.h
    src/lore_ffi/lore_call.h
    src/lore_ffi/lore_client.h
    src/lore_ffi/lore_client.cpp
)
target_include_directories(lore_ffi PUBLIC src)
target_link_libraries(lore_ffi PUBLIC lore)
set_target_properties(lore_ffi PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(godot-lore-plugin SHARED
    src/register_types.cpp
    src/register_types.h
    src/lore_vcs_plugin.cpp
    src/lore_vcs_plugin.h
)

target_link_libraries(godot-lore-plugin PRIVATE godot-cpp lore_ffi)

# CMake's automatic rpath points at lore's build-tree location, not where
# the POST_BUILD copies below actually put it, and BUILD_RPATH only adds to
# that rather than replacing it. SKIP_BUILD_RPATH on each target disables it
# so the explicit -rpath flag is the only entry: "look next to me". Both
# consumers of liblore need this — the extension library AND the test binary,
# each of which gets a copy of liblore placed beside it.
if(APPLE)
    set(GODOT_LORE_PLUGIN_RPATH_FLAG "-Wl,-rpath,@loader_path")
elseif(UNIX)
    set(GODOT_LORE_PLUGIN_RPATH_FLAG "-Wl,-rpath,$ORIGIN")
endif()

option(GODOT_LORE_PLUGIN_BUILD_FFI_TEST "Build the standalone lore_ffi test binary (no Godot involved)" ON)
if(GODOT_LORE_PLUGIN_BUILD_FFI_TEST)
    add_executable(lore_ffi_test tests/lore_ffi_test.cpp)
    target_link_libraries(lore_ffi_test PRIVATE lore_ffi)
    set_target_properties(
        lore_ffi_test
        PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/bin>"
            SKIP_BUILD_RPATH ON
    )
    if(GODOT_LORE_PLUGIN_RPATH_FLAG)
        target_link_options(lore_ffi_test PRIVATE "${GODOT_LORE_PLUGIN_RPATH_FLAG}")
    endif()
    add_custom_command(
        TARGET lore_ffi_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LORE_STAGE_DIR}/bin/${LORE_SHARED_LIB_NAME}"
            "${CMAKE_BINARY_DIR}/bin/${LORE_SHARED_LIB_NAME}"
    )
endif()

get_target_property(GODOTCPP_SUFFIX godot-cpp GODOTCPP_SUFFIX)

set(GODOT_LORE_PLUGIN_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/addons/godot-lore-plugin/${GODOT_LORE_PLUGIN_PLATFORM_DIR}")

# gersemi: off
set_target_properties(
    godot-lore-plugin
    PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        SKIP_BUILD_RPATH ON

        LINK_SEARCH_START_STATIC ON
        LINK_SEARCH_END_STATIC ON

        LIBRARY_OUTPUT_DIRECTORY "$<1:${GODOT_LORE_PLUGIN_OUTPUT_DIR}>"
        RUNTIME_OUTPUT_DIRECTORY "$<1:${GODOT_LORE_PLUGIN_OUTPUT_DIR}>"
        PDB_OUTPUT_DIRECTORY     "$<1:${GODOT_LORE_PLUGIN_OUTPUT_DIR}>"

        PREFIX ""
        OUTPUT_NAME "godot-lore-plugin${GODOTCPP_SUFFIX}"
)
# gersemi: on
if(GODOT_LORE_PLUGIN_RPATH_FLAG)
    target_link_options(godot-lore-plugin PRIVATE "${GODOT_LORE_PLUGIN_RPATH_FLAG}")
endif()

# Lore's shared library is a runtime dependency of godot-lore-plugin's own
# shared library; it must sit next to it for the loader to find it.
add_custom_command(
    TARGET godot-lore-plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${LORE_STAGE_DIR}/bin/${LORE_SHARED_LIB_NAME}"
        "${GODOT_LORE_PLUGIN_OUTPUT_DIR}/${LORE_SHARED_LIB_NAME}"
)

# These live at the repo root (shared with the FFI test/build tooling around
# them), but the addon folder is what actually ships, so mirror them in
# alongside it. Copies, not the source of truth — gitignored.
set(GODOT_LORE_PLUGIN_ADDON_DIR "${CMAKE_SOURCE_DIR}/addons/godot-lore-plugin")
add_custom_command(
    TARGET godot-lore-plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/THIRDPARTY.md"
        "${CMAKE_SOURCE_DIR}/LICENSE"
        "${GODOT_LORE_PLUGIN_ADDON_DIR}"
)
