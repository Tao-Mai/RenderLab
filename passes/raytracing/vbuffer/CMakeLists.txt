renderlab_add_plugin(VBufferRT VBufferRT.cpp)

renderlab_shader(vbuffer.slang vbuffer ENTRIES raygenMain missMain closestHitMain anyHitMain)
