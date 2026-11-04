# Sandboxed layout, mirroring the USE_SANDBOX branch of CEF's own
# tests/cefsimple/CMakeLists.txt:
#   p4_cef_hello.dll - our code (exports RunWinMain)
#   p4_cef_hello.exe - CEF's prebuilt bootstrap.exe, copied and renamed.
#                      It sets up the sandbox and calls into the DLL.
set(CEF_TARGET p4_cef_hello)

if(NOT USE_SANDBOX)
    message(FATAL_ERROR
        "p4_cef_hello requires the sandbox. Set USE_SANDBOX to ON in "
        "CMakePresets.json, then Project > Delete Cache and Reconfigure.")
endif()

# Must come first: sets CEF_TARGET_OUT_DIR used by everything below.
SET_CEF_TARGET_OUT_DIR()

add_library(${CEF_TARGET} SHARED
    main.cpp
    simple_app.cpp
    simple_app.h
    simple_client.cpp
    simple_client.h
)
SET_LIBRARY_TARGET_PROPERTIES(${CEF_TARGET})

# Copy CEF's bootstrap.exe next to the DLL, renamed to <target>.exe.
COPY_SINGLE_FILE(${CEF_TARGET}
    ${CEF_BINARY_DIR}/bootstrap.exe
    ${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.exe)

add_dependencies(${CEF_TARGET} libcef_dll_wrapper)
target_include_directories(${CEF_TARGET} PRIVATE "${CEF_ROOT}")
# Plain signature (no PRIVATE keyword), as cefsimple does.
target_link_libraries(${CEF_TARGET} libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS})

# Copy CEF's runtime and resource files next to the exe/DLL.
COPY_FILES("${CEF_TARGET}" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
COPY_FILES("${CEF_TARGET}" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")

# Permissions required by the Windows sandbox (LPAC). Keep this last.
SET_LPAC_ACLS("${CEF_TARGET}")