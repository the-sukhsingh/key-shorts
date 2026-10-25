cmake_minimum_required(VERSION 3.20)
project(KeyShorts LANGUAGES CXX RC)

add_executable(KeyShorts WIN32
    src/main.cpp
    src/ui.cpp
    src/ui_rule.cpp
    src/engine.cpp
    src/config.cpp
    src/keys.cpp
    src/util.cpp
    src/app.rc
)

target_include_directories(KeyShorts PRIVATE src)
target_compile_features(KeyShorts PRIVATE cxx_std_17)
target_compile_definitions(KeyShorts PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(KeyShorts PRIVATE comctl32 comdlg32 shell32 advapi32 ole32)

if(MSVC)
    target_compile_options(KeyShorts PRIVATE /W4 /O1 /GS- /Gy /Gw /utf-8 /permissive-)
    target_link_options(KeyShorts PRIVATE
        /OPT:REF /OPT:ICF /INCREMENTAL:NO
        /MANIFEST:EMBED
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest"
    )
    set_property(TARGET KeyShorts PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
else()
    target_compile_options(KeyShorts PRIVATE -Os -Wall -Wextra -municode)
    target_link_options(KeyShorts PRIVATE -static -mwindows -s)
endif()

set_target_properties(KeyShorts PROPERTIES OUTPUT_NAME "KeyShorts")
