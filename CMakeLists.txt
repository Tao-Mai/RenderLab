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
find_package(nlohmann_json CONFIG REQUIRED)
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

add_custom_target(RenderLabAssets
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets" "${CMAKE_CURRENT_BINARY_DIR}/assets"
        COMMENT "Synchronizing runtime assets"
        VERBATIM
)

add_executable(RenderLab
        src/main.cpp
        src/camera.cpp
        src/camera.h
        src/engine.cpp
        src/engine.h
        src/editor/editor_ui.cpp
        src/editor/editor_ui.h
        src/editor/viewport_rect.h
        src/window.cpp
        src/window.h
        src/asset/asset_manager.cpp
        src/asset/asset_manager.h
        src/asset/builtin_meshes.cpp
        src/asset/builtin_meshes.h
        src/asset/gltf_loader.cpp
        src/asset/gltf_loader.h
        src/asset/material_data.h
        src/asset/mesh_data.h
        src/asset/vertex.h
        src/platform/input_method.cpp
        src/platform/input_method.h
        src/render/renderer.cpp
        src/render/renderer.h
        src/render/device/memory.h
        src/render/device/vulkan_context.cpp
        src/render/device/vulkan_context.h
        src/render/device/frame_context.cpp
        src/render/device/frame_context.h
        src/render/device/gpu_upload_context.h
        src/render/present/swapchain.cpp
        src/render/present/swapchain.h
        src/render/resource/buffer.cpp
        src/render/resource/buffer.h
        src/render/resource/texture.cpp
        src/render/resource/texture.h
        src/render/resource/shader.cpp
        src/render/resource/shader.h
        src/render/resource/mesh.cpp
        src/render/resource/mesh.h
        src/render/resource/material_gpu.cpp
        src/render/resource/material_gpu.h
        src/render/resource/vertex_layout.h
        src/render/resource/shader_data.h
        src/render/scene/gpu_scene.cpp
        src/render/scene/gpu_scene.h
        src/render/pass/scene_pass.cpp
        src/render/pass/scene_pass.h
        src/render/pass/scene_pipelines.cpp
        src/render/pass/scene_pipelines.h
        src/render/pass/light_markers.cpp
        src/render/pass/light_markers.h
        src/render/pass/editor_picking_pass.cpp
        src/render/pass/editor_picking_pass.h
        src/scene/scene.cpp
        src/scene/scene.h
        src/scene/light.h
        src/scene/transform.cpp
        src/scene/transform.h
)
add_dependencies(RenderLab RenderLabShaders RenderLabAssets)

target_include_directories(RenderLab PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${TINYGLTF_INCLUDE_DIR}"
)

target_compile_definitions(RenderLab PRIVATE
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
        VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
        VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)

target_link_libraries(RenderLab PRIVATE
        Vulkan::Vulkan
        glfw
        glm::glm
        imgui::imgui
        imguizmo::imguizmo
        nlohmann_json::nlohmann_json
)

if (MSVC)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT RenderLab)
    set_target_properties(RenderLab PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:RenderLab>"
    )
endif ()
