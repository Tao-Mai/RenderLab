renderlab_add_plugin(PathTracer PathTracer.cpp)

renderlab_shader(pt main.slang pt ENTRIES raygenMain missMain visibilityMissMain closestHitMain)
renderlab_shader(pt_resolve resolve.slang pt_resolve ENTRIES computeMain)
