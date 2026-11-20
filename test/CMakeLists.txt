# Build only the tool needed for the current change; all reuse the engine runtime.
foreach(check IN ITEMS CaptureScene SceneImportCheck OfflineRenderingCheck MaterialWorkflowCheck SvgfToggleCheck FalcorPostProcessCheck)
    add_executable(${check} EXCLUDE_FROM_ALL ${check}.cpp)
    target_link_libraries(${check} PRIVATE RenderLabRuntime)
    target_include_directories(${check} PRIVATE "${RenderLab_SOURCE_DIR}")
    target_precompile_headers(${check} REUSE_FROM RenderLabCore)
    add_dependencies(${check} RenderLabPassPlugins)
endforeach()

foreach(check IN ITEMS CaptureScene FalcorPostProcessCheck)
    target_sources(${check} PRIVATE ../passes/postprocess/FalcorPostProcess/FalcorPostProcessConfig.cpp)
endforeach()

add_executable(PassSettingsCheck EXCLUDE_FROM_ALL PassSettingsCheck.cpp)
target_link_libraries(PassSettingsCheck PRIVATE RenderLabCore)
target_precompile_headers(PassSettingsCheck REUSE_FROM RenderLabCore)
add_dependencies(PassSettingsCheck RenderLabPassPlugins)

set(material_check_shader "${CMAKE_CURRENT_BINARY_DIR}/material-workflow.spv")
add_custom_command(OUTPUT "${material_check_shader}"
    COMMAND "${SLANGC_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/MaterialWorkflowCheck.slang"
        -I "${SHADER_SOURCE_DIR}" -I "${PROJECT_SOURCE_DIR}/src"
        -DENABLE_CLEARCOAT=0 -DENABLE_SHEEN=0 -entry computeMain
        -target spirv -profile spirv_1_4 -matrix-layout-column-major -emit-spirv-directly -fvk-use-entrypoint-name
        -depfile "${material_check_shader}.d" -o "${material_check_shader}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/MaterialWorkflowCheck.slang" "${SLANGC_EXECUTABLE}"
    DEPFILE "${material_check_shader}.d"
    VERBATIM)
add_custom_target(MaterialWorkflowCheckShader DEPENDS "${material_check_shader}")
add_dependencies(MaterialWorkflowCheck MaterialWorkflowCheckShader)
