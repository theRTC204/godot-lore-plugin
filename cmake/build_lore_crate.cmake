# Builds (or reuses) the third_party/lore cdylib and stages it, lore.h, and
# (on Windows) its import lib under LORE_STAGE_DIR. Run via `cmake -P` from
# the lore_cargo_build custom target in the top-level CMakeLists.txt, which
# passes all LORE_*/CARGO_EXECUTABLE variables below as -D arguments.
#
# Skips the cargo invocation entirely if LORE_STAGE_DIR already holds
# artifacts built from third_party/lore's current commit (tracked via the
# .lore-build-sha marker below) — this is what lets CI restore a cache
# entry keyed on that commit and skip rebuilding lore whenever the
# submodule pin hasn't moved, rather than relying on cargo's own
# fingerprinting (which still re-verifies ~300 crates, and always
# recompiles lore's own workspace-local crates regardless).

set(LORE_CARGO_OUT_DIR "${LORE_SRC_DIR}/target/${LORE_CARGO_PROFILE}")
set(LORE_SHA_MARKER "${LORE_STAGE_DIR}/.lore-build-sha")
set(LORE_LIB_OUTPUT "${LORE_STAGE_DIR}/bin/${LORE_SHARED_LIB_NAME}")
set(LORE_HEADER_OUTPUT "${LORE_STAGE_DIR}/include/lore.h")

execute_process(
    COMMAND git -C "${LORE_SRC_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE LORE_SUBMODULE_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE LORE_GIT_RESULT
)
if(NOT LORE_GIT_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to resolve third_party/lore's current commit")
endif()

set(LORE_UP_TO_DATE FALSE)
if(EXISTS "${LORE_SHA_MARKER}" AND EXISTS "${LORE_LIB_OUTPUT}" AND EXISTS "${LORE_HEADER_OUTPUT}")
    if(NOT LORE_IS_WIN32 OR EXISTS "${LORE_STAGE_DIR}/lib/lore.dll.lib")
        file(READ "${LORE_SHA_MARKER}" LORE_BUILT_SHA)
        string(STRIP "${LORE_BUILT_SHA}" LORE_BUILT_SHA)
        if(LORE_BUILT_SHA STREQUAL LORE_SUBMODULE_SHA)
            set(LORE_UP_TO_DATE TRUE)
        endif()
    endif()
endif()

if(LORE_UP_TO_DATE)
    message(STATUS "lore crate artifacts already staged for ${LORE_SUBMODULE_SHA} (${LORE_CARGO_PROFILE}) — skipping cargo build")
    return()
endif()

message(STATUS "Building lore crate (cargo, ${LORE_CARGO_PROFILE}) from third_party/lore @ ${LORE_SUBMODULE_SHA}")

file(MAKE_DIRECTORY "${LORE_STAGE_DIR}/include" "${LORE_STAGE_DIR}/lib" "${LORE_STAGE_DIR}/bin")

if(LORE_DUAL_ARCH)
    execute_process(
        COMMAND "${CARGO_EXECUTABLE}" build --profile "${LORE_CARGO_PROFILE}" -p lore --lib
            --target x86_64-apple-darwin --target aarch64-apple-darwin
        WORKING_DIRECTORY "${LORE_SRC_DIR}"
        RESULT_VARIABLE LORE_CARGO_RESULT
    )
    if(NOT LORE_CARGO_RESULT EQUAL 0)
        message(FATAL_ERROR "cargo build failed")
    endif()
    execute_process(
        COMMAND lipo -create -output "${LORE_CARGO_OUT_DIR}/${LORE_SHARED_LIB_NAME}"
            "${LORE_SRC_DIR}/target/x86_64-apple-darwin/${LORE_CARGO_PROFILE}/${LORE_SHARED_LIB_NAME}"
            "${LORE_SRC_DIR}/target/aarch64-apple-darwin/${LORE_CARGO_PROFILE}/${LORE_SHARED_LIB_NAME}"
        RESULT_VARIABLE LORE_LIPO_RESULT
    )
    if(NOT LORE_LIPO_RESULT EQUAL 0)
        message(FATAL_ERROR "lipo failed")
    endif()
else()
    execute_process(
        COMMAND "${CARGO_EXECUTABLE}" build --profile "${LORE_CARGO_PROFILE}" -p lore --lib
        WORKING_DIRECTORY "${LORE_SRC_DIR}"
        RESULT_VARIABLE LORE_CARGO_RESULT
    )
    if(NOT LORE_CARGO_RESULT EQUAL 0)
        message(FATAL_ERROR "cargo build failed")
    endif()

    if(UNIX AND NOT APPLE)
        # Only the Windows/macOS Rust targets set split-debuginfo in
        # third_party/lore's .cargo/config.toml, so the generic Linux
        # build ships full embedded DWARF unless stripped here.
        execute_process(
            COMMAND strip --strip-debug "${LORE_CARGO_OUT_DIR}/${LORE_SHARED_LIB_NAME}"
            RESULT_VARIABLE LORE_STRIP_RESULT
        )
        if(NOT LORE_STRIP_RESULT EQUAL 0)
            message(FATAL_ERROR "strip failed")
        endif()
    endif()
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${LORE_CARGO_OUT_DIR}/${LORE_SHARED_LIB_NAME}" "${LORE_LIB_OUTPUT}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${LORE_SRC_DIR}/lore-capi/lore.h" "${LORE_HEADER_OUTPUT}")
if(LORE_IS_WIN32)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${LORE_CARGO_OUT_DIR}/lore.dll.lib" "${LORE_STAGE_DIR}/lib/lore.dll.lib")
endif()

file(WRITE "${LORE_SHA_MARKER}" "${LORE_SUBMODULE_SHA}")
