cmake_minimum_required(VERSION 3.20)
project(battery_overlay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT for single-EXE portability (matches Rust +crt-static)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

add_executable(battery_overlay WIN32
    main.cpp
    res/app.rc
)

target_include_directories(battery_overlay PRIVATE ${CMAKE_SOURCE_DIR}/res)

# RC compiler needs the source root to find icon.ico
set_source_files_properties(res/app.rc PROPERTIES
    RC_INCLUDES "${CMAKE_SOURCE_DIR}"
)

target_link_libraries(battery_overlay PRIVATE
    imm32.lib
    shell32.lib
    shlwapi.lib
    comctl32.lib
    dwmapi.lib
    gdi32.lib
)

# Release optimizations (matches Rust: strip=true, lto=true)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(battery_overlay PRIVATE /O2 /GL)
    target_link_options(battery_overlay PRIVATE /LTCG /OPT:ICF /OPT:REF)
endif()
