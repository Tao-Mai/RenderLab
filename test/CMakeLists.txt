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
        ${PROJECT_SOURCE_DIR}/src/asset/import/EnvironmentMapUtils.cpp
        ${PROJECT_SOURCE_DIR}/src/asset/import/EnvironmentMapBake.cpp
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

add_executable(RenderGraphTests
        RenderGraphTest.cpp
        ShaderDataTest.cpp
        ${PROJECT_SOURCE_DIR}/src/render/pass/RenderGraphSlots.cpp
)
target_include_directories(RenderGraphTests PRIVATE "${PROJECT_SOURCE_DIR}/src")
add_dependencies(RenderGraphTests RenderLabShaders)
target_compile_definitions(RenderGraphTests PRIVATE
        RENDERLAB_SHADER_BINARY_DIR="${SHADER_BINARY_DIR}"
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
        VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
        VULKAN_HPP_NO_EXCEPTIONS
        GLOG_NO_ABBREVIATED_SEVERITIES
)
target_link_libraries(RenderGraphTests PRIVATE
        GTest::gtest_main
        Vulkan::Vulkan
        glog::glog
        glm::glm
        EnTT::EnTT
        reflectcpp::reflectcpp
)
if (MSVC)
    target_compile_options(RenderGraphTests PRIVATE /utf-8)
endif ()
add_test(NAME RenderGraphTests COMMAND RenderGraphTests)

# Manual integration check requiring Vulkan and the project's configured assets.
get_target_property(RENDERGRAPH_SMOKE_SOURCES RenderLab SOURCES)
list(REMOVE_ITEM RENDERGRAPH_SMOKE_SOURCES src/Main.cpp)
list(TRANSFORM RENDERGRAPH_SMOKE_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
add_executable(RenderGraphGpuSmoke EXCLUDE_FROM_ALL
        RenderGraphGpuSmoke.cpp ${RENDERGRAPH_SMOKE_SOURCES})
foreach (PROPERTY INCLUDE_DIRECTORIES COMPILE_DEFINITIONS LINK_LIBRARIES COMPILE_OPTIONS)
    get_target_property(RENDERGRAPH_SMOKE_${PROPERTY} RenderLab ${PROPERTY})
    if (RENDERGRAPH_SMOKE_${PROPERTY})
        set_property(TARGET RenderGraphGpuSmoke PROPERTY ${PROPERTY} "${RENDERGRAPH_SMOKE_${PROPERTY}}")
    endif ()
endforeach ()
add_dependencies(RenderGraphGpuSmoke RenderLabShaders)

add_executable(main main.cpp)
