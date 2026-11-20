target_sources(RenderLabPasses PRIVATE GBufferPass.cpp)

renderlab_shader(gbuffer gbuffer.slang gbuffer DEFINES ENABLE_CLEARCOAT=0 ENABLE_SHEEN=1)
