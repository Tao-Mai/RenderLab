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

function(renderlab_copy_runtime_libraries target)
    if (WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Python3::Python>" "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
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
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter Development.Embed)
execute_process(
    COMMAND "${Python3_EXECUTABLE}" -I -X utf8 -c "from pathlib import Path; import sys; print(Path(sys.base_prefix).as_posix())"
    OUTPUT_VARIABLE RENDERLAB_PYTHON_HOME
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ENCODING UTF-8
    COMMAND_ERROR_IS_FATAL ANY
)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(glog CONFIG REQUIRED)
find_package(tinyexr CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)
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
set_property(SOURCE src/render/resources/GpuShader.cpp APPEND PROPERTY
    COMPILE_DEFINITIONS RENDERLAB_BUILD_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
include(cmake/Shaders.cmake)

find_package(ZLIB REQUIRED)
add_subdirectory(src/asset/pbrt)

add_library(RenderLabCore STATIC
    src/core/ConfigManager.cpp
    src/core/ConfigManager.h
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
    src/scene/component/OfflineLightComponents.cpp
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
    src/asset/AssetManager.cpp
    src/asset/AssetPersistence.cpp
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
    src/asset/import/GltfLoader.cpp
    src/asset/import/AssimpImport.h
    src/asset/import/AssimpImport.cpp
    src/asset/import/ModelImport.h
    src/asset/import/GltfMaterialExtensions.h
    src/asset/import/GltfMaterialExtensions.cpp
    src/core/Vertex.h
    src/scene/system/RenderSystem.cpp
    src/scene/system/RenderSystem.h
    src/asset/AssetImporter.h
    src/asset/AssetDataManager.cpp
    src/asset/AssetDataManager.h
    src/asset/TextureMip.h
    src/asset/TextureMip.cpp
    src/core/math/Sampler.cpp
    src/core/math/Bounds.cpp
    src/core/time/Timer.cpp
    src/core/profiling/Profiler.cpp
    src/core/math/Sampler.h
    src/render/DrawItem.h
    src/render/FrameData.cpp
    src/render/FrameData.h
    src/render/RenderConfig.h
    src/render/RenderContext.cpp
    src/render/RenderContext.h
    src/render/RenderData.h
    src/render/RendererConfig.h
    src/render/ExposureConfig.h
    src/render/RenderView.h
    src/render/ViewportRect.cpp
    src/render/ViewportRect.h
    src/render/Visibility.h
    src/render/device/Device.cpp
    src/render/device/Device.h
    src/render/device/DeviceLayouts.cpp
    src/render/device/FrameContext.cpp
    src/render/device/FrameContext.h
    src/render/device/GpuProfiler.cpp
    src/render/device/GpuProfiler.h
    src/render/device/GpuUploadContext.h
    src/render/device/Memory.h
    src/render/device/Swapchain.cpp
    src/render/device/Swapchain.h
    src/render/device/VkCheck.h
    src/render/device/VulkanContext.cpp
    src/render/device/VulkanContext.h
    src/render/resources/Buffer.cpp
    src/render/resources/Buffer.h
    src/render/resources/GpuMesh.cpp
    src/render/resources/GpuMesh.h
    src/render/resources/GpuShader.cpp
    src/render/resources/GpuShader.h
    src/render/resources/GpuTexture.cpp
    src/render/resources/GpuTexture.h
    src/render/resources/Image.cpp
    src/render/resources/Image.h
    src/render/resources/MaterialData.h
    src/render/resources/MaterialTable.cpp
    src/render/resources/MaterialTable.h
    src/render/resources/RenderResourceManager.cpp
    src/render/resources/RenderResourceManager.h
    src/render/resources/ShaderData.h
    src/render/resources/ShaderManager.cpp
    src/render/resources/ShaderManager.h
    src/render/graph/EndPass.cpp
    src/render/graph/EndPass.h
    src/render/graph/PassInputs.h
    src/render/graph/RenderGraph.cpp
    src/render/graph/RenderGraph.h
    src/render/graph/RenderGraphSetup.cpp
    src/render/raytracing/AccelerationStructure.cpp
    src/render/raytracing/AccelerationStructure.h
    src/render/raytracing/offline/OfflineLightData.h
    src/render/raytracing/offline/OfflineShaderTypes.h
    src/render/postprocess/FalcorPostProcessConfig.cpp
    src/render/postprocess/FalcorPostProcessConfig.h
    src/render/editor/EditorFrameInput.h
    src/render/editor/EditorShaderData.h
    src/render/graph/RenderPassRegistry.cpp
    src/render/graph/RenderPassRegistry.h
)
target_sources(RenderLabCore PRIVATE
    src/asset/pbrt/PbrtImport.cpp
    src/asset/pbrt/PbrtData.cpp
    src/asset/pbrt/PbrtMaterial.cpp
    src/asset/pbrt/PbrtSpectrum.cpp
    src/asset/pbrt/PbrtLight.cpp
    src/asset/pbrt/PbrtImage.cpp
    src/asset/pbrt/PbrtTexture.cpp
    src/asset/pbrt/PbrtMedium.cpp
    src/asset/pbrt/PbrtSceneConversion.cpp
    src/asset/pbrt/PbrtMesh.cpp
    src/asset/builtin/PrimitiveMesh.cpp
    src/asset/pbrt/upstream/PlyReader.cpp
    src/asset/pbrt/upstream/PfmReader.cpp
    src/asset/pbrt/upstream/rply/rply.cpp
)
add_library(RenderLabDds OBJECT src/asset/import/ImportDds.cpp)
target_include_directories(RenderLabDds PRIVATE src)
target_compile_definitions(RenderLabDds PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE GLOG_NO_ABBREVIATED_SEVERITIES)
target_link_libraries(RenderLabDds PRIVATE RenderLabBuildOptions glm::glm glog::glog Microsoft::DirectXTex)
target_link_libraries(RenderLabCore PRIVATE ZLIB::ZLIB RenderLabDds)

target_include_directories(RenderLabCore PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${TINYGLTF_INCLUDE_DIR}"
)

target_compile_definitions(RenderLabCore PUBLIC
        RENDERLAB_CONFIG_FILE="${CMAKE_CURRENT_SOURCE_DIR}/config/config.json"
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
        VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
        VULKAN_HPP_NO_EXCEPTIONS
        VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLOG_NO_ABBREVIATED_SEVERITIES
)

target_link_libraries(RenderLabCore PUBLIC
        RenderLabPbrtFrontend
        mikktspace::mikktspace
        assimp::assimp
        RenderLabBuildOptions
        Vulkan::Vulkan
        glfw
        glm::glm
        nlohmann_json::nlohmann_json
        glog::glog
        unofficial::tinyexr::tinyexr
)

# Only stable dependency headers belong here; project headers stay outside the PCH.
target_precompile_headers(RenderLabCore PRIVATE
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
add_subdirectory(passes)
renderlab_build_shaders()
add_dependencies(RenderLabPasses RenderLabShaders)

add_library(RenderLabRuntime STATIC
    src/editor/Inspector.cpp
    src/editor/EditorBackend.cpp
    src/editor/SceneHierarchy.cpp
    src/editor/RendererSettings.cpp
    src/Engine.cpp
    src/Engine.h
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
    src/platform/InputMethod.cpp
    src/platform/InputMethod.h
    src/editor/LightMarkers.cpp
    src/editor/LightMarkers.h
    src/editor/PerformancePanel.cpp
    src/render/Renderer.cpp
    src/render/Renderer.h
    src/render/graph/PythonGraph.cpp
    src/render/graph/PythonGraph.h
)
target_link_libraries(RenderLabRuntime PUBLIC RenderLabCore RenderLabPasses PRIVATE
    imgui::imgui imguizmo::imguizmo Python3::Python)
set_property(SOURCE src/render/graph/PythonGraph.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    RENDERLAB_PYTHON_HOME=L"${RENDERLAB_PYTHON_HOME}"
    RENDERLAB_GRAPH_LIBRARY_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/graphs")
target_precompile_headers(RenderLabRuntime REUSE_FROM RenderLabCore)

add_executable(RenderLab src/Main.cpp)
target_link_libraries(RenderLab PRIVATE RenderLabRuntime)
target_precompile_headers(RenderLab REUSE_FROM RenderLabCore)

option(BUILD_TESTING "Enable manual validation tools" OFF)
renderlab_copy_runtime_libraries(RenderLab)
if (BUILD_TESTING)
    add_subdirectory(test EXCLUDE_FROM_ALL)
endif ()
