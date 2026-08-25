cmake_minimum_required(VERSION 3.20)

# 自动使用 vcpkg，避免每次手动传 CMAKE_TOOLCHAIN_FILE
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
            CACHE FILEPATH "Vcpkg toolchain file")
    elseif(DEFINED ENV{HOME} AND EXISTS "$ENV{HOME}/vcpkg/scripts/buildsystems/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE "$ENV{HOME}/vcpkg/scripts/buildsystems/vcpkg.cmake"
            CACHE FILEPATH "Vcpkg toolchain file")
    elseif(DEFINED ENV{USERPROFILE} AND EXISTS "$ENV{USERPROFILE}/vcpkg/scripts/buildsystems/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE "$ENV{USERPROFILE}/vcpkg/scripts/buildsystems/vcpkg.cmake"
            CACHE FILEPATH "Vcpkg toolchain file")
    endif()
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Generate compile_commands.json file" FORCE)

# Windows + MinGW：默认用 vcpkg 的 mingw triplet（与 MSVC 的 x64-windows 分离）
if(WIN32 AND NOT DEFINED VCPKG_TARGET_TRIPLET)
    if(DEFINED ENV{VCPKG_DEFAULT_TRIPLET} AND NOT "$ENV{VCPKG_DEFAULT_TRIPLET}" STREQUAL "")
        set(VCPKG_TARGET_TRIPLET "$ENV{VCPKG_DEFAULT_TRIPLET}" CACHE STRING "")
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "GNU"
           OR (DEFINED CMAKE_CXX_COMPILER AND CMAKE_CXX_COMPILER MATCHES "g\\+\\+|gcc"))
        set(VCPKG_TARGET_TRIPLET "x64-mingw-dynamic" CACHE STRING "")
    endif()
endif()

project(RenderLab)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)
find_package(glog CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# ---- base/：渲染基础设施 + 编辑器壳（不含具体学习算法 / 着色模型）----
add_library(base STATIC
    base/asset/asset_manager.cpp
    base/asset/mesh_asset.cpp
    base/asset/scene_asset.cpp
    base/core/reflection/register_reflection.cpp
    base/core/reflection/meta_json.cpp
    base/core/reflection/component_registry.cpp
    base/core/scene.cpp
    base/core/scene_ops.cpp
    base/editor/editor.cpp
    base/editor/id_picker.cpp
    base/gfx/mesh_loader.cpp
    base/gfx/texture.cpp
    base/gfx/texture_loader.cpp
    base/gfx/shader.cpp
    base/io/app_config.cpp
    base/platform/paths.cpp
    base/platform/window.cpp
)
target_include_directories(base PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ---- demos/：OBJECT 库，保证 REGISTER_DEMO 静态注册不被链接器剥离 ----
file(GLOB_RECURSE DEMO_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/demos/*.cpp"
)
add_library(demos OBJECT ${DEMO_SOURCES})
target_link_libraries(demos PUBLIC base)
target_include_directories(demos PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 统一为正斜杠，避免 Windows 反斜杠在字符串宏里被当成转义
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}" RENDERLAB_PROJECT_ROOT)
target_compile_definitions(base PUBLIC
    PROJECT_ROOT="${RENDERLAB_PROJECT_ROOT}"
)

# 公共依赖挂在 base 上，demos 继承
target_link_libraries(base PUBLIC
    glm::glm
    glfw
    glad::glad
    assimp::assimp
    yaml-cpp::yaml-cpp
    glog::glog
    imgui::imgui
    imguizmo::imguizmo
    EnTT::EnTT
    nlohmann_json::nlohmann_json
)

add_executable(RenderLab main.cpp)
target_link_libraries(RenderLab PRIVATE demos base)

set_target_properties(RenderLab PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}")

if(MSVC)
    target_compile_options(base PUBLIC /utf-8 /Zc:__cplusplus)
    target_compile_definitions(base PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(demos PUBLIC /utf-8 /Zc:__cplusplus)
    target_compile_definitions(demos PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(base PUBLIC imm32)
elseif(MINGW)
    target_compile_options(base PUBLIC -Wall -Wextra -Wno-unused-parameter)
    target_compile_definitions(base PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(demos PUBLIC -Wall -Wextra -Wno-unused-parameter)
    target_compile_definitions(demos PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(base PUBLIC opengl32 gdi32 user32 shell32 imm32)
endif()

if(WIN32)
    set_target_properties(RenderLab PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

# clangd：每次 configure 后把编译数据库复制到项目根（Windows 上比符号链接可靠）
if(CMAKE_EXPORT_COMPILE_COMMANDS)
    file(COPY "${CMAKE_BINARY_DIR}/compile_commands.json"
         DESTINATION "${CMAKE_SOURCE_DIR}"
         FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
endif()
