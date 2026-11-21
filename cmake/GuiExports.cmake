execute_process(
    COMMAND "${nm}" --extern-only --defined-only --format=posix "${imgui}" "${imguizmo}"
    OUTPUT_VARIABLE symbols
    COMMAND_ERROR_IS_FATAL ANY
)

string(REGEX MATCHALL "[^\r\n]+" lines "${symbols}")
set(exports)
foreach(line IN LISTS lines)
    if(line MATCHES "^([?][^ ]+) T ")
        list(APPEND exports "${CMAKE_MATCH_1}")
    endif()
endforeach()
if(NOT exports)
    message(FATAL_ERROR "No GUI function exports found in ${imgui} and ${imguizmo}")
endif()
list(REMOVE_DUPLICATES exports)
list(SORT exports)
list(JOIN exports "\n    " entries)
file(WRITE "${output}" "EXPORTS\n    ${entries}\n")
