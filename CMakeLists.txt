cmake_minimum_required(VERSION 4.2)

if (CMAKE_HOST_WIN32 AND NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-mingw-dynamic" CACHE STRING "vcpkg target triplet")
endif ()

if (NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
            CACHE FILEPATH "vcpkg toolchain file")
endif ()

project(RenderLab LANGUAGES C CXX)

# CLion's Default profile leaves the build type empty; use an optimized build.
# An explicitly selected Debug/RelWithDebInfo configuration remains unchanged.
if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif ()

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16.2)
    message(FATAL_ERROR "RenderLab requires GCC 16.2 or newer with C++26 reflection. Select that compiler in your toolchain.")
endif ()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

if (WIN32 AND NOT VCPKG_TARGET_TRIPLET STREQUAL "x64-mingw-dynamic")
    message(FATAL_ERROR "Windows GCC builds require VCPKG_TARGET_TRIPLET=x64-mingw-dynamic. Use a fresh build directory when changing toolchains.")
endif ()

include(CheckCXXSourceCompiles)
include(CMakePushCheckState)
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_FLAGS "-freflection")
check_cxx_source_compiles([=[
    #include <meta>
    #if !defined(__cpp_impl_reflection) || __cpp_impl_reflection < 202506L
    #error C++26 reflection is unavailable
    #endif
    static_assert(std::meta::is_type(^^int));
    int main() {}
]=] RENDERLAB_HAS_CXX26_REFLECTION)
cmake_pop_check_state()
if (NOT RENDERLAB_HAS_CXX26_REFLECTION)
    message(FATAL_ERROR "The selected GCC/libstdc++ cannot compile C++26 reflection with -freflection.")
endif ()

add_library(RenderLabBuildOptions INTERFACE)
target_compile_features(RenderLabBuildOptions INTERFACE cxx_std_26)
target_compile_options(RenderLabBuildOptions INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:-freflection>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>)
target_compile_definitions(RenderLabBuildOptions INTERFACE JSON_NOEXCEPTION TINYGLTF_NOEXCEPTION)

