renderlab_add_plugin(DepthPrepass DepthPrepass.cpp)

renderlab_shader(depth.slang depth DEFINES ENABLE_CLEARCOAT=0 ENABLE_SHEEN=0)
