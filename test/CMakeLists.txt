add_executable(RenderLabTests
        AssetIDTest.cpp
        LutGgxTest.cpp
        AssetDataManagerTest.cpp
        EnvironmentMapImportTest.cpp
        ${PROJECT_SOURCE_DIR}/src/asset/AssetDataManager.cpp
        ${PROJECT_SOURCE_DIR}/src/asset/AssetDescManager.cpp
        ${PROJECT_SOURCE_DIR}/src/asset/builtin/BuiltinMesh.cpp
        ${PROJECT_SOURCE_DIR}/src/asset/builtin/BuiltinMaterial.cpp
        ${PROJECT_SOURCE_DIR}/src/asset/builtin/BuiltinTexture.cpp
        ${PROJECT_SOURCE_DIR}/src/asset/builtin/BuiltinSampler.cpp
        ${PROJECT_SOURCE_DIR}/src/asset/import/ImportImage.cpp
        ${PROJECT_SOURCE_DIR}/src/asset/import/ImportTexture.cpp
        ${PROJECT_SOURCE_DIR}/src/asset/import/ImportEnvironmentMap.cpp
        ${PROJECT_SOURCE_DIR}/src/core/ConfigManager.cpp
        ${PROJECT_SOURCE_DIR}/src/core/Context.cpp
        ${PROJECT_SOURCE_DIR}/src/core/math/Sampler.cpp
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
        unofficial::tinyexr::tinyexr
)

if (MSVC)
    target_compile_options(RenderLabTests PRIVATE /utf-8 /bigobj)
endif ()

add_test(NAME RenderLabTests COMMAND RenderLabTests)

add_executable(main main.cpp)