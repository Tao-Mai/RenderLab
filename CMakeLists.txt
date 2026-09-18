cmake_minimum_required(VERSION 3.20)

project(RenderLab
    VERSION 0.1.0
    DESCRIPTION "A minimal C++ rendering project"
    LANGUAGES CXX
)

add_executable(RenderLab
    main.cpp
)

target_compile_features(RenderLab PRIVATE cxx_std_23)

set_target_properties(RenderLab PROPERTIES
    CXX_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(RenderLab PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(RenderLab PRIVATE -Wall -Wextra -Wpedantic)
endif()
