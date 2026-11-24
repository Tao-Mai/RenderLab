# Clang is a host tool for reading declarations. The generated headers use standard C++.
add_subdirectory(tools/metadata)

set(RENDERLAB_GENERATED_DIRECTORY "${RenderLab_BINARY_DIR}/generated/$<CONFIG>")

function(renderlab_generate_metadata target sourceDir)
    cmake_parse_arguments(PARSE_ARGV 2 metadata "SCHEMA" "" "")
    file(GLOB_RECURSE candidates CONFIGURE_DEPENDS "${sourceDir}/*.h" "${sourceDir}/*.hpp")
    # Changes to annotations in existing headers can change the scan inputs.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${candidates})

    set(output "${RENDERLAB_GENERATED_DIRECTORY}/metadata/${target}.generated.cpp")
    set(scan "${RENDERLAB_GENERATED_DIRECTORY}/scan/${target}.cpp")
    set(stamp "${RENDERLAB_GENERATED_DIRECTORY}/scan/${target}.stamp")
    set(depfile "${RENDERLAB_GENERATED_DIRECTORY}/scan/${target}.d")
    set(inputs)
    set(headers)
    set(scan_source "")
    foreach(input IN LISTS candidates)
        if(input STREQUAL "${RenderLab_SOURCE_DIR}/src/core/MetadataAnnotations.h")
            continue()
        endif()
        file(STRINGS "${input}" marker
            REGEX "(^|[^A-Za-z0-9_])RL_(RECORD|TYPE|REGISTER_TYPE|ALLOW_UNKNOWN_FIELDS|FIELD|SLIDER|ENUM|FLAGS|ENUM_VALUE|SHADER_LAYOUT)([^A-Za-z0-9_]|$)"
            LIMIT_COUNT 1
        )
        if(NOT marker)
            continue()
        endif()

        cmake_path(RELATIVE_PATH input BASE_DIRECTORY "${RenderLab_SOURCE_DIR}" OUTPUT_VARIABLE relative)
        list(APPEND inputs "${input}")
        list(APPEND headers "${relative}")
        string(APPEND scan_source "#include \"${relative}\"\n")
    endforeach()
    if(NOT inputs)
        return()
    endif()

    set(byproducts "${output}")
    set(schema "-")
    if(metadata_SCHEMA)
        set(schema "${RENDERLAB_GENERATED_DIRECTORY}/metadata/${target}.schema.inc")
        list(APPEND byproducts "${schema}")
    endif()
    file(GENERATE OUTPUT "${scan}" CONTENT "${scan_source}")

    set(includes "$<TARGET_PROPERTY:${target},INCLUDE_DIRECTORIES>")
    set(definitions "$<TARGET_PROPERTY:${target},COMPILE_DEFINITIONS>")
    add_custom_command(
        OUTPUT "${stamp}"
        BYPRODUCTS ${byproducts}
        COMMAND ${renderlab_metadata_environment} "$<TARGET_FILE:RenderLabMetadata>"
            "${RENDERLAB_METADATA_CLANG}" "${scan}" "${RenderLab_SOURCE_DIR}" "${output}" "${schema}"
            ${headers} --
            --driver-mode=g++ -std=c++23 -fsyntax-only -fno-exceptions -DRENDERLAB_METADATA_SCAN
            "$<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:-DNDEBUG>"
            "-resource-dir=${renderlab_clang_resource_dir}"
            "-I${RenderLab_SOURCE_DIR}" "-I${RenderLab_SOURCE_DIR}/src"
            "$<LIST:TRANSFORM,$<FILTER:${includes},EXCLUDE,^$>,PREPEND,-I>"
            "$<LIST:TRANSFORM,$<FILTER:${definitions},EXCLUDE,^$>,PREPEND,-D>"
            -MMD -MF "${depfile}" -MQ "${stamp}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${stamp}"
        DEPENDS RenderLabMetadata "${scan}" ${inputs}
        DEPFILE "${depfile}"
        COMMAND_EXPAND_LISTS
        VERBATIM
        COMMENT "Generating ${target} metadata"
    )
    add_custom_target(${target}Metadata DEPENDS "${stamp}")
    add_dependencies(${target} ${target}Metadata)
    target_sources(${target} PRIVATE "${output}")
endfunction()
