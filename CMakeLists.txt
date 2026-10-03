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
target_compile_options(RenderLabBuildOptions INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-freflection>)

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
find_path(TINYGLTF_INCLUDE_DIR NAMES tiny_gltf.h REQUIRED)

find_program(SLANGC_EXECUTABLE
        NAMES slangc
        HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
        REQUIRED
)

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(SLANG_SHADER "${SHADER_BINARY_DIR}/slang.spv")
set(SLANG_SHADER_REFLECTION "${SHADER_BINARY_DIR}/slang.reflection.json")
set(SLANG_LIGHT_SHADER "${SHADER_BINARY_DIR}/light.spv")
set(SLANG_LIGHT_REFLECTION "${SHADER_BINARY_DIR}/light.reflection.json")
set(SLANG_EDITOR_PICKING_SHADER "${SHADER_BINARY_DIR}/editor_picking.spv")
set(SLANG_EDITOR_PICKING_REFLECTION "${SHADER_BINARY_DIR}/editor_picking.reflection.json")
set(SLANG_SKYBOX_SHADER "${SHADER_BINARY_DIR}/skybox.spv")
set(SLANG_SKYBOX_REFLECTION "${SHADER_BINARY_DIR}/skybox.reflection.json")
set(SLANG_SHADOW_SHADER "${SHADER_BINARY_DIR}/shadow.spv")
set(SLANG_SHADOW_REFLECTION "${SHADER_BINARY_DIR}/shadow.reflection.json")

add_custom_command(
        OUTPUT "${SLANG_SHADER}" "${SLANG_SHADER_REFLECTION}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BINARY_DIR}"
        COMMAND ${SLANGC_EXECUTABLE}
        "${SHADER_SOURCE_DIR}/shader.slang"
        -target spirv
        -profile spirv_1_4
        -matrix-layout-column-major
        -emit-spirv-directly
        -fvk-use-entrypoint-name
        -entry vertMain
        -entry fragMain
        -reflection-json "${SLANG_SHADER_REFLECTION}"
        -o "${SLANG_SHADER}"
        DEPENDS "${SHADER_SOURCE_DIR}/shader.slang"
        "${SHADER_SOURCE_DIR}/scene_data.slang"
        COMMENT "Compiling Slang shader"
        VERBATIM
)

add_custom_command(
        OUTPUT "${SLANG_LIGHT_SHADER}" "${SLANG_LIGHT_REFLECTION}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BINARY_DIR}"
        COMMAND ${SLANGC_EXECUTABLE}
        "${SHADER_SOURCE_DIR}/light.slang"
        -target spirv
        -profile spirv_1_4
        -matrix-layout-column-major
        -emit-spirv-directly
        -fvk-use-entrypoint-name
        -entry vertMain
        -entry fragMain
        -reflection-json "${SLANG_LIGHT_REFLECTION}"
        -o "${SLANG_LIGHT_SHADER}"
        DEPENDS "${SHADER_SOURCE_DIR}/light.slang"
        "${SHADER_SOURCE_DIR}/scene_data.slang"
        COMMENT "Compiling light source Slang shader"
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
        "${SHADER_SOURCE_DIR}/scene_data.slang"
        COMMENT "Compiling editor picking Slang shader"
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
        COMMENT "Compiling skybox Slang shader"
        VERBATIM
)

add_custom_command(
        OUTPUT "${SLANG_SHADOW_SHADER}" "${SLANG_SHADOW_REFLECTION}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BINARY_DIR}"
        COMMAND ${SLANGC_EXECUTABLE}
        "${SHADER_SOURCE_DIR}/shadow.slang"
        -target spirv
        -profile spirv_1_4
        -matrix-layout-column-major
        -emit-spirv-directly
        -fvk-use-entrypoint-name
        -entry vertMain
        -entry fragMain
        -reflection-json "${SLANG_SHADOW_REFLECTION}"
        -o "${SLANG_SHADOW_SHADER}"
        DEPENDS "${SHADER_SOURCE_DIR}/shadow.slang"
        "${SHADER_SOURCE_DIR}/scene_data.slang"
        COMMENT "Compiling shadow Slang shader"
        VERBATIM
)

