# The runtime selects precompiled variants by source, entry points and defines.
function(renderlab_shader source binary)
    cmake_parse_arguments(S "" "" "DEFINES;ENTRIES" ${ARGN})
    cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
    cmake_path(RELATIVE_PATH source BASE_DIRECTORY "${RenderLab_SOURCE_DIR}" OUTPUT_VARIABLE sourceName)
    if(NOT S_ENTRIES)
        set(S_ENTRIES vertMain fragMain)
    endif()
    set(options "{}")
    set(arguments)
    foreach(define IN LISTS S_DEFINES)
        string(REPLACE "=" ";" pair "${define}")
        list(GET pair 0 key)
        list(GET pair 1 value)
        string(REPLACE "\\" "\\\\" jsonValue "${value}")
        string(REPLACE "\"" "\\\"" jsonValue "${jsonValue}")
        string(JSON options SET "${options}" "${key}" "\"${jsonValue}\"")
        list(APPEND arguments "-D${define}")
    endforeach()
    set(entries "[]")
    set(index 0)
    foreach(entry IN LISTS S_ENTRIES)
        list(APPEND arguments -entry "${entry}")
        string(JSON entries SET "${entries}" ${index} "\"${entry}\"")
        math(EXPR index "${index}+1")
    endforeach()
    get_property(registered GLOBAL PROPERTY "renderlab_shader_source_${binary}" SET)
    if(registered)
        message(FATAL_ERROR "Shader binary '${binary}' is already declared")
    endif()
    set_property(GLOBAL APPEND PROPERTY renderlab_shader_binaries "${binary}")
    set_property(GLOBAL PROPERTY "renderlab_shader_source_${binary}" "${source}")
    set_property(GLOBAL PROPERTY "renderlab_shader_arguments_${binary}" "${arguments}")
    set_property(GLOBAL APPEND PROPERTY renderlab_shader_sources "${sourceName}")
    set_property(GLOBAL APPEND PROPERTY "renderlab_shader_${sourceName}"
        "{\"defines\":${options},\"binary\":\"${binary}.slang-module\",\"entries\":${entries}}")
endfunction()

function(renderlab_build_shaders)
    # Output rules and their consuming target must belong to the same CMake directory.
    set(binaryDir "${SHADER_BINARY_DIR}/precompiled")
    get_property(binaries GLOBAL PROPERTY renderlab_shader_binaries)
    foreach(binary IN LISTS binaries)
        get_property(source GLOBAL PROPERTY "renderlab_shader_source_${binary}")
        get_property(arguments GLOBAL PROPERTY "renderlab_shader_arguments_${binary}")
        set(output "${binaryDir}/${binary}.slang-module")
        set(depfile "${binaryDir}/${binary}.d")
        add_custom_command(OUTPUT "${output}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${binaryDir}"
            COMMAND ${RENDERLAB_SLANGC_EXECUTABLE} "${source}"
                -I "${SHADER_SOURCE_DIR}" -I "${RenderLab_SOURCE_DIR}" -I "${RenderLab_SOURCE_DIR}/src"
                ${arguments} -emit-ir -module-name "${binary}" -matrix-layout-column-major
                -depfile "${depfile}" -o "${output}"
            DEPENDS "${source}" "${RENDERLAB_SLANGC_EXECUTABLE}"
            DEPFILE "${depfile}"
            COMMENT "Compiling ${binary}" VERBATIM)
        list(APPEND outputs "${output}")
    endforeach()

    get_property(compiledSources GLOBAL PROPERTY renderlab_shader_sources)
    list(REMOVE_DUPLICATES compiledSources)
    foreach(source IN LISTS compiledSources)
        get_property(variants GLOBAL PROPERTY "renderlab_shader_${source}")
        list(JOIN variants ",\n" contents)
        file(GENERATE OUTPUT "${binaryDir}/${source}.variants.json"
            CONTENT "[\n${contents}\n]\n")
    endforeach()

    file(GLOB_RECURSE sources CONFIGURE_DEPENDS RELATIVE "${RenderLab_SOURCE_DIR}"
        "${SHADER_SOURCE_DIR}/*.slang"
        "${SHADER_SOURCE_DIR}/*.h"
        "${RenderLab_SOURCE_DIR}/passes/*.slang")
    list(APPEND sources ${compiledSources}
        passes/raytracing/bdpt/BdptBindings.h
        passes/raytracing/restir/RestirFrameParameters.h
        src/render/raytracing/PathTracingParameters.h
        src/render/resources/PbrtMaterialProgram.h
        src/render/resources/SceneLightConstants.h)
    list(REMOVE_DUPLICATES sources)
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
