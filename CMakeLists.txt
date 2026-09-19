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
find_package(nlohmann_json CONFIG REQUIRED)
find_path(TINYGLTF_INCLUDE_DIR NAMES tiny_gltf.h REQUIRED)

find_program(SLANGC_EXECUTABLE
        NAMES slangc
        HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
        REQUIRED
)

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/compiled_shaders")
set(SLANG_SHADER "${SHADER_BINARY_DIR}/slang.spv")

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

add_custom_target(RenderLabShaders DEPENDS "${SLANG_SHADER}")

add_executable(RenderLab
        src/main.cpp
        src/engine.cpp
        src/engine.h
        src/window.cpp
        src/window.h
        src/asset/asset_manager.cpp
        src/asset/asset_manager.h
        src/asset/builtin_meshes.cpp
        src/asset/builtin_meshes.h
        src/asset/gltf_loader.cpp
        src/asset/gltf_loader.h
        src/asset/mesh_data.h
        src/render/renderer.cpp
        src/render/renderer.h
        src/render/buffer.cpp
        src/render/buffer.h
        src/render/mesh.cpp
        src/render/mesh.h
        src/render/shader.cpp
        src/render/shader.h
        src/render/vertex.cpp
        src/render/vertex.h
        src/scene/scene.cpp
        src/scene/scene.h
)
add_dependencies(RenderLab RenderLabShaders)

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
        nlohmann_json::nlohmann_json
)

add_custom_command(TARGET RenderLab POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:RenderLab>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SLANG_SHADER}" "$<TARGET_FILE_DIR:RenderLab>/shaders/slang.spv"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:RenderLab>/assets"
)

if (MSVC)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT RenderLab)
    set_target_properties(RenderLab PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:RenderLab>"
    )
endif ()
