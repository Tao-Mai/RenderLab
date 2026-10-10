cmake_minimum_required(VERSION 4.2)

if (CMAKE_HOST_WIN32 AND NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-mingw-dynamic" CACHE STRING "vcpkg target triplet")
endif ()

if (NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
            CACHE FILEPATH "vcpkg toolchain file")
endif ()

project(RenderLab LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16.2)
    message(FATAL_ERROR "RenderLab requires GCC 16.2 or newer with C++26 reflection. Select that compiler in your toolchain.")
endif ()

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
find_path(TINYGLTF_INCLUDE_DIR NAMES tiny_gltf.h REQUIRED)

find_program(SLANGC_EXECUTABLE
        NAMES slangc
        HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
        REQUIRED
)

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(SLANG_DEPTH_SHADER "${SHADER_BINARY_DIR}/depth.spv")
set(SLANG_DEPTH_REFLECTION "${SHADER_BINARY_DIR}/depth.reflection.json")
set(SLANG_EDITOR_LIGHT_MARKER_SHADER "${SHADER_BINARY_DIR}/editor_light_marker.spv")
set(SLANG_EDITOR_LIGHT_MARKER_REFLECTION "${SHADER_BINARY_DIR}/editor_light_marker.reflection.json")
set(SLANG_EDITOR_DEPTH_SHADER "${SHADER_BINARY_DIR}/editor_depth.spv")
set(SLANG_EDITOR_DEPTH_REFLECTION "${SHADER_BINARY_DIR}/editor_depth.reflection.json")
set(SLANG_EDITOR_PICKING_SHADER "${SHADER_BINARY_DIR}/editor_picking.spv")
set(SLANG_EDITOR_PICKING_REFLECTION "${SHADER_BINARY_DIR}/editor_picking.reflection.json")
set(SLANG_SKYBOX_SHADER "${SHADER_BINARY_DIR}/skybox.spv")
set(SLANG_SKYBOX_REFLECTION "${SHADER_BINARY_DIR}/skybox.reflection.json")
set(SLANG_TONE_MAPPING_SHADER "${SHADER_BINARY_DIR}/tonemapping.spv")
set(SLANG_TONE_MAPPING_REFLECTION "${SHADER_BINARY_DIR}/tonemapping.reflection.json")
set(SLANG_SHADOW_SHADER "${SHADER_BINARY_DIR}/shadow.spv")
set(SLANG_SHADOW_REFLECTION "${SHADER_BINARY_DIR}/shadow.reflection.json")
set(SLANG_RECT_AREA_SHADOW_SHADER "${SHADER_BINARY_DIR}/shadow.rect_area.spv")
set(SLANG_RECT_AREA_SHADOW_REFLECTION "${SHADER_BINARY_DIR}/shadow.rect_area.reflection.json")
set(SLANG_DIRECTIONAL_SHADOW_SHADER "${SHADER_BINARY_DIR}/shadow.directional.spv")
set(SLANG_DIRECTIONAL_SHADOW_REFLECTION "${SHADER_BINARY_DIR}/shadow.directional.reflection.json")

set(SCENE_SHADER_OUTPUTS)
set(SCENE_SHADER_INCLUDES
        "${SHADER_SOURCE_DIR}/scene_data.slang"
        "${SHADER_SOURCE_DIR}/light_data.slang"
        "${SHADER_SOURCE_DIR}/mesh_vertex.slang"
        "${SHADER_SOURCE_DIR}/mesh_surface.slang"
        "${SHADER_SOURCE_DIR}/surface.slang"
        "${SHADER_SOURCE_DIR}/material_data.slang"
        "${SHADER_SOURCE_DIR}/scene_lighting.slang"
        "${SHADER_SOURCE_DIR}/sampling.slang"
        "${SHADER_SOURCE_DIR}/brdf.slang"
        "${SHADER_SOURCE_DIR}/constant.slang"
)
foreach (enable_clearcoat IN ITEMS 0 1)
    foreach (enable_ibl IN ITEMS 1 0)
        set(scene_name "slang")
        if (enable_clearcoat)
            string(APPEND scene_name ".clearcoat")
        endif ()
        if (NOT enable_ibl)
            string(APPEND scene_name ".no_ibl")
        endif ()
        set(scene_binary "${SHADER_BINARY_DIR}/${scene_name}.spv")
        set(scene_reflection "${SHADER_BINARY_DIR}/${scene_name}.reflection.json")
        list(APPEND SCENE_SHADER_OUTPUTS "${scene_binary}" "${scene_reflection}")

        add_custom_command(
                OUTPUT "${scene_binary}" "${scene_reflection}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BINARY_DIR}"
                COMMAND ${SLANGC_EXECUTABLE}
                "${SHADER_SOURCE_DIR}/shader.slang"
                -DENABLE_IBL=${enable_ibl}
                -DENABLE_CLEARCOAT=${enable_clearcoat}
                -target spirv
                -profile spirv_1_4
                -matrix-layout-column-major
                -emit-spirv-directly
                -fvk-use-entrypoint-name
                -entry vertMain
                -entry fragMain
                -reflection-json "${scene_reflection}"
                -o "${scene_binary}"
                DEPENDS "${SHADER_SOURCE_DIR}/shader.slang"
                ${SCENE_SHADER_INCLUDES}
                COMMENT "Compiling Slang scene shader (ENABLE_IBL=${enable_ibl}, ENABLE_CLEARCOAT=${enable_clearcoat})"
                VERBATIM
        )
    endforeach ()
endforeach ()

set(DEFERRED_SHADER_OUTPUTS)
foreach (shader IN ITEMS gbuffer deferred_lighting deferred_lighting.no_ibl)
    if (shader STREQUAL "gbuffer")
        set(shader_source "${SHADER_SOURCE_DIR}/deferred/gbuffer.slang")
    else ()
        set(shader_source "${SHADER_SOURCE_DIR}/deferred/lighting.slang")
    endif ()
    if (shader STREQUAL "deferred_lighting.no_ibl")
        set(enable_ibl 0)
    else ()
        set(enable_ibl 1)
    endif ()
    set(binary "${SHADER_BINARY_DIR}/${shader}.spv")
    set(reflection "${SHADER_BINARY_DIR}/${shader}.reflection.json")
    list(APPEND DEFERRED_SHADER_OUTPUTS "${binary}" "${reflection}")
    add_custom_command(
            OUTPUT "${binary}" "${reflection}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BINARY_DIR}"
            COMMAND ${SLANGC_EXECUTABLE} "${shader_source}"
            -DENABLE_IBL=${enable_ibl} -DENABLE_CLEARCOAT=0
            -target spirv -profile spirv_1_4 -matrix-layout-column-major
            -emit-spirv-directly -fvk-use-entrypoint-name
            -entry vertMain -entry fragMain
            -reflection-json "${reflection}" -o "${binary}"
            DEPENDS "${shader_source}" ${SCENE_SHADER_INCLUDES}
            COMMENT "Compiling Slang deferred shader (${shader})"
            VERBATIM
    )
endforeach ()

add_custom_command(
        OUTPUT "${SLANG_DEPTH_SHADER}" "${SLANG_DEPTH_REFLECTION}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BINARY_DIR}"
        COMMAND ${SLANGC_EXECUTABLE} "${SHADER_SOURCE_DIR}/depth.slang"
        -DENABLE_CLEARCOAT=0
        -target spirv -profile spirv_1_4 -matrix-layout-column-major
        -emit-spirv-directly -fvk-use-entrypoint-name
        -entry vertMain -entry fragMain
        -reflection-json "${SLANG_DEPTH_REFLECTION}"
        -o "${SLANG_DEPTH_SHADER}"
        DEPENDS "${SHADER_SOURCE_DIR}/depth.slang"
        "${SHADER_SOURCE_DIR}/mesh_vertex.slang"
        "${SHADER_SOURCE_DIR}/scene_data.slang"
        "${SHADER_SOURCE_DIR}/light_data.slang"
        "${SHADER_SOURCE_DIR}/material_data.slang"
        COMMENT "Compiling Slang depth prepass shader"
        VERBATIM
)

add_custom_command(
        OUTPUT "${SLANG_EDITOR_LIGHT_MARKER_SHADER}" "${SLANG_EDITOR_LIGHT_MARKER_REFLECTION}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BINARY_DIR}"
        COMMAND ${SLANGC_EXECUTABLE}
        "${SHADER_SOURCE_DIR}/editor/light_marker.slang"
        -target spirv
        -profile spirv_1_4
        -matrix-layout-column-major
        -emit-spirv-directly
        -fvk-use-entrypoint-name
        -entry vertMain
        -entry fragMain
        -reflection-json "${SLANG_EDITOR_LIGHT_MARKER_REFLECTION}"
        -o "${SLANG_EDITOR_LIGHT_MARKER_SHADER}"
        DEPENDS "${SHADER_SOURCE_DIR}/editor/light_marker.slang"
        "${SHADER_SOURCE_DIR}/editor/view.slang"
        COMMENT "Compiling editor light marker Slang shader"
        VERBATIM
)

add_custom_command(
        OUTPUT "${SLANG_EDITOR_PICKING_SHADER}" "${SLANG_EDITOR_PICKING_REFLECTION}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BINARY_DIR}"
        COMMAND ${SLANGC_EXECUTABLE}
        "${SHADER_SOURCE_DIR}/editor/picking.slang"
        -target spirv
        -profile spirv_1_4
        -matrix-layout-column-major
        -emit-spirv-directly
        -fvk-use-entrypoint-name
        -entry vertMain
        -entry fragMain
        -reflection-json "${SLANG_EDITOR_PICKING_REFLECTION}"
        -o "${SLANG_EDITOR_PICKING_SHADER}"
        DEPENDS "${SHADER_SOURCE_DIR}/editor/picking.slang"
        "${SHADER_SOURCE_DIR}/editor/view.slang"
        COMMENT "Compiling editor picking Slang shader"
        VERBATIM
)

add_custom_command(
        OUTPUT "${SLANG_EDITOR_DEPTH_SHADER}" "${SLANG_EDITOR_DEPTH_REFLECTION}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BINARY_DIR}"
        COMMAND ${SLANGC_EXECUTABLE} "${SHADER_SOURCE_DIR}/editor/depth.slang"
        -target spirv -profile spirv_1_4 -matrix-layout-column-major
        -emit-spirv-directly -fvk-use-entrypoint-name
        -entry vertMain -entry fragMain
        -reflection-json "${SLANG_EDITOR_DEPTH_REFLECTION}"
        -o "${SLANG_EDITOR_DEPTH_SHADER}"
        DEPENDS "${SHADER_SOURCE_DIR}/editor/depth.slang"
        "${SHADER_SOURCE_DIR}/editor/view.slang"
        COMMENT "Compiling editor depth Slang shader"
        VERBATIM
)

add_custom_command(
        OUTPUT "${SLANG_SKYBOX_SHADER}" "${SLANG_SKYBOX_REFLECTION}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BINARY_DIR}"
        COMMAND ${SLANGC_EXECUTABLE}
        "${SHADER_SOURCE_DIR}/skybox.slang"
        -target spirv
        -profile spirv_1_4
        -matrix-layout-column-major
        -emit-spirv-directly
        -fvk-use-entrypoint-name
        -entry vertMain
        -entry fragMain
        -reflection-json "${SLANG_SKYBOX_REFLECTION}"
        -o "${SLANG_SKYBOX_SHADER}"
        DEPENDS "${SHADER_SOURCE_DIR}/skybox.slang"
        "${SHADER_SOURCE_DIR}/scene_data.slang"
        "${SHADER_SOURCE_DIR}/light_data.slang"
        COMMENT "Compiling skybox Slang shader"
        VERBATIM
)

add_custom_command(
        OUTPUT "${SLANG_TONE_MAPPING_SHADER}" "${SLANG_TONE_MAPPING_REFLECTION}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BINARY_DIR}"
        COMMAND ${SLANGC_EXECUTABLE} "${SHADER_SOURCE_DIR}/postprocess/tonemapping.slang"
        -target spirv -profile spirv_1_4 -matrix-layout-column-major
        -emit-spirv-directly -fvk-use-entrypoint-name
        -entry vertMain -entry fragMain
        -reflection-json "${SLANG_TONE_MAPPING_REFLECTION}"
        -o "${SLANG_TONE_MAPPING_SHADER}"
        DEPENDS "${SHADER_SOURCE_DIR}/postprocess/tonemapping.slang"
        COMMENT "Compiling Slang tone mapping shader"
        VERBATIM
)

foreach (shadow_type IN ITEMS 0 1 2)
    if (shadow_type EQUAL 0)
        set(shadow_binary "${SLANG_SHADOW_SHADER}")
        set(shadow_reflection "${SLANG_SHADOW_REFLECTION}")
    elseif (shadow_type EQUAL 1)
        set(shadow_binary "${SLANG_DIRECTIONAL_SHADOW_SHADER}")
        set(shadow_reflection "${SLANG_DIRECTIONAL_SHADOW_REFLECTION}")
    else ()
        set(shadow_binary "${SLANG_RECT_AREA_SHADOW_SHADER}")
        set(shadow_reflection "${SLANG_RECT_AREA_SHADOW_REFLECTION}")
    endif ()
    add_custom_command(
            OUTPUT "${shadow_binary}" "${shadow_reflection}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BINARY_DIR}"
            COMMAND ${SLANGC_EXECUTABLE}
            "${SHADER_SOURCE_DIR}/shadow.slang"
            -DSHADOW_TYPE=${shadow_type}
            -target spirv
            -profile spirv_1_4
            -matrix-layout-column-major
            -emit-spirv-directly
            -fvk-use-entrypoint-name
            -entry vertMain
            -entry fragMain
            -reflection-json "${shadow_reflection}"
            -o "${shadow_binary}"
            DEPENDS "${SHADER_SOURCE_DIR}/shadow.slang"
            "${SHADER_SOURCE_DIR}/scene_data.slang"
            "${SHADER_SOURCE_DIR}/light_data.slang"
            COMMENT "Compiling shadow Slang shader (SHADOW_TYPE=${shadow_type})"
            VERBATIM
    )
endforeach ()

add_custom_target(RenderLabShaders DEPENDS
        ${DEFERRED_SHADER_OUTPUTS}
        ${SCENE_SHADER_OUTPUTS}
        "${SLANG_DEPTH_SHADER}"
        "${SLANG_DEPTH_REFLECTION}"
        "${SLANG_EDITOR_LIGHT_MARKER_SHADER}"
        "${SLANG_EDITOR_LIGHT_MARKER_REFLECTION}"
        "${SLANG_EDITOR_DEPTH_SHADER}"
        "${SLANG_EDITOR_DEPTH_REFLECTION}"
        "${SLANG_EDITOR_PICKING_SHADER}"
        "${SLANG_EDITOR_PICKING_REFLECTION}"
        "${SLANG_SKYBOX_SHADER}"
        "${SLANG_SKYBOX_REFLECTION}"
        "${SLANG_TONE_MAPPING_SHADER}"
        "${SLANG_TONE_MAPPING_REFLECTION}"
        "${SLANG_SHADOW_SHADER}"
        "${SLANG_SHADOW_REFLECTION}"
        "${SLANG_RECT_AREA_SHADOW_SHADER}"
        "${SLANG_RECT_AREA_SHADOW_REFLECTION}"
        "${SLANG_DIRECTIONAL_SHADOW_SHADER}"
        "${SLANG_DIRECTIONAL_SHADOW_REFLECTION}"
        "${SHADER_BINARY_DIR}/raytracing.spv"
)

add_custom_command(
        OUTPUT "${SHADER_BINARY_DIR}/raytracing.spv"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BINARY_DIR}"
        COMMAND ${SLANGC_EXECUTABLE} "${SHADER_SOURCE_DIR}/raytracing/raytracing.slang"
        -target spirv -profile spirv_1_4 -matrix-layout-column-major
        -emit-spirv-directly -fvk-use-entrypoint-name
        -entry raygenMain -entry missMain -entry visibilityMissMain -entry closestHitMain
        -o "${SHADER_BINARY_DIR}/raytracing.spv"
        DEPENDS "${SHADER_SOURCE_DIR}/raytracing/raytracing.slang"
        "${SHADER_SOURCE_DIR}/light_data.slang"
        "${SHADER_SOURCE_DIR}/constant.slang"
        COMMENT "Compiling Slang ray tracing shader"
        VERBATIM
)

