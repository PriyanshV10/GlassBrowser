cmake_minimum_required(VERSION 3.25)
project(GlassBrowser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
	message(FATAL_ERROR "GlassBrowser is x64 only. Configure with: cmake -A x64 ...")
endif()

add_subdirectory(experiments/p1_hello_win32)
add_subdirectory(experiments/p2_window_mgmt)