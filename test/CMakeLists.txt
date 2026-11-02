# Build only the tool needed for the current change; all reuse the engine runtime.
foreach(check IN ITEMS CaptureScene SceneImportCheck OfflineRenderingCheck)
    add_executable(${check} EXCLUDE_FROM_ALL ${check}.cpp)
    target_link_libraries(${check} PRIVATE RenderLabRuntime)
    target_precompile_headers(${check} REUSE_FROM RenderLabRuntime)
    renderlab_copy_compiler_runtime(${check})
endforeach()