add_custom_target(RenderLabShaders DEPENDS
        "${SLANG_SHADER}"
        "${SLANG_SHADER_REFLECTION}"
        "${SLANG_LIGHT_SHADER}"
        "${SLANG_LIGHT_REFLECTION}"
        "${SLANG_EDITOR_PICKING_SHADER}"
        "${SLANG_EDITOR_PICKING_REFLECTION}"
        "${SLANG_SKYBOX_SHADER}"
        "${SLANG_SKYBOX_REFLECTION}"
        "${SLANG_SHADOW_SHADER}"
        "${SLANG_SHADOW_REFLECTION}"
)

add_executable(RenderLab
        src/Main.cpp
        src/Engine.cpp
        src/Engine.h
        src/core/ConfigManager.cpp
        src/core/ConfigManager.h
        src/core/Annotations.h
        src/core/Context.cpp
        src/core/Context.h
        src/core/Logger.h
        src/core/Reflect.h
        src/core/Reflect.cpp
        src/core/Window.cpp
        src/core/Window.h
        src/core/Input.h
        src/core/InputManager.h
        src/core/InputManager.cpp
        src/scene/component/Component.h
        src/scene/component/CameraComponent.h
        src/scene/component/LightComponent.h
        src/scene/component/RenderComponent.h
        src/scene/component/TransformComponent.cpp
        src/scene/component/TransformComponent.h
        src/scene/component/FreeFlyMoveComponent.h
        src/scene/component/CharacterMoveComponent.h
        src/scene/SceneManager.cpp
        src/scene/SceneManager.h
        src/scene/SceneTypes.h
        src/scene/Actor.cpp
        src/scene/Actor.h
        src/scene/actor/FreeFlyCameraActor.cpp
        src/scene/actor/FreeFlyCameraActor.h
        src/scene/actor/ALight.cpp
        src/scene/actor/ALight.h
        src/scene/actor/StaticMeshActor.cpp
        src/scene/actor/StaticMeshActor.h
        src/scene/component/FreeFlyMoveComponent.cpp
        src/scene/component/CharacterMoveComponent.cpp
        src/scene/component/CameraComponent.cpp
        src/editor/Editor.cpp
        src/editor/Editor.h
        src/editor/ComponentDraw.cpp
        src/editor/ComponentDraw.h
        src/editor/EditorUi.cpp
        src/editor/EditorUi.h
        src/editor/ViewportRect.h
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
        src/asset/JsonIo.h
        src/asset/MeshGeometry.h
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
        src/render/scene/GpuScene.cpp
        src/render/scene/GpuScene.h
        src/render/pass/ScenePass.cpp
        src/render/pass/ScenePass.h
        src/render/pass/ShadowPass.cpp
        src/render/pass/ShadowPass.h
        src/render/pass/LightMarkers.cpp
        src/render/pass/LightMarkers.h
        src/render/pass/EditorPickingPass.cpp
        src/render/pass/EditorPickingPass.h
        src/render/PipelineManager.cpp
        src/render/PipelineManager.h
        src/asset/AssetImporter.h
        src/asset/AssetDataManager.cpp
        src/asset/AssetDataManager.h
        src/asset/TextureMip.h
        src/core/math/Sampler.cpp
        src/core/math/Sampler.h
        src/render/pass/RenderGraph.cpp
        src/render/pass/RenderGraph.h
        src/render/pass/RenderGraphSlots.cpp
        src/render/pass/EditorUiPass.cpp
        src/render/pass/EditorUiPass.h
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

include(CTest)
renderlab_copy_compiler_runtime(RenderLab)
if (BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    add_subdirectory(test)
endif ()
