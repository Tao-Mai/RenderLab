add_executable(RenderLabTests
        AssetIDTest.cpp
        ConfigSerializationTest.cpp
        DebugMacrosTest.cpp
        InputManagerTest.cpp
        PartialSerializationTest.cpp
        ComponentDrawTest.cpp
        LutGgxTest.cpp
        AssetDataManagerTest.cpp
        EnvironmentMapImportTest.cpp
        SceneManagerTest.cpp
        ${PROJECT_SOURCE_DIR}/src/scene/SceneManager.cpp
        ${PROJECT_SOURCE_DIR}/src/editor/EditorCamera.cpp
        ${PROJECT_SOURCE_DIR}/src/scene/AActor.cpp
        ${PROJECT_SOURCE_DIR}/src/scene/actor/AFreeFlyCamera.cpp
        ${PROJECT_SOURCE_DIR}/src/scene/actor/ALight.cpp
        ${PROJECT_SOURCE_DIR}/src/scene/actor/AStaticMesh.cpp
        ${PROJECT_SOURCE_DIR}/src/scene/component/TransformComponent.cpp
        ${PROJECT_SOURCE_DIR}/src/scene/Component.cpp
        ${PROJECT_SOURCE_DIR}/src/scene/component/LightComponent.cpp
        ${PROJECT_SOURCE_DIR}/src/scene/component/RenderComponent.cpp
        ${PROJECT_SOURCE_DIR}/src/scene/component/CameraComponent.cpp
        ${PROJECT_SOURCE_DIR}/src/scene/component/FreeFlyMoveComponent.cpp
        ${PROJECT_SOURCE_DIR}/src/scene/component/CharacterMoveComponent.cpp
        ${PROJECT_SOURCE_DIR}/src/editor/ComponentDraw.cpp
        ${PROJECT_SOURCE_DIR}/src/core/InputManager.cpp
        ${PROJECT_SOURCE_DIR}/src/core/Window.cpp
        ${PROJECT_SOURCE_DIR}/src/asset/AssetDataManager.cpp
        ${PROJECT_SOURCE_DIR}/src/asset/TextureMip.cpp
        ${PROJECT_SOURCE_DIR}/src/asset/MeshGeometry.cpp
        ${PROJECT_SOURCE_DIR}/src/asset/AssetManager.cpp
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
        VULKAN_HPP_NO_EXCEPTIONS
        RENDERLAB_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        RENDERLAB_CONFIG_FILE="${PROJECT_SOURCE_DIR}/test/config.json"
)
target_link_libraries(RenderLabTests PRIVATE
        mikktspace::mikktspace
        RenderLabBuildOptions
        GTest::gtest_main
        glog::glog
        glm::glm
        glfw
        imgui::imgui
        nlohmann_json::nlohmann_json
        unofficial::tinyexr::tinyexr
)

add_test(NAME RenderLabTests COMMAND RenderLabTests)

add_executable(RenderGraphTests
        RenderGraphTest.cpp
        ShaderDataTest.cpp
        ${PROJECT_SOURCE_DIR}/src/render/feature/RenderGraphSetup.cpp
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
        RenderLabBuildOptions
        GTest::gtest_main
        Vulkan::Vulkan
        glog::glog
        glm::glm
        nlohmann_json::nlohmann_json
)
add_test(NAME RenderGraphTests COMMAND RenderGraphTests)

# Manual integration check requiring Vulkan and the project's configured assets.
foreach(check IN ITEMS RenderGraphGpuSmoke EditorOverlayGpuCheck DeferredRenderingCheck RayTracingVisibilityCheck)
    add_executable(${check} EXCLUDE_FROM_ALL ${check}.cpp)
    target_link_libraries(${check} PRIVATE RenderLabRuntime)
    renderlab_copy_compiler_runtime(${check})
endforeach()

add_executable(main main.cpp)
target_link_libraries(main PRIVATE RenderLabBuildOptions)

add_executable(Cxx26ReflectionTest Cxx26ReflectionTest.cpp)
target_link_libraries(Cxx26ReflectionTest PRIVATE RenderLabBuildOptions)
add_test(NAME Cxx26ReflectionTest COMMAND Cxx26ReflectionTest)

set_tests_properties(RenderLabTests RenderGraphTests Cxx26ReflectionTest PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${RENDERLAB_COMPILER_BIN_DIR}")

foreach (target RenderLabTests RenderGraphTests main Cxx26ReflectionTest)
    renderlab_copy_compiler_runtime(${target})
endforeach ()
