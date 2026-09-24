cmake_minimum_required(VERSION 4.2)

if (NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
            CACHE FILEPATH "vcpkg toolchain file")
endif ()

project(RenderLab)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan 1.4 REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)
find_package(reflectcpp CONFIG REQUIRED)
find_package(glog CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
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
set(SLANG_LIGHT_SHADER "${SHADER_BINARY_DIR}/light.spv")
set(SLANG_EDITOR_PICKING_SHADER "${SHADER_BINARY_DIR}/editor_picking.spv")

add_custom_command(
        OUTPUT "${SLANG_SHADER}"
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
        -o "${SLANG_SHADER}"
        DEPENDS "${SHADER_SOURCE_DIR}/shader.slang"
        COMMENT "Compiling Slang shader"
        VERBATIM
)

add_custom_command(
        OUTPUT "${SLANG_LIGHT_SHADER}"
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
        -o "${SLANG_LIGHT_SHADER}"
        DEPENDS "${SHADER_SOURCE_DIR}/light.slang"
        COMMENT "Compiling light source Slang shader"
        VERBATIM
)

add_custom_command(
        OUTPUT "${SLANG_EDITOR_PICKING_SHADER}"
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
        -o "${SLANG_EDITOR_PICKING_SHADER}"
        DEPENDS "${SHADER_SOURCE_DIR}/editor/picking.slang"
        COMMENT "Compiling editor picking Slang shader"
        VERBATIM
)

add_custom_target(RenderLabShaders DEPENDS
        "${SLANG_SHADER}"
        "${SLANG_LIGHT_SHADER}"
        "${SLANG_EDITOR_PICKING_SHADER}"
)

add_executable(RenderLab
        src/Main.cpp
        src/Camera.cpp
        src/Camera.h
        src/Engine.cpp
        src/Engine.h
        src/core/ConfigManager.cpp
        src/core/ConfigManager.h
        src/core/Context.cpp
        src/core/Context.h
        src/core/Logger.h
        src/core/Reflect.h
        src/core/Window.cpp
        src/core/Window.h
        src/ecs/Component.h
        src/ecs/Camera.h
        src/ecs/Light.h
        src/ecs/Render.h
        src/ecs/Transform.cpp
        src/ecs/Transform.h
        src/editor/Editor.cpp
        src/editor/Editor.h
        src/editor/ComponentDraw.cpp
        src/editor/ComponentDraw.h
        src/editor/EditorUi.cpp
        src/editor/EditorUi.h
        src/editor/ViewportRect.h
        src/asset/AssetManager.cpp
        src/asset/AssetManager.h
        src/asset/AssetId.h
        src/asset/AssetDesc.h
        src/asset/MaterialDesc.cpp
        src/asset/GeometryIo.cpp
        src/asset/GeometryIo.h
        src/asset/TextureIo.cpp
        src/asset/TextureIo.h
        src/asset/TextureImporter.cpp
        src/asset/TextureImporter.h
        src/asset/JsonIo.h
        src/asset/RflReflectors.h
        src/asset/MeshGeometry.h
        src/asset/BuiltinMeshes.cpp
        src/asset/BuiltinMeshes.h
        src/asset/GltfLoader.cpp
        src/asset/GltfLoader.h
        src/asset/Vertex.h
        src/platform/InputMethod.cpp
        src/platform/InputMethod.h
        src/render/Renderer.cpp
        src/render/Renderer.h
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
        src/render/resource/Texture.cpp
        src/render/resource/Texture.h
        src/render/resource/Shader.cpp
        src/render/resource/Shader.h
        src/render/resource/Mesh.cpp
        src/render/resource/Mesh.h
        src/render/resource/Material.cpp
        src/render/resource/Material.h
        src/render/resource/RenderResourceManager.cpp
        src/render/resource/RenderResourceManager.h
        src/render/resource/VertexLayout.h
        src/render/resource/ShaderData.h
        src/render/scene/GpuScene.cpp
        src/render/scene/GpuScene.h
        src/render/pass/ScenePass.cpp
        src/render/pass/ScenePass.h
        src/render/pass/ScenePipelines.cpp
        src/render/pass/ScenePipelines.h
        src/render/pass/LightMarkers.cpp
        src/render/pass/LightMarkers.h
        src/render/pass/EditorPickingPass.cpp
        src/render/pass/EditorPickingPass.h
        src/render/PipelineManager.cpp
        src/render/PipelineManager.h
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
        Vulkan::Vulkan
        glfw
        glm::glm
        imgui::imgui
        imguizmo::imguizmo
        reflectcpp::reflectcpp
        glog::glog
        EnTT::EnTT
        unofficial::tinyexr::tinyexr
)

if (MSVC)
    target_compile_options(RenderLab PRIVATE /utf-8)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT RenderLab)
    set_target_properties(RenderLab PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:RenderLab>"
    )
endif ()
