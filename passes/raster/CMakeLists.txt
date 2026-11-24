add_library(RenderLabForwardPbr OBJECT ForwardPbr.cpp)
target_include_directories(RenderLabForwardPbr PRIVATE "${RenderLab_SOURCE_DIR}")
target_link_libraries(RenderLabForwardPbr PRIVATE RenderLabCore)
target_precompile_headers(RenderLabForwardPbr REUSE_FROM RenderLabPluginAbi)

renderlab_runtime_shader(forward "${SHADER_SOURCE_DIR}/raster/forward.slang")

add_subdirectory(DeferredLightingPass)
add_subdirectory(DepthPrepass)
add_subdirectory(GBufferPass)
add_subdirectory(ScenePass)
add_subdirectory(ShadowPass)
add_subdirectory(SkyboxPass)
add_subdirectory(TransparentPass)
