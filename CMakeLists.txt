cmake_minimum_required(VERSION 3.26)
project(GlassBrowser LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "GlassBrowser is x64 only. Choose an x64 configuration.")
endif()

# ---- Phase 1-3 experiments (no CEF needed) ----
add_subdirectory(experiments/p1_hello_win32)
add_subdirectory(experiments/p2_window_mgmt)
add_subdirectory(experiments/p3_opacity)

# ---- CEF (Phase 4 onward) ----
set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cef")

if(EXISTS "${CEF_ROOT}/include/cef_version.h")
    # Load CEF's own CMake modules: compiler flags, libraries, helper macros.
    list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
    find_package(CEF REQUIRED)

    # Build the C++ wrapper library from the distribution's sources.
    add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" libcef_dll_wrapper)

    # A linkable "target" for libcef.lib (Debug/Release variants).
    ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")

    message(STATUS "GlassBrowser: CEF found at ${CEF_ROOT}, USE_SANDBOX=${USE_SANDBOX}")
    # add_subdirectory(experiments/p4_cef_hello)
    # add_subdirectory(experiments/p5_cef_window)
    # add_subdirectory(experiments/p6_transparent)
    # add_subdirectory(experiments/p7_privacy)
    # add_subdirectory(experiments/p8_browser_ui)
    # add_subdirectory(experiments/p9_tabs)
    add_subdirectory(experiments/p10_profile)
else()
    message(WARNING "CEF not found in third_party/cef - Phase 4+ targets are skipped.")
endif()