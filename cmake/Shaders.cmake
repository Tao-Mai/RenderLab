function(renderlab_build_shaders)
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS RELATIVE "${RenderLab_SOURCE_DIR}"
        "${SHADER_SOURCE_DIR}/*.slang"
        "${SHADER_SOURCE_DIR}/*.slangh")
    list(APPEND sources
        src/render/resources/RayType.h
        src/render/resources/SceneLightConstants.h)
    foreach(source IN LISTS sources)
        set(input "${RenderLab_SOURCE_DIR}/${source}")
        string(REGEX REPLACE "^src/(render/impl/)?" "" runtime_source "${source}")
        set(output "${SHADER_BINARY_DIR}/source/${runtime_source}")
        cmake_path(GET output PARENT_PATH directory)
        add_custom_command(OUTPUT "${output}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${directory}"
            COMMAND ${CMAKE_COMMAND} -E copy "${input}" "${output}"
            DEPENDS "${input}" VERBATIM)
        list(APPEND outputs "${output}")
    endforeach()

    add_custom_target(RenderLabShaders DEPENDS ${outputs})
endfunction()
