set(RENDERLAB_PLUGIN_DIRECTORY "${RENDERLAB_RUNTIME_DIRECTORY}/plugins")
set(RENDERLAB_PLUGIN_ABI "66-${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}-$<CONFIG>")

function(renderlab_add_plugin target)
    add_library(${target} SHARED ${ARGN})
    target_include_directories(${target} PRIVATE "${RenderLab_SOURCE_DIR}")
    target_link_libraries(${target} PRIVATE RenderLabCore RenderLabPluginAbi)
    target_precompile_headers(${target} REUSE_FROM RenderLabPluginAbi)
    set_target_properties(${target} PROPERTIES
        PREFIX ""
        DEFINE_SYMBOL ""
        RUNTIME_OUTPUT_DIRECTORY "${RENDERLAB_PLUGIN_DIRECTORY}"
        LIBRARY_OUTPUT_DIRECTORY "${RENDERLAB_PLUGIN_DIRECTORY}"
        INSTALL_RPATH "$ORIGIN/.."
    )
    renderlab_generate_metadata(${target} "${CMAKE_CURRENT_SOURCE_DIR}")
    set_property(GLOBAL APPEND PROPERTY RENDERLAB_PLUGIN_TARGETS ${target})
endfunction()

function(renderlab_build_plugins)
    get_property(plugins GLOBAL PROPERTY RENDERLAB_PLUGIN_TARGETS)
    set(names)
    foreach(plugin IN LISTS plugins)
        list(APPEND names "\"$<TARGET_FILE_BASE_NAME:${plugin}>\"")
        add_dependencies(${plugin} RenderLabShaders)
    endforeach()
    list(JOIN names ",\n    " manifest)
    file(GENERATE OUTPUT "${RENDERLAB_PLUGIN_DIRECTORY}/plugins.json" CONTENT "[\n    ${manifest}\n]\n")
    add_custom_target(RenderLabPassPlugins DEPENDS ${plugins})
endfunction()
