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
        src/main.cpp
        src/camera.cpp
        src/camera.h
        src/engine.cpp
        src/engine.h
        src/core/config_manager.cpp
        src/core/config_manager.h
        src/core/context.cpp
        src/core/context.h
        src/core/logger.h
        src/core/reflect.h
        src/core/window.cpp
        src/core/window.h
        src/ecs/component.h
        src/ecs/camera.h
        src/ecs/light.h
        src/ecs/render.h
        src/ecs/transform.cpp
        src/ecs/transform.h
        src/editor/editor.cpp
        src/editor/editor.h
        src/editor/component_draw.cpp
        src/editor/component_draw.h
        src/editor/editor_ui.cpp
        src/editor/editor_ui.h
        src/editor/viewport_rect.h
        src/asset/asset_manager.cpp
        src/asset/asset_manager.h
        src/asset/asset_id.h
        src/asset/asset_desc.h
        src/asset/material_desc.cpp
        src/asset/geometry_io.cpp
        src/asset/geometry_io.h
        src/asset/texture_io.cpp
        src/asset/texture_io.h
        src/asset/texture_importer.cpp
        src/asset/texture_importer.h
        src/asset/json_io.h
        src/asset/rfl_reflectors.h
        src/asset/mesh_geometry.h
        src/asset/builtin_meshes.cpp
        src/asset/builtin_meshes.h
        src/asset/gltf_loader.cpp
        src/asset/gltf_loader.h
        src/asset/vertex.h
        src/platform/input_method.cpp
        src/platform/input_method.h
        src/render/renderer.cpp
        src/render/renderer.h
        src/render/device/memory.h
        src/render/device/vk_check.h
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
        src/render/resource/material.cpp
        src/render/resource/material.h
        src/render/resource/render_resource_manager.cpp
        src/render/resource/render_resource_manager.h
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
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT RenderLab)
    set_target_properties(RenderLab PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:RenderLab>"
    )
endif ()