add_executable(RenderLab
        src/Main.cpp
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
        src/editor/EditorUi.cpp
        src/editor/EditorUi.h
        src/editor/MeshImportPreview.cpp
        src/editor/MeshImportPreview.h
        src/render/ViewportRect.h
        src/render/RenderView.h
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
        src/core/Vertex.h
        src/platform/InputMethod.cpp
        src/platform/InputMethod.h
        src/render/Renderer.cpp
        src/render/Renderer.h
        src/render/RenderConfig.h
        src/render/ShaderManager.cpp
        src/render/ShaderManager.h
        src/render/DescriptorManager.cpp
        src/render/DescriptorManager.h
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
        src/render/resource/GpuMaterial.cpp
        src/render/resource/GpuMaterial.h
        src/render/resource/RenderResourceManager.cpp
        src/render/resource/RenderResourceManager.h
        src/render/resource/VertexLayout.h
        src/render/resource/ShaderData.h
        src/scene/system/RenderSystem.cpp
        src/scene/system/RenderSystem.h
        src/render/RenderData.h
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
        src/render/feature/postprocess/ToneMappingPass.h
        src/render/raytracing/AccelerationStructure.cpp
        src/render/raytracing/AccelerationStructure.h
        src/render/raytracing/RayTracingPass.cpp
        src/render/raytracing/RayTracingPass.h
        src/render/raytracing/RayTracingBlitPass.cpp
        src/render/raytracing/RayTracingBlitPass.h
        src/render/feature/shadow/ShadowPass.cpp
        src/render/feature/shadow/ShadowPass.h
        src/render/feature/editor/LightMarkers.cpp
        src/render/feature/editor/LightMarkers.h
        src/render/feature/editor/EditorShaderData.h
        src/render/feature/editor/EditorDepthPass.cpp
        src/render/feature/editor/EditorDepthPass.h
        src/render/feature/editor/LightMarkerPass.cpp
        src/render/feature/editor/LightMarkerPass.h
        src/render/feature/editor/EditorPickingPass.cpp
        src/render/feature/editor/EditorPickingPass.h
        src/render/feature/editor/EditorPickingCopyPass.cpp
        src/render/feature/editor/EditorPickingCopyPass.h
        src/render/PipelineManager.cpp
        src/render/PipelineManager.h
        src/asset/AssetImporter.h
        src/asset/AssetDataManager.cpp
        src/asset/AssetDataManager.h
        src/asset/TextureMip.h
        src/asset/TextureMip.cpp
        src/core/math/Sampler.cpp
        src/core/math/Sampler.h
        src/render/feature/RenderGraph.cpp
        src/render/feature/RenderGraph.h
        src/render/feature/RenderGraphSetup.cpp
        src/render/feature/EndPass.cpp
        src/render/feature/EndPass.h
        src/render/feature/editor/EditorUiPass.cpp
        src/render/feature/editor/EditorUiPass.h
        src/render/Image.cpp
        src/render/Image.h
)
add_dependencies(RenderLab RenderLabShaders)

target_include_directories(RenderLab PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${TINYGLTF_INCLUDE_DIR}"
)

target_compile_definitions(RenderLab PRIVATE
        RENDERLAB_CONFIG_FILE="${CMAKE_CURRENT_SOURCE_DIR}/config/config.json"
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
        VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
        VULKAN_HPP_NO_EXCEPTIONS
        VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLOG_NO_ABBREVIATED_SEVERITIES
)

target_link_libraries(RenderLab PRIVATE
        mikktspace::mikktspace
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
target_precompile_headers(RenderLab PRIVATE
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
