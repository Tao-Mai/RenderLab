add_executable(RenderLabTests
        LutGgxTest.cpp
        AssetDataManagerTest.cpp
        ${PROJECT_SOURCE_DIR}/src/asset/AssetDataManager.cpp
        ${PROJECT_SOURCE_DIR}/src/asset/builtin/BuiltinMesh.cpp
        ${PROJECT_SOURCE_DIR}/src/asset/builtin/BuiltinMaterial.cpp
        ${PROJECT_SOURCE_DIR}/src/asset/builtin/BuiltinTexture.cpp
        ${PROJECT_SOURCE_DIR}/src/core/ConfigManager.cpp
        ${PROJECT_SOURCE_DIR}/src/core/Context.cpp
)

target_include_directories(RenderLabTests PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(RenderLabTests PRIVATE
        RENDERLAB_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        RENDERLAB_CONFIG_FILE="${PROJECT_SOURCE_DIR}/test/config.json"
)
target_link_libraries(RenderLabTests PRIVATE
        GTest::gtest_main
        glog::glog
        glm::glm
        EnTT::EnTT
        reflectcpp::reflectcpp
)

if (MSVC)
    target_compile_options(RenderLabTests PRIVATE /utf-8)
endif ()

add_test(NAME RenderLabTests COMMAND RenderLabTests)
