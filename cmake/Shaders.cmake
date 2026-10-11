# Compile declarations also generate runtime manifests; filenames have no runtime policy.
function(renderlab_shader program source binary)
    cmake_parse_arguments(S "" "" "DEFINES;ENTRIES;INCLUDES" ${ARGN})
    if(NOT S_ENTRIES)
        set(S_ENTRIES vertMain fragMain)
    endif()
    set(options "{}")
    set(arguments)
    foreach(define IN LISTS S_DEFINES)
        string(REPLACE "=" ";" pair "${define}")
        list(GET pair 0 key)
        list(GET pair 1 value)
        string(JSON options SET "${options}" "${key}" "${value}")
        list(APPEND arguments "-D${define}")
    endforeach()
    set(entries "[]")
    set(index 0)
    foreach(entry IN LISTS S_ENTRIES)
        list(APPEND arguments -entry "${entry}")
        string(JSON entries SET "${entries}" ${index} "\"${entry}\"")
        math(EXPR index "${index}+1")
    endforeach()
    set(output "${SHADER_BINARY_DIR}/${binary}.spv")
    set(reflection "${SHADER_BINARY_DIR}/${binary}.reflection.json")
    add_custom_command(OUTPUT "${output}" "${reflection}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SHADER_BINARY_DIR}"
        COMMAND ${SLANGC_EXECUTABLE} "${SHADER_SOURCE_DIR}/${source}"
            ${arguments} -target spirv -profile spirv_1_4 -matrix-layout-column-major
            -emit-spirv-directly -fvk-use-entrypoint-name
            -reflection-json "${reflection}" -o "${output}"
        DEPENDS "${SHADER_SOURCE_DIR}/${source}" ${S_INCLUDES}
        COMMENT "Compiling ${binary}" VERBATIM)
    set_property(GLOBAL APPEND PROPERTY renderlab_shader_outputs "${output}" "${reflection}")
    set_property(GLOBAL APPEND PROPERTY renderlab_shader_programs "${program}")
    set_property(GLOBAL APPEND PROPERTY "renderlab_shader_${program}"
        "{\"defines\":${options},\"binary\":\"${binary}.spv\",\"entries\":${entries}}")
endfunction()
function(renderlab_shader_manifests)
    get_property(programs GLOBAL PROPERTY renderlab_shader_programs)
    list(REMOVE_DUPLICATES programs)
    foreach(program IN LISTS programs)
        get_property(variants GLOBAL PROPERTY "renderlab_shader_${program}")
        list(JOIN variants ",\n" contents)
        file(GENERATE OUTPUT "${SHADER_BINARY_DIR}/${program}.variants.json"
            CONTENT "[\n${contents}\n]\n")
    endforeach()
    get_property(outputs GLOBAL PROPERTY renderlab_shader_outputs)
    add_custom_target(RenderLabShaders DEPENDS ${outputs})
endfunction()
