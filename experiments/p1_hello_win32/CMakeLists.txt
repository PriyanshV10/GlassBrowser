add_executable(p1_hello_win32 WIN32 main.cpp)

target_compile_definitions(p1_hello_win32 PRIVATE 
	UNICODE _UNICODE
	WIN32_LEAN_AND_MEAN
	NOMINMAX
)

target_compile_options(p1_hello_win32 PRIVATE
	/W4
	/permissive-
	/utf-8
)