find_package(ZLIB REQUIRED)

add_library(RenderLabPbrtFrontend STATIC
    PbrtFrontend.cpp
    upstream/Parser.cpp
    upstream/Scene.cpp
    upstream/Support.cpp
    upstream/double-conversion/bignum.cc
    upstream/double-conversion/bignum-dtoa.cc
    upstream/double-conversion/cached-powers.cc
    upstream/double-conversion/double-to-string.cc
    upstream/double-conversion/fast-dtoa.cc
    upstream/double-conversion/fixed-dtoa.cc
    upstream/double-conversion/string-to-double.cc
    upstream/double-conversion/strtod.cc
    upstream/utf8proc/utf8proc.c
)
set_target_properties(RenderLabPbrtFrontend PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_include_directories(RenderLabPbrtFrontend PRIVATE "${PROJECT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/upstream")
target_compile_definitions(RenderLabPbrtFrontend PRIVATE PBRT_FLOAT_AS_DOUBLE UTF8PROC_STATIC)
target_compile_options(RenderLabPbrtFrontend PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)
target_link_libraries(RenderLabPbrtFrontend PRIVATE glm::glm ZLIB::ZLIB)
