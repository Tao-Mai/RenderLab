function(renderlab_build_shaders)
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS RELATIVE "${RenderLab_SOURCE_DIR}"
        "${SHADER_SOURCE_DIR}/*.slang")
    list(APPEND sources
        shaders/passes/raytracing/bdpt/BdptBindings.h
        shaders/passes/raytracing/restir/RestirFrameParameters.h
        shaders/raytracing/LightingLayers.h
        src/render/raytracing/PathTracingParameters.h
        src/render/resources/PbrtMaterialProgram.h
        src/render/resources/SceneLightConstants.h)
    if(BUILD_TESTING)
        list(APPEND sources test/MaterialWorkflowCheck.slang)
    endif()
    foreach(source IN LISTS sources)
        set(input "${RenderLab_SOURCE_DIR}/${source}")
        set(output "${SHADER_BINARY_DIR}/source/${source}")
        cmake_path(GET output PARENT_PATH directory)
        add_custom_command(OUTPUT "${output}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${directory}"
            COMMAND ${CMAKE_COMMAND} -E copy "${input}" "${output}"
            DEPENDS "${input}" VERBATIM)
        list(APPEND outputs "${output}")
    endforeach()

    add_custom_target(RenderLabShaders DEPENDS ${outputs})
endfunction()
