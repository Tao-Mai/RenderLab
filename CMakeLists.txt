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
    set(renderlab_cxx_options /EHs-c- /utf-8 /Zc:preprocessor)
    set(renderlab_cxx_definitions _HAS_EXCEPTIONS=0 NOMINMAX)
else ()
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
    set(renderlab_cxx_options -fno-exceptions)
endif ()

add_library(RenderLabBuildOptions INTERFACE)
target_compile_features(RenderLabBuildOptions INTERFACE cxx_std_23)
target_compile_options(RenderLabBuildOptions INTERFACE
        "$<$<COMPILE_LANGUAGE:CXX>:${renderlab_cxx_options}>")
target_compile_definitions(RenderLabBuildOptions INTERFACE
        "$<$<COMPILE_LANGUAGE:CXX>:${renderlab_cxx_definitions}>" JSON_NOEXCEPTION TINYGLTF_NOEXCEPTION)

get_filename_component(RENDERLAB_COMPILER_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

function(renderlab_copy_runtime_libraries target)
    if (WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Python3::Python>" "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endif ()
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

find_program(RENDERLAB_SLANGC_EXECUTABLE
        NAMES slangc
        PATHS "${SLANG_RHI_SLANG_BINARY_DIR}/bin"
        NO_DEFAULT_PATH
        NO_CACHE
        REQUIRED
)

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_BINARY_DIR "${RENDERLAB_RUNTIME_DIRECTORY}/shaders")
include(cmake/Shaders.cmake)
include(cmake/Plugins.cmake)
include(cmake/Metadata.cmake)
target_include_directories(RenderLabBuildOptions INTERFACE "${RENDERLAB_GENERATED_DIRECTORY}")

find_package(ZLIB REQUIRED)
add_subdirectory(src/asset/pbrt)

add_library(RenderLabCore SHARED
    src/core/ConfigManager.cpp
    src/core/ConfigManager.h
    src/core/Settings.cpp
    src/core/Settings.h
    src/core/MetadataAnnotations.h
    src/core/platform/OS.h
    src/core/Context.cpp
    src/core/Context.h
    src/core/Logger.cpp
    src/core/Logger.h
    src/core/Macros.h
    src/core/Object.cpp
    src/core/Object.h
    src/core/Dictionary.h
    src/core/Properties.cpp
    src/core/Properties.h
    src/core/DirectedGraph.h
    src/core/DirectedGraphTraversal.h
    src/core/Metadata.h
    src/core/TypeRegistry.h
    src/core/TypeTraits.h
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
    src/scene/component/AreaLightComponents.cpp
    src/scene/component/RenderComponent.h
    src/scene/component/RenderComponent.cpp
    src/scene/component/TransformComponent.cpp
    src/scene/component/TransformComponent.h
    src/scene/component/FreeFlyMoveComponent.h
    src/scene/component/CharacterMoveComponent.h
    src/scene/SceneManager.cpp
    src/scene/SceneManager.h
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
    src/render/RenderConfig.h
    src/render/DispatchGroups.h
    src/render/RenderContext.h
    src/render/PreparedScene.cpp
    src/render/PreparedScene.h
    src/render/RenderView.h
    src/render/ViewportRect.cpp
    src/render/ViewportRect.h
    src/render/Visibility.h
    src/render/device/RhiCheck.h
    src/render/resources/CompiledShaderPath.cpp
    src/render/device/GpuProfiler.cpp
    src/render/device/GpuProfiler.h
    src/render/device/GpuResources.cpp
    src/render/device/GpuResources.h
    src/render/resources/TextureUpload.cpp
    src/render/resources/TextureUpload.h
    src/render/resources/RhiScriptBindings.cpp
    src/render/resources/GpuMesh.cpp
    src/render/resources/GpuMesh.h
    src/render/resources/MaterialData.h
    src/render/resources/MaterialTable.cpp
    src/render/resources/MaterialTable.h
    src/render/resources/RenderResourceManager.cpp
    src/render/resources/RenderResourceManager.h
    src/render/resources/SceneLighting.cpp
    src/render/resources/SceneLighting.h
    src/render/resources/LightRanges.h
    src/render/resources/ShaderData.h
    src/render/resources/PbrtMaterialProgram.h
    src/render/resources/ShaderLayout.cpp
    src/render/resources/ShaderLayout.h
    src/render/resources/ShaderManager.cpp
    src/render/resources/ShaderManager.h
    src/render/graph/RenderGraph.cpp
    src/render/graph/RenderGraph.h
    src/render/graph/RenderGraphFlags.h
    src/render/graph/RenderGraphExe.cpp
    src/render/graph/RenderGraphExe.h
    src/render/graph/ResourceCache.cpp
    src/render/graph/ResourceCache.h
    src/render/raster/MeshDraw.cpp
    src/render/raster/MeshDraw.h
    src/render/raster/RasterLightingSettings.cpp
    src/render/raster/RasterLightingSettings.h
    src/render/raster/ShadowResources.cpp
    src/render/raster/ShadowResources.h
    src/render/raytracing/SceneAccelerationStructures.cpp
    src/render/raytracing/SceneAccelerationStructures.h
    src/render/raytracing/PathTracingParameters.h
    src/render/raytracing/PathTracingShader.h
    src/render/raytracing/RayScene.cpp
    src/render/raytracing/RayScene.h
    src/render/raytracing/RectLightGeometry.h
    src/scene/system/SceneLight.h
    src/render/resources/SceneLightConstants.h
    src/render/editor/EditorFrameInput.h
    src/render/editor/EditorShaderData.h
    src/render/graph/RenderPass.cpp
    src/render/graph/RenderPassReflection.cpp
    src/render/graph/RenderPassReflection.h
    src/render/graph/RenderGraphCompiler.cpp
    src/render/graph/RenderGraphIR.cpp
    src/render/graph/RenderGraphIR.h
    src/render/graph/RenderGraphImportExport.cpp
    src/render/graph/RenderGraphImportExport.h
    src/core/GlobalState.cpp
    src/core/GlobalState.h
    src/core/ScriptBindings.cpp
    src/core/ScriptBindings.h
    src/core/Scripting.cpp
    src/core/Scripting.h
    src/core/ScriptWriter.h
    src/render/passes/ResolvePass.cpp
    src/render/passes/ResolvePass.h
    src/core/Plugin.cpp
    src/core/Plugin.h
    src/core/RuntimeDirectory.cpp
    src/render/resources/SceneShading.cpp
    src/render/resources/SceneShading.h
    src/render/raytracing/PathAccumulation.cpp
    src/render/raytracing/PathAccumulation.h
    src/render/raytracing/PathPipelineCache.cpp
    src/render/raytracing/PathPipelineCache.h
    src/editor/Gui.cpp
    src/editor/PassEditor.cpp
)
set_target_properties(RenderLabCore PROPERTIES PREFIX "" DEFINE_SYMBOL RENDERLAB_CORE_EXPORTS)
if(MINGW)
    target_link_options(RenderLabCore PRIVATE "LINKER:--export-all-symbols")
elseif(MSVC)
    set_target_properties(RenderLabCore PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    # Auto-export scans object files. Include the functions from the GUI archives as well.
    set(renderlab_gui_exports "${RENDERLAB_GENERATED_DIRECTORY}/gui-exports.def")
    add_custom_command(
        OUTPUT "${renderlab_gui_exports}"
        COMMAND "${CMAKE_COMMAND}"
            "-Dnm=${RENDERLAB_LLVM_NM}"
            "-Dimgui=$<TARGET_FILE:imgui::imgui>"
            "-Dimguizmo=$<TARGET_FILE:imguizmo::imguizmo>"
            "-Doutput=${renderlab_gui_exports}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GuiExports.cmake"
        DEPENDS "$<TARGET_FILE:imgui::imgui>" "$<TARGET_FILE:imguizmo::imguizmo>"
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GuiExports.cmake"
        VERBATIM
    )
    target_sources(RenderLabCore PRIVATE "${renderlab_gui_exports}")
endif()
set_property(SOURCE src/core/Plugin.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    RENDERLAB_PLUGIN_ABI="${RENDERLAB_PLUGIN_ABI}"
    RENDERLAB_PLUGIN_SUFFIX="${CMAKE_SHARED_LIBRARY_SUFFIX}")
# Editor code and plugin widgets share one ImGui context.
target_link_libraries(RenderLabCore PRIVATE
    imgui::imgui "$<LINK_LIBRARY:WHOLE_ARCHIVE,imguizmo::imguizmo>"
    ${CMAKE_DL_LIBS}
    Python3::Python
)
foreach(texture IN ITEMS lut_ggx lut_sheen_E)
    set(input "${CMAKE_CURRENT_SOURCE_DIR}/src/asset/builtin/data/${texture}.png")
    set(output "${RENDERLAB_GENERATED_DIRECTORY}/builtin/${texture}.inc")
    set(embed_script "${CMAKE_CURRENT_SOURCE_DIR}/tools/EmbedBinary.py")
    add_custom_command(OUTPUT "${output}"
        COMMAND "${Python3_EXECUTABLE}" "${embed_script}" "${input}" "${output}"
        DEPENDS "${input}" "${embed_script}"
        VERBATIM
    )
    target_sources(RenderLabCore PRIVATE "${output}")
endforeach()
set(renderlab_gui_backend_includes
    $<TARGET_PROPERTY:imgui::imgui,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:imguizmo::imguizmo,INTERFACE_INCLUDE_DIRECTORIES>
)
target_include_directories(RenderLabCore SYSTEM PRIVATE ${renderlab_gui_backend_includes})
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
        GLFW_INCLUDE_NONE
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLOG_NO_ABBREVIATED_SEVERITIES
)

target_link_libraries(RenderLabCore PUBLIC
        RenderLabPbrtFrontend
        mikktspace::mikktspace
        assimp::assimp
        RenderLabBuildOptions
        slang-rhi
        glfw
        glm::glm
        nlohmann_json::nlohmann_json
        glog::glog
        unofficial::tinyexr::tinyexr
)

renderlab_generate_metadata(RenderLabCore src REGISTRY RegisteredTypes.generated.h)
add_dependencies(RenderLabDds RenderLabCoreMetadata)
target_include_directories(RenderLabPbrtFrontend PRIVATE "${RENDERLAB_GENERATED_DIRECTORY}")
add_dependencies(RenderLabPbrtFrontend RenderLabCoreMetadata)

# Use the same dependency headers with separate DLL export and import definitions.
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
        <nlohmann/json.hpp>
        <slang-rhi.h>
)
target_precompile_headers(RenderLabCore PRIVATE ${renderlab_pch_headers})
add_library(RenderLabPluginAbi OBJECT src/render/plugins/PluginAbi.cpp)
target_link_libraries(RenderLabPluginAbi PRIVATE RenderLabCore)
set_property(SOURCE src/render/plugins/PluginAbi.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    RENDERLAB_PLUGIN_ABI="${RENDERLAB_PLUGIN_ABI}")
target_precompile_headers(RenderLabPluginAbi PRIVATE ${renderlab_pch_headers})
add_subdirectory(passes)
renderlab_shader(blit shaders/raster/blit.slang blit)
renderlab_shader(imgui shaders/editor/imgui.slang imgui)
option(BUILD_TESTING "Enable manual validation tools" OFF)
if(BUILD_TESTING)
    renderlab_shader(material_workflow_check test/MaterialWorkflowCheck.slang material_workflow_check
        DEFINES ENABLE_CLEARCOAT=0 ENABLE_SHEEN=0 ENTRIES computeMain)
