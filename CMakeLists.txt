cmake_minimum_required(VERSION 4.2)

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

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(RENDERLAB_RUNTIME_DIRECTORY "${CMAKE_BINARY_DIR}")
if(CMAKE_CONFIGURATION_TYPES)
    string(APPEND RENDERLAB_RUNTIME_DIRECTORY "/$<CONFIG>")
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${RENDERLAB_RUNTIME_DIRECTORY}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${RENDERLAB_RUNTIME_DIRECTORY}")

if (WIN32)
    if (MSVC)
        set(renderlab_windows_triplet "x64-windows")
    elseif (MINGW)
        set(renderlab_windows_triplet "x64-mingw-dynamic")
    endif ()
    if (NOT DEFINED renderlab_windows_triplet OR NOT VCPKG_TARGET_TRIPLET STREQUAL renderlab_windows_triplet)
        message(FATAL_ERROR
            "Compiler ${CMAKE_CXX_COMPILER_ID} and vcpkg triplet '${VCPKG_TARGET_TRIPLET}' do not match. "
            "Use '${renderlab_windows_triplet}' for this compiler in a fresh build directory.")
    endif ()
endif ()

if (MSVC)
    set(renderlab_cxx_options /EHsc /utf-8 /Zc:preprocessor)
    set(renderlab_cxx_definitions NOMINMAX)
else ()
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
    set(renderlab_cxx_options -fexceptions)
endif ()

add_library(RenderLabBuildOptions INTERFACE)
target_compile_features(RenderLabBuildOptions INTERFACE cxx_std_23)
target_compile_options(RenderLabBuildOptions INTERFACE
        "$<$<COMPILE_LANGUAGE:CXX>:${renderlab_cxx_options}>")
target_compile_definitions(RenderLabBuildOptions INTERFACE
        "$<$<COMPILE_LANGUAGE:CXX>:${renderlab_cxx_definitions}>" JSON_NOEXCEPTION TINYGLTF_NOEXCEPTION)