get_filename_component(RENDERLAB_COMPILER_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

function(renderlab_copy_compiler_runtime target)
    if (WIN32)
        foreach (runtime libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
            set(runtime_path "${RENDERLAB_COMPILER_BIN_DIR}/${runtime}")
            if (NOT EXISTS "${runtime_path}")
                message(FATAL_ERROR "Missing GCC runtime: ${runtime_path}")
            endif ()

            add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${runtime_path}" "$<TARGET_FILE_DIR:${target}>"
                    VERBATIM)
        endforeach ()
    endif ()
endfunction()

find_package(Vulkan 1.4 REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(glog CONFIG REQUIRED)
find_package(tinyexr CONFIG REQUIRED)
find_package(mikktspace CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_path(TINYGLTF_INCLUDE_DIR NAMES tiny_gltf.h REQUIRED)

find_program(SLANGC_EXECUTABLE
        NAMES slangc
        HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
        REQUIRED
)

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
include(cmake/Shaders.cmake)
set(SCENE_SHADER_INCLUDES
        "${SHADER_SOURCE_DIR}/scene_data.slang"
        "${SHADER_SOURCE_DIR}/lighting_data.slang"
        "${SHADER_SOURCE_DIR}/scene_uniforms.slang"
        "${SHADER_SOURCE_DIR}/light_data.slang"
        "${SHADER_SOURCE_DIR}/mesh_vertex.slang"
        "${SHADER_SOURCE_DIR}/mesh_surface.slang"
        "${SHADER_SOURCE_DIR}/normal_mapping.slang"
        "${SHADER_SOURCE_DIR}/surface.slang"
        "${SHADER_SOURCE_DIR}/material_data.slang"
        "${SHADER_SOURCE_DIR}/scene_lighting.slang"
        "${SHADER_SOURCE_DIR}/sampling.slang"
        "${SHADER_SOURCE_DIR}/brdf.slang"
        "${SHADER_SOURCE_DIR}/constant.slang"
)
foreach(enable_clearcoat IN ITEMS 0 1)
    foreach(enable_sheen IN ITEMS 0 1)
        foreach(enable_ibl IN ITEMS 1 0)
            set(binary slang)
            if(enable_clearcoat)
                string(APPEND binary ".clearcoat")
            endif()
            if(enable_sheen)
                string(APPEND binary ".sheen")
            endif()
            if(NOT enable_ibl)
                string(APPEND binary ".no_ibl")
            endif()
            renderlab_shader(slang shader.slang "${binary}"
                DEFINES ENABLE_IBL=${enable_ibl} ENABLE_CLEARCOAT=${enable_clearcoat} ENABLE_SHEEN=${enable_sheen}
                INCLUDES ${SCENE_SHADER_INCLUDES})
        endforeach()
    endforeach()
endforeach()
renderlab_shader(gbuffer deferred/gbuffer.slang gbuffer DEFINES ENABLE_CLEARCOAT=0 ENABLE_SHEEN=1
    INCLUDES ${SCENE_SHADER_INCLUDES})
foreach(enable_ibl IN ITEMS 1 0)
    set(binary deferred_lighting)
    if(NOT enable_ibl)
        string(APPEND binary ".no_ibl")
    endif()
    renderlab_shader(deferred_lighting deferred/lighting.slang "${binary}"
        DEFINES ENABLE_IBL=${enable_ibl} ENABLE_CLEARCOAT=0 ENABLE_SHEEN=1 INCLUDES ${SCENE_SHADER_INCLUDES})
endforeach()
renderlab_shader(depth depth.slang depth DEFINES ENABLE_CLEARCOAT=0 ENABLE_SHEEN=0 INCLUDES ${SCENE_SHADER_INCLUDES})
renderlab_shader(skybox skybox.slang skybox INCLUDES ${SCENE_SHADER_INCLUDES})
renderlab_shader(tonemapping postprocess/tonemapping.slang tonemapping)
foreach(stage IN ITEMS 0 1 2 3)
    set(binary bloom)
    if(NOT stage EQUAL 0)
        string(APPEND binary ".${stage}")
    endif()
    renderlab_shader(bloom postprocess/bloom.slang "${binary}" DEFINES BLOOM_STAGE=${stage})
endforeach()
foreach(name IN ITEMS light_marker depth picking)
    renderlab_shader(editor_${name} editor/${name}.slang editor_${name}
        INCLUDES "${SHADER_SOURCE_DIR}/editor/view.slang")
endforeach()
foreach(type IN ITEMS 0 1 2 3)
    set(names shadow shadow.directional shadow.rect_area shadow.spot)
    list(GET names ${type} binary)
    renderlab_shader(shadow shadow.slang "${binary}" DEFINES SHADOW_TYPE=${type}
        INCLUDES ${SCENE_SHADER_INCLUDES})
endforeach()
renderlab_shader(raytracing raytracing/raytracing.slang raytracing
    ENTRIES raygenMain missMain visibilityMissMain closestHitMain lightClosestHitMain
    DEFINES ENABLE_CLEARCOAT=0 ENABLE_SHEEN=0
    INCLUDES "${SHADER_SOURCE_DIR}/scene_uniforms.slang"
        "${SHADER_SOURCE_DIR}/light_data.slang" "${SHADER_SOURCE_DIR}/constant.slang"
        "${SHADER_SOURCE_DIR}/random.slang" "${SHADER_SOURCE_DIR}/point_light_sampling.slang"
        "${SHADER_SOURCE_DIR}/rect_area_light_sampling.slang"
        "${SHADER_SOURCE_DIR}/material_data.slang"
        "${SHADER_SOURCE_DIR}/normal_mapping.slang" "${SHADER_SOURCE_DIR}/pbr_brdf.slang"
        "${SHADER_SOURCE_DIR}/brdf.slang")
renderlab_shader(offline_pathtracing offline/pathtracing.slang offline_pathtracing
    ENTRIES raygenMain missMain visibilityMissMain closestHitMain lightClosestHitMain
    DEFINES ENABLE_CLEARCOAT=0 ENABLE_SHEEN=0
    INCLUDES "${SHADER_SOURCE_DIR}/low_discrepancy.slang"
        "${SHADER_SOURCE_DIR}/light_data.slang" "${SHADER_SOURCE_DIR}/constant.slang"
        "${SHADER_SOURCE_DIR}/random.slang" "${SHADER_SOURCE_DIR}/point_light_sampling.slang"
        "${SHADER_SOURCE_DIR}/rect_area_light_sampling.slang"
        "${SHADER_SOURCE_DIR}/material_data.slang"
        "${SHADER_SOURCE_DIR}/normal_mapping.slang" "${SHADER_SOURCE_DIR}/pbr_brdf.slang"
        "${SHADER_SOURCE_DIR}/brdf.slang")
renderlab_shader(temporal_reprojection raytracing/temporal_reprojection.slang temporal_reprojection
    ENTRIES computeMain INCLUDES "${SHADER_SOURCE_DIR}/scene_uniforms.slang")
renderlab_shader_manifests()

add_library(RenderLabRuntime STATIC
    src/render/offline/OfflinePathTracingPass.cpp
    src/render/offline/OfflinePathTracingPass.h
    src/editor/Inspector.cpp
    src/editor/EditorBackend.cpp
    src/editor/EnvironmentImportTask.cpp
    src/editor/AssetImportPanel.cpp
    src/editor/SceneHierarchy.cpp
    src/editor/RendererSettings.cpp
    src/render/feature/shadow/ShadowData.cpp
    src/render/device/DeviceLayouts.cpp
        src/Engine.cpp
        src/Engine.h
        src/core/ConfigManager.cpp
        src/core/ConfigManager.h
        src/render/RendererConfig.h
        src/core/Annotations.h
        src/core/Context.cpp
        src/core/Context.h
        src/core/Logger.h
        src/core/Debug.h
        src/core/Reflect.h
        src/core/Serializer.h
        src/core/Window.cpp
        src/core/Window.h
        src/core/Input.h
        src/core/InputManager.h
        src/core/InputManager.cpp
        src/scene/Component.h
        src/scene/Component.cpp
        src/scene/component/CameraComponent.h
        src/scene/component/LightComponent.h
        src/scene/component/LightComponent.cpp
        src/scene/component/RenderComponent.h
        src/scene/component/RenderComponent.cpp
        src/scene/component/TransformComponent.cpp
        src/scene/component/TransformComponent.h
        src/scene/component/FreeFlyMoveComponent.h
        src/scene/component/CharacterMoveComponent.h
        src/scene/SceneManager.cpp
        src/scene/SceneManager.h
        src/scene/SceneTypes.h
        src/scene/AActor.cpp
        src/scene/AActor.h
        src/scene/actor/AFreeFlyCamera.cpp
        src/scene/actor/AFreeFlyCamera.h
        src/scene/actor/ALight.cpp
        src/scene/actor/ALight.h
        src/scene/actor/AStaticMesh.cpp
        src/scene/actor/AStaticMesh.h
        src/scene/component/FreeFlyMoveComponent.cpp
        src/scene/component/CharacterMoveComponent.cpp
        src/scene/component/CameraComponent.cpp
        src/editor/Editor.cpp
        src/editor/EditorCamera.cpp
        src/editor/EditorCamera.h
        src/editor/EditorCameraConfig.h
        src/editor/Editor.h
        src/editor/ComponentDraw.cpp
        src/editor/ComponentDraw.h
        src/editor/MaterialExtensionsDraw.cpp
        src/editor/MaterialExtensionsDraw.h
        src/editor/EditorUi.cpp
        src/editor/EditorUi.h
        src/editor/MeshImportPreview.cpp
        src/editor/MeshImportPreview.h
        src/render/ViewportRect.h
        src/render/RenderView.h
        src/render/ViewportRect.cpp
        src/render/feature/editor/EditorFrameInput.h
        src/asset/AssetManager.cpp
        src/asset/Asset.h
        src/asset/AssetID.h
        src/asset/ImageFormat.h
        src/asset/import/ImportImage.cpp
        src/asset/import/EnvironmentMapUtils.cpp
        src/asset/import/EnvironmentMapUtils.h
        src/asset/import/EnvironmentMapBake.cpp
        src/asset/import/ImportTexture.cpp
        src/asset/import/ImportEnvironmentMap.cpp
        src/asset/import/ImportId.h
        src/asset/MeshGeometry.h
        src/asset/MeshGeometry.cpp
        src/asset/BuiltinAssets.h
        src/asset/builtin/BuiltinMesh.cpp
        src/asset/builtin/BuiltinMaterial.cpp
        src/asset/builtin/BuiltinTexture.cpp
        src/asset/builtin/BuiltinSampler.cpp
        src/asset/import/ImportMesh.cpp
        src/asset/import/ImportScene.cpp
        src/asset/import/GltfImport.h
        src/asset/import/GltfImport.cpp
        src/asset/import/AssimpImport.h
        src/asset/import/AssimpImport.cpp
        src/asset/import/ModelImport.h
        src/asset/import/GltfMaterialExtensions.h
        src/asset/import/GltfMaterialExtensions.cpp
        src/core/Vertex.h
        src/platform/InputMethod.cpp
        src/platform/InputMethod.h
        src/render/Renderer.cpp
        src/render/Renderer.h
        src/render/RenderConfig.h
        src/render/ShaderManager.cpp
        src/render/ShaderManager.h
        src/render/device/Device.cpp
        src/render/device/Device.h
        src/render/device/Memory.h
        src/render/device/VkCheck.h
        src/render/device/VulkanContext.cpp
        src/render/device/VulkanContext.h
        src/render/device/FrameContext.cpp
        src/render/device/FrameContext.h
        src/render/FrameData.cpp
        src/render/FrameData.h
        src/render/RenderContext.cpp
        src/render/RenderContext.h
        src/render/device/GpuUploadContext.h
        src/render/present/Swapchain.cpp
        src/render/present/Swapchain.h
        src/render/resource/Buffer.cpp
        src/render/resource/Buffer.h
        src/render/resource/GpuTexture.cpp
        src/render/resource/GpuTexture.h
        src/render/resource/GpuShader.cpp
        src/render/resource/GpuShader.h
        src/render/resource/GpuMesh.cpp
        src/render/resource/GpuMesh.h
        src/render/resource/RenderResourceManager.cpp
        src/render/resource/RenderResourceManager.h
        src/render/resource/VertexLayout.h
        src/render/resource/ShaderData.h
        src/scene/system/RenderSystem.cpp
        src/scene/system/RenderSystem.h
        src/render/RenderData.h
        src/render/DrawItem.h
        src/render/Visibility.h
        src/render/feature/ScenePass.cpp
        src/render/feature/ScenePass.h
        src/render/feature/SceneBindings.cpp
        src/render/feature/SceneBindings.h
        src/render/feature/deferred/GBufferPass.cpp
        src/render/feature/deferred/GBufferPass.h
        src/render/feature/deferred/DeferredLightingPass.cpp
        src/render/feature/deferred/DeferredLightingPass.h
        src/render/feature/SceneDraw.cpp
        src/render/feature/SceneDraw.h
        src/render/feature/SkyboxPass.cpp
        src/render/feature/SkyboxPass.h
        src/render/feature/TransparentPass.cpp
        src/render/feature/TransparentPass.h
        src/render/feature/DepthPrepass.cpp
        src/render/feature/DepthPrepass.h
        src/render/feature/postprocess/ToneMappingPass.cpp
        src/render/feature/postprocess/BloomPass.h
        src/render/feature/postprocess/BloomPass.cpp
        src/render/feature/postprocess/ToneMappingPass.h
        src/render/raytracing/AccelerationStructure.cpp
        src/render/raytracing/AccelerationStructure.h
        src/render/raytracing/BlasBuildPass.cpp
        src/render/raytracing/BlasBuildPass.h
        src/render/raytracing/TlasBuildPass.cpp
        src/render/raytracing/TlasBuildPass.h
        src/render/raytracing/RayTracingPass.cpp
        src/render/raytracing/RayTracingPass.h
        src/render/raytracing/RayTracingData.h
        src/render/resource/MaterialData.h
        src/render/resource/MaterialTable.h
        src/render/resource/MaterialTable.cpp
        src/render/raytracing/TemporalReprojectionPass.h
        src/render/raytracing/TemporalReprojectionPass.cpp
        src/render/raytracing/RayTracingBlitPass.cpp
        src/render/raytracing/RayTracingBlitPass.h
        src/render/feature/shadow/ShadowPass.cpp
        src/render/feature/shadow/ShadowPass.h
        src/editor/LightMarkers.cpp
        src/editor/LightMarkers.h
        src/render/feature/editor/EditorShaderData.h
        src/render/feature/editor/EditorDepthPass.cpp
        src/render/feature/editor/EditorDepthPass.h
        src/render/feature/editor/LightMarkerPass.cpp
        src/render/feature/editor/LightMarkerPass.h
        src/render/feature/editor/EditorPickingPass.cpp
        src/render/feature/editor/EditorPickingPass.h
        src/render/feature/editor/EditorPickingCopyPass.cpp
        src/render/feature/editor/EditorPickingCopyPass.h
        src/asset/AssetImporter.h
        src/asset/AssetDataManager.cpp
        src/asset/AssetDataManager.h
        src/asset/TextureMip.h
        src/asset/TextureMip.cpp
        src/core/math/Sampler.cpp
        src/core/math/Bounds.cpp
        src/core/time/Timer.cpp
        src/core/profiling/Profiler.cpp
        src/editor/PerformancePanel.cpp
        src/render/device/GpuProfiler.cpp
        src/core/math/Sampler.h
        src/render/feature/RenderGraph.cpp
        src/render/feature/RenderGraph.h
        src/render/feature/PassInputs.h
        src/render/feature/RenderGraphSetup.cpp
        src/render/feature/EndPass.cpp
        src/render/feature/EndPass.h
        src/render/feature/editor/EditorUiPass.cpp
        src/render/feature/editor/EditorUiPass.h
        src/render/Image.cpp
        src/render/Image.h
)
add_dependencies(RenderLabRuntime RenderLabShaders)

add_executable(RenderLab src/Main.cpp)
target_link_libraries(RenderLab PRIVATE RenderLabRuntime)

target_include_directories(RenderLabRuntime PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${TINYGLTF_INCLUDE_DIR}"
)

target_compile_definitions(RenderLabRuntime PUBLIC
        RENDERLAB_CONFIG_FILE="${CMAKE_CURRENT_SOURCE_DIR}/config/config.json"
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
        VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
        VULKAN_HPP_NO_EXCEPTIONS
        VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLOG_NO_ABBREVIATED_SEVERITIES
)

target_link_libraries(RenderLabRuntime PUBLIC
        mikktspace::mikktspace
        assimp::assimp
        RenderLabBuildOptions
        Vulkan::Vulkan
        glfw
        glm::glm
        imgui::imgui
        imguizmo::imguizmo
        nlohmann_json::nlohmann_json
        glog::glog
        unofficial::tinyexr::tinyexr
        comdlg32
)

# Only stable dependency headers belong here; project headers stay outside the PCH.
target_precompile_headers(RenderLabRuntime PRIVATE
        <algorithm>
        <array>
        <cstdint>
        <filesystem>
        <format>
        <memory>
        <optional>
        <string>
        <unordered_map>
        <utility>
        <vector>
        <glm/glm.hpp>
        <glm/gtc/quaternion.hpp>
        <nlohmann/json.hpp>
        <vulkan/vulkan_raii.hpp>
)

include(CTest)
renderlab_copy_compiler_runtime(RenderLab)
if (BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    add_subdirectory(test)
endif ()
