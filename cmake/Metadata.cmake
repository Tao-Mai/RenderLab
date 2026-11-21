# Clang is a host tool for reading declarations. The generated headers use standard C++.
add_subdirectory(tools/metadata)

set(RENDERLAB_GENERATED_DIRECTORY "${RenderLab_BINARY_DIR}/generated/$<CONFIG>")

function(renderlab_generate_metadata target sourceDir)
    cmake_parse_arguments(PARSE_ARGV 2 metadata "" "REGISTRY" "")
    file(GLOB_RECURSE candidates CONFIGURE_DEPENDS "${sourceDir}/*.h")
    # Adding or removing the marker can change the output list without changing the glob.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${candidates})

    set(metadata_dir "${RENDERLAB_GENERATED_DIRECTORY}/metadata")
    set(scan "${RENDERLAB_GENERATED_DIRECTORY}/scan/${target}.cpp")
    set(stamp "${RENDERLAB_GENERATED_DIRECTORY}/scan/${target}.stamp")
    set(depfile "${RENDERLAB_GENERATED_DIRECTORY}/scan/${target}.d")
    set(inputs)
    set(headers)
    set(outputs)
    set(scan_source "")
    foreach(input IN LISTS candidates)
        file(STRINGS "${input}" marker
            REGEX "^[ \t]*#[ \t]*include[ \t]+RL_METADATA_HEADER[ \t]*\\("
            LIMIT_COUNT 1
        )
        if(NOT marker)
            continue()
        endif()

        cmake_path(RELATIVE_PATH input BASE_DIRECTORY "${RenderLab_SOURCE_DIR}" OUTPUT_VARIABLE relative)
        string(REGEX REPLACE "\\.h$" ".generated.h" generated "${relative}")
        list(APPEND inputs "${input}")
        list(APPEND headers "${relative}")
        list(APPEND outputs "${metadata_dir}/${generated}")
        string(APPEND scan_source "#include \"${relative}\"\n")
    endforeach()
    if(NOT inputs)
        return()
    endif()

    set(registry "-")
    if(metadata_REGISTRY)
        set(registry "${metadata_REGISTRY}")
        string(REGEX REPLACE "\\.h$" ".cpp" registry_source "${registry}")
        list(APPEND outputs "${metadata_dir}/${registry}" "${metadata_dir}/${registry_source}")
    endif()
    file(GENERATE OUTPUT "${scan}" CONTENT "${scan_source}")

    set(includes "$<TARGET_PROPERTY:${target},INCLUDE_DIRECTORIES>")
    set(definitions "$<TARGET_PROPERTY:${target},COMPILE_DEFINITIONS>")
    add_custom_command(
        OUTPUT "${stamp}"
        BYPRODUCTS ${outputs}
        COMMAND ${renderlab_metadata_environment} "$<TARGET_FILE:RenderLabMetadata>"
            "${RENDERLAB_METADATA_CLANG}" "${scan}" "${RenderLab_SOURCE_DIR}" "${metadata_dir}" "${registry}"
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
    target_sources(${target} PRIVATE ${inputs} ${outputs})
endfunction()