endif()
renderlab_build_shaders()
renderlab_build_plugins()

add_library(RenderLabRuntime STATIC
    src/editor/Inspector.cpp
    src/editor/EditorBackend.cpp
    src/editor/SceneHierarchy.cpp
    src/editor/RendererUi.cpp
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
    src/editor/PerformancePanel.cpp
    src/render/Renderer.cpp
    src/render/Renderer.h
)
target_link_libraries(RenderLabRuntime PUBLIC RenderLabCore PRIVATE Python3::Python)
target_include_directories(RenderLabRuntime SYSTEM PRIVATE ${renderlab_gui_backend_includes})
set_property(SOURCE src/core/Scripting.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    RENDERLAB_PYTHON_HOME=L"${RENDERLAB_PYTHON_HOME}")
target_precompile_headers(RenderLabRuntime REUSE_FROM RenderLabPluginAbi)

add_executable(RenderLab src/Main.cpp)
add_dependencies(RenderLab RenderLabPassPlugins)
target_link_libraries(RenderLab PRIVATE RenderLabRuntime)
target_precompile_headers(RenderLab REUSE_FROM RenderLabPluginAbi)

renderlab_copy_runtime_libraries(RenderLabCore)
if (BUILD_TESTING)
    add_subdirectory(test EXCLUDE_FROM_ALL)
endif ()
