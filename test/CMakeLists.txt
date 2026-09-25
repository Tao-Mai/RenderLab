add_executable(RenderLabTests
        TextureIoTest.cpp
        ${PROJECT_SOURCE_DIR}/src/asset/TextureIo.cpp
)

target_include_directories(RenderLabTests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(RenderLabTests PRIVATE
        GTest::gtest_main
        glog::glog
        glm::glm
        EnTT::EnTT
)

if (MSVC)
    target_compile_options(RenderLabTests PRIVATE /utf-8)
endif ()

add_test(NAME RenderLabTests COMMAND RenderLabTests)
