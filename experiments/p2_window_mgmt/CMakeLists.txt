add_executable(p2_window_mgmt WIN32 main.cpp)

target_compile_definitions(p2_window_mgmt PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_compile_options(p2_window_mgmt PRIVATE
    /W4
    /permissive-
    /utf-8
)