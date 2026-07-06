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

project(godot-lore-plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(third_party/godot-cpp)

# Vendored Lore C API (see tools/update-lore-snapshot.ps1). Linked dynamically:
# the staticlib archive cbindgen/cargo also produce is 100s of MB to several GB
# unlinked (no dead-code elimination happens until final link), whereas the
# cdylib is already linked+stripped down to ~30MB. lore.dll ships alongside
# the extension's own DLL at runtime.
add_library(lore SHARED IMPORTED)
set_target_properties(
    lore
    PROPERTIES
        IMPORTED_IMPLIB "${CMAKE_SOURCE_DIR}/third_party/lore/lib/lore.dll.lib"
        IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_party/lore/bin/lore.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party/lore/include"
)

add_library(godot-lore-plugin SHARED
    src/register_types.cpp
    src/register_types.h
    src/lore_hello.cpp
    src/lore_hello.h
)

target_link_libraries(godot-lore-plugin PRIVATE godot-cpp lore)

get_target_property(GODOTCPP_SUFFIX godot-cpp GODOTCPP_SUFFIX)

set(GODOT_LORE_PLUGIN_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/test_project/addons/godot-lore-plugin/bin")

# gersemi: off
set_target_properties(
    godot-lore-plugin
    PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        BUILD_RPATH_USE_ORIGIN ON

        LINK_SEARCH_START_STATIC ON
        LINK_SEARCH_END_STATIC ON

        LIBRARY_OUTPUT_DIRECTORY "$<1:${GODOT_LORE_PLUGIN_OUTPUT_DIR}>"
        RUNTIME_OUTPUT_DIRECTORY "$<1:${GODOT_LORE_PLUGIN_OUTPUT_DIR}>"
        PDB_OUTPUT_DIRECTORY     "$<1:${GODOT_LORE_PLUGIN_OUTPUT_DIR}>"

        PREFIX ""
        OUTPUT_NAME "godot-lore-plugin${GODOTCPP_SUFFIX}"
)
# gersemi: on

# lore.dll is a runtime dependency of godot-lore-plugin's own DLL; it must sit
# next to it for the loader to find it.
add_custom_command(
    TARGET godot-lore-plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/lore/bin/lore.dll"
        "${GODOT_LORE_PLUGIN_OUTPUT_DIR}/lore.dll"
)
