add_library(RenderLabPasses STATIC StandardPasses.cpp)
target_include_directories(RenderLabPasses PUBLIC "${RenderLab_SOURCE_DIR}")
target_link_libraries(RenderLabPasses PUBLIC RenderLabCore PRIVATE imgui::imgui)
target_precompile_headers(RenderLabPasses REUSE_FROM RenderLabCore)

add_subdirectory(raster)
add_subdirectory(raytracing)
add_subdirectory(postprocess)
add_subdirectory(editor)