get_filename_component(RENDERLAB_COMPILER_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

function(renderlab_copy_runtime_libraries target)
    if (MINGW)
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

set(SLANG_RHI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SLANG_RHI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SLANG_RHI_INSTALL OFF CACHE BOOL "" FORCE)
set(SLANG_RHI_ENABLE_CPU OFF CACHE BOOL "" FORCE)
set(SLANG_RHI_ENABLE_D3D11 OFF CACHE BOOL "" FORCE)
set(SLANG_RHI_ENABLE_D3D12 OFF CACHE BOOL "" FORCE)
set(SLANG_RHI_ENABLE_CUDA OFF CACHE BOOL "" FORCE)
set(SLANG_RHI_ENABLE_OPTIX OFF CACHE BOOL "" FORCE)
set(SLANG_RHI_ENABLE_WGPU OFF CACHE BOOL "" FORCE)
set(SLANG_RHI_ENABLE_NVAPI OFF CACHE BOOL "" FORCE)
set(SLANG_RHI_ENABLE_AGILITY_SDK OFF CACHE BOOL "" FORCE)
add_subdirectory(external/slang-rhi)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(glog CONFIG REQUIRED)
find_package(tinyexr CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)
find_package(mikktspace CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_path(TINYGLTF_INCLUDE_DIR NAMES tiny_gltf.h REQUIRED)

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/render")
set(SHADER_BINARY_DIR "${RENDERLAB_RUNTIME_DIRECTORY}/shaders")
include(cmake/Shaders.cmake)

find_package(ZLIB REQUIRED)

include(FetchContent)
set(REFLECTCPP_BUILD_SHARED OFF)
FetchContent_Declare(reflectcpp
    URL https://codeload.github.com/getml/reflect-cpp/tar.gz/refs/tags/v0.27.1
    URL_HASH SHA256=c50b221080687b42f852b58e651441d8130107ece17312218e5d31ce081206ea
)
FetchContent_MakeAvailable(reflectcpp)

add_library(RenderLabCore STATIC
    src/core/Config.h
    src/core/Context.cpp
    src/core/Context.h
    src/core/Logger.h
    src/core/ds/DirectedGraph.h
    src/core/ds/DirectedGraphTraversal.h
    src/core/Json.h
    src/scene/Camera.cpp
    src/scene/SceneManager.cpp
    src/scene/SceneLayer.h
    src/scene/Transform.cpp
    src/asset/AssetManager.cpp
    src/asset/Asset.h
    src/asset/StandardMaterial.cpp
    src/asset/StandardMaterial.h
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
    src/asset/AssetImporter.h
    src/asset/TextureMip.h
    src/asset/TextureMip.cpp
    src/core/math/Bounds.cpp
    src/core/utils/NonCopyable.h
    src/core/utils/Timer.cpp
    src/render/DispatchGroups.h
    src/render/RenderContext.h
    src/render/RhiUtils.h
    src/render/GpuScene.cpp
    src/render/GpuScene.h
    src/render/resources/TextureUpload.cpp
    src/render/resources/TextureUpload.h
    src/render/resources/GpuMesh.cpp
    src/render/resources/GpuMesh.h
    src/render/resources/StandardMaterialData.h
    src/render/resources/MaterialTable.cpp
    src/render/resources/MaterialTable.h
    src/render/resources/RenderResourceManager.cpp
    src/render/resources/RenderResourceManager.h
    src/render/resources/ShaderData.h
    src/render/resources/ShaderLayout.cpp
    src/render/resources/ShaderLayout.h
    src/render/resources/ShaderManager.cpp
    src/render/resources/ShaderManager.h
    src/render/resources/RayTracingProgram.cpp
    src/render/resources/RayTracingProgram.h
    src/render/resources/RayType.h
    src/render/resources/SceneAccelerationStructures.cpp
    src/render/resources/SceneAccelerationStructures.h
    src/render/graph/RenderGraph.cpp
    src/render/graph/RenderGraph.h
    src/render/graph/RenderGraphExe.cpp
    src/render/graph/RenderGraphExe.h
    src/render/graph/ResourceCache.cpp
    src/render/graph/ResourceCache.h
    src/render/impl/graphs/minimal/MinimalPathTracerGraph.cpp
    src/render/impl/graphs/minimal/MinimalPathTracerGraph.h
    src/render/impl/graphs/restir/RestirPathTracerGraph.cpp
    src/render/impl/graphs/restir/RestirPathTracerGraph.h
    src/render/impl/passes/accumulate/AccumulatePass.cpp
    src/render/impl/passes/debug/DebugPass.cpp
    src/render/impl/passes/minimal/MinimalPathTracer.cpp
    src/render/impl/passes/restir/RestirPathTracer.cpp
    src/render/impl/passes/vbuffer/VBufferPass.cpp
    src/render/impl/postprocess/bloom/Bloom.cpp
    src/render/impl/postprocess/bloom/BloomUi.cpp
    src/render/impl/postprocess/ExposurePass/ExposurePass.cpp
    src/render/impl/postprocess/ExposurePass/ExposurePassUi.cpp
    src/render/impl/postprocess/FalcorPostProcess/FalcorPostProcess.cpp
    src/render/impl/postprocess/FalcorPostProcess/FalcorPostProcessConfig.cpp
    src/render/impl/postprocess/FalcorPostProcess/FalcorPostProcessUi.cpp
    src/render/impl/postprocess/ToneMappingPass/ToneMappingPass.cpp
    src/render/resources/SceneLightConstants.h
    src/render/graph/RenderPass.cpp
    src/render/graph/RenderPassReflection.cpp
    src/render/graph/RenderPassReflection.h
    src/render/graph/RenderPassOptions.h
    src/render/graph/RenderPassStandardFlags.h
    src/render/graph/RenderGraphCompiler.cpp
    src/render/resources/AnalyticLights.cpp
    src/render/resources/AnalyticLights.h
    src/render/resources/EmissiveLights.cpp
    src/render/resources/EmissiveLights.h
    src/editor/Gui.cpp
    src/editor/PassEditor.cpp
)
target_link_libraries(RenderLabCore PRIVATE imgui::imgui)
set(renderlab_gui_backend_includes
    $<TARGET_PROPERTY:imgui::imgui,INTERFACE_INCLUDE_DIRECTORIES>
)
target_include_directories(RenderLabCore SYSTEM PRIVATE ${renderlab_gui_backend_includes})
add_library(RenderLabTextureImport OBJECT
    src/asset/import/ImportDds.cpp
    src/asset/import/MaterialTexture.cpp
    src/asset/import/MaterialTexture.h
)
target_include_directories(RenderLabTextureImport PRIVATE src)
target_compile_definitions(RenderLabTextureImport PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE GLOG_NO_ABBREVIATED_SEVERITIES)
target_link_libraries(RenderLabTextureImport PRIVATE RenderLabBuildOptions glm::glm glog::glog Microsoft::DirectXTex)
target_link_libraries(RenderLabCore PRIVATE ZLIB::ZLIB RenderLabTextureImport nlohmann_json::nlohmann_json)

target_include_directories(RenderLabCore PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${TINYGLTF_INCLUDE_DIR}"
)

target_compile_definitions(RenderLabCore PUBLIC
        RENDERLAB_CONFIG_FILE="${CMAKE_CURRENT_SOURCE_DIR}/config/config.json"
        RENDERLAB_SHADER_DIRECTORY="${SHADER_BINARY_DIR}"
        GLFW_INCLUDE_NONE
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLOG_NO_ABBREVIATED_SEVERITIES
)

target_link_libraries(RenderLabCore PUBLIC
        mikktspace::mikktspace
        assimp::assimp
        RenderLabBuildOptions
        reflectcpp::reflectcpp
        slang-rhi
        glfw
        glm::glm
        glog::glog
        unofficial::tinyexr::tinyexr
)

set(renderlab_pch_headers
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
        <slang-rhi.h>
)
target_precompile_headers(RenderLabCore PRIVATE ${renderlab_pch_headers})
option(BUILD_TESTING "Enable manual validation tools" OFF)
renderlab_build_shaders()
add_dependencies(RenderLabCore RenderLabShaders)

add_library(RenderLabRuntime STATIC
    src/core/Window.cpp
    src/core/Window.h
    src/editor/EditorBackend.cpp
    src/editor/RendererUi.cpp
    src/Engine.cpp
    src/Engine.h
    src/editor/Editor.cpp
    src/editor/EditorCamera.cpp
    src/editor/EditorCamera.h
    src/editor/Editor.h
    src/editor/EditorUi.cpp
    src/editor/EditorUi.h
    src/render/Renderer.cpp
    src/render/Renderer.h
)
target_link_libraries(RenderLabRuntime PUBLIC RenderLabCore)
target_include_directories(RenderLabRuntime SYSTEM PRIVATE ${renderlab_gui_backend_includes})
target_precompile_headers(RenderLabRuntime REUSE_FROM RenderLabCore)

add_executable(RenderLab src/Main.cpp)
target_link_libraries(RenderLab PRIVATE RenderLabRuntime)
target_precompile_headers(RenderLab REUSE_FROM RenderLabCore)

renderlab_copy_runtime_libraries(RenderLabCore)
if (BUILD_TESTING)
    add_subdirectory(test EXCLUDE_FROM_ALL)
endif ()
