target_sources(RenderLabPasses PRIVATE ToneMappingPass.cpp)

renderlab_shader(tonemapping tonemapping.slang tonemapping)
