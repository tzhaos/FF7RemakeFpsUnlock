cmake_minimum_required(VERSION 3.20)

project(FF7RemakeFpsUnlock VERSION 1.0.0 LANGUAGES CXX)

add_library(xinput1_3 SHARED
    src/dllmain.cpp
    src/xinput1_3.def
)

target_compile_features(xinput1_3 PRIVATE cxx_std_17)
target_compile_definitions(xinput1_3 PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
)
target_compile_options(xinput1_3 PRIVATE /W4 /permissive-)

set_target_properties(xinput1_3 PROPERTIES
    OUTPUT_NAME "xinput1_3"
    PREFIX ""
    SUFFIX ".dll"
)
set_property(TARGET xinput1_3 PROPERTY MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_link_libraries(xinput1_3 PRIVATE user32)
