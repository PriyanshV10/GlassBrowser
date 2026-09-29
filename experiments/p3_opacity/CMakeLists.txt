add_executable(p3_opacity WIN32 main.cpp)

target_compile_definitions(p3_opacity PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_compile_options(p3_opacity PRIVATE
    /W4
    /permissive-
    /utf-8
)