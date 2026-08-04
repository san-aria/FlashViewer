cmake_minimum_required(VERSION 3.25)

# The VERSION file at the repo root is the single source of truth. Read it here,
# BEFORE project(), so PROJECT_VERSION, the generated version.h (cmake/Version.cmake)
# and the runtime string all come from one place. Previously this line carried a
# literal that had to be hand-synced with VERSION, and Application.cpp carried a
# third copy — which is how the app kept announcing "FlashViewer 0.1.0".
file(READ "${CMAKE_CURRENT_LIST_DIR}/VERSION" FLASHVIEWER_VERSION_STRING)
string(STRIP "${FLASHVIEWER_VERSION_STRING}" FLASHVIEWER_VERSION_STRING)

project(FlashViewer VERSION ${FLASHVIEWER_VERSION_STRING} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Emit executables (FlashViewer, flashviewer_tests) at the build root —
# build/<preset>/FlashViewer — rather than CMake's default per-subdirectory location
# (build/<preset>/src/, because the target is defined in src/CMakeLists.txt). This
# matches the run paths documented in INSTALL.md across all platforms/generators.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

option(FLASHVIEWER_ENABLE_ASAN "Enable AddressSanitizer + UBSan" OFF)
option(FV_PERF_INSTRUMENT "Enable performance instrumentation hooks (frame timer, stall detector)" OFF)

include(CTest)

include(cmake/Version.cmake)
include(cmake/Dependencies.cmake)

add_subdirectory(src)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Apply compiler options — must come after add_subdirectory(src) defines FlashViewer
include(cmake/CompilerOptions.cmake)
