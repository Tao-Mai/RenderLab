# Compile declarations also generate runtime manifests; filenames have no runtime policy.
function(renderlab_shader program source binary)
    cmake_parse_arguments(S "" "" "DEFINES;ENTRIES" ${ARGN})
    cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
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
    set_property(GLOBAL APPEND PROPERTY renderlab_shader_programs "${program}")
    set_property(GLOBAL APPEND PROPERTY "renderlab_shader_${program}"
        "{\"defines\":${options},\"binary\":\"${binary}.slang-module\",\"entries\":${entries}}")
endfunction()

function(renderlab_build_shaders)
    # Output rules and their consuming target must belong to the same CMake directory.
    get_property(binaries GLOBAL PROPERTY renderlab_shader_binaries)
    foreach(binary IN LISTS binaries)
        get_property(source GLOBAL PROPERTY "renderlab_shader_source_${binary}")
        get_property(arguments GLOBAL PROPERTY "renderlab_shader_arguments_${binary}")
        set(output "${SHADER_BINARY_DIR}/${binary}.slang-module")
        set(reflection "${SHADER_BINARY_DIR}/${binary}.reflection.json")
        set(depfile "${SHADER_BINARY_DIR}/${binary}.d")
        add_custom_command(OUTPUT "${output}" "${reflection}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BINARY_DIR}"
            COMMAND ${RENDERLAB_SLANGC_EXECUTABLE} "${source}"
                -I "${SHADER_SOURCE_DIR}" -I "${RenderLab_SOURCE_DIR}" -I "${RenderLab_SOURCE_DIR}/src"
                ${arguments} -emit-ir -module-name "${binary}" -matrix-layout-column-major
                -depfile "${depfile}" -o "${output}"
            COMMAND ${RENDERLAB_SLANGC_EXECUTABLE} "${source}"
                -I "${SHADER_SOURCE_DIR}" -I "${RenderLab_SOURCE_DIR}" -I "${RenderLab_SOURCE_DIR}/src"
                ${arguments} -target spirv -profile spirv_1_4 -matrix-layout-column-major
                -reflection-json "${reflection}" -o "${SHADER_BINARY_DIR}/${binary}.validation.spv"
            DEPENDS "${source}" "${RENDERLAB_SLANGC_EXECUTABLE}"
            DEPFILE "${depfile}"
            COMMENT "Compiling ${binary}" VERBATIM)
        list(APPEND outputs "${output}" "${reflection}")
    endforeach()

    get_property(programs GLOBAL PROPERTY renderlab_shader_programs)
    list(REMOVE_DUPLICATES programs)
    foreach(program IN LISTS programs)
        get_property(variants GLOBAL PROPERTY "renderlab_shader_${program}")
        list(JOIN variants ",\n" contents)
        file(GENERATE OUTPUT "${SHADER_BINARY_DIR}/${program}.variants.json"
            CONTENT "[\n${contents}\n]\n")
    endforeach()
    add_custom_target(RenderLabShaders DEPENDS ${outputs})
endfunction()
