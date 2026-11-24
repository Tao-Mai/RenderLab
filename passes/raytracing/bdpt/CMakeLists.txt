renderlab_add_plugin(BDPT BDPT.cpp)

renderlab_shader(resolve.slang bdpt_resolve ENTRIES computeMain)
renderlab_shader(light_distribution.slang bdpt_light_distribution ENTRIES computeMain)
