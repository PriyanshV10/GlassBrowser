# Sandboxed layout (same as p4_cef_hello):
#   p5_cef_window.dll - our code (exports RunWinMain)
#   p5_cef_window.exe - CEF's bootstrap.exe, copied and renamed
set(CEF_TARGET p5_cef_window)

if(NOT USE_SANDBOX)
    message(FATAL_ERROR
        "p5_cef_window requires the sandbox. Set USE_SANDBOX to ON in "
        "CMakePresets.json, then Project > Delete Cache and Reconfigure.")
endif()

# Must come first: sets CEF_TARGET_OUT_DIR used by everything below.
SET_CEF_TARGET_OUT_DIR()

add_library(${CEF_TARGET} SHARED
    main.cpp
    browser_app.cpp
    browser_app.h
    browser_client.cpp
    browser_client.h
    main_window.cpp
    main_window.h
    opacity_controller.h
)
SET_LIBRARY_TARGET_PROPERTIES(${CEF_TARGET})

COPY_SINGLE_FILE(${CEF_TARGET}
    ${CEF_BINARY_DIR}/bootstrap.exe
    ${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.exe)

add_dependencies(${CEF_TARGET} libcef_dll_wrapper)
target_include_directories(${CEF_TARGET} PRIVATE "${CEF_ROOT}")
target_link_libraries(${CEF_TARGET} libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS})

COPY_FILES("${CEF_TARGET}" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
COPY_FILES("${CEF_TARGET}" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")

# Permissions required by the Windows sandbox (LPAC). Keep this last.
SET_LPAC_ACLS("${CEF_TARGET}")