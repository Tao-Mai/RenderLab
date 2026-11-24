# Keep LLVM's host libraries in the metadata tool's directory scope.
if (MSVC)
    set(renderlab_metadata_sdk "${RenderLab_SOURCE_DIR}/.cache/metadata-sdk-msvc")
    find_package(zstd CONFIG REQUIRED HINTS
        "${renderlab_metadata_sdk}"
        "${VCPKG_INSTALLED_DIR}/x64-windows-static"
        NO_DEFAULT_PATH
    )
    set_target_properties(zstd::libzstd_static PROPERTIES
        MAP_IMPORTED_CONFIG_DEBUG Release
        MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
        MAP_IMPORTED_CONFIG_MINSIZEREL Release
    )
else ()
    set(renderlab_metadata_sdk "${RenderLab_SOURCE_DIR}/.cache/metadata-sdk")
endif ()

find_package(Clang CONFIG REQUIRED HINTS
    "${RENDERLAB_COMPILER_BIN_DIR}/../lib/cmake/clang"
    "${renderlab_metadata_sdk}"
)
find_program(RENDERLAB_METADATA_CLANG NAMES clang HINTS "${LLVM_TOOLS_BINARY_DIR}" NO_DEFAULT_PATH REQUIRED)
if (MSVC)
    find_program(RENDERLAB_LLVM_NM NAMES llvm-nm HINTS "${LLVM_TOOLS_BINARY_DIR}" NO_DEFAULT_PATH REQUIRED)
endif ()
set(renderlab_metadata_environment
    "${CMAKE_COMMAND}" -E env --modify "PATH=path_list_prepend:${LLVM_TOOLS_BINARY_DIR}"
    --modify "PATH=path_list_prepend:${RENDERLAB_COMPILER_BIN_DIR}")
execute_process(
    COMMAND ${renderlab_metadata_environment} "${RENDERLAB_METADATA_CLANG}" -print-resource-dir
    OUTPUT_VARIABLE renderlab_clang_resource_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
)
set(renderlab_metadata_environment "${renderlab_metadata_environment}" PARENT_SCOPE)
set(renderlab_clang_resource_dir "${renderlab_clang_resource_dir}" PARENT_SCOPE)

add_executable(RenderLabMetadata GenerateMetadata.cpp)
set_target_properties(RenderLabMetadata PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${RenderLab_BINARY_DIR}/tools")
target_include_directories(RenderLabMetadata SYSTEM PRIVATE ${CLANG_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})
target_link_libraries(RenderLabMetadata PRIVATE RenderLabBuildOptions)
if (MSVC)
    # The Windows LLVM development archive uses the static release CRT.
    set_target_properties(RenderLabMetadata PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreaded)
    # Clang's recursive AST traversal exceeds the default Windows stack in Debug builds.
    target_link_options(RenderLabMetadata PRIVATE /STACK:16777216)
    target_link_libraries(RenderLabMetadata PRIVATE clangTooling)
else ()
    target_link_libraries(RenderLabMetadata PRIVATE clang-cpp LLVM)
endif ()
