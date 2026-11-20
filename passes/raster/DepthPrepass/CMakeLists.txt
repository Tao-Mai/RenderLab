target_sources(RenderLabPasses PRIVATE DepthPrepass.cpp)

renderlab_shader(depth depth.slang depth DEFINES ENABLE_CLEARCOAT=0 ENABLE_SHEEN=0)
